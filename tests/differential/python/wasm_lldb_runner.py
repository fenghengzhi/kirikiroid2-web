#!/usr/bin/env python3
"""Shared LLDB helpers for Wasmtime guest-debug differential runners."""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_HOST_PYTHON_RAW = (
    os.environ.get("KRKR2_HOST_PYTHON") or shutil.which("python3")
)
DEFAULT_HOST_PYTHON = (
    Path(DEFAULT_HOST_PYTHON_RAW) if DEFAULT_HOST_PYTHON_RAW else None
)


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


def make_debug_engine(wasmtime):
    config = wasmtime.Config()
    config.debug_info = True
    config.cranelift_opt_level = "none"
    return wasmtime.Engine(config)


def instantiate_standalone_module(wasmtime, wasm_path: Path):
    engine = make_debug_engine(wasmtime)
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)
    wasi = wasmtime.WasiConfig()
    wasi.inherit_stdout()
    wasi.inherit_stderr()
    store.set_wasi(wasi)
    linker = wasmtime.Linker(engine)
    linker.define_wasi()
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


def load_lldb():
    attempts: list[str] = []

    def valid_lldb_module(module) -> bool:
        return hasattr(module, "SBDebugger")

    def describe_lldb_module(module) -> str:
        return (
            f"file={getattr(module, '__file__', None)!r}, "
            f"path={list(getattr(module, '__path__', []))!r}"
        )

    def forget_invalid_lldb_module() -> None:
        module = sys.modules.get("lldb")
        if module is not None and not valid_lldb_module(module):
            del sys.modules["lldb"]

    if sys.platform == "darwin":
        try:
            lldb_python = subprocess.check_output(
                ["xcrun", "lldb", "-P"],
                text=True,
                stderr=subprocess.STDOUT,
            ).strip()
            if lldb_python and lldb_python not in sys.path:
                sys.path.insert(0, lldb_python)
            import lldb  # type: ignore
            if not valid_lldb_module(lldb):
                raise RuntimeError("imported lldb module has no SBDebugger")
            return lldb
        except Exception as exc:
            if os.environ.get("KRKR2_LLDB_PYTHON_REEXEC") != "1":
                try:
                    xcrun_python = subprocess.check_output(
                        ["xcrun", "--find", "python3"],
                        text=True,
                        stderr=subprocess.STDOUT,
                    ).strip()
                except Exception:
                    xcrun_python = ""
                if xcrun_python:
                    env = dict(os.environ)
                    env["KRKR2_LLDB_PYTHON_REEXEC"] = "1"
                    env.setdefault("KRKR2_HOST_PYTHON", sys.executable)
                    os.execve(xcrun_python, [xcrun_python, *sys.argv], env)
            raise RuntimeError(
                "failed to import LLDB Python module. Check:\n"
                "  xcrun lldb -P\n"
                "  xcrun python3 -c 'import sys; "
                "sys.path.insert(0, __import__(\"subprocess\").check_output("
                "[\"xcrun\", \"lldb\", \"-P\"], text=True).strip()); "
                "import lldb; print(lldb.SBDebugger)'"
            ) from exc

    candidates = ["lldb", "lldb-20", "lldb-19", "lldb-18", "lldb-17"]
    for binary in candidates:
        path = shutil.which(binary)
        if not path:
            continue
        try:
            lldb_python = subprocess.check_output(
                [path, "-P"],
                text=True,
                stderr=subprocess.STDOUT,
            ).strip()
            if lldb_python and lldb_python not in sys.path:
                sys.path.insert(0, lldb_python)
            lldb_package = str(Path(lldb_python) / "lldb") if lldb_python else ""
            if lldb_package and lldb_package not in sys.path:
                sys.path.insert(1, lldb_package)
            forget_invalid_lldb_module()
            import lldb  # type: ignore
            if not valid_lldb_module(lldb):
                attempts.append(
                    f"{binary} -P -> {lldb_python!r} produced invalid "
                    f"lldb module: {describe_lldb_module(lldb)}"
                )
                forget_invalid_lldb_module()
                continue
            return lldb
        except Exception as exc:
            attempts.append(f"{binary} failed: {exc!r}")
            forget_invalid_lldb_module()
            continue

    try:
        import lldb  # type: ignore
        if valid_lldb_module(lldb):
            return lldb
        attempts.append(
            "direct import produced invalid lldb module: "
            f"{describe_lldb_module(lldb)}"
        )
        forget_invalid_lldb_module()
    except Exception as exc:
        attempts.append(f"direct import failed: {exc!r}")
        forget_invalid_lldb_module()
    raise RuntimeError(
        "failed to import LLDB Python module. Install lldb and python lldb "
        "bindings, then verify `python3 -c 'import lldb'` or `lldb -P`.\n"
        "Attempts:\n  " + "\n  ".join(attempts)
    )


