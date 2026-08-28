#!/usr/bin/env python3
"""Resolve MP-V01 no-test rows using only fixtures already in the repository."""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


TEST_CASE_START_RE = re.compile(
    r"^\s*(?:TEST_CASE|TEST_CASE_METHOD)\s*\("
)
STRING_LITERAL_RE = re.compile(r'"([^"]*)"')


@dataclass(frozen=True)
class Decision:
    disposition: str
    cases: tuple[str, ...]
    rationale: str
    remaining_gap: str


def reused(*cases: str, rationale: str = "") -> Decision:
    return Decision(
        "REUSED_EXISTING_FIXTURE",
        cases,
        rationale or "An existing local unit fixture already exercises the expressible contract",
        "Native/ADB/Wasmtime oracle strength and execution remain separate MP-V work",
    )


def added(*cases: str) -> Decision:
    return Decision(
        "ADDED_EXISTING_FIXTURE_TEST",
        cases,
        "A stable boundary was expressible with the existing motionplayer unit-test TU and no new material",
        "Deliberately uninitialized coordinates remain unasserted; formal unit execution is MP-V07",
    )


GEOMETRY_CASE = (
    "geometry facades preserve type-only defaults and initialized shared getters"
)
LAYER_GETTER_CASE = "LayerGetter is a live non-owning MotionNode facade"
ROOT_SURFACE_CASE = (
    "Motion root NCB surface preserves registrar order and dummy construction"
)
PLAYER_CTOR_CASE = (
    "Motion.Player NCB constructor preserves sentinel and first-argument ownership"
)
RESOURCE_CHAIN_CASE = "motionplayer resource chain and query surface"
PREPARED_CASE = (
    "prepared render builder retains reverse priority dispatch and trusts indexes"
)


