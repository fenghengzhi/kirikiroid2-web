#!/usr/bin/env python3
"""Build the auditable Motion NCB four-binary equivalence ledger.

The local inventory is only a candidate denominator.  This generator overlays
the native evidence already recorded in the four-binary surface reports and in
the Player address TSV.  A local row remains UNMAPPED unless one of those
authoritative evidence sources supplies all four platform mappings.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


PLATFORMS = (
    "android_arm64",
    "android_armv7",
    "ios_arm64",
    "ios_armv7",
)


@dataclass(frozen=True)
class LocalRow:
    candidate_id: str
    module: str
    owner: str
    sequence: int
    kind: str
    script_name: str
    binding: str
    local_source: str


@dataclass(frozen=True)
class Evidence:
    owner: str
    sequence: int
    script_name: str
    native_kind: str
    android_arm64: str
    android_armv7: str
    ios_arm64: str
    ios_armv7: str
    registration_status: str
    body_status: str
    evidence_report: str
    notes: str = ""


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def read_local(path: Path) -> list[LocalRow]:
    rows = []
    for raw in read_tsv(path):
        rows.append(
            LocalRow(
                candidate_id=raw["candidate_id"],
                module=raw["module"],
                owner=raw["owner"],
                sequence=int(raw["sequence"]),
                kind=raw["kind"],
                script_name=raw["script_name"],
                binding=raw["binding"],
                local_source=raw["local_source"],
            )
        )
    return rows


def section(text: str, start_heading: str, end_heading: str) -> str:
    start = text.index(start_heading)
    end = text.index(end_heading, start)
    return text[start:end]


def refs(registrar: str, callback: str) -> str:
    return f"registrar={registrar}; callback={callback}"


def add_evidence(target: dict[tuple[str, int], Evidence], row: Evidence) -> None:
    key = (row.owner, row.sequence)
    if key in target:
        raise ValueError(f"duplicate evidence key {key}")
    for platform in PLATFORMS:
        if not getattr(row, platform):
            raise ValueError(f"{key} lacks {platform} evidence")
    target[key] = row


def load_player_evidence(root: Path, target: dict[tuple[str, int], Evidence]) -> None:
    report = "analysis/motionplayer_player_ncb_surface_and_constructor_four_binary_2026-08-26.md"
    body_reports = {
        6: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        28: "analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md",
        29: "analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md",
        32: "analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md",
        33: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        34: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        37: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        68: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        69: "analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md",
        70: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        75: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        76: "analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md",
        80: "analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md",
        83: "analysis/motionplayer_raw_label_resolver_caller_closure_four_binary_2026-08-27.md",
    }
    add_evidence(
        target,
        Evidence(
            owner="Player",
            sequence=1,
            script_name="<constructor>",
            native_kind="TYPED_CONSTRUCTOR",
            android_arm64="publish=0x6D3E3C; dispatch=0x6F3FB0; attach=0x6F4088; ctor=0x6CC110",
            android_armv7="publish=0x598CFC; dispatch=0x5B0798; attach=0x5B0828; ctor=0x5935C4",
            ios_arm64="publish=0x1001253E0; dispatch=0x100146384; attach=0x100146428; ctor=0x10011EC04",
            ios_armv7="publish=0x124600; dispatch=0x1468E4; attach=0x146950; ctor=0x11D488",
            registration_status="EVIDENCED_4_4",
            body_status="CONSTRUCTOR_EVIDENCED_4_4",
            evidence_report=report,
            notes="one-tTJSVariant constructor plus Void-shell sentinel",
        ),
    )

    surface = read_tsv(root / "analysis/motionplayer_player_ncb_surface.tsv")
    if len(surface) != 92:
        raise ValueError(f"expected 92 Player surface rows, got {len(surface)}")
    for raw in surface:
        ordinal = int(raw["ordinal"])
        add_evidence(
            target,
            Evidence(
                owner="Player",
                sequence=ordinal + 1,
                script_name=raw["member"],
                native_kind=raw["kind"],
                android_arm64=(
                    f"name={raw['android_arm64_name']}; publish={raw['android_arm64_publish']}; "
                    f"callbacks={raw['android_arm64_callbacks_unordered_if_pair']}"
                ),
                android_armv7=(
                    f"name={raw['android_armv7_name']}; publish={raw['android_armv7_publish']}; "
                    f"callbacks={raw['android_armv7_callbacks']}"
                ),
                ios_arm64=(
                    f"name={raw['ios_arm64_name']}; publish={raw['ios_arm64_publish']}; "
                    f"callbacks={raw['ios_arm64_callbacks']}"
                ),
                ios_armv7=(
                    f"name={raw['ios_armv7_name']}; publish={raw['ios_armv7_publish']}; "
                    f"callbacks={raw['ios_armv7_callbacks']}"
                ),
                registration_status=raw["registration_status"],
                body_status=raw["body_status"],
                evidence_report=body_reports.get(ordinal, report),
            ),
        )


def load_layer_getter_evidence(
    root: Path, target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_layergetter_ncb_surface_and_constructor_four_binary_2026-08-26.md"
    scalar_report = (
        "analysis/motionplayer_layergetter_scalar_string_getters_four_binary_2026-08-26.md"
    )
    container_report = (
        "analysis/motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md"
    )
    object_report = (
        "analysis/motionplayer_layergetter_shape_motion_particle_lifetime_four_binary_2026-08-26.md"
    )
    implemented_scalars = {
        "type", "label", "src", "visible", "branchVisible", "layerVisible", "x", "y", "left",
        "top", "flipX", "flipY", "zoomX", "zoomY", "angleDeg", "angleRad",
        "slantX", "slantY", "originX", "originY", "opacity",
    }
    implemented_containers = {"coord", "mtx", "vtx", "color", "bezierPatch", "shape"}
    implemented_objects = {"motion", "particle"}
    text = (root / report).read_text(encoding="utf-8")
    add_evidence(
        target,
        Evidence(
            owner="LayerGetter",
            sequence=1,
            script_name="<constructor>",
            native_kind="ZERO_ARG_CONSTRUCTOR",
            android_arm64="registrar=0x698730; dispatch=0x6DFFAC; attach=0x6E0080",
            android_armv7="registrar=0x574628; dispatch=0x5A0BBC; attach=0x5A0C4C",
            ios_arm64="registrar=0x1000F81AC; dispatch=0x100131274; attach=0x100131314",
            ios_armv7="registrar=0xF4FF8; dispatch=0x13016C; attach=0x1301D8",
            registration_status="EVIDENCED_4_4",
            body_status="CONSTRUCTOR_EVIDENCED_4_4",
            evidence_report=report,
        ),
    )
    table = section(text, "## 3. 精确 property 顺序", "## 4. 字符串证据")
    pattern = re.compile(
        r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*`(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`(0x[0-9A-Fa-f]+)`\s*\|\s*`(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`(0x[0-9A-Fa-f]+)`\s*\|$",
        re.MULTILINE,
    )
    rows = pattern.findall(table)
    if len(rows) != 29:
        raise ValueError(f"expected 29 LayerGetter properties, got {len(rows)}")
    registrars = ("0x698730", "0x574628", "0x1000F81AC", "0xF4FF8")
    for ordinal, name, a64, a32, i64, i32 in rows:
        if name in implemented_scalars:
            body_status = "IMPLEMENTED"
            body_report = scalar_report
        elif name in implemented_containers:
            body_status = "IMPLEMENTED"
            body_report = container_report
        elif name in implemented_objects:
            body_status = "IMPLEMENTED"
            body_report = object_report
        else:
            body_status = "BODY_EVIDENCED_4_4"
            body_report = report
        add_evidence(
            target,
            Evidence(
                owner="LayerGetter",
                sequence=int(ordinal) + 1,
                script_name=name,
                native_kind="RO_PROPERTY",
                android_arm64=refs(registrars[0], a64),
                android_armv7=refs(registrars[1], a32),
                ios_arm64=refs(registrars[2], i64),
                ios_armv7=refs(registrars[3], i32),
                registration_status="EVIDENCED_4_4",
                body_status=body_status,
                evidence_report=body_report,
            ),
        )


def load_geometry_evidence(
    root: Path, target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_geometry_ncb_registration_surface_four_binary_2026-08-26.md"
    ctor_report = (
        "analysis/motionplayer_geometry_default_constructors_four_binary_2026-08-26.md"
    )
    scalar_report = (
        "analysis/motionplayer_geometry_scalar_getters_four_binary_2026-08-26.md"
    )
    contains_report = (
        "analysis/motionplayer_geometry_contains_boundary_four_binary_2026-08-26.md"
    )
    quad_report = (
        "analysis/motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md"
    )
    registrars = {
        "Point": ("0x68E39C", "0x56E348", "0x1000F079C", "0xECA00"),
        "Circle": ("0x68E6E0", "0x56E484", "0x1000F08BC", "0xECADA"),
        "Rect": ("0x68EA84", "0x56E5CC", "0x1000F09F4", "0xECBC8"),
        "Quad": ("0x68EEB0", "0x56E760", "0x1000F0B7C", "0xECD06"),
    }
    ctor_helpers = {
        "Point": ("inline", "0x56E3D4", "0x1000F0854", "0xECA98"),
        "Circle": ("inline", "0x56E52C", "0x1000F099C", "0xECB94"),
        "Rect": ("inline", "0x56E690", "0x1000F0AFC", "0xECCA4"),
        "Quad": ("inline", "0x56E7D0", "0x1000F0C0C", "0xECD7C"),
    }
    callbacks = {
        "type": ("0x68E628", "0x56E3FA", "0x1000F08A4", "0xECABE"),
        "contains": ("0x68E1D0", "0x56E1B0", "0x1000F0670", "0xEC8D0"),
        "x": ("0x68E630", "0x56E3FE", "0x1000F08AC", "0xECAC2"),
        "y": ("0x68E638", "0x56E408", "0x1000F08B4", "0xECACC"),
        "r": ("0x68E9DC", "0x56E552", "0x1000F09EC", "0xECBBA"),
        "l": ("0x68EDE0", "0x56E6B6", "0x1000F0B4C", "0xECCCA"),
        "t": ("0x68EDE8", "0x56E6C0", "0x1000F0B54", "0xECCD4"),
        "w": ("0x68EDF0", "0x56E6CA", "0x1000F0B5C", "0xECCDE"),
        "h": ("0x68EE00", "0x56E6DC", "0x1000F0B6C", "0xECCF0"),
        "p": ("0x68F0D4", "0x56E7F8", "0x1000F0C5C", "0xECDA4"),
    }
    members = {
        "Point": ("<constructor>", "type", "contains", "x", "y"),
        "Circle": ("<constructor>", "type", "contains", "x", "y", "r"),
        "Rect": ("<constructor>", "type", "contains", "l", "t", "w", "h"),
        "Quad": ("<constructor>", "type", "contains", "p"),
    }
    for owner, names in members.items():
        regs = registrars[owner]
        helpers = ctor_helpers[owner]
        for sequence, name in enumerate(names, 1):
            if sequence == 1:
                fields = tuple(
                    f"registrar={regs[index]}; ctor_publish={helpers[index]}"
                    for index in range(4)
                )
                native_kind = "ZERO_ARG_CONSTRUCTOR"
                body_status = "IMPLEMENTED"
                body_report = ctor_report
            else:
                callback = callbacks[name]
                fields = tuple(refs(regs[index], callback[index]) for index in range(4))
                native_kind = "METHOD" if name == "contains" else "RO_PROPERTY"
                body_status = "IMPLEMENTED"
                if name == "contains":
                    body_report = contains_report
                elif name == "p":
                    body_report = quad_report
                else:
                    body_report = scalar_report
            add_evidence(
                target,
                Evidence(
                    owner=owner,
                    sequence=sequence,
                    script_name=name,
                    native_kind=native_kind,
                    android_arm64=fields[0],
                    android_armv7=fields[1],
                    ios_arm64=fields[2],
                    ios_armv7=fields[3],
                    registration_status="EVIDENCED_4_4",
                    body_status=body_status,
                    evidence_report=body_report,
                ),
            )


def load_motion_evidence(root: Path, target: dict[tuple[str, int], Evidence]) -> None:
    report = "analysis/motionplayer_motion_class_registration_surface_four_binary_2026-08-26.md"
    method_report = (
        "analysis/motionplayer_motion_alpha_mask_d3d_available_four_binary_2026-08-27.md"
    )
    text = (root / report).read_text(encoding="utf-8")
    registrars = ("0x6D6EE8", "0x5991D0", "0x100125974", "0x124B7C")
    helpers = ("0x6D766C", "0x5994DC", "0x100125C9C", "0x124E68")

    constant_table = section(text, "## 3. 精确常量表", "## 4. 精确 subclass 表")
    constant_rows = re.findall(
        r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*(-?\d+)\s*\|$",
        constant_table,
        re.MULTILINE,
    )
    if len(constant_rows) != 23:
        raise ValueError(f"expected 23 Motion constants, got {len(constant_rows)}")
    for ordinal, name, value in constant_rows:
        fields = tuple(
            f"registrar={registrars[index]}; constant_helper={helpers[index]}; value={value}"
            for index in range(4)
        )
        add_evidence(
            target,
            Evidence(
                owner="Motion",
                sequence=int(ordinal),
                script_name=name,
                native_kind="STATIC_VARIANT_CONSTANT",
                android_arm64=fields[0],
                android_armv7=fields[1],
                ios_arm64=fields[2],
                ios_armv7=fields[3],
                registration_status="EVIDENCED_4_4",
                body_status="NOT_APPLICABLE_CONSTANT",
                evidence_report=report,
            ),
        )

    subclass_table = section(text, "## 4. 精确 subclass 表", "## 5. namespace method publication")
    subclass_rows = re.findall(
        r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*`(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`(0x[0-9A-Fa-f]+)`\s*\|\s*`(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`(0x[0-9A-Fa-f]+)`\s*\|$",
        subclass_table,
        re.MULTILINE,
    )
    if len(subclass_rows) != 11:
        raise ValueError(f"expected 11 Motion subclasses, got {len(subclass_rows)}")
    for ordinal, name, a64, a32, i64, i32 in subclass_rows:
        add_evidence(
            target,
            Evidence(
                owner="Motion",
                sequence=23 + int(ordinal),
                script_name=name,
                native_kind="STATIC_SUBCLASS",
                android_arm64=refs(registrars[0], a64),
                android_armv7=refs(registrars[1], a32),
                ios_arm64=refs(registrars[2], i64),
                ios_armv7=refs(registrars[3], i32),
                registration_status="EVIDENCED_4_4",
                body_status="SEE_SUBCLASS_LEDGER",
                evidence_report=report,
            ),
        )

    method_table = section(text, "## 5. namespace method publication", "## 6. 字符串类型修正")
    method_rows = re.findall(
        r"^\|\s*`([^`]+)`\s*\|\s*`[^`]*@(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`[^`]*@(0x[0-9A-Fa-f]+)`\s*\|\s*`[^`]*@(0x[0-9A-Fa-f]+)`\s*\|"
        r"\s*`[^`]*@(0x[0-9A-Fa-f]+)`\s*\|$",
        method_table,
        re.MULTILINE,
    )
    if len(method_rows) != 2:
        raise ValueError(f"expected 2 Motion namespace methods, got {len(method_rows)}")
    for index, (name, a64, a32, i64, i32) in enumerate(method_rows, 35):
        add_evidence(
            target,
            Evidence(
                owner="Motion",
                sequence=index,
                script_name=name,
                native_kind="STATIC_METHOD",
                android_arm64=refs(registrars[0], a64),
                android_armv7=refs(registrars[1], a32),
                ios_arm64=refs(registrars[2], i64),
                ios_armv7=refs(registrars[3], i32),
                registration_status="EVIDENCED_4_4",
                body_status="IMPLEMENTED",
                evidence_report=method_report,
            ),
        )


def load_sla_evidence(target: dict[tuple[str, int], Evidence]) -> None:
    registrar = ("0x6A9378", "0x57C5A8", "0x100103080", "0x1004A6")
    rows = (
        Evidence(
            "SeparateLayerAdaptor", 1, "<constructor>", "TYPED_CONSTRUCTOR",
            "registrar=0x6A9378; invoke=0x6EBECC; attach=0x6EBFA4; allocate=0x6EC0BC",
            "registrar=0x57C5A8; invoke=0x5AA1C8; allocate_attach=0x5AA258",
            "registrar=0x100103080; invoke=0x10013D3E8; allocate_attach=0x10013D48C",
            "registrar=0x1004A6; invoke=0x13DE14; attach=0x13DE80; allocate=0x13DFC8",
            "EVIDENCED_4_4", "IMPLEMENTED",
            "analysis/motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md",
        ),
        Evidence(
            "SeparateLayerAdaptor", 2, "absolute", "RW_PROPERTY",
            refs(registrar[0], "get=0x6A9638,set=0x6A9640"),
            refs(registrar[1], "get=0x57C67E,set=0x57C682"),
            refs(registrar[2], "get=0x100103188,set=0x100103190"),
            refs(registrar[3], "get=0x100574,set=0x100578"),
            "EVIDENCED_4_4", "IMPLEMENTED",
            "analysis/motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md",
        ),
        Evidence(
            "SeparateLayerAdaptor", 3, "targetLayer", "RW_PROPERTY",
            refs(registrar[0], "get=0x6A9648,set=0x6A9654"),
            refs(registrar[1], "get=0x57C686,set=0x57C692"),
            refs(registrar[2], "get=0x100103198,set=0x1001031A4"),
            refs(registrar[3], "get=0x10057C,set=0x100588"),
            "EVIDENCED_4_4", "IMPLEMENTED",
            "analysis/motionplayer_separate_layer_target_layer_property_four_binary_2026-08-27.md",
        ),
        Evidence(
            "SeparateLayerAdaptor", 4, "clear", "METHOD",
            refs(registrar[0], "0x6A965C"), refs(registrar[1], "0x57C698"),
            refs(registrar[2], "0x1001031AC"), refs(registrar[3], "0x100590"),
            "EVIDENCED_4_4", "IMPLEMENTED",
            "analysis/motionplayer_separate_layer_clear_destructor_four_binary_2026-08-27.md",
        ),
        Evidence(
            "SeparateLayerAdaptor", 5, "assign", "METHOD",
            "registrar=0x6A9378; wrapper=0x6EC920; body=0x6A97F0",
            "registrar=0x57C5A8; wrapper=0x5AAD1C; body=0x57C814",
            "registrar=0x100103080; wrapper=0x10013E198; body=0x10010347C",
            "registrar=0x1004A6; wrapper=0x13EED4; body=0x100874",
            "EVIDENCED_4_4", "IMPLEMENTED",
            "analysis/motionplayer_separate_layer_assign_four_binary_2026-08-27.md",
        ),
    )
    for row in rows:
        add_evidence(target, row)


def load_source_obj_evidence(target: dict[tuple[str, int], Evidence]) -> None:
    report = "analysis/motionplayer_sourcecache_objsource_ncb_surface_four_binary_2026-08-27.md"
    source_body_report = (
        "analysis/motionplayer_sourcecache_load_clear_buflayer_four_binary_2026-08-27.md"
    )
    obj_body_report = (
        "analysis/motionplayer_objsource_getters_clip_draw_decode_texture_lifetime_four_binary_2026-08-27.md"
    )

    source_regs = ("0x6A5988", "0x57B0DC", "0x100100F90", "0xFE12A")
    source_rows = (
        Evidence(
            "SourceCache", 1, "<constructor>", "ZERO_ARG_CONSTRUCTOR",
            "wrapper=0x6FB504; setup=0x6FB668; registrar=0x6A5988; ctor_publish=inline@0x6A59A8",
            "wrapper=0x599738; setup=0x5B6A20; registrar=0x57B0DC; ctor_publish=0x57B14C",
            "wrapper=0x100126064; setup=0x10014DF88; registrar=0x100100F90; ctor_publish=0x100101018",
            "wrapper=0x12514C; setup=0x14FC78; registrar=0xFE12A; ctor_publish=0xFE19C",
            "EVIDENCED_4_4", "CONSTRUCTOR_EVIDENCED_4_4", report,
        ),
        Evidence(
            "SourceCache", 2, "loadSource", "METHOD",
            refs(source_regs[0], "entry=0x6A4F88; containing_func=0x6A4CD4"),
            refs(source_regs[1], "0x57ACC8"),
            refs(source_regs[2], "0x1001009AC"),
            refs(source_regs[3], "0xFDB50"),
            "EVIDENCED_4_4", "IMPLEMENTED", source_body_report,
            "Android arm64 callback is an internal entry in one combined IDA function range",
        ),
        Evidence(
            "SourceCache", 3, "clearCache", "METHOD",
            refs(source_regs[0], "entry=0x6A5818; containing_func=0x6A4CD4"),
            refs(source_regs[1], "0x57B018"),
            refs(source_regs[2], "0x100100F10"),
            refs(source_regs[3], "0xFE0D4"),
            "EVIDENCED_4_4", "IMPLEMENTED", source_body_report,
            "Android arm64 callback is an internal entry in one combined IDA function range",
        ),
        Evidence(
            "SourceCache", 4, "bufLayer", "RO_PROPERTY",
            refs(source_regs[0], "get=0x6A58DC"),
            refs(source_regs[1], "get=0x57B060"),
            refs(source_regs[2], "get=0x100100F84"),
            refs(source_regs[3], "get=0xFE11A"),
            "EVIDENCED_4_4", "IMPLEMENTED", source_body_report,
        ),
    )

    obj_regs = ("0x69A098", "0x575028", "0x1000F8D30", "0xF5C48")
    obj_rows = (
        Evidence(
            "ObjSource", 1, "<constructor>", "ZERO_ARG_CONSTRUCTOR",
            "wrapper=0x6FB9F0; setup=0x6FBB54; registrar=0x69A098; ctor_publish=inline@0x69A0B8",
            "wrapper=0x59977C; setup=0x5B6CDC; registrar=0x575028; ctor_publish=0x5750F4",
            "wrapper=0x1001260DC; setup=0x10014E328; registrar=0x1000F8D30; ctor_publish=0x1000F8E38",
            "wrapper=0x125194; setup=0x150088; registrar=0xF5C48; ctor_publish=0xF5D24",
            "EVIDENCED_4_4", "CONSTRUCTOR_EVIDENCED_4_4", report,
        ),
        Evidence(
            "ObjSource", 2, "originX", "RO_PROPERTY",
            refs(obj_regs[0], "get=0x69A3F4"),
            refs(obj_regs[1], "get=0x57511C"),
            refs(obj_regs[2], "get=0x1000F8E88"),
            refs(obj_regs[3], "get=0xF5D4C"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
        Evidence(
            "ObjSource", 3, "originY", "RO_PROPERTY",
            refs(obj_regs[0], "get=0x69A4B8"),
            refs(obj_regs[1], "get=0x575180"),
            refs(obj_regs[2], "get=0x1000F8EEC"),
            refs(obj_regs[3], "get=0xF5E04"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
        Evidence(
            "ObjSource", 4, "width", "RO_PROPERTY",
            refs(obj_regs[0], "get=0x69A57C"),
            refs(obj_regs[1], "get=0x5751E4"),
            refs(obj_regs[2], "get=0x1000F8F50"),
            refs(obj_regs[3], "get=0xF5EBC"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
        Evidence(
            "ObjSource", 5, "height", "RO_PROPERTY",
            refs(obj_regs[0], "get=0x69A65C"),
            refs(obj_regs[1], "get=0x575258"),
            refs(obj_regs[2], "get=0x1000F8FD0"),
            refs(obj_regs[3], "get=0xF5F8C"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
        Evidence(
            "ObjSource", 6, "clip", "RO_PROPERTY",
            refs(obj_regs[0], "get=0x69A73C"),
            refs(obj_regs[1], "get=0x5752CC"),
            refs(obj_regs[2], "get=0x1000F9050"),
            refs(obj_regs[3], "get=0xF605C"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
        Evidence(
            "ObjSource", 7, "drawLayer", "METHOD",
            refs(obj_regs[0], "0x69AAB8"),
            refs(obj_regs[1], "0x5754E4"),
            refs(obj_regs[2], "0x1000F930C"),
            refs(obj_regs[3], "0xF63C0"),
            "EVIDENCED_4_4", "IMPLEMENTED", obj_body_report,
        ),
    )

    for row in (*source_rows, *obj_rows):
        add_evidence(target, row)


def load_resource_manager_evidence(
    target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_resourcemanager_ncb_surface_four_binary_2026-08-27.md"
    layer_id_report = (
        "analysis/motionplayer_resourcemanager_layer_id_set_four_binary_2026-08-27.md"
    )
    random_report = (
        "analysis/motionplayer_resourcemanager_random_dispatch_four_binary_2026-08-27.md"
    )
    unload_all_report = (
        "analysis/motionplayer_resourcemanager_unload_all_map_clear_four_binary_2026-08-27.md"
    )
    unload_report = (
        "analysis/motionplayer_resourcemanager_unload_single_node_four_binary_2026-08-27.md"
    )
    load_report = (
        "analysis/motionplayer_resourcemanager_load_cache_validate_dispatch_four_binary_2026-08-27.md"
    )
    is_exist_motion_report = (
        "analysis/motionplayer_resourcemanager_is_exist_motion_direct_fallback_scan_four_binary_2026-08-27.md"
    )
    find_motion_report = (
        "analysis/motionplayer_resourcemanager_find_motion_dispatch_array_owner_four_binary_2026-08-27.md"
    )
    find_source_report = (
        "analysis/motionplayer_resourcemanager_find_source_blank_objsource_owner_four_binary_2026-08-27.md"
    )
    source_cache_report = (
        "analysis/motionplayer_sourcecache_load_clear_buflayer_four_binary_2026-08-27.md"
    )
    body_reports = {
        "loadSource": source_cache_report,
        "clearCache": source_cache_report,
        "bufLayer": source_cache_report,
        "load": load_report,
        "findMotion": find_motion_report,
        "findSource": find_source_report,
        "isExistMotion": is_exist_motion_report,
        "random": random_report,
        "requireLayerId": layer_id_report,
        "releaseLayerId": layer_id_report,
        "unload": unload_report,
        "unloadAll": unload_all_report,
    }
    registrars = ("0x6A8C9C", "0x57C3A8", "0x100102E88", "0x1002FC")
    add_evidence(
        target,
        Evidence(
            "ResourceManager", 1, "<constructor>",
            "TYPED_CONSTRUCTOR_VARIANT_INT",
            "wrapper=0x6FBEA4; setup=0x6FC014; registrar=0x6A8C9C; ctor_publish=inline@0x6A8CBC; invoke=0x6E9A98; construct_attach=0x6E9B70; allocate_convert=0x6E9C88; native_ctor=0x6A5CAC",
            "wrapper=0x5997C0; setup=0x5B6F80; registrar=0x57C3A8; ctor_publish=0x57C510; invoke=0x5A7DC0; construct_attach=0x5A7E50; allocate_convert=0x5A7F10; native_ctor=0x57B1EC",
            "wrapper=0x100126154; setup=0x10014E6AC; registrar=0x100102E88; ctor_publish=0x100103030; invoke=0x10013A644; construct_attach=0x10013A6E8; allocate_convert=0x10013A7D8; native_ctor=0x100101158",
            "wrapper=0x1251DC; setup=0x150480; registrar=0x1002FC; ctor_publish=0x10047C; invoke=0x13A730; construct_attach=0x13A79C; allocate_convert=0x13A8E4; native_ctor=0xFE254",
            "EVIDENCED_4_4", "CONSTRUCTOR_EVIDENCED_4_4", report,
            "argc>=2; argv[0] copied as Variant; argv[1] converted to tjs_int; surplus arguments ignored",
        ),
    )

    members = (
        (
            "loadSource", "METHOD",
            ("entry=0x6A4F88; containing_func=0x6A4CD4", "0x57ACC8",
             "0x1001009AC", "0xFDB50"),
            "exact SourceCache callback reuse",
        ),
        (
            "clearCache", "METHOD",
            ("entry=0x6A5818; containing_func=0x6A4CD4", "0x57B018",
             "0x100100F10", "0xFE0D4"),
            "exact SourceCache callback reuse",
        ),
        (
            "bufLayer", "RO_PROPERTY",
            ("get=0x6A58DC", "get=0x57B060", "get=0x100100F84",
             "get=0xFE11A"),
            "exact SourceCache getter reuse; all setter/index slots null",
        ),
        ("load", "METHOD", ("0x6A616C", "0x57B338", "0x1001012D8", "0xFE40C"), ""),
        ("unload", "METHOD", ("0x6A697C", "0x57B6F8", "0x100101A28", "0xFEC04"), ""),
        (
            "unloadAll", "METHOD",
            ("entry=0x6A60D8; containing_func=0x6A5F74", "0x57B32C",
             "0x1001012CC", "0xFE3FE"),
            "Android arm64 callback is an internal entry in the destructor-containing IDA function",
        ),
        ("isExistMotion", "METHOD", ("0x6A6AD8", "0x57B780", "0x100101AC8", "0xFECF4"), ""),
        ("findMotion", "METHOD", ("0x6A72B4", "0x57B9F8", "0x100101E84", "0xFF11C"), ""),
        ("findSource", "METHOD", ("0x6A7F1C", "0x57BDE0", "0x100102594", "0xFF890"), ""),
        ("random", "METHOD", ("0x6A894C", "0x57C1CC", "0x100102C90", "0x1000F0"), ""),
        ("requireLayerId", "METHOD", ("0x6A8A74", "0x57C258", "0x100102D40", "0x100240"), ""),
        ("releaseLayerId", "METHOD", ("0x6A8B30", "0x57C2C8", "0x100102DB8", "0x10028A"), ""),
    )
    for sequence, (name, kind, callbacks, notes) in enumerate(members, 2):
        body_report = body_reports.get(name)
        body_closed = body_report is not None
        add_evidence(
            target,
            Evidence(
                "ResourceManager", sequence, name, kind,
                refs(registrars[0], callbacks[0]),
                refs(registrars[1], callbacks[1]),
                refs(registrars[2], callbacks[2]),
                refs(registrars[3], callbacks[3]),
                "EVIDENCED_4_4",
                "IMPLEMENTED" if body_closed else "BODY_PENDING_SEPARATE_SLICE",
                body_report if body_report is not None else report,
                notes,
            ),
        )


def load_d3d_adaptor_evidence(
    target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_d3dadaptor_ncb_surface_four_binary_2026-08-27.md"
    body_report = "analysis/motionplayer_d3dadaptor_simple_state_map_clear_four_binary_2026-08-27.md"
    capture_report = "analysis/motionplayer_d3dadaptor_capture_canvas_four_binary_2026-08-27.md"
    registrars = ("0x6AA274", "0x57CC58", "0x1001039A4", "0x100D94")
    add_evidence(
        target,
        Evidence(
            "D3DAdaptor", 1, "<factory>", "FACTORY",
            "wrapper=0x6FC6D8; setup=0x6FC848; registrar=0x6AA274; factory_publish=inline; callback=0x6AA8F8; native_ctor=0x6AAEF0",
            "wrapper=0x599848; setup=0x5B74C8; registrar=0x57CC58; factory_publish=0x57CE94; callback=0x57CEBC; native_ctor=0x57D0AC",
            "wrapper=0x100126244; setup=0x10014EDB4; registrar=0x1001039A4; factory_publish=0x100103BDC; callback=0x100103C30; native_ctor=0x100103FA8",
            "wrapper=0x12526C; setup=0x150C70; registrar=0x100D94; factory_publish=0x100FAC; callback=0x100FD4; native_ctor=0x10128C",
            "EVIDENCED_4_4", "FACTORY_EVIDENCED_4_4", report,
            "argc>=5; argv[0] must be a Window instance; argv[1..4] convert to tjs_int; surplus arguments ignored",
        ),
    )

    members = (
        ("setPos", "METHOD", ("0x6AAB84", "0x57CF64", "0x100103D3C", "0x101128"),
         "four-platform no-op callback"),
        ("setSize", "METHOD", ("0x6AAB88", "0x57CF66", "0x100103D40", "0x10112A"), ""),
        ("setClearColor", "METHOD", ("0x6AAB90", "0x57CF6C", "0x100103D48", "0x101130"), ""),
        ("setResizable", "METHOD", ("0x6AAB98", "0x57CF70", "0x100103D50", "0x101134"), ""),
        ("removeAllTextures", "METHOD", ("0x6AAC98", "0x57CF74", "0x100103D58", "0x101138"), ""),
        ("removeAllBg", "METHOD", ("0x6AACD0", "0x57CF7A", "0x100103D88", "0x101154"),
         "four-platform no-op callback"),
        ("removeAllCaption", "METHOD", ("0x6AACD4", "0x57CF7C", "0x100103D8C", "0x101156"),
         "four-platform no-op callback"),
        ("registerBg", "METHOD", ("0x6AACD8", "0x57CF7E", "0x100103D90", "0x101158"),
         "four-platform no-op callback"),
        ("registerCaption", "METHOD", ("0x6AACDC", "0x57CF80", "0x100103D94", "0x10115A"),
         "four-platform no-op callback"),
        ("unloadUnusedTextures", "METHOD", ("0x6AACE0", "0x57CF82", "0x100103D98", "0x10115C"),
         "four-platform no-op callback"),
        ("visible", "RW_PROPERTY",
         ("get=0x6AACE4,set=0x6AACEC", "get=0x57CF84,set=0x57CF88",
          "get=0x100103D9C,set=0x100103DA4", "get=0x10115E,set=0x101162"), ""),
        ("alphaOpAdd", "RW_PROPERTY",
         ("get=0x6AACF8,set=0x6AAD00", "get=0x57CF8C,set=0x57CF90",
          "get=0x100103DAC,set=0x100103DB4", "get=0x101166,set=0x10116A"), ""),
        ("captureCanvas", "METHOD", ("0x6AAD0C", "0x57CF94", "0x100103DBC", "0x10116E"), ""),
        ("canvasCaptureEnabled", "RW_PROPERTY",
         ("get=0x6AAEC8,set=0x6AAED0", "get=0x57D09C,set=0x57D0A0",
          "get=0x100103F88,set=0x100103F90", "get=0x10127C,set=0x101280"), ""),
        ("clearEnabled", "RW_PROPERTY",
         ("get=0x6AAEDC,set=0x6AAEE4", "get=0x57D0A4,set=0x57D0A8",
          "get=0x100103F98,set=0x100103FA0", "get=0x101284,set=0x101288"), ""),
    )
    for sequence, (name, kind, callbacks, notes) in enumerate(members, 2):
        if name == "captureCanvas":
            evidence_report = capture_report
        else:
            evidence_report = body_report
        add_evidence(
            target,
            Evidence(
                "D3DAdaptor", sequence, name, kind,
                refs(registrars[0], callbacks[0]),
                refs(registrars[1], callbacks[1]),
                refs(registrars[2], callbacks[2]),
                refs(registrars[3], callbacks[3]),
                "EVIDENCED_4_4", "IMPLEMENTED", evidence_report,
                notes,
            ),
        )


def load_layer_extension_evidence(
    target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_bezier_layer_extensions_ncb_surface_four_binary_2026-08-27.md"
    bezier_body_report = (
        "analysis/motionplayer_bezierpatch_methods_geometry_inverse_four_binary_2026-08-27.md"
    )
    extension_body_report = (
        "analysis/motionplayer_layer_extensions_callbacks_lifetime_render_four_binary_2026-08-27.md"
    )

    bezier_regs = ("0x6A195C", "0x578C48", "0x1000FE1F0", "0xFB146")
    bezier_members = (
        ("affinePatch", ("0x6A1D40", "0x578D90", "0x1000FE308", "0xFB244"), ""),
        ("translatePatch", ("0x6A2048", "0x578F28", "0x1000FE4B4", "0xFB450"), ""),
        ("affineTranslatePatch", ("0x6A2328", "0x5790B0", "0x1000FE640", "0xFB64C"), ""),
        ("calcPatchBounds", ("0x6A264C", "0x579258", "0x1000FE804", "0xFB868"), ""),
        (
            "calcMeshBounds",
            ("entry=0x6A2A04; containing_func=0x6A264C", "0x5794F8",
             "0x1000FEAB8", "0xFBBDC"),
            "Android arm64 callback is an internal entry in the calcPatchBounds/calcMeshBounds/calcBezierPatch combined function",
        ),
        (
            "calcBezierPatch",
            ("entry=0x6A2D6C; containing_func=0x6A264C", "0x5797A0",
             "0x1000FEE38", "0xFC014"),
            "Android arm64 callback is an internal entry in the calcPatchBounds/calcMeshBounds/calcBezierPatch combined function",
        ),
        ("calcBezierPatchList", ("0x6A3230", "0x579A18", "0x1000FF134", "0xFC360"), ""),
        ("reverseCalcBezierPatch", ("0x6A3874", "0x579D48", "0x1000FF508", "0xFC7A4"), ""),
    )
    bezier_attach = (
        "attach=0x6E627C; detach=0x6E630C; setup=0x6E6428",
        "attach=0x5A51C0; detach=0x5A5244; setup=0x5A52C0",
        "attach=0x100136FA4; detach=0x10013700C; setup=0x100137068",
        "attach=0x136B9C; detach=0x136C50; setup=0x136D00",
    )
    for sequence, (name, callbacks, notes) in enumerate(bezier_members, 1):
        fields = tuple(
            (
                f"{bezier_attach[index]}; registrar={bezier_regs[index]}; "
                f"callback={callbacks[index]}"
                if sequence == 1
                else refs(bezier_regs[index], callbacks[index])
            )
            for index in range(4)
        )
        add_evidence(
            target,
            Evidence(
                "BezierPatch", sequence, name, "ATTACHED_STATIC_METHOD",
                fields[0], fields[1], fields[2], fields[3],
                "EVIDENCED_4_4", "IMPLEMENTED", bezier_body_report,
                notes or "stateless method attached directly to Layer; no script constructor or per-Layer native instance",
            ),
        )

    extension_regs = ("0x6A1204", "0x578A6C", "0x1000FE030", "0xFAFB0")
    extension_attach = (
        "attach=0x6E217C; detach=0x6E227C; setup=0x6E2408",
        "attach=0x5A2D00; detach=0x5A2D60; setup=0x5A2DF8",
        "attach=0x100133D68; detach=0x100133DCC; setup=0x100133E2C",
        "attach=0x132FD0; detach=0x133088; setup=0x133144",
    )
    extension_members = (
        (
            "debugMeshApp", "ATTACHED_RW_PROPERTY",
            ("get=0x6A1768,set=0x6A1774", "get=0x578BB4,set=0x578BC0",
             "get=0x1000FE1C8,set=0x1000FE1D4", "get=0xFB11E,set=0xFB12A"),
        ),
        (
            "debugBezierApp", "ATTACHED_RW_PROPERTY",
            ("get=0x6A177C,set=0x6A1788", "get=0x578BC6,set=0x578BD2",
             "get=0x1000FE1DC,set=0x1000FE1E8", "get=0xFB130,set=0xFB13C"),
        ),
        ("meshCopy", "ATTACHED_METHOD", ("0x69F150", "0x577924", "0x1000FC6E8", "0xF9654")),
        ("operateMesh", "ATTACHED_METHOD", ("0x69F304", "0x577A44", "0x1000FC864", "0xF97F4")),
        ("drawMeshFrame", "ATTACHED_METHOD", ("0x69F5E4", "0x577B50", "0x1000FC9C0", "0xF996C")),
        ("bezierPatchCopy", "ATTACHED_METHOD", ("0x69FD7C", "0x577F3C", "0x1000FCF78", "0xF9F08")),
        ("operateBezierPatch", "ATTACHED_METHOD", ("0x69FF30", "0x57805C", "0x1000FD0F4", "0xFA0A8")),
        ("drawBezierPatchFrame", "ATTACHED_METHOD", ("0x6A0210", "0x578168", "0x1000FD250", "0xFA220")),
        ("drawBezierPatchMeshFrame", "ATTACHED_METHOD", ("0x6A0B3C", "0x5786AC", "0x1000FDAF8", "0xFAAA4")),
    )
    for sequence, (name, kind, callbacks) in enumerate(extension_members, 1):
        fields = tuple(
            (
                f"{extension_attach[index]}; registrar={extension_regs[index]}; "
                f"callback={callbacks[index]}"
                if sequence == 1
                else refs(extension_regs[index], callbacks[index])
            )
            for index in range(4)
        )
        add_evidence(
            target,
            Evidence(
                "MotionLayerExtensions_guess", sequence, name, kind,
                fields[0], fields[1], fields[2], fields[3],
                "EVIDENCED_4_4", "IMPLEMENTED", extension_body_report,
                "member attached directly to Layer through the per-Layer lazy native-instance hook",
            ),
        )


def load_emote_player_evidence(
    root: Path, target: dict[tuple[str, int], Evidence]
) -> None:
    report = "analysis/motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-27.md"
    primary_flow_body_report = (
        "analysis/motionplayer_emoteplayer_primary_flow_raw_setters_"
        "four_binary_2026-08-27.md"
    )
    player_facade_body_report = (
        "analysis/motionplayer_emoteplayer_player_facade_properties_methods_"
        "four_binary_2026-08-27.md"
    )
    scale_trigger_body_report = (
        "analysis/motionplayer_emoteplayer_scale_trigger_variablekeys_animating_"
        "four_binary_2026-08-27.md"
    )
    timeline_selector_body_report = (
        "analysis/motionplayer_emoteplayer_timeline_selector_queries_"
        "four_binary_2026-08-27.md"
    )
    rows = read_tsv(root / "analysis/motionplayer_emoteplayer_ncb_surface.tsv")
    if len(rows) != 73:
        raise ValueError(f"expected 73 EmotePlayer surface rows, got {len(rows)}")
    for expected_sequence, raw in enumerate(rows, 1):
        sequence = int(raw["sequence"])
        if sequence != expected_sequence:
            raise ValueError(
                f"EmotePlayer surface sequence gap: expected {expected_sequence}, got {sequence}"
            )
        add_evidence(
            target,
            Evidence(
                owner="EmotePlayer",
                sequence=sequence,
                script_name=raw["script_name"],
                native_kind=raw["native_kind"],
                android_arm64=raw["android_arm64"],
                android_armv7=raw["android_armv7"],
                ios_arm64=raw["ios_arm64"],
                ios_armv7=raw["ios_armv7"],
                registration_status=raw["registration_status"],
                body_status=raw["body_status"],
                evidence_report=(
                    primary_flow_body_report
                    if 4 <= sequence <= 22
                    else player_facade_body_report
                    if 23 <= sequence <= 41
                    else scale_trigger_body_report
                    if 42 <= sequence <= 53
                    else timeline_selector_body_report
                    if 54 <= sequence <= 73
                    else report
                ),
                notes=raw["notes"],
            ),
        )


def write_evidence(path: Path, evidence: list[Evidence]) -> None:
    fields = tuple(Evidence.__dataclass_fields__)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(fields)
        for row in evidence:
            writer.writerow(tuple(getattr(row, field) for field in fields))


def write_ledger(
    path: Path,
    local_rows: list[LocalRow],
    evidence: dict[tuple[str, int], Evidence],
) -> None:
    header = (
        "candidate_id", "module", "owner", "sequence", "local_kind",
        "script_name", "binding", "local_source", "native_kind",
        "android_arm64", "android_armv7", "ios_arm64", "ios_armv7",
        "registration_status", "body_status", "evidence_report",
        "disposition", "notes",
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        for local in local_rows:
            native = evidence.get((local.owner, local.sequence))
            if native is not None:
                if native.script_name != local.script_name:
                    raise ValueError(
                        f"{local.owner} #{local.sequence}: local name {local.script_name!r} "
                        f"!= native evidence {native.script_name!r}"
                    )
                disposition = (
                    "SURFACE_AND_BODY_CLOSED"
                    if native.body_status == "IMPLEMENTED"
                    else "SURFACE_MAPPED_BODY_SEPARATE_OR_PENDING"
                )
                tail = (
                    native.native_kind,
                    native.android_arm64,
                    native.android_armv7,
                    native.ios_arm64,
                    native.ios_armv7,
                    native.registration_status,
                    native.body_status,
                    native.evidence_report,
                    disposition,
                    native.notes,
                )
            else:
                tail = (
                    "", "", "", "", "", "UNMAPPED", "UNMAPPED", "",
                    "MISSING_NATIVE_SURFACE_EVIDENCE", "",
                )
            writer.writerow(
                (
                    local.candidate_id,
                    local.module,
                    local.owner,
                    local.sequence,
                    local.kind,
                    local.script_name,
                    local.binding,
                    local.local_source,
                    *tail,
                )
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument(
        "--local",
        "--inventory",
        dest="local",
        type=Path,
        default=Path("analysis/motionplayer_local_ncb_inventory.tsv"),
        help="local NCB candidate inventory (the --inventory alias is preferred in audit commands)",
    )
    parser.add_argument(
        "--evidence-output",
        type=Path,
        default=Path("analysis/motionplayer_ncb_native_evidence.tsv"),
    )
    parser.add_argument(
        "--ledger-output",
        type=Path,
        default=Path("analysis/motionplayer_ncb_equivalence.tsv"),
    )
    args = parser.parse_args()
    root = args.root.resolve()
    local_path = root / args.local
    local_rows = read_local(local_path)
    if len(local_rows) != 316:
        raise ValueError(f"expected 316 local NCB candidates, got {len(local_rows)}")
    ids = [row.candidate_id for row in local_rows]
    if len(ids) != len(set(ids)):
        raise ValueError("local candidate IDs are not unique")

    evidence: dict[tuple[str, int], Evidence] = {}
    load_player_evidence(root, evidence)
    load_layer_getter_evidence(root, evidence)
    load_geometry_evidence(root, evidence)
    load_motion_evidence(root, evidence)
    load_sla_evidence(evidence)
    load_source_obj_evidence(evidence)
    load_resource_manager_evidence(evidence)
    load_d3d_adaptor_evidence(evidence)
    load_layer_extension_evidence(evidence)
    load_emote_player_evidence(root, evidence)

    expected_counts = {
        "Circle": 6,
        "D3DAdaptor": 16,
        "BezierPatch": 8,
        "EmotePlayer": 73,
        "LayerGetter": 30,
        "Motion": 36,
        "MotionLayerExtensions_guess": 9,
        "ObjSource": 7,
        "Player": 93,
        "Point": 5,
        "Quad": 4,
        "Rect": 7,
        "ResourceManager": 13,
        "SeparateLayerAdaptor": 5,
        "SourceCache": 4,
    }
    actual_counts: dict[str, int] = {}
    for row in evidence.values():
        actual_counts[row.owner] = actual_counts.get(row.owner, 0) + 1
    if actual_counts != expected_counts:
        raise ValueError(
            f"native evidence owner counts differ: {actual_counts!r} != {expected_counts!r}"
        )

    local_keys = {(row.owner, row.sequence) for row in local_rows}
    orphaned = sorted(set(evidence) - local_keys)
    if orphaned:
        raise ValueError(f"native evidence has no local candidate: {orphaned}")

    evidence_rows = [evidence[key] for key in sorted(evidence)]
    write_evidence(root / args.evidence_output, evidence_rows)
    write_ledger(root / args.ledger_output, local_rows, evidence)
    print(
        f"wrote {len(evidence_rows)} native evidence rows and "
        f"{len(local_rows)} merged ledger rows; "
        f"{len(local_rows) - len(evidence_rows)} remain UNMAPPED"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
