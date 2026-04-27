#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_HOST_PYTHON_RAW = os.environ.get("KRKR2_HOST_PYTHON") or shutil.which("python3")
DEFAULT_HOST_PYTHON = (
    Path(DEFAULT_HOST_PYTHON_RAW) if DEFAULT_HOST_PYTHON_RAW else None
)
BREAKPOINT_NAME = "krkr2_hit_test_run"
ACTIVE_TRACER = None

EXPECTED_HITS = {
    "circle_inside": True,
    "circle_boundary": True,
    "circle_outside": False,
    "rect_left_top_inclusive": True,
    "rect_right_bottom_exclusive": False,
    "quad_inside": True,
    "quad_outside": False,
    "quad_winding_clockwise": True,
    "quad_winding_counterclockwise": True,
    "invalid_type": False,
}


def _geometry_lldb_breakpoint_callback(frame, bp_loc, _internal_dict):
    if ACTIVE_TRACER is not None:
        ACTIVE_TRACER.handle_breakpoint(frame, bp_loc)
    return False


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def _load_lldb():
    try:
        lldb_python = subprocess.check_output(
            ["xcrun", "lldb", "-P"],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
        if lldb_python and lldb_python not in sys.path:
            sys.path.insert(0, lldb_python)
        import lldb  # type: ignore
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
            "failed to import LLDB Python module. Run this verifier through "
            "`xcrun python3`, or verify Xcode Command Line Tools with:\n"
            "  xcrun lldb -P\n"
            "  xcrun python3 -c 'import sys; "
            "sys.path.insert(0, __import__(\"subprocess\").check_output("
            "[\"xcrun\", \"lldb\", \"-P\"], text=True).strip()); "
            "import lldb; print(lldb.SBDebugger)'"
        ) from exc


def _run_lldb_command(lldb, debugger, command: str) -> None:
    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(command, result)
    if not result.Succeeded():
        raise RuntimeError(
            f"LLDB command failed: {command}\n{result.GetError()}"
        )


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
            "Rebuild with `cmake --build out/web/debug --target "
            "geometry_hit_test_wasm`."
        )