SLICE_DECISIONS: dict[str, Decision] = {
    "MP-F01-ROOT-01": reused(
        ROOT_SURFACE_CASE,
        "Plugins link is a direct module-key load and unlink remains a true no-op",
    ),
    "MP-F03-NCB-SURFACE-LEDGER": reused(
        ROOT_SURFACE_CASE,
        "DrawDeviceD3D exposes the seven-class reference surface",
    ),
    "MP-A16-EMOTEPLAYER-NCB-SURFACE": reused(
        "Motion.EmotePlayer typed Factory requires arg0 and preserves the Void sentinel",
        "Motion EmotePlayer Boolean properties preserve one-way trigger semantics",
        "Motion EmotePlayer late timeline surfaces bind Engine members",
    ),
    "MP-A03-MODULE-LOADER-PLUGINS-AUTOLOAD-REGISTERED-SET": reused(
        "Plugins link is a direct module-key load and unlink remains a true no-op",
        "indexed and registered plugin names do not synthesize Storages paths",
        "autoload passes its complete Path and discovered Name as the module key",
    ),
    "MP-A14-REG-01": reused(ROOT_SURFACE_CASE),
    "MP-A11-PLAYER-REG": reused(ROOT_SURFACE_CASE, PLAYER_CTOR_CASE),
    "MP-A15-BEZIER-LAYER-EXTENSIONS-NCB-SURFACE": reused(
        ROOT_SURFACE_CASE,
        "Bezier patch basis and accumulation keep scalar operation order",
        "Mesh and Bezier renderers share one clip hint quartet",
    ),
    "MP-A12-SOURCECACHE-OBJSOURCE-NCB-SURFACE": reused(
        "Motion.SourceCache NCB constructor is default-only and ignores surplus arguments",
        "Motion.ObjSource NCB constructor publishes the empty raw-node facade",
    ),
    "MP-A09-REG-POINT": added(GEOMETRY_CASE),
    "MP-A09-REG-CIRCLE": added(GEOMETRY_CASE),
    "MP-A09-REG-RECT": added(GEOMETRY_CASE),
    "MP-A09-REG-QUAD": added(GEOMETRY_CASE),
    "MP-A09-GEOMETRY-SCALARS": added(GEOMETRY_CASE),
    "MP-B03-GEOMETRY-CONTAINS": reused(
        GEOMETRY_CASE,
        "Geometry contains rejects unordered rect bounds and keeps native quad direction",
    ),
    "MP-A10-LAYERGETTER-REG": reused(
        "Motion.LayerGetter NCB constructor publishes only the null-node facade",
        LAYER_GETTER_CASE,
    ),
    "MP-A10-LAYERGETTER-SCALARS": reused(LAYER_GETTER_CASE),
    "MP-A10-LAYERGETTER-ARRAYS": reused(LAYER_GETTER_CASE),
    "MP-L10-LAYERGETTER-VTX": reused(LAYER_GETTER_CASE),
    "MP-L10-LAYERGETTER-SHAPE": reused(
        LAYER_GETTER_CASE,
        "shape anchors resolve through LayerGetter rather than child motion",
    ),
    "MP-L10-LAYERGETTER-MOTION-PARTICLE": reused(
        LAYER_GETTER_CASE,
        "recursive layer lookup repeats particle child zero",
    ),
    "MP-A14-D3DADAPTOR-NCB-SURFACE": reused(
        "Motion.D3DAdaptor NCB factory and typed nullsubs preserve exact arity",
        "D3DAdaptor constructor follows the four-reference boundary",
    ),
    "MP-A13-RESOURCEMANAGER-NCB-SURFACE": reused(
        "Motion.ResourceManager NCB constructor preserves sentinel and arity boundaries",
        RESOURCE_CHAIN_CASE,
    ),
    "MP-C13-RESOURCE-UNLOAD-SINGLE-NODE": reused(
        RESOURCE_CHAIN_CASE,
        "ResourceManager caches raw holders and returns fresh dispatches",
    ),
    "MP-C13-RESOURCE-UNLOADALL-MAP-CLEAR": reused(
        RESOURCE_CHAIN_CASE,
        "ResourceManager caches raw holders and returns fresh dispatches",
    ),
    "MP-A11-PLAYER-DIRECT-14-20": reused(
        "Player completion, mask, and preview properties preserve native scalar boundaries",
        "Player root modified query reads the root delta dirty byte",
    ),
    "MP-A11-PLAYER-DIRECT-STATE": reused(
        "Player completion, mask, and preview properties preserve native scalar boundaries",
        "Player outsideFactor and speed preserve raw typed-property doubles",
    ),
    "MP-D11-PLAYER-PROCESSED-MESH": reused(
        "updateLayers consumes parameter mode for one update only",
        "updateLayers publishes all-node position deltas before phase 3",
    ),
    "MP-A11-PLAYER-ROOT-FLAGS": reused(
        "Player completion, mask, and preview properties preserve native scalar boundaries",
        "Player independentLayerInherit public setter dirties without committing",
    ),
    "MP-D10-LAYERGETTER-ONE": reused(LAYER_GETTER_CASE),
    "MP-D10-LAYERGETTER-LIST": reused(LAYER_GETTER_CASE),
    "MP-L11-PLAYER-CTOR": reused(PLAYER_CTOR_CASE),
    "MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER": reused(
        "Player resourceManager keeps three owners and a read-only canonical alias",
        "MotionNode value construction preserves native zero defaults",
    ),
    "MP-A30-D3DEMOTEPLAYER-SURFACE-FACTORY-CLONE-TODO": reused(
        "D3DEmotePlayer methods keep four-reference TODO boundaries",
        "D3DEmotePlayer typed factory and clone preserve D3DLayer boundary",
        "D3DEmotePlayer typed members preserve generated receiver and arity gates",
    ),
    "MP-A32-REGISTRATION-STRINGS-ARGUMENTS-BINDINGS": reused(
        ROOT_SURFACE_CASE,
        "DrawDeviceD3D exposes the seven-class reference surface",
        "Motion.EmotePlayer typed Factory requires arg0 and preserves the Void sentinel",
        "D3DEmoteModule zero-argument constructor accepts surplus and preserves the exact Void sentinel",
    ),
    "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME": reused(
        "node suffix erase retires source and render-item owners before borrowed facades",
        PREPARED_CASE,
    ),
    "MP-L05-EMOTEOBJECT-ENGINE-PLAYER-OWNER-CHAIN": reused(
        "Motion.EmotePlayer typed Factory requires arg0 and preserves the Void sentinel",
        "Player resourceManager keeps three owners and a read-only canonical alias",
    ),
    "MP-L06-EMOTEPLAYER-FACADE-VS-D3D-SHELL-TOPOLOGY": reused(
        "Motion.EmotePlayer typed Factory requires arg0 and preserves the Void sentinel",
        "D3DEmotePlayer typed factory and clone preserve D3DLayer boundary",
    ),
    "MP-L07-EMOTEENGINE-SEVEN-DIRECT-CONTROLLER-OWNERS": reused(
        "direct Emote controllers preserve native queue boundaries",
        "Motion EmotePlayer setters enqueue Engine controllers",
    ),
    "MP-C10-MOTIONNODE-ORDER": reused(
        "MotionNode value construction preserves native zero defaults",
        "motion node keeps native shallow copy assignment for deque erase",
    ),
    "MP-L15-GLOBAL-STATIC-CACHE-RNG-GUARD-LIFECYCLE": reused(
        "blink RNG preserves the four-reference MT twist and canonical bits",
        "Plugins link is a direct module-key load and unlink remains a true no-op",
        "autoload passes its complete Path and discovered Name as the module key",
    ),
    "MP-L16-ADDREF-RELEASE-NATIVE-INSTANCE-BORROWED-DELETING-DTOR": reused(
        "setEmotePSBDecryptFunc owns and invokes the four-reference closure shape",
        "accurate SLA phase owner drops temporary closure before raw Object owner",
        "D3DLayer constructor re-entry keeps the oldest borrowed four-slot view",
    ),
    "MP-C10-PLAYER-NODE-DEQUE": reused(
        "MotionNode value construction preserves native zero defaults",
        "node suffix erase retires source and render-item owners before borrowed facades",
    ),
    "MP-G11-PLAYER-APPEND-PREPARED-ITEMS": reused(
        PREPARED_CASE,
        "prepared render builder retains one particle Array across recursion",
    ),
    "MP-C13-RESOURCE-LAYER-ID-SET": reused(
        "ResourceManager layer ids preserve unsigned suffix-release boundaries",
        "Player layer-id dispatch retains ResourceManager across callbacks",
    ),
    "MP-C10-TJS-ARRAY-ITEMS": reused(
        "getCommandList preserves fresh empty arrays and zero-argument NCB boundaries",
        "Player variableKeys returns a fresh var-track array",
    ),
    "MP-L10-LAYERGETTER-ARRAY-EH": reused(
        LAYER_GETTER_CASE,
        rationale="The existing LayerGetter fixture covers Array owner production; allocator-failure execution is not available",
    ),
    "MP-C15-ALL-CONTAINER-BOUNDARY-AUDIT": reused(
        "prepared render wrapper preserves caller vectors and sorts trusted pointers",
        "selector deque raw entry leaves its gate byte untouched",
        "SourceCache loadSource uses composite identity and native pre-insert trimming",
    ),
    "MP-C19-PLAYER-FIND-SOURCE-FOR-NODE": reused(
        "Player findSource dispatch preserves context Variant and receiver",
        "node source fallback gates on exact TJS status and Void result",
        "node source fallback retains one owner across reentrant getters",
    ),
    "MP-G11-PLAYER-PREPARE-SORT-WRAPPER": reused(
        "prepared render wrapper preserves caller vectors and sorts trusted pointers"
    ),
    "MP-L11-SHARED-LAYER-FACTORY": reused(
        "SourceCache bufLayer is one persistent read-only closure across both class tables",
        "internal workspace dimensions reuse an ncb accessor for hinted double reads",
    ),
    "MP-G11-PLAYER-ORDINARY-POST-DRAW": reused(
        "Player Canvas item tail releases nested owners before frame and outer owners after frame"
    ),
}


