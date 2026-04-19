"""ADB-backed oracle engine.

Drives a harness binary running inside a live Android device / emulator
(API 24+, arm64). Real bionic / real linker64 / real libc — we just pipe
a simple line-oriented RPC over `adb shell` stdin/stdout.

Prerequisites (all done by `setup_device()`):
  - Emulator or device connected via adb (`emulator-5554` by default).
  - Rooted (`adb root`). AVD google_apis images are rooted by default.
  - `/data/local/tmp/libkrkr2.so`, `libSDL2.so`, `libffmpeg.so`,
    `harness-aarch64` all present.

Public surface used by adapters:
    engine.call(addr, ints=(), doubles=(), ret="int")
    engine.offset(off)
    engine.heap.alloc / heap.write / reset_heap()
    engine.ql.mem.read / mem.write  (ql.* is a facade; the backing is
                                     a device RPC, not a guest emulator)
"""

from __future__ import annotations

import os
import shlex
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

from . import arm64_abi
from .guest_heap import GuestHeap


HEAP_VA = 0x5000_0000
HEAP_SIZE = 16 * 1024 * 1024

ENV_ADB = "KRKR2_ADB"
ENV_SERIAL = "KRKR2_ADB_SERIAL"
ENV_REMOTE_DIR = "KRKR2_DEVICE_DIR"

DEFAULT_REMOTE_DIR = "/data/local/tmp"


def _adb_binary() -> str:
    return os.environ.get(ENV_ADB) or "adb"


def _serial() -> str | None:
    return os.environ.get(ENV_SERIAL)


def _remote_dir() -> str:
    return os.environ.get(ENV_REMOTE_DIR) or DEFAULT_REMOTE_DIR


class _MemShim:
    def __init__(self, engine: "AdbHarnessEngine"):
        self._engine = engine

    def map(self, addr: int, size: int, info: str | None = None) -> None:
        return None

    def read(self, addr: int, n: int) -> bytes:
        return self._engine._rpc_read(addr, n)

    def write(self, addr: int, data: bytes) -> None:
        self._engine._rpc_write(addr, bytes(data))


class _QlFacade:
    def __init__(self, mem: _MemShim):
        self.mem = mem


class _HeapBacking:
    def __init__(self, mem: _MemShim):
        self.mem = mem


def setup_device(
    so_path: Path,
    sdl_path: Path,
    ffmpeg_path: Path,
    harness_path: Path,
    remote_dir: str | None = None,
    adb: str | None = None,
    serial: str | None = None,
    frida_server_path: Path | None = None,
) -> None:
    """Push required files to the device. Idempotent.

    ``frida_server_path`` is optional — only needed when running with the
    Frida tracer. The binary is pushed to ``<remote_dir>/frida-server``
    and chmod'd; starting it is the operator's responsibility (see the
    README section on the Frida tracer).
    """
    adb = adb or _adb_binary()
    serial = serial or _serial()
    remote_dir = remote_dir or _remote_dir()
    prefix = [adb] + (["-s", serial] if serial else [])
    subprocess.run(prefix + ["root"], check=False, capture_output=True)
    subprocess.run(prefix + ["wait-for-device"], check=True)
    subprocess.run(prefix + ["shell", f"mkdir -p {remote_dir}"], check=True)
    for local in (so_path, sdl_path, ffmpeg_path, harness_path):
        subprocess.run(
            prefix + ["push", str(local), remote_dir + "/"],
            check=True, capture_output=True,
        )
    subprocess.run(
        prefix + ["shell", f"chmod 755 {remote_dir}/{harness_path.name}"],
        check=True, capture_output=True,
    )
    if frida_server_path is not None:
        subprocess.run(
            prefix + ["push", str(frida_server_path), f"{remote_dir}/frida-server"],
            check=True, capture_output=True,
        )
        subprocess.run(
            prefix + ["shell", f"chmod 755 {remote_dir}/frida-server"],
            check=True, capture_output=True,
        )