class LldbGuestRun:
    def __init__(
        self,
        *,
        host_python: Path,
        wasm: Path,
        spec_dir: Path,
        repo_root: Path,
        timeout: float,
    ) -> None:
        self.host_python = host_python
        self.wasm = wasm
        self.spec_dir = spec_dir
        self.repo_root = repo_root
        self.timeout = timeout
        self.hit_count = 0
        self.first_frame: dict | None = None
        self.callback_errors: list[str] = []

    def run(self) -> dict:
        global ACTIVE_TRACER
        lldb = _load_lldb()
        debugger = lldb.SBDebugger.Create()
        debugger.SetAsync(False)
        try:
            ACTIVE_TRACER = self
            _run_lldb_command(
                lldb,
                debugger,
                "settings set plugin.jit-loader.gdb.enable on",
            )
            target = debugger.CreateTarget(str(self.host_python))
            if not target or not target.IsValid():
                raise RuntimeError(
                    f"failed to create LLDB target: {self.host_python}"
                )

            breakpoint = target.BreakpointCreateByName(BREAKPOINT_NAME)
            self._install_callback(breakpoint)

            with tempfile.TemporaryDirectory(
                prefix="krkr2-geometry-lldb-"
            ) as temp_dir:
                temp = Path(temp_dir)
                output_path = temp / "result.json"
                stdout_path = temp / "host.stdout"
                stderr_path = temp / "host.stderr"
                launch = lldb.SBLaunchInfo([
                    str(Path(__file__).resolve()),
                    "--host-mode",
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

                self._drive_process(lldb, process, stdout_path, stderr_path)

                stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
                stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
                exit_status = process.GetExitStatus()
                if exit_status != 0:
                    raise RuntimeError(
                        f"geometry_hit_test Python host exited with {exit_status}\n"
                        f"stdout:\n{stdout}\nstderr:\n{stderr}"
                    )
                if not output_path.exists():
                    raise RuntimeError(
                        "geometry_hit_test Python host did not write result JSON\n"
                        f"stdout:\n{stdout}\nstderr:\n{stderr}"
                    )
                report = json.loads(output_path.read_text(encoding="utf-8"))
        finally:
            ACTIVE_TRACER = None
            lldb.SBDebugger.Destroy(debugger)

        return {
            "report": report,
            "breakpoint_hits": self.hit_count,
            "first_frame": self.first_frame,
        }

    def _install_callback(self, breakpoint) -> None:
        error = breakpoint.SetScriptCallbackBody(
            "import __main__\n"
            "return __main__._geometry_lldb_breakpoint_callback("
            "frame, bp_loc, internal_dict)"
        )
        if not error.Success():
            raise RuntimeError(
                f"failed to install LLDB breakpoint callback: "
                f"{error.GetCString()}"
            )
        breakpoint.SetAutoContinue(True)

    def _drive_process(
        self,
        lldb,
        process,
        stdout_path: Path,
        stderr_path: Path,
    ) -> None:
        deadline = time.monotonic() + self.timeout
        while True:
            if self.callback_errors:
                backtrace = self._format_backtrace(process)
                process.Kill()
                raise RuntimeError("; ".join(self.callback_errors) + "\n" + backtrace)
            state = process.GetState()
            if state == lldb.eStateExited:
                break
            if time.monotonic() > deadline:
                backtrace = self._format_backtrace(process)
                stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
                stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
                process.Kill()
                raise RuntimeError(
                    f"LLDB guest debug timed out after {self.timeout:.1f}s\n"
                    f"{backtrace}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            cont_error = process.Continue()
            if not cont_error.Success():
                raise RuntimeError(f"LLDB continue failed: {cont_error.GetCString()}")

    def _format_backtrace(self, process) -> str:
        lines = ["thread backtrace all:"]
        for thread_index in range(process.GetNumThreads()):
            thread = process.GetThreadAtIndex(thread_index)
            lines.append(
                f"thread #{thread_index}: stop_reason={thread.GetStopReason()}"
            )
            for frame_index in range(min(thread.GetNumFrames(), 32)):
                frame = thread.GetFrameAtIndex(frame_index)
                lines.append(f"  {self._describe_frame(frame)}")
        return "\n".join(lines)

    def _describe_frame(self, frame) -> str:
        function = frame.GetFunctionName() or frame.GetSymbol().GetName() or "<unknown>"
        line_entry = frame.GetLineEntry()
        file_name = ""
        line_no = 0
        if line_entry and line_entry.IsValid():
            spec = line_entry.GetFileSpec()
            directory = spec.GetDirectory() or ""
            filename = spec.GetFilename() or ""
            file_name = str(Path(directory) / filename) if directory else filename
            line_no = line_entry.GetLine()
        location = f" at {file_name}:{line_no}" if file_name and line_no else ""
        return f"{frame.GetFrameID()}: {function}{location}"

    def handle_breakpoint(self, frame, _bp_loc) -> None:
        try:
            self.hit_count += 1
            if self.first_frame is None:
                line_entry = frame.GetLineEntry()
                file_name = ""
                line_no = 0
                if line_entry and line_entry.IsValid():
                    spec = line_entry.GetFileSpec()
                    directory = spec.GetDirectory() or ""
                    filename = spec.GetFilename() or ""
                    file_name = str(Path(directory) / filename) if directory else filename
                    line_no = line_entry.GetLine()
                self.first_frame = {
                    "function": frame.GetFunctionName()
                    or frame.GetSymbol().GetName()
                    or "",
                    "file": file_name,
                    "line": line_no,
                }
        except Exception as exc:
            self.callback_errors.append(str(exc))


def run_lldb_guest_debug(
    *, host_python: Path, wasm: Path, spec_dir: Path, timeout: float
) -> dict:
    tracer = LldbGuestRun(
        host_python=host_python,
        wasm=wasm,
        spec_dir=spec_dir,
        repo_root=REPO_ROOT,
        timeout=timeout,
    )
    return tracer.run()


def validate_lldb_guest_debug(debug_result: dict) -> None:
    hits = int(debug_result.get("breakpoint_hits") or 0)
    if hits <= 0:
        raise RuntimeError(f"LLDB breakpoint {BREAKPOINT_NAME} was not hit")
    frame = debug_result.get("first_frame") or {}
    file_name = str(frame.get("file") or "")
    line = int(frame.get("line") or 0)
    if not file_name.endswith("geometry_hit_test_wasm.cpp") or line <= 0:
        raise RuntimeError(
            "LLDB did not expose a guest source frame for "
            f"{BREAKPOINT_NAME}: {frame}"
        )


def print_lldb_summary(debug_result: dict) -> None:
    frame = debug_result.get("first_frame") or {}
    print(
        f"LLDB guest breakpoint {BREAKPOINT_NAME} hit "
        f"{debug_result['breakpoint_hits']} time(s); first frame "
        f"{frame.get('function', '')} at {frame.get('file', '')}:"
        f"{frame.get('line', '')}",
        file=sys.stderr,
    )


def load_wasmtime():
    try:
        import wasmtime  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "wasmtime is not installed for the host Python. Run "
            "'python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt'"
        ) from exc
    return wasmtime


def instantiate_module(wasmtime, wasm_path: Path):
    config = wasmtime.Config()
    config.debug_info = True
    config.cranelift_opt_level = "none"
    engine = wasmtime.Engine(config)
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)
    linker = wasmtime.Linker(engine)
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

    try:
        run_fn = exports["krkr2_hit_test_run"]
    except Exception as exc:
        raise RuntimeError(
            f"krkr2_hit_test_run export not found in {wasm_path}"
        ) from exc

    return store, run_fn


def flatten_case(spec: dict) -> list[float]:
    """Mirror the geometry_hit_test wasm ABI."""
    try:
        shape = spec["shape"]
        kind = shape["kind"]
        values = [0.0] * 15
        if kind == "circle":
            hit_type = int(shape.get("type_override", 1))
            values[0] = float(shape["cx"])
            values[1] = float(shape["cy"])
            values[2] = float(shape["r"])
        elif kind == "rect":
            hit_type = int(shape.get("type_override", 2))
            values[3] = float(shape["left"])
            values[4] = float(shape["top"])
            values[5] = float(shape["right"])
            values[6] = float(shape["bottom"])
        elif kind == "quad":
            hit_type = int(shape.get("type_override", 3))
            values[7] = float(shape["x0"])
            values[8] = float(shape["y0"])
            values[9] = float(shape["x1"])
            values[10] = float(shape["y1"])
            values[11] = float(shape["x2"])
            values[12] = float(shape["y2"])
            values[13] = float(shape["x3"])
            values[14] = float(shape["y3"])
        else:
            raise RuntimeError(f"unsupported shape kind: {kind}")
    except KeyError as exc:
        raise RuntimeError(f"missing geometry_hit_test spec field: {exc}") from exc

    return [
        hit_type,
        float(spec["point"]["x"]),
        float(spec["point"]["y"]),
        *values,
    ]


def run_python_host(wasm_path: Path, spec_dir: Path, output: Path) -> int:
    wasmtime = load_wasmtime()
    store, run_fn = instantiate_module(wasmtime, wasm_path)
    results = []
    for spec in load_specs(spec_dir):
        raw_result = run_fn(store, *flatten_case(spec))
        results.append({
            "case_id": spec["id"],
            "status": "ok",
            "hit": bool(raw_result),
            "runner": "port-wasm-lldb",
        })

    report = {
        "ok": True,
        "runner": "geometry-hit-test-wasmtime-python-host",
        "results": results,
    }
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    parser.add_argument("--host-python", default=DEFAULT_HOST_PYTHON, type=Path)
    parser.add_argument("--lldb-timeout", default=120.0, type=float)
    parser.add_argument("--host-mode", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--output", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if not args.wasm.exists():
        raise RuntimeError(f"wasm module not found: {args.wasm}")
    if args.host_mode:
        if args.output is None:
            raise RuntimeError("--output is required in --host-mode")
        return run_python_host(args.wasm, args.spec_dir, args.output)

    if args.host_python is None or not args.host_python.exists():
        raise RuntimeError(f"host Python not found: {args.host_python}")
    verify_wasm_debug_info(args.wasm)

    specs = load_specs(args.spec_dir)
    if not specs:
        raise RuntimeError(f"no specs found in {args.spec_dir}")

    debug_result = run_lldb_guest_debug(
        host_python=args.host_python,
        wasm=args.wasm,
        spec_dir=args.spec_dir,
        timeout=args.lldb_timeout,
    )
    validate_lldb_guest_debug(debug_result)
    print_lldb_summary(debug_result)

    report = debug_result["report"]
    if not isinstance(report, dict) or report.get("ok") is not True:
        raise RuntimeError(f"invalid host report: {report}")
    results = report.get("results")
    if not isinstance(results, list):
        raise RuntimeError(f"host report results is not a list: {type(results)}")

    failed = False
    spec_by_id = {spec["id"]: spec for spec in specs}
    seen_cases = set()
    for result in results:
        if not isinstance(result, dict):
            raise RuntimeError(f"host result is not an object: {result}")
        case_id = str(result.get("case_id", ""))
        seen_cases.add(case_id)
        print(json.dumps(result, ensure_ascii=True))

        expected = EXPECTED_HITS.get(case_id)
        if expected is None:
            failed = True
            print(f"missing expected result for case {case_id}", file=sys.stderr)
            continue

        if result["hit"] != expected:
            failed = True
            spec = spec_by_id.get(case_id, {})
            point = spec.get("point", {})
            print(
                f"mismatch case_id={case_id} "
                f"kind={spec.get('shape', {}).get('kind')} "
                f"point=({point.get('x')}, {point.get('y')}) "
                f"wasm={result['hit']} expected={expected}",
                file=sys.stderr,
            )

    expected_cases = {spec["id"] for spec in specs}
    missing_cases = expected_cases - seen_cases
    if missing_cases:
        failed = True
        print(f"missing result case(s): {sorted(missing_cases)}", file=sys.stderr)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
