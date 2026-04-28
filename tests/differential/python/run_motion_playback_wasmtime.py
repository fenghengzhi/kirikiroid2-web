#!/usr/bin/env python3
"""Wasmtime port-side verifier for the motion_playback differential family."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import posixpath
import shutil
import subprocess
import sys
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
DEFAULT_HOST_PYTHON_RAW = os.environ.get("KRKR2_HOST_PYTHON") or shutil.which("python3")
DEFAULT_HOST_PYTHON = (
    Path(DEFAULT_HOST_PYTHON_RAW) if DEFAULT_HOST_PYTHON_RAW else None
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="motion_playback Wasmtime differential runner")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory of cached oracle JSONs")
    p.add_argument("--wasm",
                   default=str(REPO_ROOT / "out" / "wasmtime" / "debug" /
                               "krkr2_wasmtime_guest.wasm"),
                   help="Path to the Wasmtime guest wasm")
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to logo_test_oracle.xp3")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a disk golden is missing instead of "
                        "auto-skipping the case")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural Motion state fields")
    p.add_argument("--lldb-timeout", type=float, default=600.0,
                   help="Timeout for the LLDB Wasm guest tracer")
    p.add_argument("--host-python", default=DEFAULT_HOST_PYTHON, type=Path,
                   help="Python interpreter LLDB should launch as host")
    p.add_argument("--host-mode", action="store_true",
                   help=argparse.SUPPRESS)
    p.add_argument("--host-output", type=Path, help=argparse.SUPPRESS)
    p.add_argument("--host-frames", type=int, default=0,
                   help=argparse.SUPPRESS)
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def load_wasmtime():
    try:
        import wasmtime  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "wasmtime is not installed; run "
            "'python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt'"
        ) from exc
    return wasmtime


class WasmtimeEnvProvider:
    """Headless env::* provider for browser/Emscripten imports."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.canvas_width = 1920
        self.canvas_height = 1080
        self.css_width = 1920.0
        self.css_height = 1080.0
        self.main_loop: tuple[int, int, int, int] | None = None
        self._next_al_id = 1
        self._next_fd = 100
        self._fds: dict[int, dict[str, Any]] = {}

    def define_imports(self, linker: Any, module: Any) -> None:
        unknown: list[str] = []
        for imp in module.imports:
            if imp.module == "wasi_snapshot_preview1":
                callback = self._wasi_callback_for(imp.name, imp.type)
                if callback is None:
                    unknown.append(f"{imp.module}.{imp.name}")
                    continue
                linker.define_func(imp.module, imp.name, imp.type, callback,
                                   access_caller=True)
                continue
            if imp.module != "env":
                continue
            if imp.name == "memory":
                continue
            if imp.name.startswith("gl") or imp.name.startswith("emscripten_gl"):
                continue
            callback = self._callback_for(imp.name, imp.type)
            if callback is None:
                unknown.append(f"{imp.module}.{imp.name}")
                continue
            linker.define_func("env", imp.name, imp.type, callback,
                               access_caller=True)
        if unknown:
            raise RuntimeError(
                "unsupported import(s): " + ", ".join(sorted(unknown))
            )

    @staticmethod
    def _memory(caller: Any) -> Any:
        memory = caller.get("memory")
        if memory is None:
            raise RuntimeError("guest memory export is unavailable")
        return memory

    @staticmethod
    def _memory_base(caller: Any, memory: Any) -> int:
        try:
            ptr = memory.data_ptr(caller)
        except TypeError:
            ptr = memory.data_ptr()
        return ctypes.addressof(ptr.contents)

    @staticmethod
    def _memory_len(caller: Any, memory: Any) -> int:
        try:
            return int(memory.data_len(caller))
        except TypeError:
            return int(memory.data_len())

    def _read(self, caller: Any, ptr: int, size: int) -> bytes:
        if ptr == 0 or size <= 0:
            return b""
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr + size > data_len:
            raise RuntimeError(
                f"guest memory read out of bounds: ptr={ptr} size={size}"
            )
        return ctypes.string_at(self._memory_base(caller, memory) + ptr, size)

    def _write(self, caller: Any, ptr: int, data: bytes) -> None:
        if not ptr:
            return
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr + len(data) > data_len:
            raise RuntimeError(
                f"guest memory write out of bounds: ptr={ptr} size={len(data)}"
            )
        ctypes.memmove(self._memory_base(caller, memory) + ptr, data,
                       len(data))

    def _write_i32(self, caller: Any, ptr: int, value: int) -> None:
        self._write(caller, ptr, int(value).to_bytes(4, "little", signed=True))

    def _write_u32_array(self, caller: Any, ptr: int,
                         values: list[int]) -> None:
        for i, value in enumerate(values):
            self._write(caller, ptr + i * 4,
                        int(value & 0xffffffff).to_bytes(
                            4, "little", signed=False))

    def _write_f64(self, caller: Any, ptr: int, value: float) -> None:
        self._write(caller, ptr, struct.pack("<d", float(value)))

    def _write_i64(self, caller: Any, ptr: int, value: int) -> None:
        self._write(caller, ptr, int(value).to_bytes(8, "little",
                                                     signed=True))

    def _read_c_string(self, caller: Any, ptr: int) -> str:
        if ptr == 0:
            return ""
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr >= data_len:
            raise RuntimeError(f"guest string pointer out of bounds: {ptr}")
        data = ctypes.string_at(self._memory_base(caller, memory) + ptr,
                                data_len - ptr)
        end = data.find(0)
        if end < 0:
            return bytes(data[:256]).decode("utf-8", errors="replace")
        return bytes(data[:end]).decode("utf-8", errors="replace")

    @staticmethod
    def _default_return(func_type: Any) -> Any:
        results = list(getattr(func_type, "results", []))
        if not results:
            return None
        text = str(results[0])
        if "f32" in text or "f64" in text:
            return 0.0
        return 0

    def _callback_for(self, name: str, func_type: Any) -> Any | None:
        if name.startswith("__syscall_"):
            return lambda _caller, *args: self._syscall(name, _caller, *args)
        if name.startswith("fsafs_"):
            return lambda _caller, *args: self._fsafs(name, _caller, *args)
        if name.startswith("egl"):
            return lambda caller, *args: self._egl(name, caller, *args)
        if name.startswith("al") or name.startswith("alc"):
            return lambda caller, *args: self._openal(name, caller, *args)
        if name.startswith("emscripten_set_"):
            return lambda caller, *args: self._emscripten_set(
                name, func_type, caller, *args)
        if name.startswith("emscripten_get_"):
            return lambda caller, *args: self._emscripten_get(
                name, func_type, caller, *args)
        exact = {
            "emscripten_asm_const_int": self._return_zero,
            "emscripten_asm_const_int_sync_on_main_thread": self._return_zero,
            "emscripten_asm_const_double": self._return_one_double,
            "emscripten_asm_const_ptr_sync_on_main_thread": self._return_zero,
            "emscripten_notify_memory_growth": self._return_none,
            "emscripten_sample_gamepad_data": self._return_zero,
            "emscripten_has_asyncify": self._return_zero,
            "emscripten_sleep": self._return_none,
            "emscripten_exit_with_live_runtime": self._return_none,
            "emscripten_check_blocking_allowed": self._return_one,
            "emscripten_num_logical_cores": self._return_one,
            "js_decode_text": self._return_zero,
            "krkr2_get_startup_xp3_path": self._return_zero,
            "execve": self._return_minus_one,
            "flock": self._return_zero,
            "getgrnam": self._return_zero,
            "getpwnam": self._return_zero,
            "getpwnam_r": self._return_minus_one,
            "getpwuid_r": self._return_minus_one,
            "vfork": self._return_minus_one,
            "web_alert": self._return_none,
            "web_confirm": self._return_zero,
            "_cc_canvas_render_text": self._return_zero,
            "emscripten_exit_fullscreen": self._return_zero,
            "emscripten_exit_pointerlock": self._return_zero,
            "emscripten_request_fullscreen_strategy": self._return_zero,
            "emscripten_request_pointerlock": self._return_zero,
            "_emscripten_thread_set_strongref": self._return_none,
            "_emscripten_init_main_thread_js": self._return_none,
            "_emscripten_thread_mailbox_await": self._return_none,
            "_emscripten_receive_on_main_thread_js": self._return_zero,
            "_emscripten_thread_cleanup": self._return_none,
            "_emscripten_notify_mailbox_postmessage": self._return_none,
            "__pthread_create_js": self._pthread_create,
        }
        callback = exact.get(name)
        if callback is None:
            return None
        return lambda caller, *args: callback(func_type, caller, *args)

    def _wasi_callback_for(self, name: str, func_type: Any) -> Any | None:
        exact = {
            "clock_time_get": self._wasi_clock_time_get,
            "environ_get": self._wasi_environ_get,
            "environ_sizes_get": self._wasi_environ_sizes_get,
            "fd_close": self._wasi_fd_close,
            "fd_fdstat_get": self._wasi_fd_fdstat_get,
            "fd_read": self._wasi_fd_read,
            "fd_seek": self._wasi_fd_seek,
            "fd_write": self._wasi_fd_write,
            "proc_exit": self._wasi_proc_exit,
            "random_get": self._wasi_random_get,
        }
        callback = exact.get(name)
        if callback is None:
            return None
        return lambda caller, *args: callback(func_type, caller, *args)

    def _return_none(self, _func_type: Any, _caller: Any, *args: Any) -> None:
        return None

    def _return_zero(self, _func_type: Any, _caller: Any, *args: Any) -> int:
        return 0

    def _return_one(self, _func_type: Any, _caller: Any, *args: Any) -> int:
        return 1

    def _return_minus_one(self, _func_type: Any, _caller: Any,
                          *args: Any) -> int:
        return -1

    def _return_one_double(self, _func_type: Any, _caller: Any,
                           *args: Any) -> float:
        return 1.0

    def _pthread_create(self, _func_type: Any, _caller: Any,
                        *args: Any) -> int:
        return 0

    def _syscall(self, name: str, caller: Any, *args: Any) -> int:
        if name == "__syscall_getcwd" and len(args) >= 2:
            buf, size = int(args[0]), int(args[1])
            if buf and size > 1:
                self._write(caller, buf, b"/\0")
                return buf
        if name in {"__syscall_stat64", "__syscall_lstat64"} and len(args) >= 2:
            host_path = self._resolve_guest_path(
                self._read_c_string(caller, int(args[0])))
            return self._write_stat_for_path(caller, host_path, int(args[1]))
        if name == "__syscall_newfstatat" and len(args) >= 4:
            host_path = self._resolve_guest_path(
                self._read_c_string(caller, int(args[1])))
            return self._write_stat_for_path(caller, host_path, int(args[2]))
        if name == "__syscall_fstat64" and len(args) >= 2:
            entry = self._fds.get(int(args[0]))
            if entry is None:
                return -8
            return self._write_stat_for_path(caller, entry["path"],
                                             int(args[1]))
        if name == "__syscall_faccessat" and len(args) >= 2:
            host_path = self._resolve_guest_path(
                self._read_c_string(caller, int(args[1])))
            return 0 if host_path.exists() else -44
        if name == "__syscall_openat" and len(args) >= 4:
            return self._open_guest_file(
                self._read_c_string(caller, int(args[1])), int(args[2]))
        if name == "__syscall_unlinkat" and len(args) >= 2:
            host_path = self._resolve_guest_path(
                self._read_c_string(caller, int(args[1])))
            try:
                host_path.unlink()
                return 0
            except FileNotFoundError:
                return -44
            except OSError:
                return -63
        if name == "__syscall_rmdir" and args:
            host_path = self._resolve_guest_path(
                self._read_c_string(caller, int(args[0])))
            try:
                host_path.rmdir()
                return 0
            except FileNotFoundError:
                return -44
            except OSError:
                return -63
        if name == "__syscall_getuid32":
            return 0
        if name in {"__syscall_umask", "__syscall_fchownat",
                    "__syscall_chmod", "__syscall_utimensat"}:
            return 0
        if name in {"__syscall_recvfrom", "__syscall_sendto",
                    "__syscall_wait4", "__syscall_linkat"}:
            return -52
        return -52

    def _resolve_guest_path(self, path: str) -> Path:
        if path.startswith("file://"):
            path = path[len("file://"):]
        if not path:
            path = "/"
        normalized = posixpath.normpath("/" + path.lstrip("/"))
        return self.root / normalized.lstrip("/")

    def _open_guest_file(self, guest_path: str, flags: int) -> int:
        host_path = self._resolve_guest_path(guest_path)
        write = bool(flags & 0x241)  # O_WRONLY | O_CREAT | O_TRUNC
        append = bool(flags & 0x400)
        if write:
            host_path.parent.mkdir(parents=True, exist_ok=True)
            if not host_path.exists():
                host_path.write_bytes(b"")
        if not host_path.exists():
            return -44
        if host_path.is_dir():
            data = b""
        else:
            data = host_path.read_bytes()
        fd = self._next_fd
        self._next_fd += 1
        self._fds[fd] = {
            "path": host_path,
            "data": bytearray(data),
            "pos": len(data) if append else 0,
            "write": write,
            "dir": host_path.is_dir(),
        }
        return fd

    def _write_stat_for_path(self, caller: Any, host_path: Path,
                             buf: int) -> int:
        try:
            st = host_path.stat()
        except FileNotFoundError:
            return -44
        mode = (0o040000 if host_path.is_dir() else 0o100000) | (
            st.st_mode & 0o777)
        size = 0 if host_path.is_dir() else st.st_size
        self._write(caller, buf, b"\0" * 96)
        self._write_i32(caller, buf + 0, 1)
        self._write_i32(caller, buf + 4, mode)
        self._write_i32(caller, buf + 8, 1)
        self._write_i32(caller, buf + 12, 0)
        self._write_i32(caller, buf + 16, 0)
        self._write_i32(caller, buf + 20, 0)
        self._write_i64(caller, buf + 24, size)
        self._write_i32(caller, buf + 32, 4096)
        self._write_i32(caller, buf + 36, (size + 511) // 512)
        sec = int(st.st_mtime)
        nsec = int((st.st_mtime - sec) * 1_000_000_000)
        for off in (40, 56, 72):
            self._write_i64(caller, buf + off, sec)
            self._write_i32(caller, buf + off + 8, nsec)
        self._write_i64(caller, buf + 88, int(st.st_ino) & 0x7fffffff)
        return 0

    def _wasi_clock_time_get(self, _func_type: Any, caller: Any,
                             *args: Any) -> int:
        import time
        if len(args) >= 3:
            self._write_i64(caller, int(args[2]), time.time_ns())
        return 0

    def _wasi_environ_sizes_get(self, _func_type: Any, caller: Any,
                                *args: Any) -> int:
        if len(args) >= 2:
            self._write_i32(caller, int(args[0]), 0)
            self._write_i32(caller, int(args[1]), 0)
        return 0

    def _wasi_environ_get(self, _func_type: Any, _caller: Any,
                          *args: Any) -> int:
        return 0

    def _wasi_fd_close(self, _func_type: Any, _caller: Any,
                       *args: Any) -> int:
        if args and int(args[0]) >= 3:
            entry = self._fds.pop(int(args[0]), None)
            if entry and entry.get("write"):
                entry["path"].write_bytes(bytes(entry["data"]))
        return 0

    def _wasi_fd_fdstat_get(self, _func_type: Any, caller: Any,
                            *args: Any) -> int:
        if len(args) >= 2:
            fd, buf = int(args[0]), int(args[1])
            entry = self._fds.get(fd)
            filetype = 2 if entry and entry.get("dir") else 4
            self._write(caller, buf, b"\0" * 24)
            self._write(caller, buf, bytes([filetype]))
        return 0

    def _wasi_fd_read(self, _func_type: Any, caller: Any,
                      *args: Any) -> int:
        if len(args) < 4:
            return 28
        fd, iovs, iovs_len, nread_ptr = map(int, args[:4])
        entry = self._fds.get(fd)
        if entry is None:
            self._write_i32(caller, nread_ptr, 0)
            return 8
        total = 0
        for i in range(iovs_len):
            ptr = int.from_bytes(self._read(caller, iovs + i * 8, 4),
                                 "little")
            length = int.from_bytes(self._read(caller, iovs + i * 8 + 4, 4),
                                    "little")
            pos = int(entry["pos"])
            chunk = bytes(entry["data"][pos:pos + length])
            self._write(caller, ptr, chunk)
            entry["pos"] = pos + len(chunk)
            total += len(chunk)
            if len(chunk) < length:
                break
        self._write_i32(caller, nread_ptr, total)
        return 0

    def _wasi_fd_write(self, _func_type: Any, caller: Any,
                       *args: Any) -> int:
        if len(args) < 4:
            return 28
        fd, iovs, iovs_len, nwritten_ptr = map(int, args[:4])
        chunks: list[bytes] = []
        total = 0
        for i in range(iovs_len):
            ptr = int.from_bytes(self._read(caller, iovs + i * 8, 4),
                                 "little")
            length = int.from_bytes(self._read(caller, iovs + i * 8 + 4, 4),
                                    "little")
            chunk = self._read(caller, ptr, length)
            chunks.append(chunk)
            total += len(chunk)
        if fd in (1, 2):
            stream = sys.stdout.buffer if fd == 1 else sys.stderr.buffer
            stream.write(b"".join(chunks))
            stream.flush()
        else:
            entry = self._fds.get(fd)
            if entry is None:
                return 8
            pos = int(entry["pos"])
            data = b"".join(chunks)
            buf = entry["data"]
            end = pos + len(data)
            if end > len(buf):
                buf.extend(b"\0" * (end - len(buf)))
            buf[pos:end] = data
            entry["pos"] = end
            entry["write"] = True
        self._write_i32(caller, nwritten_ptr, total)
        return 0

    def _wasi_fd_seek(self, _func_type: Any, caller: Any,
                      *args: Any) -> int:
        if len(args) < 4:
            return 28
        fd, offset, whence, newoffset_ptr = int(args[0]), int(args[1]), int(args[2]), int(args[3])
        entry = self._fds.get(fd)
        if entry is None:
            return 8
        base = 0
        if whence == 1:
            base = int(entry["pos"])
        elif whence == 2:
            base = len(entry["data"])
        new_pos = max(0, base + offset)
        entry["pos"] = new_pos
        self._write_i64(caller, newoffset_ptr, new_pos)
        return 0

    def _wasi_proc_exit(self, _func_type: Any, _caller: Any,
                        *args: Any) -> None:
        code = int(args[0]) if args else 0
        raise RuntimeError(f"guest called proc_exit({code})")

    def _wasi_random_get(self, _func_type: Any, caller: Any,
                         *args: Any) -> int:
        if len(args) >= 2:
            self._write(caller, int(args[0]), os.urandom(int(args[1])))
        return 0

    def _fsafs(self, name: str, caller: Any, *args: Any) -> Any:
        if name == "fsafs_is_host_stream":
            return 0
        if name == "fsafs_open_stream":
            return -1
        if name == "fsafs_get_stream_size":
            return 0.0
        if name == "fsafs_read_stream":
            return -1
        return None

    def _emscripten_set(self, name: str, func_type: Any, caller: Any,
                        *args: Any) -> Any:
        if name == "emscripten_set_main_loop_arg":
            self.main_loop = tuple(int(v) for v in args[:4])
            return None
        if name == "emscripten_set_canvas_element_size" and len(args) >= 3:
            self.canvas_width = int(args[1])
            self.canvas_height = int(args[2])
            return 0
        if name == "emscripten_set_element_css_size" and len(args) >= 3:
            self.css_width = float(args[1])
            self.css_height = float(args[2])
            return 0
        if name == "emscripten_set_window_title":
            return None
        return self._default_return(func_type)

    def _emscripten_get(self, name: str, func_type: Any, caller: Any,
                        *args: Any) -> Any:
        if name == "emscripten_get_canvas_element_size" and len(args) >= 3:
            self._write_i32(caller, int(args[1]), self.canvas_width)
            self._write_i32(caller, int(args[2]), self.canvas_height)
            return 0
        if name == "emscripten_get_element_css_size" and len(args) >= 3:
            self._write_f64(caller, int(args[1]), self.css_width)
            self._write_f64(caller, int(args[2]), self.css_height)
            return 0
        if name == "emscripten_get_screen_size" and len(args) >= 2:
            self._write_i32(caller, int(args[0]), self.canvas_width)
            self._write_i32(caller, int(args[1]), self.canvas_height)
            return None
        if name == "emscripten_get_device_pixel_ratio":
            return 1.0
        if name == "emscripten_get_num_gamepads":
            return 0
        if name == "emscripten_get_gamepad_status":
            return -1
        return self._default_return(func_type)

    def _egl(self, name: str, caller: Any, *args: Any) -> Any:
        if name == "eglGetError":
            return 0x3000
        if name == "eglGetDisplay":
            return 1
        if name == "eglInitialize":
            if len(args) >= 3:
                self._write_i32(caller, int(args[1]), 1)
                self._write_i32(caller, int(args[2]), 4)
            return 1
        if name == "eglChooseConfig":
            if len(args) >= 5:
                configs, config_size, num_config = int(args[2]), int(args[3]), int(args[4])
                if configs and config_size > 0:
                    self._write_i32(caller, configs, 1)
                if num_config:
                    self._write_i32(caller, num_config, 1)
            return 1
        if name == "eglGetConfigAttrib":
            if len(args) >= 4:
                self._write_i32(caller, int(args[3]), 8)
            return 1
        if name in {"eglCreateContext", "eglCreateWindowSurface"}:
            return 1
        if name in {"eglMakeCurrent", "eglBindAPI", "eglSwapBuffers",
                    "eglSwapInterval", "eglWaitGL", "eglWaitNative",
                    "eglTerminate", "eglDestroyContext",
                    "eglDestroySurface"}:
            return 1
        if name == "eglQueryString":
            return 0
        return 0

    def _openal(self, name: str, caller: Any, *args: Any) -> Any:
        if name in {"alcOpenDevice", "alcCreateContext",
                    "alcGetCurrentContext"}:
            return 1
        if name in {"alcMakeContextCurrent", "alcCloseDevice",
                    "alcDestroyContext"}:
            return 1
        if name in {"alcGetString", "alcIsExtensionPresent"}:
            return 0
        if name == "alGetError":
            return 0
        if name in {"alGenSources", "alGenBuffers"} and len(args) >= 2:
            count, ptr = int(args[0]), int(args[1])
            ids = list(range(self._next_al_id, self._next_al_id + count))
            self._next_al_id += count
            self._write_u32_array(caller, ptr, ids)
            return None
        if name == "alGetSourcei" and len(args) >= 3:
            self._write_i32(caller, int(args[2]), 0)
            return None
        if name == "alGetSourcef" and len(args) >= 3:
            self._write(caller, int(args[2]), struct.pack("<f", 0.0))
            return None
        if name == "alGetSourcefv" and len(args) >= 3:
            self._write(caller, int(args[2]), struct.pack("<fff", 0.0, 0.0, 0.0))
            return None
        return None


def define_emscripten_imports(wasmtime, linker, module, root: Path) -> None:
    del wasmtime
    WasmtimeEnvProvider(root).define_imports(linker, module)


@dataclass(frozen=True)
class BrowserBootstrapInfo:
    root: Path
    preload_files: int
    font_guest_path: str
    xp3_guest_path: str

    def summary(self) -> str:
        return (
            "bootstrap: "
            f"guestRoot={self.root}, preloadFiles={self.preload_files}, "
            f"font={self.font_guest_path}, xp3={self.xp3_guest_path}"
        )


def prepare_browser_bootstrap(root: Path,
                              startup_xp3: Path) -> BrowserBootstrapInfo:
    preload_src = REPO_ROOT / "ui" / "cocos-studio"
    if not preload_src.is_dir():
        raise FileNotFoundError(f"browser preload source missing: {preload_src}")

    for dirname in ("savedata", "save", "tmp", "reference/xp3"):
        (root / dirname).mkdir(parents=True, exist_ok=True)

    preload_count = 0
    for src in preload_src.rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to(preload_src)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        preload_count += 1

    font_path = root / "NotoSansCJK-Regular.ttc"
    if not font_path.is_file():
        raise FileNotFoundError(
            "browser preload did not provide /NotoSansCJK-Regular.ttc"
        )

    xp3_guest_rel = Path("reference/xp3/logo_test_oracle.xp3")
    xp3_dst = root / xp3_guest_rel
    xp3_dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(startup_xp3, xp3_dst)

    return BrowserBootstrapInfo(
        root=root,
        preload_files=preload_count,
        font_guest_path="/NotoSansCJK-Regular.ttc",
        xp3_guest_path="/" + xp3_guest_rel.as_posix(),
    )


def instantiate_module(wasmtime, wasm_path: Path, enable_gl: bool,
                       wasi_root: Path = REPO_ROOT):
    config = wasmtime.Config()
    config.debug_info = True
    config.cranelift_opt_level = "none"
    config.wasm_exceptions = True
    config.wasm_simd = True
    config.wasm_threads = True
    config.shared_memory = True
    engine = wasmtime.Engine(config)
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)

    wasi = wasmtime.WasiConfig()
    wasi.inherit_stdout()
    wasi.inherit_stderr()
    wasi.preopen_dir(str(wasi_root), "/")
    store.set_wasi(wasi)

    linker = wasmtime.Linker(engine)
    for imp in module.imports:
        if imp.module == "env" and imp.name == "memory":
            memory_type = imp.type
            if getattr(memory_type, "is_shared", False):
                import wasmtime._sharedmemory as sharedmemory  # type: ignore

                def _fixed_shared_memory_as_extern(self):
                    ffi = sharedmemory.ffi
                    union = ffi.wasmtime_extern_union(
                        sharedmemory=self.ptr())
                    return ffi.wasmtime_extern_t(
                        ffi.WASMTIME_EXTERN_SHAREDMEMORY, union)

                sharedmemory.SharedMemory._as_extern = (
                    _fixed_shared_memory_as_extern)
                memory = wasmtime.SharedMemory(engine, memory_type)
            else:
                memory = wasmtime.Memory(store, memory_type)
            linker.define(store, "env", "memory", memory)
    define_emscripten_imports(wasmtime, linker, module, wasi_root)
    if enable_gl:
        from wasmtime_gl_provider import WasmtimeGLProvider

        gl_provider = WasmtimeGLProvider()
        gl_provider.define_imports(linker, module)
    else:
        gl_imports = [
            f"{imp.module}.{imp.name}" for imp in module.imports
            if imp.module == "env" and imp.name.startswith("gl")
        ]
        if gl_imports:
            raise RuntimeError(
                "wasm module has GL imports but GL provider is disabled: "
                f"imports: {', '.join(gl_imports)}"
            )
    instance = linker.instantiate(store, module)
    exports = instance.exports(store)

    initialize = None
    for init_name in ("__initialize", "_initialize"):
        try:
            initialize = exports[init_name]
            break
        except Exception:
            continue
    if initialize is not None:
        initialize(store)

    return store, exports


