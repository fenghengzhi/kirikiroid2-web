"""ADB-backed oracle engine.

Drives the `HarnessActivity` inside the repacked `krkr2-harness.apk`
running on a real Android device / emulator (API 24+, arm64), speaking
a simple line-oriented RPC over a TCP socket forwarded by
`adb forward tcp:5039 tcp:5039`.

Prerequisites (done by operator + `setup_device()`):
  - Emulator or device connected via adb (`emulator-5554` by default).
  - Rooted (`adb root`). AVD google_apis / Redroid images are rooted.
  - `/data/local/tmp/libkrkr2.so`, `libSDL2.so`, `libffmpeg.so` pushed
    (setup_device pushes them).
  - `krkr2-harness.apk` built via `harness-apk/build.sh` and installed
    with `adb install`. This is the operator's one-time step.

Public surface used by adapters:
    engine.call(addr, ints=(), doubles=(), ret="int")
    engine.offset(off)
    engine.heap.alloc / heap.write / reset_heap()
    engine.ql.mem.read / mem.write  (ql.* is a facade; the backing is
                                     a device RPC, not a guest emulator)
    engine.tjs_init() / tjs_exec() / tjs_exec_str() / tjs_global() / tjs_reset()
    engine.startup_from(path)  # TVPMainScene::startupFrom via native std::string
"""

from __future__ import annotations

import os
import select
import socket
import struct
import subprocess
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
ENV_RPC_TRANSPORT = "KRKR2_ADB_RPC_TRANSPORT"

DEFAULT_REMOTE_DIR = "/data/local/tmp"
DEFAULT_START_TIMEOUT = 120.0


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


class _AdbShellTcpSocket:
    """Socket-like wrapper around `adb shell nc 127.0.0.1 <port>`.

    Redroid CI has shown flaky host-side `adb forward` behaviour for the
    long-lived motion oracle connection. This transport keeps the TCP
    connection entirely inside Android and uses adb only as a byte stream.
    """

    def __init__(self, proc: subprocess.Popen[bytes]):
        self._proc = proc
        self._timeout: float | None = None

    def setsockopt(self, *args) -> None:
        return None

    def settimeout(self, timeout: float | None) -> None:
        self._timeout = timeout

    def sendall(self, data: bytes) -> None:
        if self._proc.stdin is None:
            raise RuntimeError("adb shell transport stdin is closed")
        try:
            self._proc.stdin.write(data)
            self._proc.stdin.flush()
        except BrokenPipeError as exc:
            raise RuntimeError("adb shell transport write failed") from exc

    def recv(self, n: int) -> bytes:
        if self._proc.stdout is None:
            raise RuntimeError("adb shell transport stdout is closed")
        fd = self._proc.stdout.fileno()
        timeout = self._timeout
        if timeout is not None:
            ready, _, _ = select.select([fd], [], [], max(0.0, timeout))
            if not ready:
                raise socket.timeout("adb shell transport recv timed out")
        try:
            return os.read(fd, n)
        except OSError as exc:
            raise RuntimeError("adb shell transport read failed") from exc

    def close(self) -> None:
        for pipe in (self._proc.stdin, self._proc.stdout, self._proc.stderr):
            if pipe is not None:
                try:
                    pipe.close()
                except Exception:
                    pass
        if self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=1.0)


