#!/usr/bin/env python3
"""Patch an apktool-decoded APK for the differential HarnessActivity.

apktool decodes the binary XML to plain text under decoded/AndroidManifest.xml.
We splice our <activity> snippet in just before the closing </application> and
pin the harness target SDK in both the manifest and apktool.yml.  Idempotent:
skips the Activity insertion if HarnessActivity is already present.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


MARKER = "org.github.krkr2.HarnessActivity"
HARNESS_TARGET_SDK = "23"


def pin_harness_target_sdk(manifest: str) -> tuple[str, bool]:
    """Opt the repacked test APK into runtime permissions.

    Kirikiroid2 1.3.9 targets a pre-Marshmallow SDK.  Android's permission
    review compatibility UI then intercepts the first Activity launch before
    HarnessActivity can start its RPC server.  The harness APK is installed
    with ``adb install -g``, so targeting API 23 keeps the test non-interactive
    while preserving the oldest runtime-permission behavior.
    """
    uses_sdk = re.search(r"<uses-sdk\b[^>]*?/?>", manifest)
    if uses_sdk:
        element = uses_sdk.group(0)
        target = re.search(r'android:targetSdkVersion="[^"]*"', element)
        if target:
            replacement = (
                element[: target.start()]
                + f'android:targetSdkVersion="{HARNESS_TARGET_SDK}"'
                + element[target.end() :]
            )
        else:
            close = "/>" if element.endswith("/>") else ">"
            replacement = (
                element[: -len(close)]
                + f' android:targetSdkVersion="{HARNESS_TARGET_SDK}"'
                + close
            )
        return (
            manifest[: uses_sdk.start()] + replacement + manifest[uses_sdk.end() :],
            replacement != element,
        )

    manifest_open = re.search(r"<manifest\b[^>]*>", manifest)
    if not manifest_open:
        raise ValueError("no <manifest> root element")
    insertion = (
        f'\n    <uses-sdk android:targetSdkVersion="{HARNESS_TARGET_SDK}" />'
    )
    return (
        manifest[: manifest_open.end()] + insertion + manifest[manifest_open.end() :],
        True,
    )


def pin_apktool_target_sdk(metadata: str) -> tuple[str, bool]:
    """Keep apktool's aapt2 ``--target-sdk-version`` in sync with the manifest."""
    target = re.search(
        r"(?m)^(\s*targetSdkVersion:\s*)([^#\r\n]*?)(\s*(?:#.*)?)$",
        metadata,
    )
    if target:
        replacement = (
            target.group(1)
            + f"'{HARNESS_TARGET_SDK}'"
            + target.group(3)
        )
        return (
            metadata[: target.start()] + replacement + metadata[target.end() :],
            replacement != target.group(0),
        )

    sdk_info = re.search(r"(?m)^(\s*)sdkInfo:\s*(?:#.*)?$", metadata)
    if not sdk_info:
        raise ValueError("no sdkInfo section in apktool metadata")
    newline = "\r\n" if "\r\n" in metadata else "\n"
    insertion = (
        newline
        + sdk_info.group(1)
        + f"  targetSdkVersion: '{HARNESS_TARGET_SDK}'"
    )
    return (
        metadata[: sdk_info.end()] + insertion + metadata[sdk_info.end() :],
        True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("snippet", type=Path)
    parser.add_argument("apktool_yml", type=Path)
    args = parser.parse_args()

    manifest = args.manifest.read_text()
    try:
        manifest, target_changed = pin_harness_target_sdk(manifest)
        metadata = args.apktool_yml.read_text()
        metadata, metadata_changed = pin_apktool_target_sdk(metadata)
    except ValueError as exc:
        print(f"cannot pin harness target SDK: {exc}", file=sys.stderr)
        return 1

    if target_changed:
        args.manifest.write_text(manifest)
    if metadata_changed:
        args.apktool_yml.write_text(metadata)

    if MARKER in manifest:
        if target_changed or metadata_changed:
            print(
                f"set harness targetSdkVersion={HARNESS_TARGET_SDK} "
                "in AndroidManifest.xml and apktool.yml"
            )
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
    print(
        f"set harness targetSdkVersion={HARNESS_TARGET_SDK} "
        "in AndroidManifest.xml and apktool.yml"
    )
    print(f"injected HarnessActivity into {args.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