def mem_base(store, memory) -> int:
    try:
        ptr = memory.data_ptr(store)
    except TypeError:
        ptr = memory.data_ptr()
    return ctypes.addressof(ptr.contents)


def write_bytes(store, memory, ptr: int, data: bytes) -> None:
    ctypes.memmove(mem_base(store, memory) + ptr, data, len(data))


def read_string(store, memory, ptr: int, length: int) -> str:
    if ptr == 0 or length <= 0:
        return ""
    buf = (ctypes.c_char * length).from_address(mem_base(store, memory) + ptr)
    return bytes(buf).decode("utf-8", errors="replace")


def call_with_guest_bytes(store, memory, malloc, free, data: bytes, callback):
    ptr = malloc(store, len(data))
    if ptr == 0 and data:
        raise RuntimeError("guest malloc failed")
    try:
        write_bytes(store, memory, ptr, data)
        return callback(ptr, len(data))
    finally:
        free(store, ptr)


def drive_full_guest(wasm_path: Path, startup_xp3: Path,
                     frames: int) -> dict[str, Any]:
    if not wasm_path.exists():
        raise FileNotFoundError(
            f"wasm module not found: {wasm_path}. Build with "
            "`cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`."
        )
    if not startup_xp3.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {startup_xp3}")

    wasmtime = load_wasmtime()
    with tempfile.TemporaryDirectory(prefix="krkr2-wasmtime-browserfs-") as tmp:
        bootstrap = prepare_browser_bootstrap(Path(tmp), startup_xp3)
        try:
            return _drive_full_guest_with_bootstrap(
                wasmtime, wasm_path, bootstrap, frames)
        except Exception as exc:
            raise RuntimeError(f"{exc}\n{bootstrap.summary()}") from exc


