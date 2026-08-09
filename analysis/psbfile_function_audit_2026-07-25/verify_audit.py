#!/usr/bin/env python3
"""Mechanical verifier for the 2026-07-25 psbfile function audit."""

from __future__ import annotations

import hashlib
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


BASE = Path(__file__).resolve().parent
REPO_ROOT = BASE.parents[1]
MANIFEST = BASE / "MANIFEST.md"
TASK_TREE = BASE / "TASK_TREE.md"
REPORT_DIR = BASE / "functions"
SOURCE_SNAPSHOT = BASE / "SOURCE_SNAPSHOT.sha256"
AUDITED_SOURCE_ROOT = REPO_ROOT / "cpp/plugins/psbfile"

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
}

# main.cpp is intentionally ambiguous repository-wide.  In this psbfile audit,
# an unqualified main.cpp marker always denotes the plugin translation unit.
EXPLICIT_BASENAME_ALIASES = {
    "main.cpp": "cpp/plugins/psbfile/main.cpp",
}

SOURCE_EXTENSION_PATTERN = r"(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inc|inl)"
FULL_SOURCE_PATH_PATTERN = (
    rf"cpp/(?:[A-Za-z0-9_+.-]+/)*[A-Za-z0-9_+.-]+\.{SOURCE_EXTENSION_PATTERN}"
)
BASENAME_PATTERN = (
    rf"(?<![/A-Za-z0-9_.+-])[A-Za-z0-9_][A-Za-z0-9_.+-]*\.{SOURCE_EXTENSION_PATTERN}"
)
SOURCE_TOKEN_PATTERN = rf"(?:{FULL_SOURCE_PATH_PATTERN}|{BASENAME_PATTERN})"
LINE_RANGE_LIST_PATTERN = (
    r"\d+(?:\s*[-\u2013\u2014]\s*\d+)?"
    r"(?:\s*[,，、]\s*\d+(?:\s*[-\u2013\u2014]\s*\d+)?)*"
)
DIRECT_SOURCE_REFERENCE_RE = re.compile(
    rf"(?P<path>{SOURCE_TOKEN_PATTERN})"
    rf"(?:\:(?!0[xX])(?P<colon>{LINE_RANGE_LIST_PATTERN})"
    rf"|\#L(?P<hash_start>\d+)(?:[-\u2013\u2014]L?(?P<hash_end>\d+))?)"
    rf"(?!\d|\.\d)"
)
PROSE_SOURCE_REFERENCE_RE = re.compile(
    rf"(?P<path>{SOURCE_TOKEN_PATTERN})(?![:#])"
    rf"(?:\]\([^\n)]+\))?`?\s*(?:[，,：:]\s*)?"
    rf"第\s*(?P<ranges>{LINE_RANGE_LIST_PATTERN})\s*行"
)


@dataclass(frozen=True)
class SourceReference:
    path: str
    start: int
    end: int
    marker: str


@dataclass(frozen=True)
class SemanticAnchor:
    label: str
    report_address: str
    source_path: str
    signature_pattern: str


SEMANTIC_ANCHORS = (
    SemanticAnchor(
        "PSBFileFactory",
        "0X5980F4",
        "cpp/plugins/psbfile/main.cpp",
        r"^[ \t]*static[ \t]+tjs_error[ \t]+PSBFileFactory[ \t]*\(",
    ),
    SemanticAnchor(
        "NCB_REGISTER_CLASS(PSBFile)",
        "0X597F38",
        "cpp/plugins/psbfile/main.cpp",
        r"^[ \t]*NCB_REGISTER_CLASS[ \t]*\([ \t]*PSBFile[ \t]*\)[ \t]*\{",
    ),
    SemanticAnchor(
        "PSBFile::GetRootDispatch",
        "0X5981F8",
        "cpp/plugins/psbfile/main.cpp",
        r"^[ \t]*iTJSDispatch2[ \t]*\*[ \t]*PSBFile::GetRootDispatch[ \t]*\(",
    ),
    SemanticAnchor(
        "PSBFile::Load",
        "0X598268",
        "cpp/plugins/psbfile/PSBRawFile.cpp",
        r"^[ \t]*bool[ \t]+PSBFile::Load[ \t]*\(",
    ),
    SemanticAnchor(
        "PSBFile::LoadStorage",
        "0X598538",
        "cpp/plugins/psbfile/PSBRawFile.cpp",
        r"^[ \t]*bool[ \t]+PSBFile::LoadStorage[ \t]*\(",
    ),
    SemanticAnchor(
        "PSBFile::Adopt",
        "0X598708",
        "cpp/plugins/psbfile/PSBRawFile.cpp",
        r"^[ \t]*bool[ \t]+PSBFile::Adopt[ \t]*\(",
    ),
    SemanticAnchor(
        "PSBFile::GetRoot",
        "0X598A3C",
        "cpp/plugins/psbfile/PSBRawFile.cpp",
        r"^[ \t]*PSBRawNode[ \t]+PSBFile::GetRoot[ \t]*\(",
    ),
)