def setup_device(
    so_path: Path,
    sdl_path: Path,
    ffmpeg_path: Path,
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

    `libharness.so` is *not* pushed here — it ships inside
    `krkr2-harness.apk`, which the operator installs with `adb install`.
    """
    adb = adb or _adb_binary()
    serial = serial or _serial()
    remote_dir = remote_dir or _remote_dir()
    prefix = [adb] + (["-s", serial] if serial else [])
    subprocess.run(prefix + ["root"], check=False, capture_output=True)
    subprocess.run(prefix + ["wait-for-device"], check=True)
    subprocess.run(prefix + ["shell", f"mkdir -p {remote_dir}"], check=True)
    for local in (so_path, sdl_path, ffmpeg_path):
        subprocess.run(
            prefix + ["push", str(local), remote_dir + "/"],
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
    """Oracle engine backed by adb + HarnessActivity in krkr2-harness.apk.

    HarnessActivity extends ``Cocos2dxActivity`` and, inside
    ``System.loadLibrary("krkr2")``'s init chain, populates the
    ``TVPScriptEngine`` global — so by the time we arrive here TJS has
    every NCB class (Motion.Player etc.) registered. The Activity opens
    a ``ServerSocket`` on port 5039; we connect to it via
    ``adb forward tcp:5039 tcp:5039``.
    """

    def __init__(
        self,
        adb: str | None = None,
        serial: str | None = None,
        remote_dir: str | None = None,
        so_name: str = "libkrkr2.so",
        apk_package: str = APK_PACKAGE,
        apk_activity: str = APK_ACTIVITY,
        apk_port: int = APK_RPC_PORT,
    ):
        self.adb = adb or _adb_binary()
        self.serial = serial or _serial()
        self.remote_dir = remote_dir or _remote_dir()
        self.so_name = so_name
        self.apk_package = apk_package
        self.apk_activity = apk_activity
        self.apk_port = apk_port

        self.load_base = 0
        self.heap: GuestHeap | None = None
        self._socket: socket.socket | None = None
        self._socket_buf = b""
        self.pid: int = 0   # guest PID of harness process; set by start()

        self._mem = _MemShim(self)
        self.ql = _QlFacade(self._mem)
        self._heap_backing = _HeapBacking(self._mem)

    # ------------------------------------------------------------------ lifecycle
    def start(self, timeout: float = DEFAULT_START_TIMEOUT) -> None:
        line = self._start_apk(timeout)

        # Drain until READY.
        for _ in range(8):
            if line.startswith("READY "):
                break
            line = self._readline(timeout)
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

    def _start_apk(self, timeout: float) -> str:
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
            prefix + ["forward", "--remove", f"tcp:{self.apk_port}"],
            check=False, capture_output=True,
        )
        subprocess.run(
            prefix + ["wait-for-device"],
            check=True, capture_output=True,
        )
        start_out = subprocess.run(
            prefix + ["shell", "am", "start", "-W", "-n", self.apk_activity],
            check=True, capture_output=True,
        )

        # Poll the device-side socket table before creating the host adb
        # forward. Redroid's adbd can accept host-side forwarded TCP before
        # the device ServerSocket is really ready; probing that host socket
        # repeatedly leaves CI with "timeout expired while flushing socket"
        # noise and can starve the actual harness connection.
        deadline = time.time() + timeout
        last_err: Exception | None = None
        while time.time() < deadline:
            try:
                if self._device_port_listening(prefix):
                    break
            except Exception as exc:
                last_err = exc
            time.sleep(0.5)
        else:
            stdout = start_out.stdout.decode("utf-8", "replace").strip()
            stderr = start_out.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(
                f"apk harness never listened on {self.apk_port}: "
                f"{last_err!r}\nam start stdout:\n{stdout}\n"
                f"am start stderr:\n{stderr}"
            )

        if os.environ.get(ENV_RPC_TRANSPORT) == "shell-nc":
            print(
                f"[adb-engine] connecting harness via adb shell nc "
                f"127.0.0.1:{self.apk_port}",
                flush=True,
            )
            self._socket = self._connect_adb_shell_nc(prefix)
            self._socket_buf = b""
            try:
                line = self._read_socket_line(
                    timeout=max(1.0, deadline - time.time()))
            except Exception:
                self._invalidate_socket()
                raise
            self._socket.settimeout(None)
            return line

        subprocess.run(
            prefix + ["forward", f"tcp:{self.apk_port}", f"tcp:{self.apk_port}"],
            check=True, capture_output=True,
        )

        while time.time() < deadline:
            try:
                s = socket.create_connection(
                    ("127.0.0.1", self.apk_port), timeout=2.0,
                )
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self._socket = s
                self._socket_buf = b""
                try:
                    line = self._read_socket_line(
                        timeout=max(1.0, deadline - time.time()))
                except Exception as exc:
                    last_err = exc
                    self._invalidate_socket()
                    time.sleep(0.5)
                    continue
                s.settimeout(None)
                return line
            except (ConnectionRefusedError, socket.timeout, OSError) as exc:
                last_err = exc
                self._invalidate_socket()
                time.sleep(0.5)
        raise RuntimeError(
            f"apk harness never accepted on {self.apk_port}: {last_err!r}"
        )

    def _connect_adb_shell_nc(self, prefix: list[str]) -> _AdbShellTcpSocket:
        cmd = (
            f"toybox nc 127.0.0.1 {self.apk_port} 2>/dev/null "
            f"|| nc 127.0.0.1 {self.apk_port} 2>/dev/null"
        )
        proc = subprocess.Popen(
            prefix + ["shell", cmd],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        sock = _AdbShellTcpSocket(proc)
        sock.settimeout(2.0)
        return sock

    def _device_port_listening(self, prefix: list[str]) -> bool:
        port_hex = f"{self.apk_port:04X}"
        out = subprocess.run(
            prefix + [
                "shell",
                "cat /proc/net/tcp /proc/net/tcp6 2>/dev/null || true",
            ],
            check=False,
            capture_output=True,
            timeout=5.0,
        )
        text = out.stdout.decode("utf-8", "replace")
        for line in text.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[1].upper().endswith(
                    f":{port_hex}") and parts[3] == "0A":
                return True
        return False

    def _query_guest_pid(self) -> int:
        """Resolve the harness process's PID inside the guest.

        Required for Frida attach. Returns 0 if resolution fails — callers
        that need it should surface the error themselves.
        """
        prefix = [self.adb] + (["-s", self.serial] if self.serial else [])
        try:
            out = subprocess.run(
                prefix + ["shell", f"pidof {self.apk_package}"],
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
        return self._socket is not None

    def restart(self) -> None:
        """Kill and respawn the harness. Used after a crash (SIGSEGV during
        a call we can't catch) so subsequent cases don't cascade-fail.
        tTJS state is reconstructed on next `tjs_init()`."""
        self.stop()
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
        if self._socket is None:
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

    def tjs_exec_str(self, ascii_source: str) -> str:
        """Run a TJS script whose final expression yields a String, and
        return the UTF-8 decoded result. Used by the motion_playback oracle
        adapter to ferry a JSON payload back from libkrkr2 in one round-trip
        instead of chunking via repeated TJS_GLOBAL calls."""
        if not getattr(self, "_tjs_ptr", 0):
            raise RuntimeError("call tjs_init() first")
        self._writeline(
            f"TJS_EXEC_STR {ascii_source.encode('utf-8').hex()}")
        reply = self._readline(timeout=60.0)
        if reply.startswith("ERR "):
            raise RuntimeError(f"TJS_EXEC_STR error: {reply[4:]}")
        if not reply.startswith("OK_STR "):
            raise RuntimeError(
                f"unexpected TJS_EXEC_STR reply: {reply[:120]!r}")
        return bytes.fromhex(reply[7:]).decode("utf-8")

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

    def startup_from(self, game_path_on_device: str) -> bool:
        """Call TVPMainScene::startupFrom(path) inside the native harness.

        The harness constructs the GNU-libstdc++ std::string itself so
        Python never has to forge a libkrkr2 C++ object layout.
        """
        self._writeline(
            f"STARTUP_FROM {game_path_on_device.encode('utf-8').hex()}")
        reply = self._readline(timeout=60.0)
        if reply.startswith("ERR "):
            raise RuntimeError(f"STARTUP_FROM error: {reply[4:]}")
        if not reply.startswith("OK "):
            raise RuntimeError(f"unexpected STARTUP_FROM reply: {reply!r}")
        return bool(int(reply[3:], 16) & 1)

    # -------------------------------------------------------------- RPC plumbing
    def _writeline(self, s: str) -> None:
        if self._socket is None:
            raise RuntimeError("engine not started")
        self._socket.sendall(s.encode() + b"\n")

    def _readline(self, timeout: float = 10.0) -> str:
        if self._socket is None:
            raise RuntimeError("engine not started")
        return self._read_socket_line(timeout)

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
