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
import socket
import struct
import subprocess
import sys
import time
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
    libharness_path: Path | None = None,
    launcher_dex_path: Path | None = None,
) -> None:
    """Push required files to the device. Idempotent.

    ``frida_server_path`` is optional — only needed when running with the
    Frida tracer. The binary is pushed to ``<remote_dir>/frida-server``
    and chmod'd; starting it is the operator's responsibility (see the
    README section on the Frida tracer).

    ``libharness_path`` / ``launcher_dex_path`` are the app_process-mode
    artifacts (see harness/HarnessMain.java). When both are provided the
    engine defaults to the app_process launch path; callers that want the
    bare-bionic ``harness-aarch64`` can still override via harness_name.
    """
    adb = adb or _adb_binary()
    serial = serial or _serial()
    remote_dir = remote_dir or _remote_dir()
    prefix = [adb] + (["-s", serial] if serial else [])
    subprocess.run(prefix + ["root"], check=False, capture_output=True)
    subprocess.run(prefix + ["wait-for-device"], check=True)
    subprocess.run(prefix + ["shell", f"mkdir -p {remote_dir}"], check=True)
    push_list = [so_path, sdl_path, ffmpeg_path, harness_path]
    if libharness_path is not None:
        push_list.append(libharness_path)
    if launcher_dex_path is not None:
        push_list.append(launcher_dex_path)
    for local in push_list:
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


APK_PACKAGE = "org.github.krkr2"
APK_ACTIVITY = f"{APK_PACKAGE}/.HarnessActivity"
APK_RPC_PORT = 5039