ALLOWED_RELATIONS = {
    "[direct-call]",
    "[vdispatch]",
    "[registration]",
    "[lifecycle]",
    "[vtable-member]",
    "[helper]",
    "[stl-instantiation]",
    "[classification-only]",
}
DIMENSIONS = (
    "源代码结构",
    "数据流",
    "调用链",
    "对象生命周期",
    "内部容器实现",
    "边界行为",
)
ALLOWED_DIMENSION_STATES = {
    "MATCH",
    "GAP",
    "PARTIAL",
    "EVIDENCE_LIMITED",
    "N/A",
}
ALLOWED_VERDICTS = {"ALIGNED", "HAS_GAP", "EVIDENCE_LIMITED"}


def normalize_address(value: str) -> str:
    if value.lower().startswith("0x"):
        value = value[2:]
    elif value.startswith("@"):
        value = value[1:]
    return "0X" + value.upper()


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def verify_source_snapshot(errors: list[str]) -> int:
    """Reject an audit whose psbfile source set or bytes have drifted."""
    if not SOURCE_SNAPSHOT.is_file():
        fail(errors, f"missing source snapshot: {SOURCE_SNAPSHOT.name}")
        return 0

    expected: dict[str, str] = {}
    for line_number, raw_line in enumerate(
        SOURCE_SNAPSHOT.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (cpp/plugins/psbfile/[^\s]+)", line)
        if match is None:
            fail(
                errors,
                f"{SOURCE_SNAPSHOT.name}:{line_number}: invalid SHA-256 row",
            )
            continue
        digest, relative_path = match.groups()
        if relative_path in expected:
            fail(
                errors,
                f"{SOURCE_SNAPSHOT.name}:{line_number}: duplicate {relative_path}",
            )
            continue
        target = (REPO_ROOT / relative_path).resolve()
        try:
            target.relative_to(AUDITED_SOURCE_ROOT.resolve())
        except ValueError:
            fail(
                errors,
                f"{SOURCE_SNAPSHOT.name}:{line_number}: path escapes audited source root",
            )
            continue
        expected[relative_path] = digest

    actual_paths = {
        path.relative_to(REPO_ROOT).as_posix()
        for path in AUDITED_SOURCE_ROOT.rglob("*")
        if path.is_file()
    }
    expected_paths = set(expected)
    if actual_paths != expected_paths:
        fail(
            errors,
            "psbfile source snapshot set mismatch: "
            f"source-only={sorted(actual_paths - expected_paths)} "
            f"snapshot-only={sorted(expected_paths - actual_paths)}",
        )

    for relative_path in sorted(actual_paths & expected_paths):
        actual_digest = hashlib.sha256(
            (REPO_ROOT / relative_path).read_bytes()
        ).hexdigest()
        if actual_digest != expected[relative_path]:
            fail(
                errors,
                f"psbfile source drift: {relative_path}: "
                f"expected {expected[relative_path]}, got {actual_digest}",
            )
    return len(expected)


def build_source_aliases(errors: list[str]) -> dict[str, str]:
    """Resolve an unqualified source basename only when its target is unambiguous."""
    by_basename: dict[str, list[str]] = defaultdict(list)
    source_root = REPO_ROOT / "cpp"
    for path in source_root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            by_basename[path.name].append(path.relative_to(REPO_ROOT).as_posix())

    aliases = {
        basename: paths[0]
        for basename, paths in by_basename.items()
        if len(paths) == 1
    }
    for basename, relative_path in EXPLICIT_BASENAME_ALIASES.items():
        target = REPO_ROOT / relative_path
        if not target.is_file():
            fail(errors, f"configured source alias {basename} points to missing {relative_path}")
            continue
        if target.name != basename:
            fail(
                errors,
                f"configured source alias {basename} has mismatched target {relative_path}",
            )
            continue
        aliases[basename] = relative_path
    return aliases


def parse_line_ranges(value: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for item in re.split(r"\s*[,，、]\s*", value):
        match = re.fullmatch(r"(\d+)(?:\s*[-\u2013\u2014]\s*(\d+))?", item)
        if not match:
            raise ValueError(f"invalid line range {item!r}")
        start = int(match.group(1))
        end = int(match.group(2) or start)
        ranges.append((start, end))
    return ranges


def resolve_source_token(
    token: str,
    aliases: dict[str, str],
    errors: list[str],
    context: str,
) -> str | None:
    if token.startswith("cpp/"):
        relative_path = token
        if ".." in Path(relative_path).parts:
            fail(errors, f"{context}: source marker escapes cpp/: {token}")
            return None
    else:
        relative_path = aliases.get(token, "")
        if not relative_path:
            fail(errors, f"{context}: ambiguous or unknown source basename {token}")
            return None

    target = REPO_ROOT / relative_path
    if not target.is_file():
        fail(errors, f"{context}: source marker path does not exist: {relative_path}")
        return None
    return relative_path


def extract_source_references(
    text: str,
    aliases: dict[str, str],
    errors: list[str],
    context: str,
) -> list[SourceReference]:
    references: list[SourceReference] = []
    seen: set[tuple[str, int, int, int]] = set()

    direct_matches = list(DIRECT_SOURCE_REFERENCE_RE.finditer(text))
    candidates: list[tuple[re.Match[str], list[tuple[int, int]]]] = []
    for match in direct_matches:
        if match.group("colon") is not None:
            ranges = parse_line_ranges(match.group("colon"))
        else:
            start = int(match.group("hash_start"))
            ranges = [(start, int(match.group("hash_end") or start))]
        candidates.append((match, ranges))

    direct_spans = [match.span() for match in direct_matches]
    for match in PROSE_SOURCE_REFERENCE_RE.finditer(text):
        if any(match.start() < end and match.end() > start for start, end in direct_spans):
            continue
        candidates.append((match, parse_line_ranges(match.group("ranges"))))

    for match, ranges in candidates:
        relative_path = resolve_source_token(match.group("path"), aliases, errors, context)
        if relative_path is None:
            continue
        for start, end in ranges:
            key = (relative_path, start, end, match.start())
            if key in seen:
                continue
            seen.add(key)
            references.append(
                SourceReference(
                    path=relative_path,
                    start=start,
                    end=end,
                    marker=match.group(0),
                )
            )
    return references


def mask_cpp_non_code(text: str) -> str:
    """Blank comments and literals while preserving byte positions and newlines."""
    masked = list(text)

    def blank(start: int, end: int) -> None:
        for index in range(start, end):
            if text[index] != "\n":
                masked[index] = " "

    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end == -1 else end
            blank(index, end)
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) if end == -1 else end + 2
            blank(index, end)
            index = end
            continue
        if text.startswith('R"', index):
            opener = re.match(r'R"([^\s()\\]{0,16})\(', text[index:])
            if opener:
                terminator = ")" + opener.group(1) + '"'
                end = text.find(terminator, index + opener.end())
                end = len(text) if end == -1 else end + len(terminator)
                blank(index, end)
                index = end
                continue
        if text[index] in {'"', "'"}:
            quote = text[index]
            end = index + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                    continue
                end += 1
                if text[end - 1] == quote:
                    break
            blank(index, min(end, len(text)))
            index = end
            continue
        index += 1
    return "".join(masked)


def find_definition_range(
    anchor: SemanticAnchor,
    errors: list[str],
) -> tuple[int, int] | None:
    source_file = REPO_ROOT / anchor.source_path
    if not source_file.is_file():
        fail(errors, f"semantic anchor {anchor.label}: missing source {anchor.source_path}")
        return None

    source = source_file.read_text()
    masked = mask_cpp_non_code(source)
    matches = list(re.finditer(anchor.signature_pattern, masked, re.MULTILINE))
    if len(matches) != 1:
        fail(
            errors,
            f"semantic anchor {anchor.label}: found {len(matches)} source definitions, expected 1",
        )
        return None

    match = matches[0]
    opening_brace = masked.find("{", match.start(), match.end())
    if opening_brace == -1:
        opening_brace = masked.find("{", match.end())
        declaration_end = masked.find(";", match.end(), opening_brace)
        if opening_brace == -1 or declaration_end != -1:
            fail(errors, f"semantic anchor {anchor.label}: definition body is absent")
            return None

    depth = 0
    closing_brace = -1
    for index in range(opening_brace, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                closing_brace = index
                break
    if closing_brace == -1:
        fail(errors, f"semantic anchor {anchor.label}: definition body is unbalanced")
        return None

    start_line = source.count("\n", 0, match.start()) + 1
    end_line = source.count("\n", 0, closing_brace) + 1
    return start_line, end_line


def main() -> int:
    errors: list[str] = []
    source_snapshot_count = verify_source_snapshot(errors)
    manifest_text = MANIFEST.read_text()
    tree_text = TASK_TREE.read_text()

    manifest_rows: dict[str, tuple[str, str]] = {}
    for line in manifest_text.splitlines():
        match = re.match(
            r"\|\s*\d+\s*\|\s*([A-H])\s*\|\s*`(0x[0-9A-F]+)`\s*\|\s*(.*?)\s*\|",
            line,
        )
        if match:
            manifest_rows[normalize_address(match.group(2))] = (
                match.group(1),
                match.group(3),
            )

    node_pattern = re.compile(
        r"^\s*- NODE `(0x[0-9A-F]+)` (?P<name>.*?) "
        r"`(?P<relation>\[[a-z-]+\])` parent=`(?P<parent>ROOT|@[0-9A-F]+)`$",
        re.MULTILINE,
    )
    nodes: dict[str, tuple[str, str, str]] = {}
    node_occurrences: list[str] = []
    for match in node_pattern.finditer(tree_text):
        address = normalize_address(match.group(1))
        parent = match.group("parent")
        parent = "ROOT" if parent == "ROOT" else normalize_address(parent)
        node_occurrences.append(address)
        nodes[address] = (match.group("name"), match.group("relation"), parent)

    manifest_set = set(manifest_rows)
    node_set = set(nodes)
    all_tree_address_tokens = re.findall(r"0x[0-9A-F]+", tree_text, re.IGNORECASE)

    if len(manifest_rows) != 114:
        fail(errors, f"MANIFEST row count is {len(manifest_rows)}, expected 114")
    if len(node_occurrences) != 114:
        fail(errors, f"TASK_TREE node count is {len(node_occurrences)}, expected 114")
    if len(node_set) != 114:
        fail(errors, f"TASK_TREE unique address count is {len(node_set)}, expected 114")
    if len(all_tree_address_tokens) != 114:
        fail(errors, f"TASK_TREE 0x token count is {len(all_tree_address_tokens)}, expected 114")
    if manifest_set != node_set:
        fail(
            errors,
            "TASK_TREE/MANIFEST mismatch: "
            f"tree-only={sorted(node_set - manifest_set)} "
            f"manifest-only={sorted(manifest_set - node_set)}",
        )
    duplicates = sorted(address for address, count in Counter(node_occurrences).items() if count != 1)
    if duplicates:
        fail(errors, f"TASK_TREE duplicate nodes: {duplicates}")

    roots: list[str] = []
    for address, (_, relation, parent) in nodes.items():
        if relation not in ALLOWED_RELATIONS:
            fail(errors, f"{address}: invalid relation {relation}")
        if parent == "ROOT":
            roots.append(address)
        elif parent not in nodes:
            fail(errors, f"{address}: missing canonical parent {parent}")
    if sorted(roots) != ["0X42CEF8", "0X42CF28"]:
        fail(errors, f"unexpected TASK_TREE roots: {sorted(roots)}")

    for start in nodes:
        current = start
        seen: set[str] = set()
        while nodes[current][2] != "ROOT":
            if current in seen:
                fail(errors, f"cycle while walking canonical parents from {start}")
                break
            seen.add(current)
            current = nodes[current][2]

    expected_paths: dict[str, str] = {}

    def expected_path(address: str) -> str:
        if address in expected_paths:
            return expected_paths[address]
        parent = nodes[address][2]
        base = "/root" if parent == "ROOT" else expected_path(parent)
        result = f"{base}/f_{address[2:].lower()}"
        expected_paths[address] = result
        return result

    reports = {path.stem.upper(): path for path in REPORT_DIR.glob("0x*.md")}
    report_set = set(reports)
    if len(reports) != 114:
        fail(errors, f"report file count is {len(reports)}, expected 114")
    if report_set != manifest_set:
        fail(
            errors,
            "report/MANIFEST mismatch: "
            f"report-only={sorted(report_set - manifest_set)} "
            f"manifest-only={sorted(manifest_set - report_set)}",
        )

    verdict_counts: Counter[str] = Counter()
    dimension_counts: dict[str, Counter[str]] = {dimension: Counter() for dimension in DIMENSIONS}
    group_verdict_counts: dict[str, Counter[str]] = defaultdict(Counter)
    source_aliases = build_source_aliases(errors)
    source_line_counts: dict[str, int] = {}
    source_references: dict[str, list[SourceReference]] = {}
    source_reference_count = 0

    for address, report_path in sorted(reports.items()):
        text = report_path.read_text()
        group, _ = manifest_rows[address]
        _, relation, parent = nodes[address]

        references = extract_source_references(
            text,
            source_aliases,
            errors,
            address,
        )
        source_references[address] = references
        source_reference_count += len(references)
        if not references:
            fail(errors, f"{address}: no parseable local source reference")
        for reference in references:
            line_count = source_line_counts.get(reference.path)
            if line_count is None:
                line_count = len((REPO_ROOT / reference.path).read_text().splitlines())
                source_line_counts[reference.path] = line_count
            if reference.start < 1:
                fail(
                    errors,
                    f"{address}: source marker starts below line 1: {reference.marker}",
                )
            if reference.end < reference.start:
                fail(
                    errors,
                    f"{address}: reversed source marker range: {reference.marker}",
                )
            if reference.end > line_count:
                fail(
                    errors,
                    f"{address}: source marker exceeds {reference.path} ({line_count} lines): "
                    f"{reference.marker}",
                )

        if expected_path(address) not in text:
            fail(errors, f"{address}: expected collaboration path is absent")
        if "2026-07-25" not in text:
            fail(errors, f"{address}: current-session date is absent")
        if not re.search(r"fresh", text, re.IGNORECASE):
            fail(errors, f"{address}: fresh marker is absent")
        if not re.search(r"decompile", text, re.IGNORECASE):
            fail(errors, f"{address}: decompile record is absent")
        if relation not in text:
            fail(errors, f"{address}: canonical relation {relation} is absent")
        if parent == "ROOT":
            if "ROOT" not in text:
                fail(errors, f"{address}: ROOT parent is absent")
        elif ("0x" + parent[2:]).lower() not in text.lower():
            fail(errors, f"{address}: canonical parent {parent} is absent")
        if not (
            re.search(rf"\|\s*MANIFEST 分组\s*\|\s*{group}\s*\|", text)
            or re.search(rf"(?:分组|canonical 组)[：:\s`]*{group}\b", text)
            or re.search(rf"(?:TASK_TREE[：:]?\s*)?组\s*{group}\b", text)
            or re.search(rf"\b{group}\s*[（(/]", text)
        ):
            fail(errors, f"{address}: MANIFEST group {group} is absent")
        if "Android" not in text:
            fail(errors, f"{address}: Android attribution is absent")
        if "cpp/" not in text:
            fail(errors, f"{address}: local cpp path is absent")
        if not re.search(
            r"cpp/[^\s`\])]+(?::\d+|#L\d+)|"
            r"(?:cpp/[^\n]{0,200})(?:第\s*\d+|lines?\s+\d+|行\s*\d+)",
            text,
            re.IGNORECASE,
        ):
            fail(errors, f"{address}: exact local line marker is absent")
        if not re.search(r"cross[- ]?reference|交叉引用", text, re.IGNORECASE):
            fail(errors, f"{address}: cross-reference section is absent")
        if not re.search(
            r"确定\s*(?:生产代码\s*)?GAP|所有确定\s*GAP|生产实现 GAP|确定偏差",
            text,
            re.IGNORECASE,
        ):
            fail(errors, f"{address}: determined-GAP statement is absent")

        states: dict[str, str] = {}
        for dimension in DIMENSIONS:
            matches = re.findall(
                rf"^\|\s*{re.escape(dimension)}\s*\|\s*`?([A-Z_/]+)`?\s*\|",
                text,
                re.MULTILINE,
            )
            if len(matches) != 1:
                fail(errors, f"{address}: {dimension} has {len(matches)} status rows")
                continue
            state = matches[0]
            if state not in ALLOWED_DIMENSION_STATES:
                fail(errors, f"{address}: {dimension} has invalid state {state}")
                continue
            states[dimension] = state
            dimension_counts[dimension][state] += 1

        verdict_matches: list[str] = []
        verdict_headings = list(re.finditer(r"^##[^\n]*总判定[^\n]*$", text, re.MULTILINE))
        if len(verdict_headings) != 1:
            fail(errors, f"{address}: expected exactly one total-verdict heading")
        else:
            heading = verdict_headings[0]
            section_tail = text[heading.end() :]
            next_heading = re.search(r"^##\s", section_tail, re.MULTILINE)
            section = section_tail[: next_heading.start()] if next_heading else section_tail
            verdict_line = re.compile(
                r"^(?:总判定[：:\s]*)?[`*_]*(ALIGNED|HAS_GAP|EVIDENCE_LIMITED)[`*_]*[。.]?$"
            )
            for line in [heading.group(0), *section.splitlines()]:
                match = verdict_line.fullmatch(line.strip())
                if match:
                    verdict_matches.append(match.group(1))
        verdict_set = set(verdict_matches)
        if len(verdict_set) != 1 or not verdict_set <= ALLOWED_VERDICTS:
            fail(errors, f"{address}: invalid/ambiguous total verdict {verdict_matches}")
        else:
            verdict = next(iter(verdict_set))
            verdict_counts[verdict] += 1
            group_verdict_counts[group][verdict] += 1
            has_known_gap = any(value in {"GAP", "PARTIAL"} for value in states.values())
            if has_known_gap and verdict != "HAS_GAP":
                fail(errors, f"{address}: GAP/PARTIAL dimension without HAS_GAP verdict")
            if verdict == "HAS_GAP" and not has_known_gap:
                fail(errors, f"{address}: HAS_GAP verdict without GAP/PARTIAL dimension")

        pseudo = re.search(
            r"^##[^\n]*伪代码[^\n]*\n.*?(?P<fence>```|~~~)"
            r"(?:text|cpp|c\+\+)?\s*\n(?P<body>.*?)(?P=fence)",
            text,
            re.MULTILINE | re.DOTALL | re.IGNORECASE,
        )
        if not pseudo:
            fail(errors, f"{address}: Android pseudocode block is absent")
        else:
            line_count = sum(1 for line in pseudo.group("body").splitlines() if line.strip())
            if not 1 <= line_count <= 10:
                fail(errors, f"{address}: pseudocode has {line_count} non-empty lines")

    semantic_anchor_count = 0
    for anchor in SEMANTIC_ANCHORS:
        definition_range = find_definition_range(anchor, errors)
        if definition_range is None:
            continue
        semantic_anchor_count += 1
        start_line, end_line = definition_range
        anchor_references = source_references.get(anchor.report_address, [])
        if not any(
            reference.path == anchor.source_path
            and reference.start <= end_line
            and reference.end >= start_line
            for reference in anchor_references
        ):
            fail(
                errors,
                f"{anchor.report_address}: semantic anchor {anchor.label} has no source marker "
                f"overlapping its current definition in "
                f"{anchor.source_path}:{start_line}-{end_line}",
            )

    if errors:
        print(f"FAIL ({len(errors)} errors)")
        for error in errors:
            print(f"- {error}")
        return 1

    print("PASS")
    print("task_tree_nodes=114 task_tree_unique=114 manifest=114 reports=114")
    print("task_tree_manifest_reports_sets_equal=true graph_acyclic=true roots=2")
    print("expected_agent_paths=114 fresh_decompile_reports=114 schema_valid_reports=114")
    print(
        f"source_reference_reports={len(source_references)} "
        f"source_references={source_reference_count} "
        f"semantic_anchors={semantic_anchor_count}"
    )
    print(
        f"source_snapshot_files={source_snapshot_count} "
        "source_snapshot_sha256=true source_snapshot_set_equal=true"
    )
    print("verdicts=" + ",".join(f"{key}:{verdict_counts[key]}" for key in sorted(verdict_counts)))
    for dimension in DIMENSIONS:
        values = ",".join(
            f"{key}:{dimension_counts[dimension][key]}" for key in sorted(dimension_counts[dimension])
        )
        print(f"dimension[{dimension}]={values}")
    for group in "ABCDEFGH":
        values = ",".join(
            f"{key}:{group_verdict_counts[group][key]}" for key in sorted(group_verdict_counts[group])
        )
        print(f"group[{group}]={values}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
