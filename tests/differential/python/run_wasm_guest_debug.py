#!/usr/bin/env python3
"""Launch Wasmtime's architecture-neutral Guest/Wasm-only debugger.

Wasmtime 44+ instruments guest execution and exposes a virtual WebAssembly
machine over its built-in gdbstub.  A Wasm-aware LLDB connects with the
``wasm`` process plugin; no native CPU register mapping is involved.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

from run_scalar_wasmtime_cli import (
    find_wasmtime,
    require_guest_debug_wasmtime,
)


def find_wasm_lldb() -> Path | None:
    """Find an explicitly configured Wasm-aware LLDB.

    A system ``lldb`` is deliberately not selected because most vendor builds
    do not include the WebAssembly process plugin required by guest debugging.
    """
    configured = os.environ.get("WASM_LLDB")
    if configured:
        return Path(configured)
    wasi_sdk = os.environ.get("WASI_SDK_PATH")
    if wasi_sdk:
        candidate = Path(wasi_sdk) / "bin" / "lldb"
        if candidate.is_file():
            return candidate
    return None


def quote_command(command: list[str]) -> str:
    return shlex.join(command)


def connect_url(listen: str) -> str:
    if ":" not in listen:
        return f"connect://127.0.0.1:{listen}"
    host, port = listen.rsplit(":", 1)
    if host in ("", "0.0.0.0", "::"):
        host = "127.0.0.1"
    return f"connect://{host}:{port}"


def wasmtime_command(args: argparse.Namespace) -> list[str]:
    command = [str(args.wasmtime), "run", "-g", args.listen]
    if args.invoke:
        command += ["--invoke", args.invoke]
    command.append(str(args.wasm))
    guest_args = list(args.guest_args)
    if guest_args[:1] == ["--"]:
        guest_args.pop(0)
    return [*command, *guest_args]


def lldb_command(args: argparse.Namespace) -> list[str]:
    if args.lldb is None:
        raise RuntimeError(
            "Wasm-aware LLDB not found; pass --lldb with the wasi-sdk LLDB"
        )
    command = [str(args.lldb)]
    command += [
        "-o",
        f"process connect --plugin wasm {connect_url(args.listen)}",
    ]
    for breakpoint in args.breakpoint:
        command += ["-o", f"breakpoint set --name {breakpoint}"]
    return command


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Wasmtime 44+ Guest/Wasm-only debugging")
    parser.add_argument("--wasmtime", type=Path, default=find_wasmtime())
    parser.add_argument(
        "--lldb",
        type=Path,
        default=find_wasm_lldb(),
        help=(
            "Wasm-aware LLDB from wasi-sdk (default: WASM_LLDB or "
            "WASI_SDK_PATH/bin/lldb)"
        ),
    )
    parser.add_argument(
        "--listen", default="127.0.0.1:1234",
        help="Wasmtime gdbstub listen address (default: 127.0.0.1:1234)",
    )
    parser.add_argument("--invoke", help="Exported Wasm function to invoke")
    parser.add_argument(
        "--breakpoint", action="append", default=[],
        help="Wasm function breakpoint to install after connecting; repeatable",
    )
    parser.add_argument(
        "--launch-lldb", action="store_true",
        help="Launch LLDB automatically instead of waiting for another terminal",
    )
    parser.add_argument(
        "--print-only", action="store_true",
        help="Validate inputs and print commands without launching processes",
    )
    parser.add_argument("wasm", type=Path)
    parser.add_argument(
        "guest_args", nargs=argparse.REMAINDER,
        help="Arguments after the wasm path; prefix with -- when needed",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.wasmtime is None:
        raise RuntimeError(
            "Wasmtime CLI not found; install Wasmtime 44+ or pass --wasmtime"
        )
    if not args.wasm.is_file():
        raise FileNotFoundError(f"Wasm module not found: {args.wasm}")
    version = require_guest_debug_wasmtime(args.wasmtime)
    runtime_command = wasmtime_command(args)
    debugger_command = lldb_command(args) if args.launch_lldb else None

    print("Wasmtime:", ".".join(str(value) for value in version))
    print("Guest debug server:", quote_command(runtime_command))
    print(
        "LLDB connect:",
        f"process connect --plugin wasm {connect_url(args.listen)}",
    )
    for breakpoint in args.breakpoint:
        print("LLDB breakpoint:", f"breakpoint set --name {breakpoint}")
    if debugger_command is not None:
        print("LLDB launch:", quote_command(debugger_command))
    if args.print_only:
        return 0

    if not args.launch_lldb:
        print("Start Wasm-aware LLDB in another terminal, then connect above.")
        try:
            return subprocess.run(runtime_command).returncode
        except KeyboardInterrupt:
            return 130

    process = subprocess.Popen(runtime_command)
    try:
        # Wasmtime creates the local listener before waiting for LLDB.  Avoid a
        # TCP readiness probe because the gdbstub accepts that as the debugger
        # connection; a short delay preserves the one real connection for LLDB.
        time.sleep(0.5)
        if process.poll() is not None:
            return int(process.returncode or 1)
        try:
            return subprocess.run(debugger_command).returncode
        except KeyboardInterrupt:
            return 130
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
