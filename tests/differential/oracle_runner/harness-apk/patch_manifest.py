#!/usr/bin/env python3
"""Inject HarnessActivity into an apktool-decoded AndroidManifest.xml.

apktool decodes the binary XML to plain text under decoded/AndroidManifest.xml.
We splice our <activity> snippet in just before the closing </application>.
Idempotent: skips if HarnessActivity is already present.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


MARKER = "org.github.krkr2.HarnessActivity"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("snippet", type=Path)
    args = parser.parse_args()

    manifest = args.manifest.read_text()
    if MARKER in manifest:
        print(f"HarnessActivity already present in {args.manifest}")
        return 0

    snippet = args.snippet.read_text()
    # Drop XML comments from the snippet — they confuse aapt when the
    # manifest gets re-binary-encoded on apktool b.
    cleaned_lines = []
    in_comment = False
    for line in snippet.splitlines():
        stripped = line.strip()
        if in_comment:
            if "-->" in stripped:
                in_comment = False
            continue
        if stripped.startswith("<!--"):
            if "-->" in stripped:
                continue
            in_comment = True
            continue
        cleaned_lines.append(line)
    snippet = "\n".join(cleaned_lines).strip() + "\n"

    close_tag = "</application>"
    idx = manifest.rfind(close_tag)
    if idx < 0:
        print(f"no {close_tag} in manifest?", file=sys.stderr)
        return 1

    # Indent the snippet by 4 spaces to match apktool output style.
    indented = "\n".join(
        ("    " + line) if line.strip() else line for line in snippet.splitlines()
    )
    new_manifest = manifest[:idx] + indented + "\n" + manifest[idx:]
    args.manifest.write_text(new_manifest)
    print(f"injected HarnessActivity into {args.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
