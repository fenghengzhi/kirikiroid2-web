#!/usr/bin/env python3

from __future__ import annotations

import argparse
import platform
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[4]

PLATFORMS = {
    "Darwin": {
        "binary_platform": "mac",
        "preset_prefix": "MacOS",
        "out_platform": "macos",
        "binary_ext": "",
    },
    "Linux": {
        "binary_platform": "linux",
        "preset_prefix": "Linux",
        "out_platform": "linux",
        "binary_ext": "",
    },
    "Windows": {
        "binary_platform": "win",
        "preset_prefix": "Windows",
        "out_platform": "windows",
        "binary_ext": ".exe",
    },
}

BUILD_TYPES = {
    "release": {
        "binary_mode": "rel",
        "out_mode": "release",
        "label": "Release",
    },
    "debug": {
        "binary_mode": "dbg",
        "out_mode": "debug",
        "label": "Debug",
    },
}


def build_instructions(system_name: str, build_type: str, binary_path: Path) -> str:
    platform_cfg = PLATFORMS[system_name]
    build_cfg = BUILD_TYPES[build_type]
    preset = f'{platform_cfg["preset_prefix"]} {build_cfg["label"]} Config'
    out_dir = Path("out") / platform_cfg["out_platform"] / build_cfg["out_mode"]
    lines = [
        "xp3 binary not found.",
        f"Expected: {binary_path}",
        "Build it with:",
        f'  cmake --preset "{preset}"',
        f"  cmake --build {out_dir} --target xp3",
    ]
    return "\n".join(lines)


def resolve_binary(system_name: str, build_type: str) -> Path:
    platform_cfg = PLATFORMS[system_name]
    build_cfg = BUILD_TYPES[build_type]
    return (
        REPO_ROOT
        / "tools"
        / "bin"
        / platform_cfg["binary_platform"]
        / build_cfg["binary_mode"]
        / f'xp3{platform_cfg["binary_ext"]}'
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Resolve the built xp3 executable for this repository."
    )
    parser.add_argument(
        "--build-type",
        choices=sorted(BUILD_TYPES.keys()),
        default="release",
        help="Select the expected build variant.",
    )
    args = parser.parse_args()

    system_name = platform.system()
    if system_name not in PLATFORMS:
        print(f"Unsupported host platform: {system_name}", file=sys.stderr)
        return 2

    binary_path = resolve_binary(system_name, args.build_type)
    if binary_path.is_file():
        print(binary_path)
        return 0

    print(build_instructions(system_name, args.build_type, binary_path), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
