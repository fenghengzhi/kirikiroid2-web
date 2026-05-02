"""Frida tracer for staged motion_playback Android oracle diagnostics."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Sequence

try:
    import frida  # type: ignore
except ModuleNotFoundError:  # pragma: no cover - raised at attach time
    frida = None  # noqa: N816


_AGENT_PATH = Path(__file__).with_name("frida_motion_stage_agent.js")
_FRAME_SELECTION_PROJECTION_PATH = (
    Path(__file__).resolve().parents[1]
    / "motion_stage_projections" / "frame_selection_v1.json"
)

STAGES: tuple[str, ...] = (
    "static_parse",
    "init_motion",
    "variable_binding",
    "frame_selection",
    "sub_motion_decision",
    "trace_flatten",
)

RENDER_STAGES: tuple[str, ...] = (
    "draw_dispatch",
    "render_prepare",
    "render_commands",
    "render_execute",
    "layer_save",
)


def _load_agent_source() -> str:
    source = _AGENT_PATH.read_text(encoding="utf-8")
    projection = json.loads(
        _FRAME_SELECTION_PROJECTION_PATH.read_text(encoding="utf-8")
    )
    return source.replace(
        "__FRAME_SELECTION_PROJECTION_JSON__",
        json.dumps(projection, separators=(",", ":")),
    )


class FridaMotionStageTracer:
    """Installs the staged motion diagnostic agent in the APK harness."""

    def __init__(
        self,
        adb_engine,
        *,
        device_id: str | None = None,
        attach_timeout: float = 10.0,
    ) -> None:
        if frida is None:
            raise RuntimeError(
                "frida-python is not installed; "
                "`pip install -r tests/differential/oracle_runner/"
                "requirements-oracle.txt`"
            )
        self._adb = adb_engine
        self._device_id = device_id or adb_engine.serial
        self._attach_timeout = attach_timeout
        self._session: Any = None
        self._script: Any = None
        self._api: Any = None
        self._info: dict[str, Any] | None = None
        self._image_checkpoint_dir: Path | None = None
        self._image_checkpoints: list[dict[str, Any]] = []

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
                    f"{self._device_id!r}; push tools/bin/android/frida-server "
                    "to /data/local/tmp/frida-server and start it as root"
                ) from exc
            except frida.ProcessNotFoundError as exc:
                last_err = exc
                time.sleep(0.2)
        else:
            raise RuntimeError(
                f"frida attach(pid={pid}) timed out: {last_err!r}"
            )

        source = _load_agent_source()
        self._script = self._session.create_script(source)
        self._script.on("message", self._on_message)
        self._script.load()
        self._api = self._script.exports_sync
        self._info = dict(self._api.setup())

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

    def __enter__(self) -> "FridaMotionStageTracer":
        self.attach()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.detach()

    @property
    def info(self) -> dict[str, Any] | None:
        return self._info

    def configure_image_checkpoints(self, raw_dir: Path | None) -> None:
        self._image_checkpoint_dir = raw_dir
        self._image_checkpoints = []
        if raw_dir is not None:
            raw_dir.mkdir(parents=True, exist_ok=True)

    def image_checkpoints(self) -> list[dict[str, Any]]:
        return [dict(item) for item in self._image_checkpoints]

    def start_record(
        self,
        stages: Sequence[str],
        *,
        record_render_step_checkpoints: bool = False,
    ) -> None:
        if self._api is None:
            raise RuntimeError("tracer not attached; call attach() first")
        self._api.start_record(list(stages), {
            "recordRenderStepCheckpoints": bool(
                record_render_step_checkpoints),
        })

    def stop_record(self) -> list[dict[str, Any]]:
        if self._api is None:
            return []
        raw = self._api.stop_record()
        return list(raw or [])

    def event_count(self) -> int:
        if self._api is None:
            return 0
        return int(self._api.event_count())

    def raw_event_count(self) -> int:
        if self._api is None:
            return 0
        return int(self._api.raw_event_count())

    def _get_device(self):
        mgr = frida.get_device_manager()
        if self._device_id:
            return mgr.get_device(self._device_id, timeout=self._attach_timeout)
        return frida.get_usb_device(timeout=self._attach_timeout)

    def _resolve_pid(self) -> int:
        if getattr(self._adb, "pid", 0):
            return self._adb.pid
        raise RuntimeError(
            "AdbHarnessEngine has no pid - call engine.start() first"
        )

    def _on_message(self, message, data) -> None:
        import sys

        if message.get("type") == "error":
            print(
                f"[frida-motion-stage-agent] "
                f"{message.get('stack') or message}",
                file=sys.stderr,
            )
            return
        if message.get("type") != "send":
            return
        payload = message.get("payload")
        if not isinstance(payload, dict):
            return
        if payload.get("type") != "render_image_checkpoint":
            return
        record = dict(payload)
        record.pop("type", None)
        if record.get("ok") and data is not None:
            raw_dir = self._image_checkpoint_dir
            if raw_dir is None:
                record["ok"] = False
                record["error"] = "host raw checkpoint directory is not configured"
            else:
                frame_value = record.get("frameId")
                if not isinstance(frame_value, int):
                    record["ok"] = False
                    record["error"] = "checkpoint has no integer frameId"
                    self._image_checkpoints.append(record)
                    return
                frame_id = int(frame_value)
                phase = str(record.get("phase") or "unknown")
                raw_path = raw_dir / f"frame_{frame_id:04d}_{phase}.bgra"
                raw_path.write_bytes(bytes(data))
                record["rawPath"] = str(raw_path)
        self._image_checkpoints.append(record)
