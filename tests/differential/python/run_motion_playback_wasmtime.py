#!/usr/bin/env python3
"""Wasmtime port-side verifier for the motion_playback differential family."""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import posixpath
import shutil
import subprocess
import sys
import struct
import tempfile
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
from oracle_runner.png_artifacts import images_changed, png_manifest_entry

DEFAULT_HOST_PYTHON_RAW = os.environ.get("KRKR2_HOST_PYTHON") or shutil.which("python3")
DEFAULT_HOST_PYTHON = (
    Path(DEFAULT_HOST_PYTHON_RAW) if DEFAULT_HOST_PYTHON_RAW else None
)
FRAMEBUFFER_SCHEMA = "motion-framebuffer-wasmtime-v1"
FRAMEBUFFER_SOURCE = "wasmtime-port-saveLayerImage"
FRAMEBUFFER_CAPTURE_SURFACE = (
    "Layer main image immediately after Motion.Player.draw(base)"
)
FRAMEBUFFER_CAPTURE_GUEST_ROOT = "/framebuffer_capture"
RENDER_SCHEMA = "motion-render-stage-wasmtime-v1"
RENDER_EVENT_SCHEMA = "motion-render-stage-wasmtime-v1-event"
RENDER_SOURCE = "wasmtime-port-render-stage"
RENDER_CAPTURE_GUEST_ROOT = "/render_stage_capture"
RENDER_CAPTURE_SURFACES: tuple[str, ...] = ("pre_draw", "post_draw")
RENDER_STEP_CHECKPOINT_SURFACES: tuple[str, ...] = (
    "execute_pre",
    "execute_post",
)
RENDER_STAGES: tuple[str, ...] = (
    "draw_dispatch",
    "render_prepare",
    "render_commands",
    "render_execute",
    "layer_save",
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
    p.add_argument("--record-framebuffer", action="store_true",
                   help="Save every Wasmtime motion_playback frame as PNG "
                        "artifacts and write a manifest.json")
    p.add_argument("--framebuffer-dir", type=Path, default=None,
                   help="Framebuffer artifact output directory. Default: "
                        "tests/differential/artifacts/"
                        "motion_playback_framebuffer_wasmtime/<run-id>")
    p.add_argument("--record-render-stages", action="store_true",
                   help="Save Wasmtime render_path stage artifacts: "
                        "pre/post draw PNGs plus render stage JSON")
    p.add_argument("--record-render-step-checkpoints", action="store_true",
                   help="With --record-render-stages, save execute_pre/"
                        "execute_post images around executeLayerRenderCommands")
    p.add_argument("--render-artifact-dir", type=Path, default=None,
                   help="Render stage artifact output directory. Default: "
                        "tests/differential/artifacts/"
                        "motion_playback_render_stages_wasmtime/<run-id>")
    p.add_argument("--host-mode", action="store_true",
                   help=argparse.SUPPRESS)
    p.add_argument("--host-output", type=Path, help=argparse.SUPPRESS)
    p.add_argument("--host-frames", type=int, default=0,
                   help=argparse.SUPPRESS)
    p.add_argument("--manifest-startup-xp3", type=Path, default=None,
                   help=argparse.SUPPRESS)
    p.add_argument("--render-stage-out", type=Path, default=None,
                   help=argparse.SUPPRESS)
    return p.parse_args(argv)


def default_framebuffer_dir() -> Path:
    run_id = time.strftime("%Y%m%d-%H%M%S")
    return (
        REPO_ROOT / "tests" / "differential" / "artifacts"
        / "motion_playback_framebuffer_wasmtime" / run_id
    )


def default_render_artifact_dir() -> Path:
    run_id = time.strftime("%Y%m%d-%H%M%S")
    return (
        REPO_ROOT / "tests" / "differential" / "artifacts"
        / "motion_playback_render_stages_wasmtime" / run_id
    )


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
            "web_alert": self._return_zero,
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


def _read_render_probe_events(store, exports, memory) -> list[dict[str, Any]]:
    try:
        get_ptr = exports["krkr2_wasm_get_render_probe_ptr"]
        get_len = exports["krkr2_wasm_get_render_probe_len"]
        clear = exports["krkr2_wasm_clear_render_probe"]
    except KeyError as exc:
        raise RuntimeError(
            "Wasmtime guest does not expose render probe buffer exports; "
            "rebuild krkr2_wasmtime_guest") from exc

    length = int(get_len(store))
    if length <= 0:
        clear(store)
        return []
    ptr = int(get_ptr(store))
    text = read_string(store, memory, ptr, length)
    clear(store)
    events: list[dict[str, Any]] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"render probe JSONL decode failed at line {lineno}: {exc}"
            ) from exc
        if not isinstance(event, dict):
            raise RuntimeError(
                f"render probe JSONL line {lineno} is not an object: {event!r}")
        events.append(event)
    return events


def call_with_guest_bytes(store, memory, malloc, free, data: bytes, callback):
    ptr = malloc(store, len(data))
    if ptr == 0 and data:
        raise RuntimeError("guest malloc failed")
    try:
        write_bytes(store, memory, ptr, data)
        return callback(ptr, len(data))
    finally:
        free(store, ptr)


