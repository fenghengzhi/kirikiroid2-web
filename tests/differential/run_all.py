#!/usr/bin/env python3
"""Build and run every differential Wasmtime LLDB guest-debug family.

Discovers families from `tests/differential/wasmtime/<family>_wasm.cpp` and
reads build directives from the top comment block of each source:

    // @exports: _foo,_bar       required; em++ EXPORTED_FUNCTIONS list
    // @plugin-include           optional flag; adds -I<repo>/cpp/plugins
    // @requires-lldb            optional documentation flag

The ADB+Frida oracle is a separate lane — invoke the per-family
``run_<family>_adb.py`` scripts directly (see
``tests/differential/oracle_runner/README.md``).

Usage:
    run_all.py                              # build + run LLDB guest-debug harness
    run_all.py --no-build                   # run without rebuilding .wasm
    run_all.py --family bezier_curve        # restrict to one family
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WASM_DIR = REPO_ROOT / "tests/differential/wasmtime"
PYTHON_DIR = REPO_ROOT / "tests/differential/python"
SPEC_ROOT = REPO_ROOT / "tests/differential/specs"
PLUGIN_DIR = REPO_ROOT / "cpp/plugins"

DIRECTIVE_RE = re.compile(r"//\s*@([a-zA-Z0-9_-]+)(?::\s*(.+))?\s*$")


class Family:
    def __init__(self, name: str, src: Path, exports: list[str],
                 plugin_include: bool, requires_lldb: bool) -> None:
        self.name = name
        self.src = src
        self.exports = exports
        self.plugin_include = plugin_include
        self.requires_lldb = requires_lldb

    @property
    def wasm(self) -> Path:
        return WASM_DIR / f"{self.name}.wasm"

    @property
    def wasmtime_runner(self) -> Path:
        return PYTHON_DIR / f"run_{self.name}_wasmtime.py"

    @property
    def spec_dir(self) -> Path:
        return SPEC_ROOT / self.name


def parse_directives(src: Path) -> tuple[list[str], bool, bool]:
    exports: list[str] = []
    plugin_include = False
    requires_lldb = False
    for line in src.read_text(encoding="utf-8").splitlines():
        if not line.startswith("//"):
            if line.strip() and not line.strip().startswith("/*"):
                break  # directives only valid in leading comment block
        m = DIRECTIVE_RE.match(line)
        if not m:
            continue
        key, value = m.group(1), (m.group(2) or "").strip()
        if key == "exports":
            exports = [e.strip() for e in value.split(",") if e.strip()]
        elif key == "plugin-include":
            plugin_include = True
        elif key == "requires-lldb":
            requires_lldb = True
    if not exports:
        raise RuntimeError(f"{src}: missing `// @exports:` directive")
    return exports, plugin_include, requires_lldb


def discover_families() -> list[Family]:
    families: list[Family] = []
    for src in sorted(WASM_DIR.glob("*_wasm.cpp")):
        name = src.stem[:-len("_wasm")]
        exports, plugin_include, requires_lldb = parse_directives(src)
        families.append(Family(name, src, exports, plugin_include,
                               requires_lldb))
    if not families:
        raise RuntimeError(f"no *_wasm.cpp files under {WASM_DIR}")
    return families


def build(family: Family) -> None:
    exports_arg = "[" + ",".join(f"'{e}'" for e in family.exports) + "]"
    cmd = [
        "em++", str(family.src),
        "-std=c++17", "-g3", "-O0", "--profiling-funcs",
        "-sSTANDALONE_WASM=1",
        f"-sEXPORTED_FUNCTIONS={exports_arg}",
        "--no-entry",
        "-o", str(family.wasm),
    ]
    if family.plugin_include:
        cmd.insert(3, f"-I{PLUGIN_DIR}")
    print(f"[build] {family.name}: {' '.join(cmd)}", flush=True)
    subprocess.run(cmd, check=True)


def run_wasmtime(family: Family) -> int:
    if not family.wasmtime_runner.exists():
        raise RuntimeError(f"missing runner: {family.wasmtime_runner}")
    cmd = [
        sys.executable, str(family.wasmtime_runner),
        "--spec-dir", str(family.spec_dir),
        "--wasm", str(family.wasm),
    ]
    print(f"[wasmtime] {family.name}", flush=True)
    return subprocess.run(cmd).returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-build", action="store_true",
                        help="skip em++ compilation")
    parser.add_argument("--family", action="append",
                        help="restrict to specific family (repeatable)")
    args = parser.parse_args()

    families = discover_families()
    if args.family:
        wanted = set(args.family)
        families = [f for f in families if f.name in wanted]
        missing = wanted - {f.name for f in families}
        if missing:
            raise RuntimeError(f"unknown families: {sorted(missing)}")

    if not args.no_build:
        for f in families:
            build(f)

    failed = 0
    for f in families:
        if run_wasmtime(f) != 0:
            failed += 1

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
