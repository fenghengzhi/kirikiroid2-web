#!/usr/bin/env python3
"""Run existing PSB/MDF files through the historical Android oracle.

All native offsets reached through ``oracle_runner.adapters.psbfile_load``
belong to the removed Android ``libkrkr2.so`` build.  This runner is retained
only for that legacy APK/Frida environment; it is not evidence for any of the
four current ``reference/binaries`` targets and must not be used as though its
offsets had been rebased.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from oracle_runner.adb_engine import (  # noqa: E402
    ENV_REMOTE_DIR,
    AdbHarnessEngine,
)
from oracle_runner.adapters import psbfile_load as adapter  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[3]
MEDIA_FIRST_INPUT = REPO_ROOT / "tests/test_files/emote/ezsave.pimg"
MEDIA_FIRST_SHA256 = (
    "d90d4ee955258b63efdc648f159990aa2c605dceef396ab9ea56eb8d281a7aa3"
)
MEDIA_REPLACEMENT_INPUT = (
    REPO_ROOT / "tests/test_files/emote/e-mote3.0バニラパジャマa.psb"
)
MEDIA_STARTUP_XP3 = (
    REPO_ROOT / "reference/xp3/caution_minimal/caution_minimal.xp3"
)
MEDIA_FIRST_CONTAINER = "psb-media-ezsave.pimg"
MEDIA_REPLACEMENT_CONTAINER = "psb-media-encrypted.psb"
MEDIA_RESOURCE_NAME = "2036.tlg"
MEDIA_RESOURCE_SIZE = 48265
MEDIA_STARTUP_REMOTE_NAME = "psbfile-oracle-bootstrap.xp3"
MEDIA_DICTIONARY_INPUT = (
    REPO_ROOT
    / "reference/xp3/caution_test/DRACU-RIOT/data/motion/autoskip.psb"
)
MEDIA_DICTIONARY_SHA256 = (
    "131b436405c0aa8cd137a496c98fb77a77da95ca29e8af4597da1f7a42fd4a5d"
)
MEDIA_DICTIONARY_CONTAINER = "psb-media-autoskip.psb"
MEDIA_DICTIONARY_PATH = "source/main/icon"
MEDIA_DICTIONARY_KEYS = ("arrow", "auto", "skip")
MEDIA_ARRAY_CONTAINER = "psb-media-array.pimg"
MEDIA_ARRAY_PATH = "layers"
MEDIA_ARRAY_NODE_OFFSET = 0x20B
MEDIA_ARRAY_NODE_PREFIX = bytes.fromhex(
    "200d200e000036006c009f00d4000a0140017501ab01e60122025e029a02d602"
)
MEDIA_ARRAY_COUNT = 32
MEDIA_ARRAY_PACKED_TABLE_SIZE = 67
MEDIA_ADAPTOR_NULL_CONTAINER = "psb-media-adaptor-null.pimg"
INTEGER_BOUNDARY_INPUTS = {
    "m2logo": {
        "path": REPO_ROOT / "reference/xp3/logo_test/m2logo.mtn",
        "sha256": (
            "4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe"
        ),
        "remote_name": "psbfile-integer-m2logo.mtn",
    },
    "autoskip": {
        "path": MEDIA_DICTIONARY_INPUT,
        "sha256": MEDIA_DICTIONARY_SHA256,
        "remote_name": "psbfile-integer-autoskip.psb",
    },
}
INTEGER_BOUNDARY_CASES = (
    {
        "name": "tag04-zero",
        "input": "m2logo",
        "node_offset": 0x48CB,
        "node_bytes": bytes.fromhex("04"),
        "variant_value": 0,
        "get_int": 0,
        "expression": 'oracle_psb_integer_file.root["screenSize"]["originX"]',
    },
    {
        "name": "tag05-signed8-negative",
        "input": "m2logo",
        "node_offset": 0x40E6,
        "node_bytes": bytes.fromhex("05 8e"),
        "variant_value": -114,
        "get_int": -114,
        "expression": (
            'oracle_psb_integer_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["children"][0]'
            '["children"][4]["children"][0]["frameList"][2]'
            '["content"]["coord"][0]'
        ),
    },
    {
        "name": "tag06-signed16-negative",
        "input": "m2logo",
        "node_offset": 0x3705,
        "node_bytes": bytes.fromhex("06 7c ff"),
        "variant_value": -132,
        "get_int": -132,
        "expression": (
            'oracle_psb_integer_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["children"][0]'
            '["children"][3]["children"][1]["frameList"][3]'
            '["content"]["coord"][0]'
        ),
    },
    {
        "name": "tag07-signed24",
        "input": "m2logo",
        "node_offset": 0x30FD,
        "node_bytes": bytes.fromhex("07 13 00 08"),
        "variant_value": 524307,
        "get_int": 524307,
        "expression": (
            'oracle_psb_integer_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["children"][0]'
            '["children"][0]["children"][0]["frameList"][1]'
            '["content"]["mask"]'
        ),
    },
    {
        "name": "tag08-signed32",
        "input": "m2logo",
        "node_offset": 0x4174,
        "node_bytes": bytes.fromhex("08 6c 06 40 02"),
        "variant_value": 37750380,
        "get_int": 37750380,
        "expression": (
            'oracle_psb_integer_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["children"][0]'
            '["children"][4]["inheritMask"]'
        ),
    },
    {
        "name": "tag09-low32-sign",
        "input": "m2logo",
        "node_offset": 0x36F8,
        "node_bytes": bytes.fromhex("09 00 00 00 ff 00"),
        "variant_value": 0xFF000000,
        "get_int": -0x01000000,
        "expression": (
            'oracle_psb_integer_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["children"][0]'
            '["children"][3]["children"][1]["frameList"][3]'
            '["content"]["color"]'
        ),
    },
    {
        "name": "tag09-low32-all-ones",
        "input": "autoskip",
        "node_offset": 0x12C3,
        "node_bytes": bytes.fromhex("09 ff ff ff ff 00"),
        "variant_value": 0xFFFFFFFF,
        "get_int": -1,
        "expression": (
            'oracle_psb_integer_file.root["object"]["AUTOSKIP"]'
            '["motion"]["skiparrow"]["layer"][0]["frameList"][3]'
            '["content"]["color"]'
        ),
    },
)
VALUE_BOUNDARY_INPUTS = {
    "m2logo": INTEGER_BOUNDARY_INPUTS["m2logo"],
    "prologue": {
        "path": (
            REPO_ROOT
            / "reference" / "xp3" / "caution_test" / "DRACU-RIOT" / "scn"
            / "★プロローグa（始まり）.ks.scn"
        ),
        "sha256": (
            "b3f47bdb7b54688f097d08d0f15ed9905c9c8e2413a3aaff6421b8e60f553616"
        ),
        "remote_name": "psbfile-value-prologue.scn",
    },
    "config": {
        "path": (
            REPO_ROOT
            / "reference" / "xp3" / "caution_test" / "DRACU-RIOT"
            / "data" / "motion" / "config.psb"
        ),
        "sha256": (
            "2f75a019655d9741dd613b2f18a07d4e54053fb2db4ff8b252be2ed72985eb75"
        ),
        "remote_name": "psbfile-value-config.psb",
    },
}
REAL_BOUNDARY_CASES = (
    {
        "name": "tag1d-zero",
        "input": "m2logo",
        "node_offset": 0x51BA,
        "node_bytes": bytes.fromhex("1d"),
        "double_bits_le": bytes.fromhex("00 00 00 00 00 00 00 00"),
        "expression": (
            'oracle_psb_real_file.root["source"]["logo"]["icon"]'
            '["icon42"]["clip"]["left"]'
        ),
    },
    {
        "name": "tag1e-float32-widen",
        "input": "m2logo",
        "node_offset": 0x5399,
        "node_bytes": bytes.fromhex("1e 85 eb 41 40"),
        "double_bits_le": bytes.fromhex("00 00 00 a0 70 3d 08 40"),
        "expression": 'oracle_psb_real_file.root["version"]',
    },
    {
        "name": "tag1f-float64",
        "input": "prologue",
        "node_offset": 0x11A903,
        "node_bytes": bytes.fromhex("1f 9a 99 99 99 99 19 72 c0"),
        "double_bits_le": bytes.fromhex("9a 99 99 99 99 19 72 c0"),
        "expression": (
            'oracle_psb_real_file.root["scenes"][0]["texts"][899][5]'
            '["objectList"][9]["actionList"][4][2]'
        ),
    },
)
STRING_BOUNDARY_CASES = (
    {
        "name": "tag15-utf8",
        "input": "m2logo",
        "node_offset": 0x445B,
        "node_bytes": bytes.fromhex("15 46"),
        "string_data_offset": 0x57B8,
        "string_size": 10,
        "string_sha256": (
            "ee099761ad5b7ef2161fb231afd6cfbc4f47283fee5021c19cfacf7eaaaf0257"
        ),
        "expression": (
            'oracle_psb_string_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["layer"][1]["label"]'
        ),
    },
    {
        "name": "tag16-utf8",
        "input": "config",
        "node_offset": 0x82D55,
        "node_bytes": bytes.fromhex("16 52 02"),
        "string_data_offset": 0x8D794,
        "string_size": 38,
        "string_sha256": (
            "9b8ce5d55aabd9c53396bd420c4e43928d64fa3a4e5fc4e09ef563b600af77f0"
        ),
        "expression": (
            'oracle_psb_string_file.root["object"]["tgsys_TITLE"]'
            '["motion"]["press"]["layer"][1]["frameList"][2]'
            '["content"]["src"]'
        ),
    },
)
NULL_BOUNDARY_CASES = (
    {
        "name": "tag01",
        "input": "m2logo",
        "node_offset": 0x5365,
        "node_bytes": bytes.fromhex("01"),
        "expression": (
            'oracle_psb_shape_file.root["source"]["logo"]["metadata"]'
        ),
    },
)
COLLECTION_BOUNDARY_CASES = (
    {
        "name": "tag20-array-count30",
        "input": "m2logo",
        "node_offset": 0x448B,
        "entry_count": 30,
        "table_byte_size": 34,
        "node_prefix": bytes.fromhex(
            "200d1e0d00020406080a0c0e10121416181a1c1e20222426282a2c2e30323436"
        ),
        "expression": (
            'oracle_psb_shape_file.root["object"]["m2cheeseware_logo"]'
            '["motion"]["back_white"]["priority"][0]["content"]'
        ),
        "probe_expression": "oracle_psb_shape_value[0]",
        "probe_type": adapter.TJS_VARIANT_INTEGER_TYPE,
        "probe_value": 29,
        "negative_index_value": 0,
    },
    {
        "name": "tag21-dictionary-count36",
        "input": "m2logo",
        "node_offset": 0x4972,
        "entry_count": 36,
        "table_byte_size": 115,
        "node_prefix": bytes.fromhex(
            "210d240d1c1d1e1f202122232425262728292a2b2c2d2f303132333435363738"
        ),
        "expression": (
            'oracle_psb_shape_file.root["source"]["logo"]["icon"]'
        ),
        "probe_expression": (
            'oracle_psb_shape_value["icon42"]["clip"]["left"]'
        ),
        "probe_type": adapter.TJS_VARIANT_REAL_TYPE,
        "probe_bits_le": bytes.fromhex("00 00 00 00 00 00 00 00"),
    },
)
RESOURCE_BOUNDARY_REMOTE_NAME = "psbfile-resource-ezsave.pimg"
RESOURCE_BOUNDARY_NODE_OFFSET = 0x1FE
RESOURCE_BOUNDARY_NODE_BYTES = bytes.fromhex("19 00")
RESOURCE_BOUNDARY_DATA_OFFSET = 0xB1C
RESOURCE_BOUNDARY_SIZE = 612
RESOURCE_BOUNDARY_SHA256 = (
    "62adc968fb6380e3e7a718ef39e7bf44d5231e9e2f08b4fa390cf38e0fc47005"
)
RESOURCE_BOUNDARY_EXPRESSION = (
    'oracle_psb_resource_file.root["2157.tlg"]'
)


def _adb_prefix(engine) -> list[str]:
    adb = [engine.adb]
    if engine.serial:
        adb += ["-s", engine.serial]
    return adb


def _app_files_dir(engine) -> str:
    return f"/data/user/0/{engine.apk_package}/files"


def _push_input(engine, input_path: Path, remote_path: str) -> None:
    adb = _adb_prefix(engine)
    subprocess.run(
        adb + ["push", str(input_path), remote_path],
        check=True, capture_output=True,
    )
    subprocess.run(
        adb + ["shell", "chmod", "644", remote_path],
        check=True, capture_output=True,
    )


def _push_app_file(engine, input_path: Path, remote_name: str) -> str:
    """Install an oracle input where the APK can read it under SELinux."""
    if not remote_name or "/" in remote_name or remote_name in {".", ".."}:
        raise ValueError(f"invalid Android app-private file name: {remote_name!r}")
    adb = _adb_prefix(engine)
    app_dir = _app_files_dir(engine)
    remote_path = f"{app_dir}/{remote_name}"
    staging_path = (
        f"/data/local/tmp/.krkr2-oracle-{os.getpid()}-{remote_name}")

    try:
        subprocess.run(
            adb + ["push", str(input_path), staging_path],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "mkdir", "-p", app_dir],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "cp", staging_path, remote_path],
            check=True, capture_output=True,
        )
        owner = subprocess.run(
            adb + ["shell", "stat", "-c", "%u:%g", app_dir],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if not owner:
            raise RuntimeError(
                f"could not determine Android app owner for {app_dir}")
        subprocess.run(
            adb + ["shell", "chown", owner, remote_path],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "chmod", "644", remote_path],
            check=True, capture_output=True,
        )
    finally:
        subprocess.run(
            adb + ["shell", "rm", "-f", staging_path],
            check=False, capture_output=True,
        )
    return remote_path


def _push_readable_input(
    engine, input_path: Path, remote_dir: str, remote_name: str,
) -> str:
    normalized_dir = remote_dir.rstrip("/") or "/"
    if normalized_dir == _app_files_dir(engine):
        return _push_app_file(engine, input_path, remote_name)
    subprocess.run(
        _adb_prefix(engine) + ["shell", "mkdir", "-p", normalized_dir],
        check=True, capture_output=True,
    )
    remote_path = (
        f"/{remote_name}" if normalized_dir == "/"
        else f"{normalized_dir}/{remote_name}"
    )
    _push_input(engine, input_path, remote_path)
    return remote_path


def _trigger_startup(engine, remote_xp3: str, timeout: float = 15.0) -> None:
    """Schedule the real Android startup chain that creates Full TJS."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            accepted = engine.startup_from(remote_xp3)
            break
        except RuntimeError as exc:
            if ("TVPMainScene::GetInstance returned null" not in str(exc)
                    or time.monotonic() >= deadline):
                raise
            time.sleep(0.2)
    if not accepted:
        raise RuntimeError(
            f"TVPMainScene::startupFrom({remote_xp3!r}) returned false")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input", action="append", default=[], type=Path,
        help="existing PSB\\0 or mdf\\0 file; repeat to run more than one",
    )
    parser.add_argument("--serial", default=None)
    parser.add_argument("--remote-dir", default=None)
    parser.add_argument(
        "--trace", action="store_true",
        help="attach Frida and include the native PSB load call sequence",
    )
    parser.add_argument(
        "--storage", action="store_true",
        help="also push each input and exercise PSBFile's storage entry",
    )
    parser.add_argument(
        "--decrypt-seed", type=lambda value: int(value, 0), default=None,
        help="also apply Android's Emote PSB xorshift filter to raw PSB input",
    )
    parser.add_argument(
        "--media-interface", action="store_true",
        help=(
            "exercise PSBMedia's exact 11-slot vtable, non-atomic "
            "reference transitions, name/normalization slots and both "
            "destructor forms"
        ),
    )
    parser.add_argument(
        "--media-lifecycle", action="store_true",
        help=(
            "exercise PSBMedia replacement and borrowed-stream destruction "
            "with the repository's two tracked Emote PSB files"
        ),
    )
    parser.add_argument(
        "--media-dictionary", action="store_true",
        help=(
            "exercise PSBMedia dictionary listing with the existing "
            "autoskip.psb reference input"
        ),
    )
    parser.add_argument(
        "--media-array", action="store_true",
        help=(
            "exercise PSBMedia array listing with the existing "
            "ezsave.pimg reference input"
        ),
    )
    parser.add_argument(
        "--media-adaptor-null", action="store_true",
        help=(
            "exercise PSBMedia's successful-load/null-adaptor boundary with "
            "the repository's existing ezsave.pimg"
        ),
    )
    parser.add_argument(
        "--integer-boundary", action="store_true",
        help=(
            "exercise natural PSB integer tags 0x04..0x09 through both "
            "PSBValueDispatch and raw scalar getters"
        ),
    )
    parser.add_argument(
        "--real-boundary", action="store_true",
        help=(
            "exercise natural PSB Real tags 0x1d..0x1f through both "
            "PSBValueDispatch and raw GetDouble"
        ),
    )
    parser.add_argument(
        "--string-boundary", action="store_true",
        help=(
            "exercise natural PSB String tags 0x15 and 0x16 through copied "
            "TJS String and borrowed raw GetString paths"
        ),
    )
    parser.add_argument(
        "--shape-boundary", action="store_true",
        help=(
            "exercise natural Null/Array/Dictionary nodes through public "
            "Variants; raw GetRoot/Transfer, Dictionary holder ownership, "
            "and ordered gnustl COW key-vector lifetimes; "
            "native-instance borrowing; primary/secondary native lifecycle slots; all "
            "unsupported primary dispatch slots; exact IsInstanceOf; ordered "
            "EnumMembers callbacks; raw type categories; and intrusive "
            "dispatch/owner lifetimes"
        ),
    )
    parser.add_argument(
        "--resource-boundary", action="store_true",
        help=(
            "exercise a natural PSB tag-0x19 Resource through copied TJS "
            "Octet and borrowed raw GetResource paths"
        ),
    )
    parser.add_argument(
        "--startup-xp3", type=Path, default=MEDIA_STARTUP_XP3,
        help=(
            "existing self-contained XP3 used to initialize Full TJS before "
            "the TJS-backed PSBFile/PSBMedia modes (default: "
            "reference/xp3/caution_minimal/"
            "caution_minimal.xp3)"
        ),
    )
    parser.add_argument(
        "--startup-settle-seconds", type=float, default=4.0,
        help=(
            "wait after Full TJS startup before cross-thread harness calls; "
            "the default lets caution_minimal remove its continuous handler"
        ),
    )
    args = parser.parse_args()
    media_mode = (
        args.media_interface
        or args.media_lifecycle
        or args.media_dictionary
        or args.media_array
        or args.media_adaptor_null
    )
    value_boundary_cases = (
        (REAL_BOUNDARY_CASES if args.real_boundary else ())
        + (STRING_BOUNDARY_CASES if args.string_boundary else ())
        + (NULL_BOUNDARY_CASES if args.shape_boundary else ())
        + (COLLECTION_BOUNDARY_CASES if args.shape_boundary else ())
    )
    tjs_mode = (
        media_mode
        or args.integer_boundary
        or bool(value_boundary_cases)
        or args.resource_boundary
    )
    if not args.input and not tjs_mode:
        parser.error(
            "provide --input, --integer-boundary, --real-boundary, "
            "--string-boundary, --shape-boundary, --media-lifecycle, "
            "--media-interface, --media-dictionary, --media-array, "
            "--media-adaptor-null, "
            "and/or "
            "--resource-boundary")
    if not args.input and (args.storage or args.decrypt_seed is not None):
        parser.error("--storage and --decrypt-seed require at least one --input")
    if tjs_mode and not args.startup_xp3.is_file():
        parser.error(
            f"TJS-backed oracle startup XP3 does not exist: "
            f"{args.startup_xp3}")
    if not 0.0 <= args.startup_settle_seconds <= 60.0:
        parser.error("--startup-settle-seconds must be between 0 and 60")
    if args.integer_boundary:
        for input_spec in INTEGER_BOUNDARY_INPUTS.values():
            input_path = input_spec["path"]
            if not input_path.is_file():
                parser.error(
                    f"--integer-boundary input does not exist: {input_path}")
            integer_digest = hashlib.sha256(input_path.read_bytes()).hexdigest()
            if integer_digest != input_spec["sha256"]:
                parser.error(
                    "--integer-boundary input digest changed: "
                    f"expected {input_spec['sha256']}, got {integer_digest}")
    if value_boundary_cases:
        for input_name in sorted({
            case["input"] for case in value_boundary_cases
        }):
            input_spec = VALUE_BOUNDARY_INPUTS[input_name]
            input_path = input_spec["path"]
            if not input_path.is_file():
                parser.error(
                    f"natural value-boundary input does not exist: {input_path}")
            value_digest = hashlib.sha256(input_path.read_bytes()).hexdigest()
            if value_digest != input_spec["sha256"]:
                parser.error(
                    "natural value-boundary input digest changed: "
                    f"expected {input_spec['sha256']}, got {value_digest}")
    if args.resource_boundary:
        if not MEDIA_FIRST_INPUT.is_file():
            parser.error(
                f"--resource-boundary input does not exist: "
                f"{MEDIA_FIRST_INPUT}")
        resource_digest = hashlib.sha256(
            MEDIA_FIRST_INPUT.read_bytes()).hexdigest()
        if resource_digest != MEDIA_FIRST_SHA256:
            parser.error(
                "--resource-boundary input digest changed: "
                f"expected {MEDIA_FIRST_SHA256}, got {resource_digest}")
    if args.media_dictionary:
        if not MEDIA_DICTIONARY_INPUT.is_file():
            parser.error(
                f"--media-dictionary input does not exist: "
                f"{MEDIA_DICTIONARY_INPUT}")
        dictionary_digest = hashlib.sha256(
            MEDIA_DICTIONARY_INPUT.read_bytes()).hexdigest()
        if dictionary_digest != MEDIA_DICTIONARY_SHA256:
            parser.error(
                "--media-dictionary input digest changed: "
                f"expected {MEDIA_DICTIONARY_SHA256}, got {dictionary_digest}")
    if args.media_array:
        if not MEDIA_FIRST_INPUT.is_file():
            parser.error(
                f"--media-array input does not exist: {MEDIA_FIRST_INPUT}")
        array_digest = hashlib.sha256(
            MEDIA_FIRST_INPUT.read_bytes()).hexdigest()
        if array_digest != MEDIA_FIRST_SHA256:
            parser.error(
                "--media-array input digest changed: "
                f"expected {MEDIA_FIRST_SHA256}, got {array_digest}")

    failed = False
    tracer = None
    with AdbHarnessEngine(
        serial=args.serial, remote_dir=args.remote_dir) as engine:
        if args.trace:
            from oracle_runner.frida_tracer import FridaTracerEngine
            from oracle_runner.trace_targets import (
                PSBFILE_INTEGER_TARGETS,
                PSBFILE_LOAD_TARGETS,
                PSBFILE_MEDIA_TARGETS,
                PSBFILE_REAL_TARGETS,
                PSBFILE_RESOURCE_TARGETS,
                PSBFILE_SHAPE_TARGETS,
                PSBFILE_STRING_TARGETS,
            )

            targets = list(dict.fromkeys(
                PSBFILE_LOAD_TARGETS
                + (PSBFILE_INTEGER_TARGETS
                   if args.integer_boundary else [])
                + (PSBFILE_REAL_TARGETS if args.real_boundary else [])
                + (PSBFILE_STRING_TARGETS if args.string_boundary else [])
                + (PSBFILE_SHAPE_TARGETS if args.shape_boundary else [])
                + (PSBFILE_RESOURCE_TARGETS
                   if args.resource_boundary else [])
                + (PSBFILE_MEDIA_TARGETS if media_mode else [])
            ))
            tracer = FridaTracerEngine(engine, targets)
            tracer.attach()
        try:
            if args.remote_dir is not None or os.environ.get(ENV_REMOTE_DIR):
                readable_remote_dir = engine.remote_dir.rstrip("/") or "/"
            else:
                readable_remote_dir = _app_files_dir(engine)
            for index, input_path in enumerate(args.input):
                cases = [("octet", None)]
                if args.storage:
                    remote_path = _push_readable_input(
                        engine,
                        input_path,
                        readable_remote_dir,
                        f"psbfile-load-oracle-{index}.bin",
                    )
                    cases.append(("storage", remote_path))
                if args.decrypt_seed is not None:
                    cases.append(("decrypt", None))

                for entry, remote_path in cases:
                    trace = None
                    try:
                        if tracer is not None:
                            tracer.start_case()
                        if entry == "storage":
                            result = adapter.run_storage_case(
                                engine, input_path, remote_path)
                        elif entry == "decrypt":
                            result = adapter.run_decrypt_case(
                                engine, input_path, args.decrypt_seed)
                        else:
                            result = adapter.run_case(engine, input_path)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_path),
                            "entry": entry,
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

            if args.shape_boundary:
                trace = None
                trace_started = False
                input_spec = VALUE_BOUNDARY_INPUTS["m2logo"]
                try:
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_raw_holder_lifecycle_case(
                        engine, input_spec["path"])
                    result["input_sha256"] = input_spec["sha256"]
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(input_spec["path"]),
                        "entry": "raw-holder-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

                trace = None
                trace_started = False
                try:
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_raw_dictionary_lifecycle_case(
                        engine, input_spec["path"])
                    result["input_sha256"] = input_spec["sha256"]
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(input_spec["path"]),
                        "entry": "raw-dictionary-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

                trace = None
                trace_started = False
                try:
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_raw_dictionary_keys_lifecycle_case(
                        engine, input_spec["path"])
                    result["input_sha256"] = input_spec["sha256"]
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(input_spec["path"]),
                        "entry": "raw-dictionary-keys-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            tjs_startup_error = None
            if tjs_mode:
                try:
                    remote_startup = _push_app_file(
                        engine, args.startup_xp3, MEDIA_STARTUP_REMOTE_NAME)
                    _trigger_startup(engine, remote_startup)
                    if args.startup_settle_seconds:
                        time.sleep(args.startup_settle_seconds)
                except Exception as exc:
                    tjs_startup_error = exc

            if args.media_interface:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_interface_lifecycle_case(
                        engine)
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "entry": "media-interface-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.integer_boundary:
                remote_integer_inputs = {}
                integer_setup_error = tjs_startup_error
                if integer_setup_error is None:
                    try:
                        for input_name, input_spec in (
                            INTEGER_BOUNDARY_INPUTS.items()
                        ):
                            remote_integer_inputs[input_name] = (
                                _push_readable_input(
                                    engine,
                                    input_spec["path"],
                                    readable_remote_dir,
                                    input_spec["remote_name"],
                                )
                            )
                    except Exception as exc:
                        integer_setup_error = exc

                for case in INTEGER_BOUNDARY_CASES:
                    trace = None
                    trace_started = False
                    input_spec = INTEGER_BOUNDARY_INPUTS[case["input"]]
                    try:
                        if integer_setup_error is not None:
                            raise RuntimeError(
                                "integer boundary setup failed: "
                                f"{integer_setup_error!r}"
                            ) from integer_setup_error
                        if tracer is not None:
                            tracer.start_case()
                            trace_started = True
                        result = adapter.run_integer_boundary_case(
                            engine,
                            case_name=case["name"],
                            input_path=input_spec["path"],
                            remote_path=remote_integer_inputs[case["input"]],
                            node_offset=case["node_offset"],
                            expected_node_bytes=case["node_bytes"],
                            tjs_expression=case["expression"],
                            expected_variant_value=case["variant_value"],
                            expected_get_int=case["get_int"],
                        )
                        result["input_sha256"] = input_spec["sha256"]
                        result["startup_xp3"] = str(args.startup_xp3)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_spec["path"]),
                            "entry": f"integer-boundary-{case['name']}",
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None and trace_started:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

            if value_boundary_cases:
                remote_value_inputs = {}
                value_setup_error = tjs_startup_error
                if value_setup_error is None:
                    try:
                        for input_name in sorted({
                            case["input"] for case in value_boundary_cases
                        }):
                            input_spec = VALUE_BOUNDARY_INPUTS[input_name]
                            remote_value_inputs[input_name] = (
                                _push_readable_input(
                                    engine,
                                    input_spec["path"],
                                    readable_remote_dir,
                                    input_spec["remote_name"],
                                )
                            )
                    except Exception as exc:
                        value_setup_error = exc

                for case in (
                    REAL_BOUNDARY_CASES if args.real_boundary else ()
                ):
                    trace = None
                    trace_started = False
                    input_spec = VALUE_BOUNDARY_INPUTS[case["input"]]
                    try:
                        if value_setup_error is not None:
                            raise RuntimeError(
                                "real boundary setup failed: "
                                f"{value_setup_error!r}"
                            ) from value_setup_error
                        if tracer is not None:
                            tracer.start_case()
                            trace_started = True
                        result = adapter.run_real_boundary_case(
                            engine,
                            case_name=case["name"],
                            input_path=input_spec["path"],
                            remote_path=remote_value_inputs[case["input"]],
                            node_offset=case["node_offset"],
                            expected_node_bytes=case["node_bytes"],
                            expected_double_bits_le=case["double_bits_le"],
                            tjs_expression=case["expression"],
                        )
                        result["input_sha256"] = input_spec["sha256"]
                        result["startup_xp3"] = str(args.startup_xp3)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_spec["path"]),
                            "entry": f"real-boundary-{case['name']}",
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None and trace_started:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

                for case in (
                    STRING_BOUNDARY_CASES if args.string_boundary else ()
                ):
                    trace = None
                    trace_started = False
                    input_spec = VALUE_BOUNDARY_INPUTS[case["input"]]
                    try:
                        if value_setup_error is not None:
                            raise RuntimeError(
                                "string boundary setup failed: "
                                f"{value_setup_error!r}"
                            ) from value_setup_error
                        if tracer is not None:
                            tracer.start_case()
                            trace_started = True
                        result = adapter.run_string_boundary_case(
                            engine,
                            case_name=case["name"],
                            input_path=input_spec["path"],
                            remote_path=remote_value_inputs[case["input"]],
                            node_offset=case["node_offset"],
                            expected_node_bytes=case["node_bytes"],
                            string_data_offset=case["string_data_offset"],
                            expected_string_size=case["string_size"],
                            expected_string_sha256=case["string_sha256"],
                            tjs_expression=case["expression"],
                        )
                        result["input_sha256"] = input_spec["sha256"]
                        result["startup_xp3"] = str(args.startup_xp3)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_spec["path"]),
                            "entry": f"string-boundary-{case['name']}",
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None and trace_started:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

                for case in (
                    NULL_BOUNDARY_CASES if args.shape_boundary else ()
                ):
                    trace = None
                    trace_started = False
                    input_spec = VALUE_BOUNDARY_INPUTS[case["input"]]
                    try:
                        if value_setup_error is not None:
                            raise RuntimeError(
                                "null boundary setup failed: "
                                f"{value_setup_error!r}"
                            ) from value_setup_error
                        if tracer is not None:
                            tracer.start_case()
                            trace_started = True
                        result = adapter.run_null_boundary_case(
                            engine,
                            case_name=case["name"],
                            input_path=input_spec["path"],
                            remote_path=remote_value_inputs[case["input"]],
                            node_offset=case["node_offset"],
                            expected_node_bytes=case["node_bytes"],
                            tjs_expression=case["expression"],
                        )
                        result["input_sha256"] = input_spec["sha256"]
                        result["startup_xp3"] = str(args.startup_xp3)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_spec["path"]),
                            "entry": f"null-boundary-{case['name']}",
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None and trace_started:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

                for case in (
                    COLLECTION_BOUNDARY_CASES
                    if args.shape_boundary else ()
                ):
                    trace = None
                    trace_started = False
                    input_spec = VALUE_BOUNDARY_INPUTS[case["input"]]
                    try:
                        if value_setup_error is not None:
                            raise RuntimeError(
                                "collection boundary setup failed: "
                                f"{value_setup_error!r}"
                            ) from value_setup_error
                        if tracer is not None:
                            tracer.start_case()
                            trace_started = True
                        result = adapter.run_collection_lifecycle_case(
                            engine,
                            case_name=case["name"],
                            input_path=input_spec["path"],
                            remote_path=remote_value_inputs[case["input"]],
                            node_offset=case["node_offset"],
                            expected_entry_count=case["entry_count"],
                            expected_table_byte_size=
                                case["table_byte_size"],
                            expected_node_prefix=case["node_prefix"],
                            tjs_expression=case["expression"],
                            probe_expression=case["probe_expression"],
                            expected_probe_type=case["probe_type"],
                            expected_probe_value=case.get("probe_value"),
                            expected_probe_bits_le=
                                case.get("probe_bits_le"),
                            expected_negative_index_value=
                                case.get("negative_index_value"),
                        )
                        result["input_sha256"] = input_spec["sha256"]
                        result["startup_xp3"] = str(args.startup_xp3)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_spec["path"]),
                            "entry": f"collection-boundary-{case['name']}",
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None and trace_started:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

            if args.resource_boundary:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    remote_resource_input = _push_readable_input(
                        engine,
                        MEDIA_FIRST_INPUT,
                        readable_remote_dir,
                        RESOURCE_BOUNDARY_REMOTE_NAME,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_resource_boundary_case(
                        engine,
                        input_path=MEDIA_FIRST_INPUT,
                        remote_path=remote_resource_input,
                        node_offset=RESOURCE_BOUNDARY_NODE_OFFSET,
                        expected_node_bytes=RESOURCE_BOUNDARY_NODE_BYTES,
                        resource_data_offset=RESOURCE_BOUNDARY_DATA_OFFSET,
                        expected_resource_size=RESOURCE_BOUNDARY_SIZE,
                        expected_resource_sha256=RESOURCE_BOUNDARY_SHA256,
                        tjs_expression=RESOURCE_BOUNDARY_EXPRESSION,
                    )
                    result["input_sha256"] = MEDIA_FIRST_SHA256
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_FIRST_INPUT),
                        "entry": "resource-boundary",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_lifecycle:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_FIRST_INPUT,
                        readable_remote_dir,
                        MEDIA_FIRST_CONTAINER,
                    )
                    _push_readable_input(
                        engine,
                        MEDIA_REPLACEMENT_INPUT,
                        readable_remote_dir,
                        MEDIA_REPLACEMENT_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_lifecycle_case(
                        engine,
                        first_input=MEDIA_FIRST_INPUT,
                        replacement_input=MEDIA_REPLACEMENT_INPUT,
                        remote_dir=readable_remote_dir,
                        first_container=MEDIA_FIRST_CONTAINER,
                        replacement_container=MEDIA_REPLACEMENT_CONTAINER,
                        resource_name=MEDIA_RESOURCE_NAME,
                        expected_size=MEDIA_RESOURCE_SIZE,
                    )
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_FIRST_INPUT),
                        "replacement_input": str(MEDIA_REPLACEMENT_INPUT),
                        "entry": "media-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_dictionary:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_DICTIONARY_INPUT,
                        readable_remote_dir,
                        MEDIA_DICTIONARY_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_dictionary_case(
                        engine,
                        input_path=MEDIA_DICTIONARY_INPUT,
                        remote_dir=readable_remote_dir,
                        container=MEDIA_DICTIONARY_CONTAINER,
                        dictionary_path=MEDIA_DICTIONARY_PATH,
                        expected_keys=MEDIA_DICTIONARY_KEYS,
                    )
                    result["input_sha256"] = MEDIA_DICTIONARY_SHA256
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_DICTIONARY_INPUT),
                        "entry": "media-dictionary-list",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_array:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_FIRST_INPUT,
                        readable_remote_dir,
                        MEDIA_ARRAY_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_array_case(
                        engine,
                        input_path=MEDIA_FIRST_INPUT,
                        remote_dir=readable_remote_dir,
                        container=MEDIA_ARRAY_CONTAINER,
                        array_path=MEDIA_ARRAY_PATH,
                        node_offset=MEDIA_ARRAY_NODE_OFFSET,
                        expected_node_prefix=MEDIA_ARRAY_NODE_PREFIX,
                        expected_count=MEDIA_ARRAY_COUNT,
                        expected_packed_table_size=
                            MEDIA_ARRAY_PACKED_TABLE_SIZE,
                    )
                    result["input_sha256"] = MEDIA_FIRST_SHA256
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_FIRST_INPUT),
                        "entry": "media-array-list",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_adaptor_null:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_FIRST_INPUT,
                        readable_remote_dir,
                        MEDIA_ADAPTOR_NULL_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_adaptor_null_case(
                        engine,
                        input_path=MEDIA_FIRST_INPUT,
                        remote_dir=readable_remote_dir,
                        container=MEDIA_ADAPTOR_NULL_CONTAINER,
                    )
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_FIRST_INPUT),
                        "entry": "media-adaptor-null",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