SPECIAL_SLICE_DECISIONS: dict[str, Decision] = {
    "MP-L04-PLAYER-NONPOLYMORPHIC-PAYLOAD-ADAPTOR-VTABLE": Decision(
        "STATIC_ONLY_NO_RUNTIME_OBSERVATION",
        (),
        "The source-level non-polymorphic payload and deleting-thunk disposition are compile/link products, not a safe runtime fixture surface",
        "Retain the four-target vtable/type ledger; no fixture material should be fabricated",
    ),
    "MP-C16-STL-CONTAINER-SOURCE-ATTRIBUTION": Decision(
        "STATIC_ONLY_NO_RUNTIME_OBSERVATION",
        (),
        "The ticket compares foreign libstdc++ and libc++ lowering; one local test process cannot instantiate those four shipped ABIs",
        "Static four-target disassembly is the available verification material",
    ),
    "MP-G23-WEB-COCOS-REFERENCE-RENDER-PLATFORM-BOUNDARIES": Decision(
        "LOCAL_PLATFORM_BOUNDARY_REGRESSION",
        (
            "software texture updates packed atlas sub-rects in place",
            "KRKR D3D source path builds and uploads the production atlas",
            "triangle batch preserves the native asymmetric cache key",
        ),
        "Existing local fixtures guard the explicit Web adaptation; they cannot turn the platform boundary into a native-equivalence oracle",
        "Native/reference render comparison remains MP-V05 and the boundary stays explicit",
    ),
    "MP-G24-ONE-FRAME-INPUT-STATE-TO-FINAL-DRAW-PRODUCTS": Decision(
        "REQUIRES_TRACE_DIFFERENTIAL",
        (),
        "A complete frame product requires coordinated native/ADB/Wasmtime trace streams, not a synthetic unit fixture",
        "Execute and reconcile the existing trace tooling under MP-V04 and MP-V05",
    ),
}


