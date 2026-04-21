"""Frida-based call tracer for the ADB oracle harness.

Attaches to the live `HarnessActivity` process (`org.github.krkr2`)
spawned by `AdbHarnessEngine.start()` and installs `Interceptor.attach` hooks on a
curated list of libkrkr2.so offsets (see `trace_targets.py`). Per-case
event capture is gated by `start_case()`/`stop_case()`, so hooks stay
resident across the whole session but only record inside the interesting
window.

Prerequisites on device (rooted Android emulator / device):
  - `/data/local/tmp/frida-server` running as root on port 27042 (the
    default). Operator step:
        adb push frida-server-<ver>-android-arm64 /data/local/tmp/frida-server
        adb shell "su 0 chmod 755 /data/local/tmp/frida-server"
        adb shell "su 0 /data/local/tmp/frida-server -D &"
  - `frida==<ver>` pinned in requirements-oracle.txt (version must match
    the server binary exactly).
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Any, Iterable, Sequence

try:
    import frida  # type: ignore
except ModuleNotFoundError:  # pragma: no cover — raised at attach time
    frida = None  # noqa: N816

from .trace_targets import arg_counts, return_kind


_AGENT_PATH = Path(__file__).with_name("frida_agent.js")


class FridaTracerEngine:
    """Companion engine to AdbHarnessEngine that records call events.

    Lifecycle:
        tracer = FridaTracerEngine(adb_engine, BEZIER_CURVE_TARGETS)
        with tracer:                        # attach + hook
            for spec in specs:
                tracer.start_case()
                run_case(engine, spec)      # triggers CALL in harness
                events = tracer.stop_case()
    """

    def __init__(
        self,
        adb_engine,
        target_offsets: Sequence[int],
        *,
        device_id: str | None = None,
        attach_timeout: float = 10.0,
    ) -> None:
        if frida is None:
            raise RuntimeError(
                "frida-python is not installed; "
                "`pip install -r tests/differential/oracle_runner/requirements-oracle.txt`"
            )
        self._adb = adb_engine
        self._targets = [int(x) for x in target_offsets]
        self._device_id = device_id or adb_engine.serial
        self._attach_timeout = attach_timeout
        self._device: Any = None
        self._session: Any = None
        self._script: Any = None
        self._api: Any = None
        self._hooked_base: str | None = None

    # ------------------------------------------------------------- lifecycle
    def attach(self) -> None:
        if self._session is not None:
            return

        device = self._get_device()
        pid = self._resolve_pid()
        deadline = time.time() + self._attach_timeout
        last_err: Exception | None = None
        while time.time() < deadline:
            try:
                self._session = device.attach(pid)
                break
            except frida.ServerNotRunningError as exc:
                raise RuntimeError(
                    "frida-server not running on device at "
                    f"{self._device_id!r}; start it first"
                ) from exc
            except frida.ProcessNotFoundError as exc:
                last_err = exc
                time.sleep(0.2)
        else:
            raise RuntimeError(
                f"frida attach(pid={pid}) timed out: {last_err!r}"
            )

        source = _AGENT_PATH.read_text(encoding="utf-8")
        self._script = self._session.create_script(source)
        self._script.on("message", self._on_message)
        self._script.load()
        self._api = self._script.exports_sync
        info = self._api.set_targets(self._targets)
        self._hooked_base = info.get("base") if isinstance(info, dict) else None

    def detach(self) -> None:
        if self._script is not None:
            try:
                self._script.unload()
            except Exception:
                pass
            self._script = None
        if self._session is not None:
            try:
                self._session.detach()
            except Exception:
                pass
            self._session = None
        self._api = None

    def __enter__(self) -> "FridaTracerEngine":
        self.attach()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.detach()

    # ----------------------------------------------------------------- API
    def start_case(self) -> None:
        if self._api is None:
            raise RuntimeError("tracer not attached; call attach() first")
        try:
            self._api.start_case()
        except frida.InvalidOperationError:
            # Script already destroyed — the target probably died. Record-
            # ing the name so the adapter's finally doesn't also raise.
            self._api = None

    def stop_case(self) -> list[dict] | None:
        """Flush the agent buffer. Returns None when the target process
        has died (script torn down). Callers must treat None as 'no trace
        produced' rather than an error — that's what a SIGSEGV mid-case
        looks like."""
        if self._api is None:
            return None
        try:
            raw = self._api.stop_case()
        except frida.InvalidOperationError:
            self._api = None
            return None
        return _normalize_events(raw or [])

    # ------------------------------------------------------------- helpers
    def _get_device(self):
        mgr = frida.get_device_manager()
        if self._device_id:
            return mgr.get_device(self._device_id, timeout=self._attach_timeout)
        # frida's usb/android default: pick the first connected device.
        return frida.get_usb_device(timeout=self._attach_timeout)

    def _resolve_pid(self) -> int:
        if getattr(self._adb, "pid", 0):
            return self._adb.pid
        raise RuntimeError(
            "AdbHarnessEngine has no pid — call engine.start() first"
        )

    def _on_message(self, message, data):  # agent diagnostics channel
        import sys
        if message.get("type") == "error":
            print(f"[frida-agent] {message.get('stack') or message}",
                  file=sys.stderr)


# Non-deterministic pointer threshold. Values at or above this are
# treated as dynamic (bionic heap, libkrkr2 text, stack, TLS) and
# replaced with a placeholder — their exact bits change across sessions.
# Values below are either our deterministic GuestHeap (base 0x50000000),
# small scalars/counts, or bools — worth asserting.
DYNAMIC_PTR_THRESHOLD = 0x1_0000_0000
PTR_PLACEHOLDER = "<ptr>"
UNUSED_REG_PLACEHOLDER = "-"


def _normalize_events(raw: Iterable[dict]) -> list[dict]:
    """Canonicalise JS-side values for deterministic JSON round-trip.

    Integer-register values that look like dynamic pointers (bionic
    heap, libkrkr2 text) are replaced with a placeholder so traces
    survive process restarts with different ASLR slides. Float values
    and in-GuestHeap pointers stay raw — those are the actual signal.
    """
    out = []
    for ev in raw:
        kind = ev.get("kind")
        addr = int(ev["addr"]) if isinstance(ev["addr"], (int, str)) else ev["addr"]
        n_int, n_dbl = arg_counts(addr)
        entry: dict[str, Any] = {
            "kind": kind,
            "addr": addr,
            "depth": int(ev.get("depth", 0)),
        }
        if kind == "enter":
            xs = list(ev.get("x", []))
            ds = list(ev.get("d", []))
            entry["x"] = [
                _canon_int_reg(xs[i]) if i < n_int else UNUSED_REG_PLACEHOLDER
                for i in range(len(xs) or 8)
            ]
            entry["d"] = [
                _canon_double(ds[i]) if i < n_dbl else UNUSED_REG_PLACEHOLDER
                for i in range(len(ds) or 8)
            ]
        else:  # "exit"
            rk = return_kind(addr)
            if rk == "int":
                entry["x0"] = _canon_int_reg(ev.get("x0", "0x0"))
                entry["d0"] = UNUSED_REG_PLACEHOLDER
            elif rk == "double":
                entry["x0"] = UNUSED_REG_PLACEHOLDER
                entry["d0"] = _canon_double(ev.get("d0", "0x0"))
            else:  # void
                entry["x0"] = UNUSED_REG_PLACEHOLDER
                entry["d0"] = UNUSED_REG_PLACEHOLDER
        out.append(entry)
    return out


def _canon_int_reg(value) -> str:
    """Canonical form for an x-register snapshot. Masks dynamic pointers."""
    n = _parse_int(value)
    if n is None:
        return str(value)
    if n >= DYNAMIC_PTR_THRESHOLD:
        return PTR_PLACEHOLDER
    return f"0x{(n & 0xFFFFFFFFFFFFFFFF):016x}"


def _canon_double(value) -> str:
    """Canonical form for a d-register snapshot.

    Frida's arm64 CpuContext exposes d-registers as Number (decimal
    string on toString()); we pass them through unchanged. Raw-bits
    shapes (hex strings from x-style emission, or ints) fall back to
    the same masking to keep the diff stable.
    """
    if isinstance(value, (int, float)):
        return repr(float(value))
    s = str(value)
    if s.startswith("0x") or s.startswith("0X"):
        n = _parse_int(s)
        if n is None:
            return s
        return f"0x{(n & 0xFFFFFFFFFFFFFFFF):016x}"
    return s


def _parse_int(value) -> int | None:
    if isinstance(value, int):
        return value
    s = str(value).strip().lower()
    body = s[2:] if s.startswith("0x") else s
    try:
        return int(body, 16)
    except ValueError:
        return None