class AdbHarnessEngine:
    """Oracle engine backed by adb shell + C harness in emulator/device."""

    def __init__(
        self,
        adb: str | None = None,
        serial: str | None = None,
        remote_dir: str | None = None,
        harness_name: str = "harness-aarch64",
        so_name: str = "libkrkr2.so",
    ):
        self.adb = adb or _adb_binary()
        self.serial = serial or _serial()
        self.remote_dir = remote_dir or _remote_dir()
        self.harness_name = harness_name
        self.so_name = so_name

        self.load_base = 0
        self.heap: GuestHeap | None = None
        self._proc: subprocess.Popen | None = None
        self.pid: int = 0   # guest PID of harness-aarch64; set by start()

        self._mem = _MemShim(self)
        self.ql = _QlFacade(self._mem)
        self._heap_backing = _HeapBacking(self._mem)

    # ------------------------------------------------------------------ lifecycle
    def start(self, timeout: float = 20.0) -> None:
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
        remote_cmd = (
            f"cd {self.remote_dir} && "
            f"LD_LIBRARY_PATH={self.remote_dir} "
            f"./{self.harness_name} {self.remote_dir}/{self.so_name}"
        )
        self._proc = subprocess.Popen(
            prefix + ["shell", remote_cmd],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,
            bufsize=0,
        )

        # The first interesting line is READY; earlier lines may be linker
        # warnings or shell noise. Drain until READY or give up.
        line = None
        for _ in range(8):
            line = self._readline(timeout)
            if line.startswith("READY "):
                break
        else:
            raise RuntimeError(f"no READY line (last: {line!r})")
        parts = line.split()
        if len(parts) < 3:
            raise RuntimeError(f"malformed READY: {line!r}")
        self.load_base = int(parts[1], 16)
        reported_heap = int(parts[2], 16)
        if reported_heap != HEAP_VA:
            raise RuntimeError(
                f"harness heap at 0x{reported_heap:x}; expected 0x{HEAP_VA:x}"
            )
        self.heap = GuestHeap(self._heap_backing, base=HEAP_VA, size=HEAP_SIZE)
        self.pid = self._query_guest_pid()

    def _query_guest_pid(self) -> int:
        """Resolve the harness-aarch64 PID inside the guest via `pidof`.

        Required for Frida attach. Returns 0 if resolution fails — callers
        that need it should surface the error themselves.
        """
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
        try:
            out = subprocess.run(
                prefix + ["shell", f"pidof {self.harness_name}"],
                check=True, capture_output=True, timeout=5.0,
            )
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            return 0
        text = out.stdout.decode("utf-8", "replace").strip()
        # `pidof` may return multiple space-separated pids if there are
        # stale processes; take the most recent (last) one.
        parts = text.split()
        if not parts:
            return 0
        try:
            return int(parts[-1])
        except ValueError:
            return 0

    def stop(self) -> None:
        if self._proc is None:
            return
        try:
            self._proc.stdin.write(b"QUIT\n")
            self._proc.stdin.flush()
        except Exception:
            pass
        try:
            self._proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        self._proc = None
        self.pid = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()

    # -------------------------------------------------------------------- API
    def offset(self, off: int) -> int:
        return self.load_base + off

    def reset_heap(self) -> None:
        if self.heap is not None:
            self.heap.reset()

    def is_alive(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def restart(self) -> None:
        """Kill and respawn the harness. Used after a crash (SIGSEGV during
        a call we can't catch) so subsequent cases don't cascade-fail.
        tTJS state is reconstructed on next `tjs_init()`."""
        self.stop()
        # Reset init cache so tjs_init() re-runs in the new process.
        self._tjs_ptr = 0
        self.start()

    def call(
        self,
        addr: int,
        *,
        ints: Iterable[int] = (),
        doubles: Iterable[float] = (),
        ret: arm64_abi.ReturnKind = "int",
    ) -> Any:
        if self._proc is None:
            raise RuntimeError("engine not started")
        int_list = list(ints)
        dbl_list = list(doubles)
        if len(int_list) > 8 or len(dbl_list) > 8:
            raise ValueError("harness supports ≤ 8 ints and ≤ 8 doubles")

        parts = [f"CALL {addr:x} {ret} {len(int_list)}"]
        for v in int_list:
            parts.append(f"{v & 0xFFFFFFFFFFFFFFFF:x}")
        parts.append(str(len(dbl_list)))
        for d in dbl_list:
            bits = struct.unpack("<Q", struct.pack("<d", float(d)))[0]
            parts.append(f"{bits:x}")
        self._writeline(" ".join(parts))
        reply = self._readline()

        if reply.startswith("ERR "):
            raise RuntimeError(f"harness error: {reply[4:]}")
        if ret == "void":
            if reply != "OK_VOID":
                raise RuntimeError(f"expected OK_VOID, got {reply!r}")
            return None
        if ret == "double":
            if not reply.startswith("OK_DOUBLE "):
                raise RuntimeError(f"expected OK_DOUBLE, got {reply!r}")
            bits = int(reply[10:], 16)
            return struct.unpack("<d", struct.pack("<Q", bits))[0]
        if not reply.startswith("OK "):
            raise RuntimeError(f"unexpected reply: {reply!r}")
        x0 = int(reply[3:], 16)
        if ret == "bool":
            return bool(x0 & 1)
        if ret == "int":
            return x0 - (1 << 64) if (x0 & (1 << 63)) else x0
        return x0

    # ---------------------------------------------------------------- TJS helpers
    def tjs_init(self) -> int:
        """Construct the harness-private tTJS instance. Idempotent. Returns
        the guest VA of the tTJS (a 0x68-byte C++ instance)."""
        if getattr(self, "_tjs_ptr", 0):
            return self._tjs_ptr
        self._writeline("TJS_INIT")
        reply = self._readline()
        if not reply.startswith("OK "):
            raise RuntimeError(f"TJS_INIT failed: {reply!r}")
        self._tjs_ptr = int(reply[3:], 16)
        return self._tjs_ptr

    def tjs_exec(self, ascii_source: str) -> None:
        """Run a TJS script. Source must be ASCII/UTF-8 (TJS is happy with
        7-bit ASCII for our inputs). Raises on eTJSScriptError (which the
        harness lets propagate and aborts — wrap your calls in defensive
        source text)."""
        if not getattr(self, "_tjs_ptr", 0):
            raise RuntimeError("call tjs_init() first")
        self._writeline(f"TJS_EXEC {ascii_source.encode('utf-8').hex()}")
        reply = self._readline()
        if reply.startswith("ERR "):
            raise RuntimeError(f"TJS_EXEC error: {reply[4:]}")
        if reply != "OK_VOID":
            raise RuntimeError(f"unexpected TJS_EXEC reply: {reply!r}")

    def tjs_global(self, name: str) -> int:
        """Look up a global variable on the tTJS GlobalContext and return
        a guest VA of a freshly-allocated 24-byte tTJSVariant holding the
        value. Re-allocate on every call; reset via tjs_reset()."""
        if not getattr(self, "_tjs_ptr", 0):
            raise RuntimeError("call tjs_init() first")
        # UTF-16LE, null-terminated (bionic wchar is 4 bytes but TJS uses 2).
        key_hex = (name + "\0").encode("utf-16-le").hex()
        self._writeline(f"TJS_GLOBAL {key_hex}")
        reply = self._readline()
        if reply.startswith("ERR "):
            raise RuntimeError(f"TJS_GLOBAL {name!r} error: {reply[4:]}")
        if not reply.startswith("OK "):
            raise RuntimeError(f"unexpected TJS_GLOBAL reply: {reply!r}")
        return int(reply[3:], 16)

    def tjs_reset(self) -> None:
        """Reset the harness's private tTJSVariant heap so variant pointers
        from prior cases don't stack up. tTJS state persists — only the
        output-variant allocator resets."""
        self._writeline("TJS_RESET")
        reply = self._readline()
        if reply != "OK_VOID":
            raise RuntimeError(f"unexpected TJS_RESET reply: {reply!r}")

    # -------------------------------------------------------------- RPC plumbing
    def _writeline(self, s: str) -> None:
        self._proc.stdin.write(s.encode() + b"\n")
        self._proc.stdin.flush()

    def _readline(self, timeout: float = 10.0) -> str:
        line = self._proc.stdout.readline()
        if not line:
            rc = self._proc.poll()
            raise RuntimeError(f"harness stdout closed (exit={rc})")
        return line.decode(errors="replace").rstrip("\r\n")

    def _rpc_read(self, addr: int, n: int) -> bytes:
        self._writeline(f"READ {addr:x} {n}")
        reply = self._readline()
        if reply.startswith("ERR "):
            raise RuntimeError(f"READ: {reply[4:]}")
        if not reply.startswith("OK_DATA "):
            raise RuntimeError(f"unexpected READ reply: {reply!r}")
        hex_payload = reply[8:]
        if len(hex_payload) != 2 * n:
            raise RuntimeError(
                f"READ size mismatch: want {n} bytes, got {len(hex_payload)//2}"
            )
        return bytes.fromhex(hex_payload)

    def _rpc_write(self, addr: int, data: bytes) -> None:
        self._writeline(f"WRITE {addr:x} {len(data)} {data.hex()}")
        reply = self._readline()
        if reply != "OK_VOID":
            raise RuntimeError(f"WRITE: {reply!r}")