def _build_framebuffer_capture_xp3(specs: list[dict], work_dir: Path) -> Path:
    from oracle_runner.adapters import motion_playback as mpb

    specs_by_id = {s["id"]: s for s in specs}
    builder = getattr(mpb, "_build_framebuffer_capture_xp3", None)
    if builder is None:
        raise RuntimeError(
            "motion_playback adapter does not expose framebuffer XP3 builder")
    return builder(specs_by_id, FRAMEBUFFER_CAPTURE_GUEST_ROOT, work_dir)


def _build_render_stage_capture_xp3(specs: list[dict], work_dir: Path) -> Path:
    from oracle_runner.adapters import motion_playback as mpb

    specs_by_id = {s["id"]: s for s in specs}
    builder = getattr(mpb, "_build_render_stage_capture_xp3", None)
    if builder is None:
        raise RuntimeError(
            "motion_playback adapter does not expose render stage XP3 builder")
    return builder(specs_by_id, RENDER_CAPTURE_GUEST_ROOT, work_dir)


def _write_wasmtime_framebuffer_manifest(
    framebuffer_dir: Path,
    specs: list[dict],
    *,
    wasm_path: Path,
    startup_xp3: Path,
    manifest_startup_xp3: Path,
    bootstrap: BrowserBootstrapInfo,
) -> Path:
    from oracle_runner.adapters import motion_playback as mpb

    specs_by_id = {s["id"]: s for s in specs}
    cases: list[dict[str, Any]] = []
    total_frames = 0
    for spec_id in mpb.SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        expected = int(spec["frames"])
        case_dir = framebuffer_dir / spec_id
        images: list[dict[str, Any]] = []
        for frame in range(expected):
            rel = Path(spec_id) / f"frame_{frame:04d}.png"
            path = framebuffer_dir / rel
            if not path.exists():
                raise RuntimeError(f"missing Wasmtime framebuffer PNG: {path}")
            images.append(png_manifest_entry(
                frame=frame,
                path=path,
                rel=rel,
            ))
        extras = sorted(case_dir.glob("frame_*.png"))[expected:]
        if extras:
            raise RuntimeError(
                f"unexpected extra Wasmtime framebuffer PNG(s) for {spec_id}: "
                f"{[p.name for p in extras[:5]]}"
            )
        total_frames += len(images)
        cases.append({
            "caseId": spec_id,
            "mtnPath": spec.get("mtn_path"),
            "chara": spec.get("chara"),
            "label": spec.get("label"),
            "frames": expected,
            "images": images,
        })

    manifest = {
        "schema": FRAMEBUFFER_SCHEMA,
        "source": FRAMEBUFFER_SOURCE,
        "captureSurface": FRAMEBUFFER_CAPTURE_SURFACE,
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "localRoot": str(framebuffer_dir),
        "wasm": str(wasm_path),
        "startupXp3": str(manifest_startup_xp3),
        "captureXp3": str(startup_xp3),
        "fixture": {
            "guestRoot": str(bootstrap.root),
            "guestCaptureRoot": FRAMEBUFFER_CAPTURE_GUEST_ROOT,
            "xp3": bootstrap.xp3_guest_path,
            "window": {"width": 1920, "height": 1080},
            "deltaMs": 1000.0 / 60.0,
            "segmentOrder": list(mpb.SEGMENT_ORDER),
        },
        "summary": {
            "caseCount": len(cases),
            "frameCount": total_frames,
        },
        "cases": cases,
    }
    target = framebuffer_dir / "manifest.json"
    target.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True,
                   allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return target