def _drive_full_guest_with_bootstrap(wasmtime, wasm_path: Path,
                                     bootstrap: BrowserBootstrapInfo,
                                     frames: int) -> dict[str, Any]:
    store, exports = instantiate_module(
        wasmtime, wasm_path, enable_gl=True, wasi_root=bootstrap.root)
    memory = exports["memory"]
    malloc = exports["malloc"]
    free = exports["free"]
    init = exports["krkr2_wasm_init"]
    startup = exports["krkr2_wasm_startup_from"]
    tick = exports["krkr2_wasm_tick"]

    guest_path = bootstrap.xp3_guest_path.encode("utf-8")
    config = json.dumps({
        "guestRoot": "/",
        "xp3": guest_path.decode("utf-8"),
        "headless": True,
        "bootstrap": {
            "preloadFiles": bootstrap.preload_files,
            "font": bootstrap.font_guest_path,
        },
    }).encode("utf-8")

    init_ok = call_with_guest_bytes(
        store, memory, malloc, free, config,
        lambda ptr, length: init(store, ptr, length))
    if not init_ok:
        err = read_string(store, memory,
                          exports["krkr2_wasm_get_error_ptr"](store),
                          exports["krkr2_wasm_get_error_len"](store))
        raise RuntimeError(err or "krkr2_wasm_init returned false")

    startup_ok = call_with_guest_bytes(
        store, memory, malloc, free, guest_path,
        lambda ptr, length: startup(store, ptr, length))

    err = read_string(store, memory,
                      exports["krkr2_wasm_get_error_ptr"](store),
                      exports["krkr2_wasm_get_error_len"](store))
    if not startup_ok:
        raise RuntimeError(err or "krkr2_wasm_startup_from returned false")

    for _ in range(frames):
        tick_ok = tick(store, 1000.0 / 60.0)
        if not tick_ok:
            err = read_string(store, memory,
                              exports["krkr2_wasm_get_error_ptr"](store),
                              exports["krkr2_wasm_get_error_len"](store))
            raise RuntimeError(err or "krkr2_wasm_tick returned false")

    return {
        "ok": True,
        "runner": "motion-playback-wasmtime-python-host",
        "framesDriven": frames,
        "bootstrap": {
            "guestRoot": str(bootstrap.root),
            "preloadFiles": bootstrap.preload_files,
            "font": bootstrap.font_guest_path,
            "xp3": bootstrap.xp3_guest_path,
        },
    }