def run_lldb_command(lldb, debugger, command: str) -> None:
    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(command, result)
    if not result.Succeeded():
        raise RuntimeError(
            f"LLDB command failed: {command}\n{result.GetError()}"
        )


def sb_int(value, default: int | None = None) -> int | None:
    if not value or not value.IsValid():
        return default
    raw = value.GetValue()
    if raw is not None:
        try:
            return int(raw, 0)
        except Exception:
            pass
    try:
        return int(value.GetValueAsSigned(default if default is not None else 0))
    except Exception:
        return default


def sb_uint(value, default: int | None = None) -> int | None:
    if not value or not value.IsValid():
        return default
    raw = value.GetValue()
    if raw is not None:
        try:
            return int(raw, 0)
        except Exception:
            pass
    try:
        return int(value.GetValueAsUnsigned(default if default is not None else 0))
    except Exception:
        return default


def register_int_arg(frame, index: int) -> int:
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        reg = f"x{4 + index}"
    else:
        raise RuntimeError(f"unsupported LLDB probe architecture: {machine}")
    value = sb_uint(frame.FindRegister(reg))
    if value is None:
        raise RuntimeError(f"probe integer arg {index} missing register {reg}")
    return value


def register_double_arg(frame, index: int) -> float:
    reg = f"d{index}"
    value = frame.FindRegister(reg)
    if not value or not value.IsValid():
        raise RuntimeError(f"probe double arg {index} missing register {reg}")
    raw = value.GetValue()
    if raw is not None:
        try:
            return float(raw)
        except Exception:
            pass
    bits = sb_uint(value)
    if bits is None:
        raise RuntimeError(f"probe double arg {index} has no value in {reg}")
    import struct
    return struct.unpack("<d", int(bits).to_bytes(8, "little"))[0]


