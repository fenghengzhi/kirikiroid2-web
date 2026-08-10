#!/usr/bin/env python3
"""Keep the repacked 1.3.9 harness alive until STARTUP_FROM is received.

The stock ``KR2Activity.exit()`` calls ``System.exit(0)``.  With no game path
on the launch Intent, 1.3.9 calls it from the GL initialization thread before
the differential driver can connect and provide its fixture via STARTUP_FROM.
Only the repacked test APK is patched; the original APK and libgame.so remain
unchanged.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


METHOD_HEADER = re.compile(r"(?m)^\.method\s+public static exit\(\)V\s*$")
METHOD_END = re.compile(r"(?m)^\.end method\s*$")
SYSTEM_EXIT = "Ljava/lang/System;->exit(I)V"


def patch_exit_method(smali: str) -> tuple[str, bool]:
    headers = list(METHOD_HEADER.finditer(smali))
    if len(headers) != 1:
        raise ValueError(
            f"expected one public static exit()V method, found {len(headers)}"
        )

    header = headers[0]
    method_end = METHOD_END.search(smali, header.end())
    if method_end is None:
        raise ValueError("exit()V has no .end method")

    body = smali[header.end() : method_end.start()]
    if SYSTEM_EXIT not in body:
        if re.search(r"(?m)^\s*\.locals\s+0\s*$", body) and re.search(
            r"(?m)^\s*return-void\s*$", body
        ):
            return smali, False
        raise ValueError("exit()V does not call java.lang.System.exit")

    newline = "\r\n" if "\r\n" in smali else "\n"
    replacement = (
        header.group(0)
        + newline
        + "    .locals 0"
        + newline
        + newline
        + "    return-void"
        + newline
        + ".end method"
    )
    return (
        smali[: header.start()] + replacement + smali[method_end.end() :],
        True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("smali", type=Path)
    args = parser.parse_args()

    source = args.smali.read_text()
    try:
        patched, changed = patch_exit_method(source)
    except ValueError as exc:
        print(f"{args.smali}: {exc}", file=sys.stderr)
        return 1

    if changed:
        args.smali.write_text(patched)
        print(f"patched KR2Activity.exit() to return without terminating: {args.smali}")
    else:
        print(f"KR2Activity.exit() already patched: {args.smali}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
