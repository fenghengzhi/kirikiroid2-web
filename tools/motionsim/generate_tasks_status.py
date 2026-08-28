#!/usr/bin/env python3
"""Generate the authoritative per-ticket status ledger for tasks.md.

The historical coverage TSV uses later semantic-slice IDs, so its aggregate
status must not be projected onto the original MP-F/A/L/C/D/R/G/B/V tickets.
Every original ticket starts open unless this file contains an explicit,
auditable mapping to current evidence.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


TASK_RE = re.compile(r"^\|\s*(MP-[A-Z]\d+(?:[a-z])?)\s*\|\s*(.*?)\s*\|\s*$")
EXAMPLE_RE = re.compile(r"^-\s+`(MP-R03[a-e])`：\s*(.*?)\s*$")
QUOTED_EXAMPLE_RE = re.compile(r"^>\s+`(MP-R03a)\s+(.*?)`\s*$")


@dataclass(frozen=True)
class Evidence:
    status: str
    coverage_slices: str
    reports: str
    verification: str
    remaining_gap: str


# Only task IDs with a direct requirement-by-requirement audit belong here.
# Similar wording or an aggregate final report is deliberately insufficient.
EVIDENCE: dict[str, Evidence] = {
    "MP-F01": Evidence(
        "CLOSED_STATIC",
        "MP-F01-ROOT-01; MP-F01-ROOT-02; MP-F01-ROOT-03",
        "analysis/motionplayer_reconstruction_scope_and_roots_four_binary_2026-08-26.md; analysis/motionplayer_emoteplayer_module_decrypt_root_four_binary_2026-08-27.md; analysis/motionplayer_drawdevice_d3d_dependency_root_four_binary_2026-08-27.md",
        "Four roots and exclusion rules have four-target evidence; IDBs saved",
        "No static scope/root gap; formal build remains separate MP-V work",
    ),
    "MP-F02": Evidence(
        "CLOSED_STATIC",
        "MP-F01-ROOT-01; MP-F01-ROOT-02; MP-F01-ROOT-03",
        "analysis/motionplayer_reconstruction_scope_and_roots_four_binary_2026-08-26.md",
        "Four binaries and four matching IDBs were identity-checked",
        "No target/IDB identity gap",
    ),
    "MP-F03": Evidence(
        "CLOSED_STATIC",
        "MP-F03-NCB-SURFACE-LEDGER; MP-F03-ROOT-REACHABLE-FUNCTION-LEDGER",
        "analysis/motionplayer_ncb_equivalence_ledger_2026-08-27.md; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_v15_audit_summary.tsv",
        "The 316-row NCB denominator is reproducible and all 139 original MP-A/L/C/D/R/G/B tasks map through 306 associations to 159 unique terminal four-target slices",
        "No static function-denominator gap in the tasks.md scope; MP-V04 is verified and the two terminal external-evidence blockers are tracked by MP-V03 and MP-V05",
    ),
    "MP-F04": Evidence(
        "CLOSED_STATIC",
        "MP-F04-OBJECT-VTABLE-CONTAINER-OWNER-LEDGER",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_v15_audit_summary.tsv; analysis/motionplayer_tasks_status.tsv",
        "All 16 MP-L and 16 MP-C original tasks have one-to-one task mappings covering 50 unique terminal four-target object/owner/container slices",
        "No static object-ledger gap; unavailable destruction/reentry fault-injection material remains explicitly recorded by MP-V02",
    ),
    "MP-F05": Evidence(
        "CLOSED_STATIC",
        "MP-F05-REPORT-STATUS-RECONCILIATION",
        "analysis/motionplayer_coverage_status_reconciliation_2026-08-27.md; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_tasks_status.tsv",
        "All 163 original tickets are indexed; every static business task has direct coverage/report mappings and nonterminal verification tasks retain explicit gaps",
        "No report-index gap; the old overbroad final-closure claim remains superseded by the current per-ticket ledger",
    ),
    "MP-F06": Evidence(
        "CLOSED_STATIC",
        "MP-F06-LOCAL-IMPLEMENTATION-TEST-MAP",
        "analysis/motionplayer_tasks_status.tsv; analysis/motionplayer_vertical_test_matrix.tsv; analysis/motionplayer_fixture_decisions.tsv; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md",
        "Every original static ticket maps to coverage implementation/report fields; MP-V01 and MP-V02 provide the test/no-material decision denominator",
        "No mapping gap; MP-V04 is verified and the two terminal external-evidence blockers are tracked independently by MP-V03 and MP-V05",
    ),
    "MP-F07": Evidence(
        "CLOSED_STATIC",
        "MP-F07-FINAL-GAP-DENOMINATOR",
        "analysis/motionplayer_v15_final_differences.tsv; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_tasks_status.tsv",
        "The final difference set is reproducibly separated into three accepted platform boundaries, one ABI/compiler class, and two external differential-verification gaps; no shared-source semantic or IDA-transport blocker remains",
        "Gap audit is complete; two external verification rows remain explicitly terminal EVIDENCE_BLOCKED under MP-V03 and MP-V05",
    ),
    "MP-F08": Evidence(
        "VERIFIED",
        "MP-F08-FINAL-ACCEPTANCE",
        "plan.md; analysis/motionplayer_coverage.tsv; analysis/motionplayer_tasks_status.tsv; analysis/motionplayer_v15_final_differences.tsv; analysis/motionplayer_final_reconstruction_report_2026-08-28.md",
        "Status vocabulary and the complete 163-ticket denominator are defined; all rows now have terminal dispositions: 146 CLOSED_STATIC, 15 VERIFIED, and two explicit EVIDENCE_BLOCKED external-verification rows",
        "No acceptance-ledger gap; MP-V03 and MP-V05 remain terminal evidence blockers and are not represented as successful Android executions",
    ),
    "MP-V06": Evidence(
        "VERIFIED",
        "MP-V06-WEB-DEBUG-FORMAL-BUILD",
        "analysis/motionplayer_web_debug_build_2026-08-28.md; analysis/motionplayer_v06_web_build_products.tsv",
        "CMake Web Debug Config completed a full current-worktree compile and final link; all five fixed products exist and have freshly recorded sizes and SHA-256 digests",
        "No MP-V06 build gap",
    ),
    "MP-V07": Evidence(
        "VERIFIED",
        "MP-V07-NATIVE-FULL; MP-V07-TTSTR-HASH",
        "analysis/motionplayer_v07_v08_native_runtime_diagnostics_2026-08-28.md; analysis/motionplayer_v07_v08_native_runtime_status.tsv; analysis/motionplayer_runtime_reconciliation_four_binary_2026-08-28.md; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md",
        "Fresh final macOS native runs: declared order has 357 test cases, 356 passed and one expected headless-OpenGL skip with all 23259 assertions passed; the previously failing random seed 2862347432 also passes all 23260 assertions. The fully rebuilt ttstr hash suite passes 23 cases and 150 assertions",
        "No MP-V07 runtime gap; the one skip requires the application's live OpenGL context and is an explicit platform integration boundary",
    ),
    "MP-V08": Evidence(
        "VERIFIED",
        "MP-F08-FINAL-ACCEPTANCE",
        "analysis/motionplayer_v07_v08_native_runtime_diagnostics_2026-08-28.md; analysis/motionplayer_v07_v08_native_runtime_status.tsv; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md",
        "Post-fix git diff --check passes; recovery-only diagnostic scan has zero PRTDIAG/stdout/stderr/platform-debug calls; six TVPAddLog sites are reference or unchanged baseline error paths; final native and hash suites pass",
        "No MP-V08 diagnostic or patch-integrity gap",
    ),
    "MP-V09": Evidence(
        "VERIFIED",
        "MP-V09-GUESS-SOURCE-NAME-AUDIT",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_guess_audit.tsv",
        "Reproducible source scan dispositions all 943 unique _guess identifiers and 3816 occurrences; none has trustworthy four-target source-identifier evidence authorizing a rename",
        "No MP-V09 gap; retained _guess labels remain explicitly non-authoritative",
    ),
    "MP-V10": Evidence(
        "VERIFIED",
        "MP-V10-LEGACY-TOKEN-COMMENT-AUDIT",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v10_legacy_token_audit.tsv",
        "Like_0x, IDA auto-symbol and @0x code-address patterns have zero matches; all 52 hex-comment tokens and nine staleness-marker comments have current dispositions",
        "No MP-V10 source-token or stale-comment gap",
    ),
    "MP-V11": Evidence(
        "VERIFIED",
        "MP-V11-FOUR-TARGET-INLINE-DEAD-STRIP-DISPOSITION-AUDIT",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_v15_audit_summary.tsv; analysis/motionplayer_android_ios_arm64_armv7_difference_classification_four_binary_2026-08-28.md",
        "All 199 coverage rows have four nonempty target dispositions; inline/dead-strip/absent/folding conclusions remain cross-target dispositions rather than one-search conclusions",
        "No MP-V11 static gap; the runtime reconciliation received fresh four-target evidence and is complete",
    ),
    "MP-V12": Evidence(
        "VERIFIED",
        "MP-F03-ROOT-REACHABLE-FUNCTION-LEDGER",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_v15_audit_summary.tsv; analysis/motionplayer_tasks_status.tsv",
        "All 139 original MP-A/L/C/D/R/G/B tasks map through 306 associations to 159 unique IMPLEMENTED or PLATFORM_BOUNDARY slices with four target dispositions",
        "No static tasks.md function-denominator gap; MP-V04 is verified and MP-V03/MP-V05 have explicit terminal EVIDENCE_BLOCKED dispositions",
    ),
    "MP-V13": Evidence(
        "VERIFIED",
        "MP-F04-OBJECT-VTABLE-CONTAINER-OWNER-LEDGER",
        "analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md; analysis/motionplayer_v09_v15_audit_summary.tsv; analysis/motionplayer_tasks_status.tsv",
        "All 32 MP-L/MP-C lifecycle and container tasks map to 50 unique terminal four-target slices with reports, implementation, verification, and gap fields",
        "No MP-V13 static gap; unavailable fault-injection fixtures remain explicit MP-V02 verification gaps",
    ),
    "MP-V14": Evidence(
        "VERIFIED",
        "MP-A32-REGISTRATION-STRINGS-ARGUMENTS-BINDINGS",
        "analysis/motionplayer_registration_strings_arguments_bindings_final_reconciliation_2026-08-27.md; analysis/motionplayer_registration_contracts.tsv; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md",
        "All 494 unique registration contracts are EVIDENCED_4_4; the three exposed boundary mismatches received fresh four-target IDA evidence, were reconciled, and the final 357-case native suite passes with one expected integration skip and zero failed assertions",
        "No MP-V14 script-surface contract gap",
    ),
    "MP-V15": Evidence(
        "VERIFIED",
        "MP-F07-FINAL-GAP-DENOMINATOR",
        "analysis/motionplayer_v15_final_differences.tsv; analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md",
        "The final list contains three accepted platform boundaries, one explained ABI/STL/compiler class, and two explicit external verification gaps; shared-source semantic gaps and the IDA transport blocker are closed",
        "No MP-V15 audit gap; VERIFY-001 and VERIFY-002 are terminal evidence blockers with external follow-up commands under MP-V03 and MP-V05",
    ),
    "MP-V16": Evidence(
        "VERIFIED",
        "MP-V16-FINAL-RECONSTRUCTION-REPORT",
        "analysis/motionplayer_final_reconstruction_report_2026-08-28.md; analysis/motionplayer_tasks_status.tsv; analysis/motionplayer_v15_final_differences.tsv",
        "Published the final scoped reconstruction report with four-target evidence, denominator counts, implementation deltas, fresh build/test results, remaining external differentials, and reproducible commands",
        "No MP-V16 publication gap; the report records all 163 terminal dispositions and deliberately does not misstate MP-V03/MP-V05 as Android PASS",
    ),
    "MP-B05": Evidence(
        "CLOSED_STATIC",
        "MP-B05-STRING-NULL-ALLOCATED-EMPTY-CASE-UTF16-NUL-HASH",
        "analysis/motionplayer_string_null_allocated_empty_case_utf16_nul_hash_four_binary_2026-08-28.md",
        "Mapped to a direct four-target IMPLEMENTED slice; added an embedded-NUL regression without asserting indeterminate lowercase tail bytes",
        "No task-local static gap; runtime verification is tracked independently under MP-V",
    ),
    "MP-B11": Evidence(
        "CLOSED_STATIC",
        "MP-B11-ANDROID-IOS-ARM64-ARMV7-DIFFERENCE-CLASSIFICATION",
        "analysis/motionplayer_android_ios_arm64_armv7_difference_classification_four_binary_2026-08-28.md",
        "Mapped to direct four-target IMPLEMENTED and explicit PLATFORM_BOUNDARY slices; no task-local UNKNOWN difference remains",
        "No task-local unexplained difference; cross-category final audit reached its terminal disposition under MP-V15/MP-V16",
    ),
    "MP-B12": Evidence(
        "CLOSED_STATIC",
        "MP-B12-DEAD-VALUE-NOOP-REFCOUNT-UNINITIALIZED-INACTIVE-TAIL",
        "analysis/motionplayer_dead_value_noop_refcount_uninitialized_inactive_tail_four_binary_2026-08-28.md",
        "Fresh four-target audit of 72 ranges and 23727 complete instructions; production matched; undefined residue is intentionally not frozen into a test oracle; 72 IDB anchors, four bookmarks, and four saved IDBs",
        "No task-local static gap; runtime/final acceptance reached terminal dispositions under MP-V03..MP-V16",
    ),
    "MP-V01": Evidence(
        "VERIFIED",
        "MP-V01-VERTICAL-SLICE-EXISTING-UNIT-TEST-MAP",
        "analysis/motionplayer_vertical_slice_existing_unit_test_map_2026-08-28.md; analysis/motionplayer_vertical_test_matrix.tsv",
        "Reproducible generator mapped all 127 closed vertical tasks and 294 task-slice associations to 190 runtime-test rows, two compile-time-contract rows, or 102 explicit no-existing-test rows",
        "No MP-V01 mapping gap; explicit no-test rows are the MP-V02 denominator",
    ),
    "MP-V02": Evidence(
        "VERIFIED",
        "MP-V02-EXISTING-FIXTURE-SUPPLEMENT-DECISIONS",
        "analysis/motionplayer_existing_fixture_supplement_decisions_2026-08-28.md; analysis/motionplayer_fixture_decisions.tsv",
        "All 102 MP-V01 no-test associations and 55 unique slices have an existing-material decision: 91 reused, five mapped to one added geometry test, two failure-injection gaps, two static-only observations, one local platform regression, and one trace differential",
        "No MP-V02 classification gap; MP-V04 is verified and the unavailable external Android executions are terminal evidence blockers under MP-V03 and MP-V05",
    ),
    "MP-V03": Evidence(
        "EVIDENCE_BLOCKED",
        "MP-V03-SCALAR-WASMTIME-CLI-21-OF-21; MP-V03-ADB-EVIDENCE-BLOCKED",
        "analysis/motionplayer_geometry_bezier_position_hit_test_wasmtime_adb_differential_2026-08-28.md; analysis/motionplayer_v03_differential_results.tsv",
        "Fresh current-source Emscripten 6.0.8 builds executed through independent Wasmtime CLI 31.0.0: geometry/hit-test 10/10, Bezier 6/6, and position 5/5 exactly matched the existing expectations; a 2026-08-29 audit again found no local adb/emulator/Docker, APK, or prebuilt harness and no CI scalar-ADB lane",
        "Terminal EVIDENCE_BLOCKED disposition: current ADB return values are unavailable until an arm64 device/emulator plus the existing harness APK is supplied; no historical result is promoted to PASS",
    ),
    "MP-V04": Evidence(
        "VERIFIED",
        "MP-V04-ADB-ORACLE-88-FRAMES; MP-V04-WASMTIME-88-FRAMES; MP-V04-NATIVE-88-FRAMES",
        "analysis/motionplayer_motion_playback_native_adb_wasmtime_trace_differential_2026-08-28.md; analysis/motionplayer_v04_trace_artifacts.tsv",
        "The hash-pinned 15 Hz fixtures now pass all three lanes: reference Android ADB oracle, current-worktree Wasmtime, and freshly rebuilt macOS native LLDB; m2logo 25/25 and yuzulogo 63/63 pass independent normalized comparators in both port lanes",
        "No MP-V04 playback-trace gap; 88 frames and 2418 flattened layer snapshots were checked per current lane",
    ),
    "MP-V05": Evidence(
        "EVIDENCE_BLOCKED",
        "MP-V05-WASMTIME-RENDER-STAGE-15HZ; MP-V05-GITHUB-RUN540-ANDROID-RENDER-15HZ; MP-V05-CURRENT-DRAW-DISPATCH-EVIDENCE-BLOCKED",
        "analysis/motionplayer_render_stage_draw_dispatch_render_step_trace_differential_2026-08-28.md; analysis/motionplayer_v05_render_trace_status.tsv",
        "Current-worktree Wasmtime 15 Hz render capture is complete: 88 trace frames, 1472 stage events, and 176 decoded 1920x1080 PNGs pass manifest/schema/frame/hash audits. GitHub run 540 used the same two hash-pinned 15 Hz fixtures, uploaded Android and Wasmtime render-stage artifacts, and its render-step compare job passed",
        "Terminal EVIDENCE_BLOCKED disposition: run 540 is a514c688 evidence and did not invoke compare_motion_draw_dispatch.py; latest baseline run 550 has no render artifact, while the current worktree has no authenticated local copy of the Android artifact or current paired draw-dispatch result. Download and schema-check run 540 artifacts, then compare them to the current capture or rerun the current pipeline; do not call the current draw path PASS yet",
    ),
}


# Direct mappings for original tasks whose entire static requirement is
# covered by one or more existing four-target semantic slices. Cross-cutting
# "all objects/all containers/final verification" tickets are intentionally
# absent until their complete denominator has been audited.
STATIC_TASK_SLICES: dict[str, tuple[str, ...]] = {
    "MP-A01": ("MP-F01-ROOT-01", "MP-F03-NCB-SURFACE-LEDGER"),
    "MP-A02": ("MP-F01-ROOT-02", "MP-A16-EMOTEPLAYER-NCB-SURFACE"),
    "MP-A03": ("MP-A03-MODULE-LOADER-PLUGINS-AUTOLOAD-REGISTERED-SET",),
    "MP-A04": ("MP-F03-NCB-SURFACE-LEDGER", "MP-A14-REG-01"),
    "MP-A05": ("MP-F03-NCB-SURFACE-LEDGER", "MP-A11-PLAYER-REG", "MP-A16-EMOTEPLAYER-NCB-SURFACE"),
    "MP-A06": ("MP-A15-BEZIER-LAYER-EXTENSIONS-NCB-SURFACE", "MP-G10-BEZIERPATCH-METHODS-INVERSE"),
    "MP-A07": ("MP-A15-BEZIER-LAYER-EXTENSIONS-NCB-SURFACE", "MP-L10-LAYER-EXTENSIONS-RENDER-LIFETIME"),
    "MP-A08": ("MP-A12-SOURCECACHE-OBJSOURCE-NCB-SURFACE", "MP-L12-SOURCECACHE-LOAD-CLEAR-BUFLAYER", "MP-L12-OBJSOURCE-GETTERS-DRAW-TEXTURE"),
    "MP-A09": ("MP-A09-REG-POINT", "MP-A09-REG-CIRCLE", "MP-A09-REG-RECT", "MP-A09-REG-QUAD", "MP-A09-GEOMETRY-SCALARS", "MP-B03-GEOMETRY-CONTAINS"),
    "MP-A10": ("MP-A10-LAYERGETTER-REG", "MP-A10-LAYERGETTER-SCALARS", "MP-A10-LAYERGETTER-ARRAYS", "MP-L10-LAYERGETTER-VTX", "MP-L10-LAYERGETTER-SHAPE", "MP-L10-LAYERGETTER-MOTION-PARTICLE"),
    "MP-A11": ("MP-L11-SLA-NCB-SURFACE", "MP-L11-SLA-TARGET-LAYER-PROPERTY", "MP-L11-SLA-CLEAR-DTOR"),
    "MP-A12": ("MP-A14-D3DADAPTOR-NCB-SURFACE", "MP-C14-D3DADAPTOR-SIMPLE-STATE-CLEAR", "MP-D14-D3DADAPTOR-CAPTURE-CANVAS"),
    "MP-A13": ("MP-A13-RESOURCEMANAGER-NCB-SURFACE", "MP-D13-RESOURCE-LOAD-CACHE-VALIDATE-DISPATCH", "MP-C13-RESOURCE-UNLOAD-SINGLE-NODE", "MP-C13-RESOURCE-UNLOADALL-MAP-CLEAR", "MP-L13-RESOURCE-FINDMOTION-DISPATCH-ARRAY", "MP-L13-RESOURCE-FINDSOURCE-BLANK-OBJSOURCE"),
    "MP-A14": ("MP-A14-REG-01", "MP-D10-MOTION-ALPHA-MASK-D3D-AVAILABLE"),
    "MP-A15": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-TIME", "MP-C11-PLAYER-VARIABLE-KEYS"),
    "MP-A16": ("MP-A11-PLAYER-REG", "MP-L11-PLAYER-CHARA-PROPS", "MP-D11-PLAYER-MOTION-PROP-ROUTE", "MP-L11-PLAYER-VARIANT-OWNERS", "MP-A11-PLAYER-DIRECT-14-20"),
    "MP-A17": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-DIRECT-STATE", "MP-A11-PLAYER-STATIC-DEFAULTS", "MP-A11-PLAYER-TRANSFORM-ORDER", "MP-D11-PLAYER-COLOR-INDEPENDENT-Z", "MP-D11-PLAYER-PROCESSED-MESH"),
    "MP-A18": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-ROOT-COORD-ANGLE", "MP-A11-PLAYER-ROOT-FLAGS", "MP-A11-PLAYER-STATIC-DEFAULTS", "MP-G11-PLAYER-DIRECT-D3D"),
    "MP-A19": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-SETVARIABLE-RAW", "MP-C11-PLAYER-GETVARIABLE", "MP-D11-PLAYER-PLAY-RAW", "MP-D11-PLAYER-PROGRESS-RAW", "MP-D11-PLAYER-CLEAR", "MP-A11-PLAYER-STOP-SYNC", "MP-G11-PLAYER-DRAW-ROUTER"),
    "MP-A20": ("MP-A11-PLAYER-REG", "MP-D11-PLAYER-CONTAINS", "MP-C11-PLAYER-CALC-VIEW", "MP-C11-PLAYER-COMMAND-LIST", "MP-D10-LAYERGETTER-ONE", "MP-D10-LAYERGETTER-LIST", "MP-L11-PLAYER-ISEXIST"),
    "MP-A21": ("MP-L11-PLAYER-CTOR", "MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER"),
    "MP-A22": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-DIRECT-14-20", "MP-A11-PLAYER-DIRECT-STATE", "MP-A11-PLAYER-ROOT-COORD-ANGLE", "MP-A11-PLAYER-ROOT-FLAGS", "MP-A11-PLAYER-CAMERA-OFFSETS"),
    "MP-A23": ("MP-A11-PLAYER-REG", "MP-A11-PLAYER-SETVARIABLE-RAW", "MP-D11-PLAYER-PLAY-RAW", "MP-D11-PLAYER-PROGRESS-RAW"),
    "MP-A24": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-PRIMARY-FLOW-RAW-SETTERS"),
    "MP-A25": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-PRIMARY-FLOW-RAW-SETTERS"),
    "MP-A26": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-PLAYER-FACADE-PROPERTIES-METHODS"),
    "MP-A27": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-PLAYER-FACADE-PROPERTIES-METHODS"),
    "MP-A28": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-SCALE-TRIGGER-VARIABLEKEYS-ANIMATING"),
    "MP-A29": ("MP-A16-EMOTEPLAYER-NCB-SURFACE", "MP-L14-EMOTEPLAYER-TIMELINE-SELECTOR-QUERIES"),
    "MP-A30": ("MP-A30-D3DEMOTEPLAYER-SURFACE-FACTORY-CLONE-TODO",),
    "MP-A31": ("MP-A31-DRAWDEVICED3D-SEVEN-CLASS-NCB-SURFACES",),
    "MP-A32": ("MP-A32-REGISTRATION-STRINGS-ARGUMENTS-BINDINGS",),
    "MP-L01": ("MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER",),
    "MP-L02": ("MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER", "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME"),
    "MP-L03": ("MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER", "MP-L11-PLAYER-CTOR"),
    "MP-L04": ("MP-L04-PLAYER-NONPOLYMORPHIC-PAYLOAD-ADAPTOR-VTABLE",),
    "MP-L05": ("MP-L05-EMOTEOBJECT-ENGINE-PLAYER-OWNER-CHAIN",),
    "MP-L06": ("MP-L06-EMOTEPLAYER-FACADE-VS-D3D-SHELL-TOPOLOGY",),
    "MP-L07": ("MP-L07-EMOTEENGINE-SEVEN-DIRECT-CONTROLLER-OWNERS",),
    "MP-L08": ("MP-L08-CONTROLLER-CONTAINER-ELEMENT-OWNER-PUBLICATION",),
    "MP-L09": ("MP-C10-MOTIONNODE-ORDER", "MP-C12-PLAYER-BUILD-NODE-TREE", "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME"),
    "MP-L10": ("MP-C12-PLAYER-BUILD-NODE-TREE", "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME"),
    "MP-L11": ("MP-A13-RESOURCEMANAGER-NCB-SURFACE", "MP-L12-SOURCECACHE-LOAD-CLEAR-BUFLAYER", "MP-L12-OBJSOURCE-GETTERS-DRAW-TEXTURE", "MP-L13-RESOURCE-FINDMOTION-DISPATCH-ARRAY", "MP-L13-RESOURCE-FINDSOURCE-BLANK-OBJSOURCE"),
    "MP-L12": ("MP-L11-SLA-ORDERED-MAP-LIFECYCLE", "MP-L11-SLA-ASSIGN-INNER", "MP-L11-SLA-CLEAR-DTOR", "MP-L11-SLA-TARGET-LAYER-PROPERTY"),
    "MP-L13": ("MP-L13-D3D-ADAPTOR-LISTENER-TEXTURE-MANAGED-LIFECYCLE",),
    "MP-L14": ("MP-L14-STATE-CLONE-SERIALIZE-RESTORE-PARTIAL-COMMIT",),
    "MP-L15": ("MP-L15-GLOBAL-STATIC-CACHE-RNG-GUARD-LIFECYCLE",),
    "MP-L16": ("MP-L16-ADDREF-RELEASE-NATIVE-INSTANCE-BORROWED-DELETING-DTOR",),
    "MP-C01": ("MP-C10-PLAYER-NODE-DEQUE", "MP-C10-MOTIONNODE-ORDER", "MP-C12-PLAYER-BUILD-NODE-TREE"),
    "MP-C02": ("MP-C11-PLAYER-PARAMETER-TABLE-PIPELINE", "MP-C13-PLAYER-INIT-VARIABLES", "MP-C31-PLAYER-VARIABLE-BINDER", "MP-L14-EMOTEPLAYER-TIMELINE-SELECTOR-QUERIES"),
    "MP-C03": ("MP-C03-TIMELINE-TRACK-CURSOR-PLAYLOG-STATE-MAP-CONTAINERS",),
    "MP-C04": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS",),
    "MP-C05": ("MP-C25-PLAYER-MOTION-SUB-PHASE", "MP-C26-PLAYER-PARTICLE-EMITTER-PHASE", "MP-C27-PLAYER-PARTICLE-SYSTEM-PHASE"),
    "MP-C06": ("MP-L07-EMOTEENGINE-SEVEN-DIRECT-CONTROLLER-OWNERS", "MP-L08-CONTROLLER-CONTAINER-ELEMENT-OWNER-PUBLICATION"),
    "MP-C07": ("MP-L08-CONTROLLER-CONTAINER-ELEMENT-OWNER-PUBLICATION",),
    "MP-C08": ("MP-C08-SELECTOR-TRANSITION-SPRING-BUST-HAIR-PARTS-WIND-CONTAINERS",),
    "MP-C09": ("MP-G11-PLAYER-APPEND-PREPARED-ITEMS", "MP-G11-PLAYER-BUILD-COMMANDS", "MP-R14-D3D-DEEP-BATCH-STENCIL"),
    "MP-C10": ("MP-C13-RESOURCE-LAYER-ID-SET", "MP-C13-RESOURCE-UNLOAD-SINGLE-NODE", "MP-C13-RESOURCE-UNLOADALL-MAP-CLEAR", "MP-D13-RESOURCE-LOAD-CACHE-VALIDATE-DISPATCH"),
    "MP-C11": ("MP-L12-SOURCECACHE-LOAD-CLEAR-BUFLAYER", "MP-C30-PLAYER-KRKR-ATLAS-IMAGEPACKER", "MP-R14-D3D-SOURCE-GETTER-MAP-INSERT"),
    "MP-C12": ("MP-C12-D3D-TEXTURE-BACKGROUND-CAPTION-LISTENER-MANAGER-CONTAINERS",),
    "MP-C13": ("MP-C10-TJS-ARRAY-ITEMS", "MP-A10-LAYERGETTER-ARRAYS", "MP-L10-LAYERGETTER-ARRAY-EH"),
    "MP-C14": ("MP-C14-TTSTR-HASH-EQUALITY-KEY-BOUNDARIES",),
    "MP-C15": ("MP-C15-ALL-CONTAINER-BOUNDARY-AUDIT",),
    "MP-C16": ("MP-C16-STL-CONTAINER-SOURCE-ATTRIBUTION",),
    "MP-D01": ("MP-D13-RESOURCE-LOAD-CACHE-VALIDATE-DISPATCH", "MP-C13-RESOURCE-UNLOAD-SINGLE-NODE", "MP-D13-RESOURCE-ISEXISTMOTION-DIRECT-SCAN"),
    "MP-D02": ("MP-D13-RESOURCE-LOAD-CACHE-VALIDATE-DISPATCH", "MP-C13-RESOURCE-UNLOAD-SINGLE-NODE", "MP-C13-RESOURCE-UNLOADALL-MAP-CLEAR"),
    "MP-D03": ("MP-D13-RESOURCE-ISEXISTMOTION-DIRECT-SCAN", "MP-L13-RESOURCE-FINDMOTION-DISPATCH-ARRAY"),
    "MP-D04": ("MP-L13-RESOURCE-FINDSOURCE-BLANK-OBJSOURCE", "MP-C19-PLAYER-FIND-SOURCE-FOR-NODE"),
    "MP-D05": ("MP-D05-PSB-MTN-DECRYPT-RAW-NODE-BUFFER-LIFETIME",),
    "MP-D06": ("MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2", "MP-C32-PLAYER-FRAME-PROGRESS-EVENTS"),
    "MP-D07": ("MP-C19-PLAYER-FIND-SOURCE-FOR-NODE", "MP-G11-PLAYER-APPEND-PREPARED-ITEMS"),
    "MP-D08": ("MP-C30-PLAYER-KRKR-ATLAS-IMAGEPACKER", "MP-L12-OBJSOURCE-GETTERS-DRAW-TEXTURE"),
    "MP-D09": ("MP-L12-SOURCECACHE-LOAD-CLEAR-BUFLAYER", "MP-R14-D3D-SOURCE-GETTER-MAP-INSERT"),
    "MP-D10": ("MP-C11-PLAYER-PARAMETER-TABLE-PIPELINE", "MP-C12-PLAYER-BUILD-NODE-TREE", "MP-C13-PLAYER-INIT-VARIABLES"),
    "MP-D11": ("MP-C12-PLAYER-BUILD-NODE-TREE", "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME"),
    "MP-D12": ("MP-D12-RELOAD-CLEAR-UNLOAD-LIVE-NODE-RENDER-SOURCE",),
    "MP-D13": ("MP-C13-RESOURCE-LAYER-ID-SET",),
    "MP-R01": ("MP-D11-PLAYER-MOTION-PROP-ROUTE", "MP-D11-PLAYER-PLAYBACK-STATE-MACHINE"),
    "MP-R02": ("MP-D11-PLAYER-PLAYBACK-STATE-MACHINE", "MP-D11-PLAYER-CLEAR", "MP-A11-PLAYER-STOP-SYNC"),
    "MP-R03": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-A11-PLAYER-TIME"),
    "MP-R04": ("MP-D11-PLAYER-PLAYBACK-STATE-MACHINE", "MP-C25-PLAYER-MOTION-SUB-PHASE"),
    "MP-R05": ("MP-C11-PLAYER-PARAMETER-TABLE-PIPELINE", "MP-C13-PLAYER-INIT-VARIABLES"),
    "MP-R06": ("MP-A11-PLAYER-SETVARIABLE-RAW", "MP-C11-PLAYER-GETVARIABLE", "MP-C11-PLAYER-VARIABLE-KEYS", "MP-L14-EMOTEPLAYER-TIMELINE-SELECTOR-QUERIES"),
    "MP-R07": ("MP-C31-PLAYER-VARIABLE-BINDER", "MP-C32-PLAYER-FRAME-PROGRESS-EVENTS"),
    "MP-R08": ("MP-D11-PLAYER-PLAYBACK-STATE-MACHINE", "MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-L14-EMOTEPLAYER-TIMELINE-SELECTOR-QUERIES"),
    "MP-R09": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-L14-EMOTEPLAYER-TIMELINE-SELECTOR-QUERIES"),
    "MP-R10": ("MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2", "MP-C32-PLAYER-FRAME-PROGRESS-EVENTS"),
    "MP-R11": ("MP-R11-ANGLE-VAR-LOOP-CONTROLLER-RUNTIME",),
    "MP-R12": ("MP-R12-BLINK-EYEBROW-MOUTH-TRACK-RNG-OVERSHOOT",),
    "MP-R13": ("MP-R13-SELECTOR-TRANSITION-BORROW-INDEX-PERSISTENCE",),
    "MP-R14": ("MP-R14-SPRING-SCALE-OUTER-FORCE-RUNTIME",),
    "MP-R15": ("MP-R15-WIND-MT19937-WIDTH-STOP-RUNTIME",),
    "MP-R16": ("MP-C25-PLAYER-MOTION-SUB-PHASE",),
    "MP-R17": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS",),
    "MP-R18": ("MP-C26-PLAYER-PARTICLE-EMITTER-PHASE", "MP-C27-PLAYER-PARTICLE-SYSTEM-PHASE"),
    "MP-R19": ("MP-C21-PLAYER-CAMERA-CONSTRAINT-PHASE", "MP-C23-PLAYER-CAMERA-NODE-PHASE", "MP-A11-PLAYER-CAMERA-OFFSETS"),
    "MP-R20": ("MP-A11-PLAYER-ROOT-COORD-ANGLE", "MP-A11-PLAYER-ROOT-FLAGS", "MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2"),
    "MP-R21": ("MP-L14-STATE-CLONE-SERIALIZE-RESTORE-PARTIAL-COMMIT",),
    "MP-R03a": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-A11-PLAYER-TIME"),
    "MP-R03b": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-C31-PLAYER-VARIABLE-BINDER"),
    "MP-R03c": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS",),
    "MP-R03d": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS", "MP-C15-PLAYER-UPDATE-LAYERS-DISPATCHER"),
    "MP-R03e": ("MP-C32-PLAYER-FRAME-PROGRESS-EVENTS",),
    "MP-G01": ("MP-C15-PLAYER-UPDATE-LAYERS-DISPATCHER",),
    "MP-G02": ("MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2",),
    "MP-G03": ("MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2", "MP-C25-PLAYER-MOTION-SUB-PHASE"),
    "MP-G04": ("MP-C21-PLAYER-CAMERA-CONSTRAINT-PHASE", "MP-C23-PLAYER-CAMERA-NODE-PHASE"),
    "MP-G05": ("MP-C29-PLAYER-UPDATE-LAYERS-PHASE1-PHASE2",),
    "MP-G06": ("MP-C28-PLAYER-ANCHOR-NODE-PHASE",),
    "MP-G07": ("MP-C26-PLAYER-PARTICLE-EMITTER-PHASE", "MP-C27-PLAYER-PARTICLE-SYSTEM-PHASE"),
    "MP-G08": ("MP-C20-PLAYER-VERTEX-MESH-PHASE",),
    "MP-G09": ("MP-R14-BEZIER-BASIS-TESSELLATION", "MP-G10-BEZIERPATCH-METHODS-INVERSE", "MP-C17-PLAYER-CALC-BOUNDS"),
    "MP-G10": ("MP-C16-PLAYER-SHAPE-AABB", "MP-C24-PLAYER-SHAPE-GEOMETRY-PHASE", "MP-B03-GEOMETRY-CONTAINS"),
    "MP-G11": ("MP-D10-MOTION-ALPHA-MASK-D3D-AVAILABLE", "MP-G11-PLAYER-BUILD-COMMANDS", "MP-R14-D3D-DEEP-BATCH-STENCIL", "MP-C22-MOTIONNODE-VISIBLE-ANCESTOR-POINTER"),
    "MP-G12": ("MP-C17-PLAYER-CALC-BOUNDS",),
    "MP-G13": ("MP-G11-PLAYER-APPEND-PREPARED-ITEMS", "MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME"),
    "MP-G14": ("MP-G11-PLAYER-PREPARE-SORT-WRAPPER", "MP-G11-PLAYER-BUILD-COMMANDS", "MP-G11-PLAYER-COMPOSED-GROUP-PRODUCER"),
    "MP-G15": ("MP-C19-PLAYER-FIND-SOURCE-FOR-NODE", "MP-C30-PLAYER-KRKR-ATLAS-IMAGEPACKER", "MP-R14-D3D-SOURCE-GETTER-MAP-INSERT"),
    "MP-G16": ("MP-L11-PLAYER-INTERNAL-LAYER-MATERIALIZE", "MP-L11-SHARED-LAYER-FACTORY"),
    "MP-G17": ("MP-G11-PLAYER-DIRECT-SLA", "MP-G11-PLAYER-ACCURATE-SLA", "MP-G11-PLAYER-ACCURATE-SLA-POST", "MP-L11-SLA-PRIVATE-GLL-ENSURE", "MP-C33-PRIVATE-GLL-CLASS-DRAW"),
    "MP-G18": ("MP-A14-D3DADAPTOR-NCB-SURFACE", "MP-D14-D3DADAPTOR-CAPTURE-CANVAS", "MP-C14-D3DADAPTOR-SIMPLE-STATE-CLEAR", "MP-L14-D3DADAPTOR-LIFECYCLE"),
    "MP-G19": ("MP-R14-D3D-DEEP-BATCH-STENCIL", "MP-R14-D3D-MESH-SUBMIT-CELLS", "MP-R14-BEZIER-BASIS-TESSELLATION", "MP-C33-PRIVATE-GLL-CLASS-DRAW"),
    "MP-G20": ("MP-D10-MOTION-ALPHA-MASK-D3D-AVAILABLE", "MP-R14-D3D-DEEP-BATCH-STENCIL", "MP-G11-PLAYER-COMPOSED-GROUP-PRODUCER"),
    "MP-G21": ("MP-G11-PLAYER-CANVAS-ENVELOPE", "MP-G11-PLAYER-CANVAS-ITEM-EXECUTOR", "MP-G11-PLAYER-ORDINARY-POST-DRAW", "MP-G11-PLAYER-ACCURATE-SLA-POST"),
    "MP-G22": ("MP-G22-FULL-COORDINATE-CHAIN-PSB-OWNER-PRIMARY-PAINTBOX-SCREEN",),
    "MP-G23": ("MP-G23-WEB-COCOS-REFERENCE-RENDER-PLATFORM-BOUNDARIES",),
    "MP-G24": ("MP-G24-ONE-FRAME-INPUT-STATE-TO-FINAL-DRAW-PRODUCTS",),
    "MP-B01": ("MP-B01-NULL-VOID-MISSING-STATUS-NONOBJECT-BOUNDARY",),
    "MP-B02": ("MP-B02-EMPTY-SINGLE-DUPLICATE-NEGATIVE-END-HUGE-COUNT",),
    "MP-B03": ("MP-B03-NAN-INF-NEGATIVEZERO-SUBNORMAL-DIVZERO-UNORDERED",),
    "MP-B04": ("MP-B04-FP-INTEGER-SATURATION-TRUNCATION-WRAP-ROUNDING",),
    "MP-B05": ("MP-B05-STRING-NULL-ALLOCATED-EMPTY-CASE-UTF16-NUL-HASH",),
    "MP-B06": ("MP-B06-EXCEPTION-SETTER-STORE-PUSH-PUBLICATION-PARTIAL-COMMIT",),
    "MP-B07": ("MP-B07-CALLBACK-REENTRY-OWNER-CLEAR-CONTAINER-GROWTH-OBJECT-REPLACEMENT",),
    "MP-B08": ("MP-B08-ALIAS-OUTPUTS-SAME-OBJECT-MULTIPLE-ARGS-BORROWED-INVALIDATION",),
    "MP-B09": ("MP-L15-GLOBAL-STATIC-CACHE-RNG-GUARD-LIFECYCLE",),
    "MP-B10": ("MP-L16-ADDREF-RELEASE-NATIVE-INSTANCE-BORROWED-DELETING-DTOR",),
    "MP-B11": ("MP-B11-ANDROID-IOS-ARM64-ARMV7-DIFFERENCE-CLASSIFICATION",),
    "MP-B12": ("MP-B12-DEAD-VALUE-NOOP-REFCOUNT-UNINITIALIZED-INACTIVE-TAIL",),
}


def parse_tasks(path: Path) -> list[tuple[str, str]]:
    tasks: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = (
            TASK_RE.match(line)
            or EXAMPLE_RE.match(line)
            or QUOTED_EXAMPLE_RE.match(line)
        )
        if not match:
            continue
        task_id, description = match.groups()
        if task_id in seen:
            continue
        seen.add(task_id)
        tasks.append((task_id, description.strip()))
    if len(tasks) != 163:
        raise ValueError(f"expected 163 unique tasks, found {len(tasks)}")
    return tasks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tasks", type=Path, default=Path("tasks.md"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/motionplayer_tasks_status.tsv"),
    )
    parser.add_argument(
        "--coverage",
        type=Path,
        default=Path("analysis/motionplayer_coverage.tsv"),
    )
    args = parser.parse_args()

    tasks = parse_tasks(args.tasks)
    with args.coverage.open(encoding="utf-8", newline="") as handle:
        coverage_rows = {
            row["slice_id"]: row
            for row in csv.DictReader(handle, delimiter="\t")
        }
    for task_id, slice_ids in STATIC_TASK_SLICES.items():
        if task_id not in {task_id for task_id, _ in tasks}:
            raise ValueError(f"static mapping references unknown task {task_id}")
        for slice_id in slice_ids:
            row = coverage_rows.get(slice_id)
            if row is None:
                raise ValueError(
                    f"{task_id} references missing coverage slice {slice_id}"
                )
            if row["evidence_status"] not in {
                "IMPLEMENTED",
                "PLATFORM_BOUNDARY",
            }:
                raise ValueError(
                    f"{task_id} references non-terminal slice {slice_id}: "
                    f"{row['evidence_status']}"
                )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "task_id",
                "description",
                "status",
                "coverage_slices",
                "analysis_reports",
                "verification",
                "remaining_gap",
            )
        )
        for task_id, description in tasks:
            evidence = EVIDENCE.get(task_id)
            if evidence is None and task_id in STATIC_TASK_SLICES:
                slice_ids = STATIC_TASK_SLICES[task_id]
                reports: list[str] = []
                for slice_id in slice_ids:
                    for report in coverage_rows[slice_id][
                        "analysis_report"
                    ].split(";"):
                        report = report.strip()
                        if report and report not in reports:
                            reports.append(report)
                evidence = Evidence(
                    "CLOSED_STATIC",
                    "; ".join(slice_ids),
                    "; ".join(reports),
                    "Mapped to direct four-target IMPLEMENTED slices; runtime verification is tracked independently under MP-V",
                    "No task-local static gap; cross-category final audit reached its terminal disposition under MP-V15/MP-V16",
                )
            if evidence is None:
                evidence = Evidence(
                    "OPEN_UNAUDITED",
                    "",
                    "",
                    "No direct requirement-by-requirement audit recorded",
                    "Map current evidence, then complete missing four-target evidence, implementation, and verification",
                )
            writer.writerow(
                (
                    task_id,
                    description,
                    evidence.status,
                    evidence.coverage_slices,
                    evidence.reports,
                    evidence.verification,
                    evidence.remaining_gap,
                )
            )

    print(f"wrote {len(tasks)} task rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
