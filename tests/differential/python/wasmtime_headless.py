"""Shared subprocess helper for the generic KrKr2 Wasmtime headless host."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any


def run_headless_host(
    *,
    host: Path,
    wasm: Path,
    repo_root: Path,
    xp3: Path,
    frames: int,
    trace: str,
) -> dict[str, Any]:
    cmd = [
        str(host),
        "--wasm",
        str(wasm),
        "--repo-root",
        str(repo_root),
        "--xp3",
        str(xp3),
        "--frames",
        str(frames),
        "--trace",
        trace,
        "--json",
    ]
    proc = subprocess.run(
        cmd,
        cwd=str(repo_root),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "krkr2_wasmtime_host failed with exit code "
            f"{proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    try:
        report = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"krkr2_wasmtime_host did not emit JSON: {exc}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        ) from exc
    if not isinstance(report, dict):
        raise RuntimeError(
            f"krkr2_wasmtime_host JSON root is not an object: {type(report)}"
        )
    return report