def verify_wasm_debug_info(wasm_path: Path) -> None:
    objdump = shutil.which("wasm-objdump")
    if objdump is None:
        raise RuntimeError(
            "wasm-objdump not found; install WABT or ensure EMSDK tools are on PATH"
        )
    proc = subprocess.run(
        [objdump, "-h", str(wasm_path)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"wasm-objdump failed for {wasm_path}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    missing = [
        section for section in ("\"name\"", "\".debug_info\"", "\".debug_line\"")
        if section not in proc.stdout
    ]
    if missing:
        raise RuntimeError(
            f"{wasm_path} is missing debug section(s): {', '.join(missing)}. "
            "Rebuild this family with run_all.py."
        )


def describe_frame(frame) -> dict[str, Any]:
    function = frame.GetFunctionName() or frame.GetSymbol().GetName() or ""
    line_entry = frame.GetLineEntry()
    file_name = ""
    line_no = 0
    if line_entry and line_entry.IsValid():
        spec = line_entry.GetFileSpec()
        directory = spec.GetDirectory() or ""
        filename = spec.GetFilename() or ""
        file_name = str(Path(directory) / filename) if directory else filename
        line_no = line_entry.GetLine()
    return {"function": function, "file": file_name, "line": line_no}


def breakpoint_ids_for_thread(thread) -> set[int]:
    ids: set[int] = set()
    count = thread.GetStopReasonDataCount()
    for i in range(0, count, 2):
        ids.add(int(thread.GetStopReasonDataAtIndex(i)))
    return ids


def format_backtrace(process) -> str:
    lines = ["thread backtrace all:"]
    for thread_index in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(thread_index)
        lines.append(
            f"thread #{thread_index}: stop_reason={thread.GetStopReason()}"
        )
        for frame_index in range(min(thread.GetNumFrames(), 32)):
            frame = thread.GetFrameAtIndex(frame_index)
            desc = describe_frame(frame)
            location = ""
            if desc["file"] and desc["line"]:
                location = f" at {desc['file']}:{desc['line']}"
            lines.append(
                f"  {frame.GetFrameID()}: {desc['function'] or '<unknown>'}"
                f"{location}"
            )
    return "\n".join(lines)


SampleReader = Callable[[Any], dict[str, Any]]


class LldbGuestProbeRun:
    def __init__(
        self,
        *,
        breakpoint_name: str,
        sample_reader: SampleReader,
        driver: Path,
        host_python: Path,
        wasm: Path,
        spec_dir: Path,
        repo_root: Path = REPO_ROOT,
        timeout: float = 120.0,
    ) -> None:
        self.breakpoint_name = breakpoint_name
        self.sample_reader = sample_reader
        self.driver = driver
        self.host_python = host_python
        self.wasm = wasm
        self.spec_dir = spec_dir
        self.repo_root = repo_root
        self.timeout = timeout
        self.samples: list[dict[str, Any]] = []
        self.hit_count = 0
        self.first_frame: dict[str, Any] | None = None
        self.callback_errors: list[str] = []

    def run(self) -> dict[str, Any]:
        lldb = load_lldb()
        debugger = lldb.SBDebugger.Create()
        debugger.SetAsync(False)
        try:
            if sys.platform == "darwin":
                run_lldb_command(
                    lldb,
                    debugger,
                    "settings set plugin.jit-loader.gdb.enable on",
                )
            target = debugger.CreateTarget(str(self.host_python))
            if not target or not target.IsValid():
                raise RuntimeError(
                    f"failed to create LLDB target: {self.host_python}"
                )
            breakpoint = target.BreakpointCreateByName(self.breakpoint_name)

            with tempfile.TemporaryDirectory(
                prefix="krkr2-wasmtime-lldb-"
            ) as temp_dir:
                temp = Path(temp_dir)
                output_path = temp / "driver.json"
                stdout_path = temp / "driver.stdout"
                stderr_path = temp / "driver.stderr"
                launch = lldb.SBLaunchInfo([
                    str(self.driver),
                    "--driver-mode",
                    "--wasm",
                    str(self.wasm),
                    "--spec-dir",
                    str(self.spec_dir),
                    "--output",
                    str(output_path),
                ])
                launch.SetWorkingDirectory(str(self.repo_root))
                launch.AddOpenFileAction(1, str(stdout_path), False, True)
                launch.AddOpenFileAction(2, str(stderr_path), False, True)

                error = lldb.SBError()
                process = target.Launch(launch, error)
                if not error.Success():
                    raise RuntimeError(f"LLDB launch failed: {error.GetCString()}")
                self._drive(lldb, process, breakpoint.GetID(),
                            stdout_path, stderr_path)
                stdout, stderr = self._read_stdio(stdout_path, stderr_path)
                if process.GetExitStatus() not in (0, -1):
                    raise RuntimeError(
                        f"driver process exited with {process.GetExitStatus()}\n"
                        f"stdout:\n{stdout}\nstderr:\n{stderr}"
                    )
                if not output_path.exists():
                    raise RuntimeError(
                        "driver process did not write result JSON\n"
                        f"stdout:\n{stdout}\nstderr:\n{stderr}"
                    )
                report = json.loads(output_path.read_text(encoding="utf-8"))
        finally:
            lldb.SBDebugger.Destroy(debugger)

        return {
            "report": report,
            "samples": self.samples,
            "breakpoint": self.breakpoint_name,
            "hit_count": self.hit_count,
            "first_frame": self.first_frame,
        }

    @staticmethod
    def _read_stdio(stdout_path: Path, stderr_path: Path) -> tuple[str, str]:
        stdout = stdout_path.read_text(encoding="utf-8", errors="replace") \
            if stdout_path.exists() else ""
        stderr = stderr_path.read_text(encoding="utf-8", errors="replace") \
            if stderr_path.exists() else ""
        return stdout, stderr

    def _drive(self, lldb, process, breakpoint_id: int,
               stdout_path: Path, stderr_path: Path) -> None:
        deadline = time.monotonic() + self.timeout
        while True:
            if self.callback_errors:
                backtrace = format_backtrace(process)
                stdout, stderr = self._read_stdio(stdout_path, stderr_path)
                process.Kill()
                raise RuntimeError(
                    "; ".join(self.callback_errors)
                    + f"\n{backtrace}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            state = process.GetState()
            if state == lldb.eStateExited:
                break
            if time.monotonic() > deadline:
                backtrace = format_backtrace(process)
                stdout, stderr = self._read_stdio(stdout_path, stderr_path)
                process.Kill()
                raise RuntimeError(
                    f"LLDB guest debug timed out after {self.timeout:.1f}s "
                    f"with {self.hit_count} breakpoint hit(s)\n"
                    f"{backtrace}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            if state == lldb.eStateStopped:
                self._handle_stopped(lldb, process, breakpoint_id)
            cont_error = process.Continue()
            if not cont_error.Success():
                raise RuntimeError(
                    f"LLDB continue failed: {cont_error.GetCString()}"
                )

    def _handle_stopped(self, lldb, process, breakpoint_id: int) -> None:
        for thread_index in range(process.GetNumThreads()):
            thread = process.GetThreadAtIndex(thread_index)
            if thread.GetStopReason() != lldb.eStopReasonBreakpoint:
                continue
            if breakpoint_id not in breakpoint_ids_for_thread(thread):
                continue
            frame = thread.GetFrameAtIndex(0)
            try:
                sample = self.sample_reader(frame)
                self.hit_count += 1
                self.samples.append(sample)
                if self.first_frame is None:
                    self.first_frame = describe_frame(frame)
            except Exception as exc:
                self.callback_errors.append(str(exc))


def run_lldb_probe(
    *,
    breakpoint_name: str,
    sample_reader: SampleReader,
    driver: Path,
    host_python: Path,
    wasm: Path,
    spec_dir: Path,
    timeout: float,
) -> dict[str, Any]:
    return LldbGuestProbeRun(
        breakpoint_name=breakpoint_name,
        sample_reader=sample_reader,
        driver=driver,
        host_python=host_python,
        wasm=wasm,
        spec_dir=spec_dir,
        timeout=timeout,
    ).run()