TASK_OVERRIDES: dict[tuple[str, str], Decision] = {
    (
        "MP-L03",
        "MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER",
    ): Decision(
        "NO_EXISTING_FAILURE_INJECTION_FIXTURE",
        (),
        "Per-publication Player constructor rollback needs deterministic allocator/Dictionary/root-deque failure injection that the current fixture does not provide",
        "Record the verification gap; do not construct new fixture material",
    ),
    (
        "MP-L03",
        "MP-L11-PLAYER-CTOR",
    ): Decision(
        "NO_EXISTING_FAILURE_INJECTION_FIXTURE",
        (),
        "NCB attachment and constructor-prefix rollback need deterministic failure injection absent from the existing fixture",
        "Record the verification gap; do not construct new fixture material",
    ),
}


def test_case_locations(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    lines = path.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if not TEST_CASE_START_RE.match(line):
            index += 1
            continue
        start = index
        declaration = line
        while ")" not in declaration and index + 1 < len(lines):
            index += 1
            declaration += lines[index]
        name = "".join(STRING_LITERAL_RE.findall(declaration))
        if not name:
            raise ValueError(f"unparsed TEST_CASE at {path}:{start + 1}")
        if name in result:
            raise ValueError(f"duplicate TEST_CASE name: {name}")
        result[name] = start + 1
        index += 1
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("analysis/motionplayer_vertical_test_matrix.tsv"),
    )
    parser.add_argument(
        "--test-file",
        type=Path,
        default=Path("tests/unit-tests/plugins/motionplayer-dll.cpp"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/motionplayer_fixture_decisions.tsv"),
    )
    args = parser.parse_args()

    with args.input.open(encoding="utf-8", newline="") as handle:
        rows = [
            row
            for row in csv.DictReader(handle, delimiter="\t")
            if row["mapping_kind"] == "NO_EXISTING_TEST"
        ]
    if len(rows) != 102:
        raise ValueError(f"expected 102 MP-V01 no-test rows, found {len(rows)}")

    locations = test_case_locations(args.test_file)
    output_rows: list[tuple[str, ...]] = []
    used_slices: set[str] = set()
    for row in rows:
        key = (row["task_id"], row["slice_id"])
        decision = TASK_OVERRIDES.get(key)
        if decision is None:
            decision = SPECIAL_SLICE_DECISIONS.get(row["slice_id"])
        if decision is None:
            decision = SLICE_DECISIONS.get(row["slice_id"])
        if decision is None:
            raise ValueError(f"no MP-V02 decision for {key}")
        used_slices.add(row["slice_id"])

        case_locations: list[str] = []
        for case in decision.cases:
            line_number = locations.get(case)
            if line_number is None:
                raise ValueError(
                    f"mapped TEST_CASE does not exist: {case!r} for {key}"
                )
            case_locations.append(f"{args.test_file}:{line_number}")

        output_rows.append(
            (
                row["task_id"],
                row["description"],
                row["slice_id"],
                decision.disposition,
                "; ".join(decision.cases),
                "; ".join(case_locations),
                decision.rationale,
                decision.remaining_gap,
            )
        )

    expected_slices = {
        row["slice_id"] for row in rows
    }
    if used_slices != expected_slices:
        raise ValueError("MP-V02 slice denominator mismatch")
    if len(expected_slices) != 55:
        raise ValueError(
            f"expected 55 unique no-test slices, found {len(expected_slices)}"
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "task_id",
                "description",
                "slice_id",
                "fixture_disposition",
                "test_cases",
                "test_locations",
                "rationale",
                "remaining_gap",
            )
        )
        writer.writerows(output_rows)

    print(
        f"wrote {len(output_rows)} fixture decisions for "
        f"{len(expected_slices)} unique slices to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