def _collect_wasmtime_render_stage_capture(
    bootstrap: BrowserBootstrapInfo,
    render_artifact_dir: Path,
    specs: list[dict],
    *,
    record_render_step_checkpoints: bool = False,
) -> dict[str, Any]:
    from oracle_runner.adapters import motion_playback as mpb

    capture_root = bootstrap.root / RENDER_CAPTURE_GUEST_ROOT.lstrip("/")
    if not capture_root.is_dir():
        raise RuntimeError(
            f"Wasmtime render stage capture directory missing: {capture_root}")

    render_artifact_dir.mkdir(parents=True, exist_ok=True)
    images_root = render_artifact_dir / "images"
    if images_root.exists():
        shutil.rmtree(images_root)
    images_root.mkdir(parents=True, exist_ok=True)

    specs_by_id = {s["id"]: s for s in specs}
    cases: list[dict[str, Any]] = []
    total_images = 0
    global_frame_offset = 0
    for spec_id in mpb.SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        expected = int(spec["frames"])
        src_case = capture_root / spec_id
        if not src_case.is_dir():
            raise RuntimeError(
                f"Wasmtime render stage case directory missing: {src_case}")
        dst_case = images_root / spec_id
        shutil.copytree(src_case, dst_case)

        if record_render_step_checkpoints:
            for phase in RENDER_STEP_CHECKPOINT_SURFACES:
                src_phase = capture_root / "_execute" / phase
                if not src_phase.is_dir():
                    raise RuntimeError(
                        "missing Wasmtime execute checkpoint directory: "
                        f"{src_phase}")
                dst_phase = dst_case / phase
                if dst_phase.exists():
                    shutil.rmtree(dst_phase)
                dst_phase.mkdir(parents=True, exist_ok=True)
                for local_frame in range(expected):
                    global_frame = global_frame_offset + local_frame
                    src_png = src_phase / f"frame_{global_frame:04d}.png"
                    if not src_png.exists():
                        raise RuntimeError(
                            "missing Wasmtime execute checkpoint PNG: "
                            f"{src_png}")
                    shutil.copy2(
                        src_png, dst_phase / f"frame_{local_frame:04d}.png")

        phases: dict[str, list[dict[str, Any]]] = {}
        phases_to_collect = list(RENDER_CAPTURE_SURFACES)
        if record_render_step_checkpoints:
            phases_to_collect.extend(RENDER_STEP_CHECKPOINT_SURFACES)
        for phase in phases_to_collect:
            phase_dir = dst_case / phase
            if not phase_dir.is_dir():
                raise RuntimeError(
                    f"missing Wasmtime render stage image directory: {phase_dir}")
            images: list[dict[str, Any]] = []
            for frame in range(expected):
                rel = Path("images") / spec_id / phase / f"frame_{frame:04d}.png"
                path = render_artifact_dir / rel
                if not path.exists():
                    raise RuntimeError(
                        f"missing Wasmtime render stage PNG: {path}")
                images.append(png_manifest_entry(
                    frame=frame,
                    phase=phase,
                    path=path,
                    rel=rel,
                ))
            extras = sorted(phase_dir.glob("frame_*.png"))[expected:]
            if extras:
                raise RuntimeError(
                    f"unexpected extra Wasmtime render stage PNG(s) for "
                    f"{spec_id}/{phase}: {[p.name for p in extras[:5]]}"
                )
            phases[phase] = images
            total_images += len(images)

        cases.append({
            "caseId": spec_id,
            "mtnPath": spec.get("mtn_path"),
            "chara": spec.get("chara"),
            "label": spec.get("label"),
            "frames": expected,
            "phases": phases,
        })
        global_frame_offset += expected

    capture_surfaces = list(RENDER_CAPTURE_SURFACES)
    if record_render_step_checkpoints:
        capture_surfaces.extend(RENDER_STEP_CHECKPOINT_SURFACES)
    image_manifest = {
        "guestCaptureRoot": RENDER_CAPTURE_GUEST_ROOT,
        "captureSurfaces": capture_surfaces,
        "cases": cases,
        "summary": {
            "caseCount": len(cases),
            "imageCount": total_images,
        },
    }
    image_manifest_path = render_artifact_dir / "image_manifest.json"
    image_manifest_path.write_text(
        json.dumps(image_manifest, indent=2, ensure_ascii=True,
                   allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return image_manifest


def _collect_wasmtime_framebuffer_capture(
    bootstrap: BrowserBootstrapInfo,
    framebuffer_dir: Path,
    specs: list[dict],
    *,
    wasm_path: Path,
    startup_xp3: Path,
    manifest_startup_xp3: Path,
) -> Path:
    from oracle_runner.adapters import motion_playback as mpb

    capture_root = bootstrap.root / FRAMEBUFFER_CAPTURE_GUEST_ROOT.lstrip("/")
    if not capture_root.is_dir():
        raise RuntimeError(
            f"Wasmtime framebuffer capture directory missing: {capture_root}")

    framebuffer_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = framebuffer_dir / "manifest.json"
    if manifest_path.exists():
        manifest_path.unlink()

    specs_by_id = {s["id"]: s for s in specs}
    for spec_id in mpb.SEGMENT_ORDER:
        if spec_id not in specs_by_id:
            continue
        src_case = capture_root / spec_id
        if not src_case.is_dir():
            raise RuntimeError(
                f"Wasmtime framebuffer case directory missing: {src_case}")
        dst_case = framebuffer_dir / spec_id
        if dst_case.exists():
            shutil.rmtree(dst_case)
        shutil.copytree(src_case, dst_case)

    return _write_wasmtime_framebuffer_manifest(
        framebuffer_dir, specs,
        wasm_path=wasm_path,
        startup_xp3=startup_xp3,
        manifest_startup_xp3=manifest_startup_xp3,
        bootstrap=bootstrap)


def drive_full_guest(wasm_path: Path, startup_xp3: Path,
                     frames: int, *,
                     framebuffer_dir: Path | None = None,
                     render_artifact_dir: Path | None = None,
                     record_render_step_checkpoints: bool = False,
                     render_stage_out: Path | None = None,
                     specs: list[dict] | None = None,
                     manifest_startup_xp3: Path | None = None) -> dict[str, Any]:
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
            if framebuffer_dir is not None:
                if specs is None:
                    raise RuntimeError(
                        "framebuffer capture requires motion_playback specs")
                capture_root = (
                    bootstrap.root / FRAMEBUFFER_CAPTURE_GUEST_ROOT.lstrip("/")
                )
                for spec in specs:
                    (capture_root / str(spec["id"])).mkdir(
                        parents=True, exist_ok=True)
            if render_artifact_dir is not None:
                if specs is None:
                    raise RuntimeError(
                        "render stage capture requires motion_playback specs")
                capture_root = (
                    bootstrap.root / RENDER_CAPTURE_GUEST_ROOT.lstrip("/")
                )
                for spec in specs:
                    for phase in RENDER_CAPTURE_SURFACES:
                        (capture_root / str(spec["id"]) / phase).mkdir(
                            parents=True, exist_ok=True)
                if record_render_step_checkpoints:
                    for phase in RENDER_STEP_CHECKPOINT_SURFACES:
                        (capture_root / "_execute" / phase).mkdir(
                            parents=True, exist_ok=True)
            summary = _drive_full_guest_with_bootstrap(
                wasmtime, wasm_path, bootstrap, frames,
                render_stage_out=render_stage_out)
            if framebuffer_dir is not None:
                manifest = _collect_wasmtime_framebuffer_capture(
                    bootstrap, framebuffer_dir, specs,
                    wasm_path=wasm_path,
                    startup_xp3=startup_xp3,
                    manifest_startup_xp3=(
                        manifest_startup_xp3 or startup_xp3))
                summary["framebufferManifest"] = str(manifest)
            if render_artifact_dir is not None:
                image_manifest = _collect_wasmtime_render_stage_capture(
                    bootstrap, render_artifact_dir, specs,
                    record_render_step_checkpoints=(
                        record_render_step_checkpoints))
                summary["renderStageImageManifest"] = image_manifest["summary"]
            return summary
        except Exception as exc:
            raise RuntimeError(f"{exc}\n{bootstrap.summary()}") from exc


def _drive_full_guest_with_bootstrap(wasmtime, wasm_path: Path,
                                     bootstrap: BrowserBootstrapInfo,
                                     frames: int, *,
                                     render_stage_out: Path | None = None
                                     ) -> dict[str, Any]:
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

    render_probe_summary: dict[str, Any] | None = None
    if render_stage_out is not None:
        events = _read_render_probe_events(store, exports, memory)
        render_stage_out.parent.mkdir(parents=True, exist_ok=True)
        render_stage_out.write_text(
            json.dumps(events, ensure_ascii=False, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        render_probe_summary = {"eventCount": len(events)}

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
        "renderProbe": render_probe_summary,
    }


def run_python_host_driver(wasm_path: Path, startup_xp3: Path,
                           frames: int, output: Path, *,
                           framebuffer_dir: Path | None = None,
                           render_artifact_dir: Path | None = None,
                           record_render_step_checkpoints: bool = False,
                           render_stage_out: Path | None = None,
                           specs: list[dict] | None = None,
                           manifest_startup_xp3: Path | None = None) -> int:
    summary = drive_full_guest(
        wasm_path, startup_xp3, frames,
        framebuffer_dir=framebuffer_dir,
        render_artifact_dir=render_artifact_dir,
        record_render_step_checkpoints=record_render_step_checkpoints,
        render_stage_out=render_stage_out,
        specs=specs,
        manifest_startup_xp3=manifest_startup_xp3)
    output.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return 0


def run_lldb_guest_trace(wasm_path: Path, startup_xp3: Path, *,
                         spec_dir: Path,
                         expected_frames: int,
                         timeout: float,
                         host_python: Path,
                         framebuffer_dir: Path | None = None,
                         render_artifact_dir: Path | None = None,
                         record_render_step_checkpoints: bool = False,
                         manifest_startup_xp3: Path | None = None
                         ) -> tuple[list[dict], list[dict]]:
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
        render_stage_path = temp / "render_stages.json"
        host_report = temp / "host.json"
        tracer = REPO_ROOT / "tests" / "differential" / "python" / \
            "wasm_lldb_motion_trace.py"
        cmd = [
            "xcrun", "python3", str(tracer),
            "--driver", str(Path(__file__).resolve()),
            "--host-python", str(host_python),
            "--wasm", str(wasm_path),
            "--startup-xp3", str(startup_xp3),
            "--spec-dir", str(spec_dir),
            "--trace-out", str(trace_path),
            "--host-output", str(host_report),
            "--expected-frames", str(expected_frames),
            "--timeout", str(timeout),
            "--repo-root", str(REPO_ROOT),
        ]
        if framebuffer_dir is not None:
            cmd += [
                "--record-framebuffer",
                "--framebuffer-dir", str(framebuffer_dir),
            ]
            if manifest_startup_xp3 is not None:
                cmd += [
                    "--manifest-startup-xp3", str(manifest_startup_xp3),
                ]
        if render_artifact_dir is not None:
            cmd += [
                "--record-render-stages",
                "--render-artifact-dir", str(render_artifact_dir),
                "--render-stage-out", str(render_stage_path),
            ]
            if record_render_step_checkpoints:
                cmd.append("--record-render-step-checkpoints")
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
        render_events: list[dict[str, Any]] = []
        if render_artifact_dir is not None:
            if not render_stage_path.exists():
                raise RuntimeError(
                    "Wasmtime LLDB tracer did not write render stage output\n"
                    f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
                )
            try:
                render_events = json.loads(
                    render_stage_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                raise RuntimeError(
                    f"Wasmtime render stage JSON decode failed: {exc}\n"
                    f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
                ) from exc
    if not isinstance(events, list):
        raise RuntimeError(f"Wasmtime LLDB trace root is not a list: {type(events)}")
    if render_artifact_dir is not None and not isinstance(render_events, list):
        raise RuntimeError(
            f"Wasmtime render stage trace root is not a list: {type(render_events)}")
    return events, render_events


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


def render_case_segments(events: list[dict], specs: list[dict], mpb) -> list[dict]:
    specs_by_id = {s["id"]: s for s in specs}
    segments = _segment_events(events)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    out: list[dict[str, Any]] = []
    for i, spec_id in enumerate(mpb.SEGMENT_ORDER):
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        if i >= len(substantive):
            raise RuntimeError(
                f"missing Wasmtime segment for render stage case {spec_id}")
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"Wasmtime segment {i} ({spec_id}) has {len(frames)} frame(s); "
                f"render stage capture requires {wanted}.")
        selected = frames[:wanted]
        frame_ids = [int(frame["frameId"]) for frame in selected]
        out.append({
            "caseId": spec_id,
            "player": substantive[i].get("player"),
            "firstFrameId": min(frame_ids),
            "lastFrameId": max(frame_ids),
            "frames": selected,
        })
    return out


def _assign_render_case(ev: dict[str, Any],
                        case_segments: list[dict[str, Any]]) -> str | None:
    frame_id = ev.get("frameId")
    if not isinstance(frame_id, int):
        return None
    for segment in case_segments:
        if int(segment["firstFrameId"]) <= frame_id <= int(segment["lastFrameId"]):
            return str(segment["caseId"])
    return None


def _render_stage_summary(events: list[dict[str, Any]],
                          trace_frame_count: int) -> dict[str, Any]:
    frame_ids = [
        int(ev["frameId"]) for ev in events if isinstance(ev.get("frameId"), int)
    ]
    seqs = [int(ev["seq"]) for ev in events if isinstance(ev.get("seq"), int)]
    kinds = Counter(str(ev.get("kind")) for ev in events)
    summary: dict[str, Any] = {
        "eventCount": len(events),
        "kindCounts": dict(sorted(kinds.items())),
        "traceFrameCount": trace_frame_count,
        "framesWithEvents": len(set(frame_ids)),
    }
    if frame_ids:
        summary["eventFrameIdRange"] = [min(frame_ids), max(frame_ids)]
    if seqs:
        summary["eventSeqRange"] = [min(seqs), max(seqs)]
    return summary


def _split_render_events_by_stage_case(
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
) -> dict[str, dict[str, list[dict[str, Any]]]]:
    out: dict[str, dict[str, list[dict[str, Any]]]] = {}
    for ev in events:
        stage = str(ev.get("stage") or "")
        if stage not in RENDER_STAGES or stage == "layer_save":
            continue
        case_id = _assign_render_case(ev, case_segments)
        if case_id is None:
            continue
        cloned = dict(ev)
        cloned["caseId"] = case_id
        out.setdefault(stage, {}).setdefault(case_id, []).append(cloned)
    return out


def _layer_save_events_for_case(case_images: dict[str, Any],
                                case_segment: dict[str, Any]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    case_id = str(case_images["caseId"])
    first_frame_id = int(case_segment["firstFrameId"])
    seq = 0
    for phase in RENDER_CAPTURE_SURFACES:
        for image in case_images.get("phases", {}).get(phase, []):
            local_frame = int(image["frame"])
            events.append({
                "schema": RENDER_EVENT_SCHEMA,
                "source": RENDER_SOURCE,
                "stage": "layer_save",
                "kind": "save_layer_image",
                "samplePoint": f"startup.tjs.{phase}",
                "caseId": case_id,
                "frameId": first_frame_id + local_frame,
                "frame": local_frame,
                "seq": seq,
                "phase": phase,
            "path": image["path"],
            "width": image["width"],
            "height": image["height"],
            "bytes": image["bytes"],
            **({"rgbaSha256": image["rgbaSha256"]}
               if "rgbaSha256" in image else {}),
                "diagnostics": {
                    "synthetic": True,
                    "source": "wasmtime-render-png-manifest",
                },
            })
            seq += 1
    return events


def _phase_images_by_frame(
    case_images: dict[str, Any],
    phase: str,
) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for image in case_images.get("phases", {}).get(phase, []):
        frame = image.get("frame")
        if isinstance(frame, int):
            out[frame] = dict(image)
    return out


def _add_image_manifest_error(
    event: dict[str, Any],
    message: str,
) -> None:
    diagnostics = dict(event.get("diagnostics") or {})
    existing = diagnostics.get("imageManifestError")
    if existing:
        diagnostics["imageManifestError"] = f"{existing}; {message}"
    else:
        diagnostics["imageManifestError"] = message
    event["diagnostics"] = diagnostics


def _set_draw_path_image_changed(
    event: dict[str, Any],
    image_changed: bool | None,
) -> None:
    draw_path = event.get("drawPath")
    if isinstance(draw_path, dict):
        updated = dict(draw_path)
        updated["imageChanged"] = image_changed
        event["drawPath"] = updated


def _enrich_draw_dispatch_events_for_case(
    events: list[dict[str, Any]],
    case_segment: dict[str, Any],
    case_images: dict[str, Any],
) -> list[dict[str, Any]]:
    first_frame_id = int(case_segment["firstFrameId"])
    last_frame_id = int(case_segment["lastFrameId"])
    pre_by_frame = _phase_images_by_frame(case_images, "pre_draw")
    post_by_frame = _phase_images_by_frame(case_images, "post_draw")
    enriched: list[dict[str, Any]] = []

    for source_event in events:
        event = dict(source_event)
        frame_id = event.get("frameId")
        if not isinstance(frame_id, int):
            _add_image_manifest_error(event, "event has no integer frameId")
            enriched.append(event)
            continue
        if frame_id < first_frame_id or frame_id > last_frame_id:
            _add_image_manifest_error(
                event,
                f"frameId {frame_id} outside case segment "
                f"{first_frame_id}..{last_frame_id}",
            )
            enriched.append(event)
            continue

        local_frame = frame_id - first_frame_id
        pre_draw = pre_by_frame.get(local_frame)
        post_draw = post_by_frame.get(local_frame)

        kind = str(event.get("kind") or "")
        if kind == "draw_enter":
            event["preDrawImage"] = pre_draw
            if pre_draw is None:
                _add_image_manifest_error(
                    event, f"missing pre_draw image for frame {local_frame}")
        elif kind == "draw_leave":
            event["preDrawImage"] = pre_draw
            event["postDrawImage"] = post_draw
            image_changed = images_changed(pre_draw, post_draw)
            event["imageChanged"] = image_changed
            _set_draw_path_image_changed(event, image_changed)
            if pre_draw is None:
                _add_image_manifest_error(
                    event, f"missing pre_draw image for frame {local_frame}")
            if post_draw is None:
                _add_image_manifest_error(
                    event, f"missing post_draw image for frame {local_frame}")

        enriched.append(event)

    return enriched


def _semantic_render_item(item: dict[str, Any]) -> dict[str, Any]:
    flags = item.get("flags")
    clip_valid = (
        bool(flags.get("clipValid21"))
        if isinstance(flags, dict) else False
    )
    clip_rect = item.get("clipRect") if clip_valid else [0, 0, 0, 0]
    build_clip_rect = (
        item.get("buildClipRect", item.get("clipRect"))
        if clip_valid else [0, 0, 0, 0]
    )
    return {
        "index": item.get("index"),
        "nodeIndex": item.get("nodeIndex"),
        "sourceKey": item.get("sourceKey"),
        "flags": flags,
        "layerIds": item.get("layerIds"),
        "sortKey64": item.get("sortKey64"),
        "paintBox": item.get("paintBox"),
        "clipRect": clip_rect,
        "buildClipRect": build_clip_rect,
        "dirtyRect": item.get("dirtyRect"),
        "viewportRect": item.get("viewportRect"),
        "sourceGate232": item.get("sourceGate232"),
        "stencilType244": item.get("stencilType244"),
        "parentItemIndex": item.get("parentItemIndex"),
        "parentItem264": item.get("parentItem264"),
        "childItemCount": item.get("childItemCount"),
        "meshType280": item.get("meshType280"),
        "leafLayerVariantTag": item.get("leafLayerVariantTag"),
        "composedLayerVariantTag": item.get("composedLayerVariantTag"),
        "leafLayerVariantTag320": item.get("leafLayerVariantTag320"),
        "composedLayerVariantTag340": item.get("composedLayerVariantTag340"),
        "leafBuilt": item.get("leafBuilt"),
        "composedBuilt": item.get("composedBuilt"),
        "executedDirect": item.get("executedDirect"),
    }


def _has_valid_clip_rect(item: dict[str, Any]) -> bool:
    flags = item.get("flags")
    flag16 = bool(flags.get("flag16")) if isinstance(flags, dict) else False
    flag17 = bool(flags.get("flag17")) if isinstance(flags, dict) else False
    if flag16 or flag17:
        return False
    source_gate = item.get("sourceGate232")
    try:
        if int(source_gate) == 0:
            return False
    except (TypeError, ValueError):
        pass
    clip_valid = (
        bool(flags.get("clipValid21"))
        if isinstance(flags, dict) else False
    )
    rect = (
        item.get("buildClipRect", item.get("clipRect"))
        if clip_valid else item.get("paintBox")
    )
    if not clip_valid:
        viewport = item.get("viewportRect")
        if isinstance(rect, list) and len(rect) == 4 and \
                isinstance(viewport, list) and len(viewport) == 4:
            try:
                v_left, v_top, v_right, v_bottom = (
                    float(value) for value in viewport)
                left, top, right, bottom = (float(value) for value in rect)
            except (TypeError, ValueError):
                pass
            else:
                if v_right >= v_left and v_bottom >= v_top:
                    rect = [
                        max(left, math.floor(v_left)),
                        max(top, math.floor(v_top)),
                        min(right, math.ceil(v_right)),
                        min(bottom, math.ceil(v_bottom)),
                    ]
    if not isinstance(rect, list) or len(rect) != 4:
        return False
    try:
        left, top, right, bottom = (float(value) for value in rect)
    except (TypeError, ValueError):
        return False
    return left < right and top < bottom


def _build_flow_summary(event: dict[str, Any]) -> dict[str, Any]:
    if isinstance(event.get("buildFlow"), dict):
        return dict(event["buildFlow"])
    main_items = event.get("mainListSemanticItems")
    if isinstance(main_items, list):
        main_semantic_items = [
            _semantic_render_item(item)
            for item in main_items if isinstance(item, dict)
        ]
        aux_items = event.get("auxListSemanticItems")
        aux_semantic_items = [
            _semantic_render_item(item)
            for item in aux_items if isinstance(item, dict)
        ] if isinstance(aux_items, list) else []
        return {
            "inputItemCount": event.get("inputItemCount"),
            "builtItemCount": event.get("builtItemCount",
                                        len(main_semantic_items)),
            "validDrawableItemCount": event.get("validDrawableItemCount"),
            "leafBuiltCount": event.get("leafBuiltCount"),
            "composedBuiltCount": event.get("composedBuiltCount"),
            "mainListSemanticItems": main_semantic_items,
            "auxListSemanticItems": aux_semantic_items,
            "items": main_semantic_items,
        }
    render_lists = event.get("renderLists")
    main_list = (
        render_lists.get("mainList")
        if isinstance(render_lists, dict) else None
    )
    items = main_list.get("items") if isinstance(main_list, dict) else []
    semantic_items = [
        _semantic_render_item(item)
        for item in items if isinstance(item, dict)
    ]
    return {
        "inputItemCount": main_list.get("count")
        if isinstance(main_list, dict) else None,
        "builtItemCount": len(semantic_items),
        "validDrawableItemCount": sum(
            1 for item in semantic_items if _has_valid_clip_rect(item)),
        "leafBuiltCount": None,
        "composedBuiltCount": None,
        "mainListSemanticItems": semantic_items,
        "auxListSemanticItems": [],
        "items": semantic_items,
    }


def _enrich_render_commands_events_for_case(
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    enriched: list[dict[str, Any]] = []
    for source_event in events:
        event = dict(source_event)
        if str(event.get("kind") or "").startswith("build_commands"):
            event["buildFlow"] = _build_flow_summary(event)
        enriched.append(event)
    return enriched


def _enrich_render_execute_events_for_case(
    events: list[dict[str, Any]],
    case_segment: dict[str, Any],
    case_images: dict[str, Any],
) -> list[dict[str, Any]]:
    first_frame_id = int(case_segment["firstFrameId"])
    last_frame_id = int(case_segment["lastFrameId"])
    pre_by_frame = _phase_images_by_frame(case_images, "execute_pre")
    post_by_frame = _phase_images_by_frame(case_images, "execute_post")
    enriched: list[dict[str, Any]] = []

    for source_event in events:
        event = dict(source_event)
        frame_id = event.get("frameId")
        if not isinstance(frame_id, int):
            _add_image_manifest_error(event, "event has no integer frameId")
            enriched.append(event)
            continue
        if frame_id < first_frame_id or frame_id > last_frame_id:
            _add_image_manifest_error(
                event,
                f"frameId {frame_id} outside case segment "
                f"{first_frame_id}..{last_frame_id}",
            )
            enriched.append(event)
            continue

        local_frame = frame_id - first_frame_id
        execute_pre = pre_by_frame.get(local_frame)
        execute_post = post_by_frame.get(local_frame)
        kind = str(event.get("kind") or "")
        if kind == "execute_enter":
            event["executePreImage"] = execute_pre
            if execute_pre is None:
                _add_image_manifest_error(
                    event,
                    f"missing execute_pre image for frame {local_frame}",
                )
        elif kind == "execute_leave":
            event["executePreImage"] = execute_pre
            event["executePostImage"] = execute_post
            image_changed = images_changed(execute_pre, execute_post)
            event["executeImageChanged"] = image_changed
            if execute_pre is None:
                _add_image_manifest_error(
                    event,
                    f"missing execute_pre image for frame {local_frame}",
                )
            if execute_post is None:
                _add_image_manifest_error(
                    event,
                    f"missing execute_post image for frame {local_frame}",
                )
        enriched.append(event)

    return enriched


def write_render_stage_artifacts(
    *,
    artifact_dir: Path,
    specs: list[dict[str, Any]],
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
    image_manifest: dict[str, Any],
    wasm_path: Path,
    startup_xp3: Path,
    capture_xp3: Path,
) -> Path:
    events_by_stage_case = _split_render_events_by_stage_case(
        events, case_segments)
    case_segment_by_id = {
        str(segment["caseId"]): segment for segment in case_segments
    }
    image_case_by_id = {
        str(case["caseId"]): case for case in image_manifest.get("cases", [])
    }
    events_root = artifact_dir / "events"
    total_event_count = 0
    specs_by_id = {str(spec["id"]): spec for spec in specs}

    for stage in RENDER_STAGES:
        for spec_id in [sid for sid in case_segment_by_id if sid in specs_by_id]:
            case_segment = case_segment_by_id[spec_id]
            if stage == "layer_save":
                stage_events = _layer_save_events_for_case(
                    image_case_by_id.get(spec_id, {
                        "caseId": spec_id,
                        "phases": {},
                    }),
                    case_segment,
                )
            else:
                stage_events = (
                    events_by_stage_case.get(stage, {}).get(spec_id, [])
                )
                if stage == "draw_dispatch":
                    stage_events = _enrich_draw_dispatch_events_for_case(
                        stage_events,
                        case_segment,
                        image_case_by_id.get(spec_id, {
                            "caseId": spec_id,
                            "phases": {},
                        }),
                    )
                elif stage == "render_commands":
                    stage_events = _enrich_render_commands_events_for_case(
                        stage_events)
                elif stage == "render_execute":
                    stage_events = _enrich_render_execute_events_for_case(
                        stage_events,
                        case_segment,
                        image_case_by_id.get(spec_id, {
                            "caseId": spec_id,
                            "phases": {},
                        }),
                    )
            total_event_count += len(stage_events)
            payload = {
                "schema": RENDER_SCHEMA,
                "source": RENDER_SOURCE,
                "stage": stage,
                "caseId": spec_id,
                "events": stage_events,
                "summary": _render_stage_summary(
                    stage_events, len(case_segment["frames"])),
            }
            target = events_root / stage / f"{spec_id}.wasmtime.json"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                json.dumps(payload, indent=2, ensure_ascii=True,
                           allow_nan=False) + "\n",
                encoding="utf-8",
            )

    manifest = {
        "schema": RENDER_SCHEMA,
        "source": RENDER_SOURCE,
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "localRoot": str(artifact_dir),
        "wasm": str(wasm_path),
        "startupXp3": str(startup_xp3),
        "captureXp3": str(capture_xp3),
        "fixture": {
            "guestCaptureRoot": RENDER_CAPTURE_GUEST_ROOT,
            "window": {"width": 1920, "height": 1080},
            "deltaMs": 1000.0 / 60.0,
            "segmentOrder": [str(segment["caseId"]) for segment in case_segments],
        },
        "stages": list(RENDER_STAGES),
        "eventsRoot": "events",
        "imagesRoot": "images",
        "images": image_manifest,
        "summary": {
            "caseCount": len(case_segments),
            "traceFlattenFrameCount": sum(
                len(segment["frames"]) for segment in case_segments),
            "renderEventCount": total_event_count,
            "imageCount": image_manifest.get("summary", {}).get("imageCount", 0),
        },
    }
    target = artifact_dir / "manifest.json"
    target.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True,
                   allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return target


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    wasm_path = Path(args.wasm)
    startup_xp3 = Path(args.startup_xp3)
    framebuffer_dir = (
        Path(args.framebuffer_dir) if args.framebuffer_dir is not None
        else default_framebuffer_dir()
    ) if args.record_framebuffer else None
    render_artifact_dir = (
        Path(args.render_artifact_dir) if args.render_artifact_dir is not None
        else default_render_artifact_dir()
    ) if args.record_render_stages else None

    if framebuffer_dir is not None and render_artifact_dir is not None:
        print("--record-framebuffer and --record-render-stages currently use "
              "different capture XP3 fixtures; run them separately",
              file=sys.stderr)
        return 2
    if args.record_render_step_checkpoints and render_artifact_dir is None:
        print("--record-render-step-checkpoints requires "
              "--record-render-stages", file=sys.stderr)
        return 2

    if args.host_mode:
        if args.host_output is None:
            print("--host-output is required in --host-mode", file=sys.stderr)
            return 2
        frames = int(args.host_frames or 0)
        if frames <= 0:
            print("--host-frames must be positive in --host-mode",
                  file=sys.stderr)
            return 2
        if render_artifact_dir is not None and args.render_stage_out is None:
            print("--render-stage-out is required with --record-render-stages "
                  "in --host-mode", file=sys.stderr)
            return 2
        try:
            specs = load_specs(spec_dir)
            return run_python_host_driver(
                wasm_path, startup_xp3, frames, args.host_output,
                framebuffer_dir=framebuffer_dir,
                render_artifact_dir=render_artifact_dir,
                record_render_step_checkpoints=(
                    args.record_render_step_checkpoints),
                render_stage_out=args.render_stage_out,
                specs=specs,
                manifest_startup_xp3=(
                    Path(args.manifest_startup_xp3)
                    if args.manifest_startup_xp3 is not None
                    else None))
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
        trace_startup_xp3 = startup_xp3
        temp_capture: tempfile.TemporaryDirectory[str] | None = None
        if framebuffer_dir is not None:
            temp_capture = tempfile.TemporaryDirectory(
                prefix="krkr2-motion-wasmtime-framebuffer-xp3-")
            trace_startup_xp3 = _build_framebuffer_capture_xp3(
                specs, Path(temp_capture.name))
        elif render_artifact_dir is not None:
            temp_capture = tempfile.TemporaryDirectory(
                prefix="krkr2-motion-wasmtime-render-stage-xp3-")
            trace_startup_xp3 = _build_render_stage_capture_xp3(
                specs, Path(temp_capture.name))
        try:
            port_events, render_events = run_lldb_guest_trace(
                wasm_path,
                trace_startup_xp3,
                spec_dir=spec_dir,
                expected_frames=expected_frames,
                timeout=args.lldb_timeout,
                host_python=args.host_python,
                framebuffer_dir=framebuffer_dir,
                render_artifact_dir=render_artifact_dir,
                record_render_step_checkpoints=(
                    args.record_render_step_checkpoints),
                manifest_startup_xp3=startup_xp3
                if (framebuffer_dir is not None or render_artifact_dir is not None)
                else None,
            )
        finally:
            if temp_capture is not None:
                temp_capture.cleanup()
        port_frames_by_id = partition_port_frames(port_events, specs, mpb)
        render_manifest: Path | None = None
        if render_artifact_dir is not None:
            image_manifest_path = render_artifact_dir / "image_manifest.json"
            if not image_manifest_path.exists():
                raise RuntimeError(
                    f"Wasmtime render image manifest missing: {image_manifest_path}")
            image_manifest = json.loads(
                image_manifest_path.read_text(encoding="utf-8"))
            render_manifest = write_render_stage_artifacts(
                artifact_dir=render_artifact_dir,
                specs=specs,
                events=render_events,
                case_segments=render_case_segments(port_events, specs, mpb),
                image_manifest=image_manifest,
                wasm_path=wasm_path,
                startup_xp3=startup_xp3,
                capture_xp3=trace_startup_xp3,
            )
            image_manifest_path.unlink(missing_ok=True)
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
    if framebuffer_dir is not None:
        manifest = framebuffer_dir / "manifest.json"
        if manifest.exists():
            print(f"[record] framebuffer manifest: {manifest}")
    if render_artifact_dir is not None:
        manifest = render_artifact_dir / "manifest.json"
        if manifest.exists():
            print(f"[record] render stage manifest: {manifest}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