def run_python_host_driver(wasm_path: Path, startup_xp3: Path,
                           frames: int, output: Path) -> int:
    summary = drive_full_guest(wasm_path, startup_xp3, frames)
    output.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return 0


def run_lldb_guest_trace(wasm_path: Path, startup_xp3: Path, *,
                         expected_frames: int,
                         timeout: float,
                         host_python: Path) -> list[dict]:
    if host_python is None or not host_python.exists():
        raise FileNotFoundError(f"host Python not found: {host_python}")
    if not wasm_path.exists():
        raise FileNotFoundError(
            f"wasm module not found: {wasm_path}. Build with "
            "`cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`."
        )
    if not startup_xp3.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {startup_xp3}")
    if sys.platform != "darwin":
        raise RuntimeError("Wasmtime LLDB guest trace is only supported on macOS")

    with tempfile.TemporaryDirectory(prefix="krkr2-motion-wasmtime-lldb-") as td:
        temp = Path(td)
        trace_path = temp / "trace.json"
        host_report = temp / "host.json"
        tracer = REPO_ROOT / "tests" / "differential" / "python" / \
            "wasm_lldb_motion_trace.py"
        cmd = [
            "xcrun", "python3", str(tracer),
            "--driver", str(Path(__file__).resolve()),
            "--host-python", str(host_python),
            "--wasm", str(wasm_path),
            "--startup-xp3", str(startup_xp3),
            "--trace-out", str(trace_path),
            "--host-output", str(host_report),
            "--expected-frames", str(expected_frames),
            "--timeout", str(timeout),
            "--repo-root", str(REPO_ROOT),
        ]
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(REPO_ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout + 30.0,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(
                f"Wasmtime LLDB trace timed out after {timeout + 30.0:.1f}s\n"
                f"stdout:\n{exc.stdout or ''}\nstderr:\n{exc.stderr or ''}"
            ) from exc
        if proc.returncode != 0:
            raise RuntimeError(
                f"Wasmtime LLDB tracer failed with exit code "
                f"{proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n"
                f"{proc.stderr}"
            )
        if not trace_path.exists():
            raise RuntimeError(
                "Wasmtime LLDB tracer did not write trace output\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            )
        try:
            events = json.loads(trace_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"Wasmtime LLDB trace JSON decode failed: {exc}\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            ) from exc
    if not isinstance(events, list):
        raise RuntimeError(f"Wasmtime LLDB trace root is not a list: {type(events)}")
    return events


def _segment_events(events: list[dict]) -> list[dict]:
    segments: list[dict] = []
    for ev in events:
        key = ev.get("objthis") or ev.get("topPlayer")
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(ev)
    return segments


def partition_port_frames(events: list[dict], specs: list[dict], mpb) -> dict:
    specs_by_id = {s["id"]: s for s in specs}
    unknown = [sid for sid in specs_by_id if sid not in mpb.SEGMENT_ORDER]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. "
            f"Expected ids are fixed by logo_test_oracle.xp3: "
            f"{mpb.SEGMENT_ORDER}."
        )

    segments = _segment_events(events)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive Wasmtime segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    results: dict[str, list[dict]] = {}
    for i, spec_id in enumerate(mpb.SEGMENT_ORDER):
        if spec_id not in specs_by_id:
            continue
        spec = specs_by_id[spec_id]
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"Wasmtime segment {i} ({spec_id}) has "
                f"{len(frames)} frames; spec requires {wanted}."
            )
        results[spec_id] = [
            mpb.normalize_frame(fr, fi)
            for fi, fr in enumerate(frames[:wanted])
        ]
    return results


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    wasm_path = Path(args.wasm)
    startup_xp3 = Path(args.startup_xp3)

    if args.host_mode:
        if args.host_output is None:
            print("--host-output is required in --host-mode", file=sys.stderr)
            return 2
        frames = int(args.host_frames or 0)
        if frames <= 0:
            print("--host-frames must be positive in --host-mode",
                  file=sys.stderr)
            return 2
        try:
            return run_python_host_driver(
                wasm_path, startup_xp3, frames, args.host_output)
        except Exception as exc:
            print(f"FAIL: Wasmtime host driver error: {exc}", file=sys.stderr)
            return 1

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb

    try:
        expected_frames = sum(int(spec["frames"]) for spec in specs)
        port_events = run_lldb_guest_trace(
            wasm_path,
            startup_xp3,
            expected_frames=expected_frames,
            timeout=args.lldb_timeout,
            host_python=args.host_python,
        )
        port_frames_by_id = partition_port_frames(port_events, specs, mpb)
    except Exception as exc:
        print(f"FAIL: Wasmtime LLDB trace error: {exc}", file=sys.stderr)
        return 1

    failures = 0
    for spec in specs:
        oracle_path = trace_dir / f"{spec['id']}.oracle.json"
        if not oracle_path.exists():
            msg = f"no oracle for {spec['id']} at {oracle_path}"
            if args.strict_missing_trace:
                print(f"FAIL: {msg}", file=sys.stderr)
                failures += 1
            else:
                print(f"SKIP: {msg}")
            continue
        oracle_frames = json.loads(oracle_path.read_text(encoding="utf-8"))

        port_frames = port_frames_by_id.get(spec["id"])
        if port_frames is None:
            print(f"FAIL: {spec['id']}: no Wasmtime frames captured",
                  file=sys.stderr)
            failures += 1
            continue

        result = mpb.run_case(None, spec,
                              port_frames=port_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec['id']} ({len(port_frames)} frames)")
        else:
            print(f"FAIL: {spec['id']}: {result['status']} "
                  f"({len(result['mismatches'])} mismatches)")
            for mismatch in result["mismatches"][:10]:
                print(f"  {mismatch}")
            if len(result["mismatches"]) > 10:
                print(f"  ... +{len(result['mismatches']) - 10} more")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