class AdbHarnessEngine:
    """Oracle engine backed by adb shell + C harness in emulator/device.

    Three launch modes:

    - **Bare bionic** (``launcher_mode="elf"``): spawns the PIE executable
      ``harness-aarch64`` directly. No JVM, no Android runtime. Suitable
      for ``CALL``/``READ``/``WRITE`` and the minimal ``TJS_INIT`` path
      (just ``sub_97EA40``). Fastest to boot.
    - **app_process** (``launcher_mode="app_process"``): spawns an ART VM
      via ``app_process`` → ``org.krkr2.HarnessMain``, which reflection-
      sets up ``ActivityThread.systemMain()`` before handing control to
      the JNI-backed native RPC loop in ``libharness.so``. Minimal TJS
      only — cocos2d's init chain doesn't run.
    - **apk** (``launcher_mode="apk"``, the default): installs and starts
      the repacked ``krkr2-harness.apk``, whose ``HarnessActivity`` extends
      ``KR2Activity``/``Cocos2dxActivity``. cocos2d runs, populates the
      TVPScriptEngine global, and then accepts a TCP socket RPC client
      on localhost:5039. This is the only mode where ``TJS_INIT`` returns
      a "real" tTJS with every NCB class registered.
    """

    def __init__(
        self,
        adb: str | None = None,
        serial: str | None = None,
        remote_dir: str | None = None,
        harness_name: str = "harness-aarch64",
        so_name: str = "libkrkr2.so",
        libharness_name: str = "libharness.so",
        launcher_dex_name: str | None = "harness-launcher.dex",
        launcher_class: str = "org.krkr2.HarnessMain",
        launcher_mode: str | None = None,
        apk_package: str = APK_PACKAGE,
        apk_activity: str = APK_ACTIVITY,
        apk_port: int = APK_RPC_PORT,
    ):
        self.adb = adb or _adb_binary()
        self.serial = serial or _serial()
        self.remote_dir = remote_dir or _remote_dir()
        self.harness_name = harness_name
        self.so_name = so_name
        self.libharness_name = libharness_name
        self.launcher_dex_name = launcher_dex_name
        self.launcher_class = launcher_class
        self.launcher_mode = launcher_mode or os.environ.get(
            "KRKR2_LAUNCHER_MODE", "apk"
        )
        self.apk_package = apk_package
        self.apk_activity = apk_activity
        self.apk_port = apk_port

        self.load_base = 0
        self.heap: GuestHeap | None = None
        self._proc: subprocess.Popen | None = None
        self._socket: socket.socket | None = None
        self._socket_buf = b""
        self.pid: int = 0   # guest PID of harness process; set by start()

        self._mem = _MemShim(self)
        self.ql = _QlFacade(self._mem)
        self._heap_backing = _HeapBacking(self._mem)

    # ------------------------------------------------------------------ lifecycle
    def start(self, timeout: float = 60.0) -> None:
        if self.launcher_mode == "apk":
            self._start_apk(timeout)
        else:
            self._start_subprocess(timeout)

        # Drain until READY — transport-agnostic via _readline.
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

    def _start_subprocess(self, timeout: float) -> None:
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
        if self.launcher_mode == "app_process":
            if not self.launcher_dex_name:
                raise RuntimeError(
                    "launcher_mode=app_process requires launcher_dex_name"
                )
            remote_cmd = (
                f"cd {self.remote_dir} && "
                f"LD_LIBRARY_PATH={self.remote_dir} "
                f"CLASSPATH={self.remote_dir}/{self.launcher_dex_name} "
                f"exec app_process {self.remote_dir} {self.launcher_class} "
                f"{self.remote_dir}/{self.libharness_name} "
                f"{self.remote_dir}/{self.so_name}"
            )
        elif self.launcher_mode == "elf":
            remote_cmd = (
                f"cd {self.remote_dir} && "
                f"LD_LIBRARY_PATH={self.remote_dir} "
                f"exec ./{self.harness_name} {self.remote_dir}/{self.so_name}"
            )
        else:
            raise ValueError(f"unknown launcher_mode: {self.launcher_mode!r}")
        self._proc = subprocess.Popen(
            prefix + ["shell", remote_cmd],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,
            bufsize=0,
        )

    def _start_apk(self, timeout: float) -> None:
        """Launch HarnessActivity inside the already-installed krkr2-harness.apk
        and open a TCP connection to its RPC socket.

        Install is the caller's responsibility (see setup_device).
        """
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])

        # Kill any stale instance + forward port.
        subprocess.run(
            prefix + ["shell", f"am force-stop {self.apk_package}"],
            check=False, capture_output=True,
        )
        subprocess.run(
            prefix + ["forward", f"tcp:{self.apk_port}", f"tcp:{self.apk_port}"],
            check=True, capture_output=True,
        )
        # am start -W waits for the activity to be resumed.
        subprocess.run(
            prefix + ["shell", "am", "start", "-W", "-n", self.apk_activity],
            check=True, capture_output=True,
        )

        # Poll until the in-process ServerSocket is listening. HarnessActivity
        # only binds after onWindowFocusChanged(true), which fires AFTER
        # activity resume — so `am start -W`'s return signal is too early.
        # Verify via `pidof` + a logcat-free ready probe: try the connection
        # and demand that we see READY within the small per-conn timeout
        # before declaring success. If the server isn't bound yet, adb
        # forward returns ECONNREFUSED which we retry.
        deadline = time.time() + timeout
        last_err: Exception | None = None
        while time.time() < deadline:
            try:
                s = socket.create_connection(
                    ("127.0.0.1", self.apk_port), timeout=2.0,
                )
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                # The adb forward tunnel accepts connects on the host side
                # even before the device-side ServerSocket has bound — in
                # that case a recv returns 0 (EOF) immediately. Probe with
                # a small recv; if we got EOF/reset, back off and retry.
                s.settimeout(2.0)
                try:
                    peek = s.recv(6, socket.MSG_PEEK)
                except socket.timeout:
                    peek = b"?"   # data not ready yet but connection alive
                if not peek:
                    s.close()
                    last_err = RuntimeError("remote not listening yet (EOF)")
                    time.sleep(0.2)
                    continue
                s.settimeout(None)
                self._socket = s
                self._socket_buf = b""
                return
            except (ConnectionRefusedError, socket.timeout, OSError) as exc:
                last_err = exc
                time.sleep(0.2)
        raise RuntimeError(
            f"apk harness never accepted on {self.apk_port}: {last_err!r}"
        )

    def _query_guest_pid(self) -> int:
        """Resolve the harness process's PID inside the guest.

        Required for Frida attach. Returns 0 if resolution fails — callers
        that need it should surface the error themselves.
        """
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
        if self.launcher_mode == "apk":
            lookup_cmd = f"pidof {self.apk_package}"
        elif self.launcher_mode == "app_process":
            # Wrap first char in [] so the pgrep invocation itself doesn't
            # match (toybox pgrep on Android doesn't self-filter).
            head, rest = self.launcher_class[0], self.launcher_class[1:]
            pattern = f"[{head}]{rest}"
            lookup_cmd = f"pgrep -f '{pattern}' | tail -1"
        else:
            lookup_cmd = f"pidof {self.harness_name}"
        try:
            out = subprocess.run(
                prefix + ["shell", lookup_cmd],
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
        # Best-effort QUIT, then tear down transport.
        try:
            self._writeline("QUIT")
            # Drain a reply if one shows up; don't block forever.
            try: self._readline(timeout=2.0)
            except Exception: pass
        except Exception:
            pass

        if self._socket is not None:
            try: self._socket.close()
            except Exception: pass
            self._socket = None
            self._socket_buf = b""
            # Leave the Activity alive — it'll accept the next session too.
            # Only force-stop the app if the caller explicitly restart()s.

        if self._proc is not None:
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
        if self._socket is not None:
            return True  # socket stays alive unless stop() closes it
        return self._proc is not None and self._proc.poll() is None

    def restart(self) -> None:
        """Kill and respawn the harness. Used after a crash (SIGSEGV during
        a call we can't catch) so subsequent cases don't cascade-fail.
        tTJS state is reconstructed on next `tjs_init()`."""
        self.stop()
        if self.launcher_mode == "apk":
            # Force-stop the app so cocos2d re-initializes cleanly.
            prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
            subprocess.run(
                prefix + ["shell", f"am force-stop {self.apk_package}"],
                check=False, capture_output=True,
            )
        # Reset init cache so tjs_init() re-runs.
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
        if self._proc is None and self._socket is None:
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
        data = s.encode() + b"\n"
        if self._socket is not None:
            self._socket.sendall(data)
            return
        self._proc.stdin.write(data)
        self._proc.stdin.flush()

    def _readline(self, timeout: float = 10.0) -> str:
        if self._socket is not None:
            return self._read_socket_line(timeout)
        line = self._proc.stdout.readline()
        if not line:
            rc = self._proc.poll()
            raise RuntimeError(f"harness stdout closed (exit={rc})")
        return line.decode(errors="replace").rstrip("\r\n")

    def _read_socket_line(self, timeout: float) -> str:
        deadline = time.time() + timeout
        while True:
            nl = self._socket_buf.find(b"\n")
            if nl >= 0:
                line = self._socket_buf[:nl]
                self._socket_buf = self._socket_buf[nl + 1 :]
                return line.decode(errors="replace").rstrip("\r")
            remaining = deadline - time.time()
            if remaining <= 0:
                self._invalidate_socket()
                raise TimeoutError(
                    f"harness socket readline timed out "
                    f"(buf={self._socket_buf[:120]!r}...)"
                )
            self._socket.settimeout(remaining)
            try:
                chunk = self._socket.recv(65536)
            except socket.timeout:
                self._invalidate_socket()
                raise TimeoutError("harness socket recv timed out")
            except OSError as e:
                self._invalidate_socket()
                raise RuntimeError(f"harness socket read error: {e!r}")
            if not chunk:
                self._invalidate_socket()
                raise RuntimeError("harness socket closed by peer")
            self._socket_buf += chunk

    def _invalidate_socket(self) -> None:
        if self._socket is not None:
            try: self._socket.close()
            except Exception: pass
            self._socket = None
            self._socket_buf = b""

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
