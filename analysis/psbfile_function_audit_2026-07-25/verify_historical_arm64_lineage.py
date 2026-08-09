#!/usr/bin/env python3
"""Verify the non-authoritative historical Android ARM64 psbfile lineage.

The repository history retains an older ARM64 ``libkrkr2.so`` blob.  This
script proves whether its psbfile surface is merely the current surface shifted
by link layout, or whether it preserves a genuinely different function/EH/code
shape that could add source-reconstruction evidence.  The historical image is
read from Git and exists only in an automatically removed temporary file.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import verify_elf_surface as elf_surface


BASE = Path(__file__).resolve().parent
REPO_ROOT = BASE.parents[1]
DEFAULT_CURRENT_BINARY = REPO_ROOT / "reference/libkrkr2/libkrkr2.so"
HISTORICAL_BLOB = "11979ea5e1d8acce2fb17690edebd0f3f4292a5e"
HISTORICAL_COMMIT = "8f4d8af5d64728630dd04133ab5d14c3c7cc5a64"
CURRENT_SHA256 = (
    "ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38"
)
HISTORICAL_SHA256 = (
    "05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f"
)
CURRENT_BUILD_ID = "985d9f685e07ce4497472523c3e84b1f38989235"
HISTORICAL_BUILD_ID = "de22234bffa0545d276b705487ca0c3d35101386"
CURRENT_SIZE = 27_929_688
HISTORICAL_SIZE = 27_917_400
ADDRESS_SHIFT = 0x3E0

HISTORICAL_ONLY_DYNSYMS = frozenset({
    "_ZNSt14_Function_base13_Base_managerISt5_BindIFPFviiiEiiiEEE10_M_managerERSt9_Any_dataRKS7_St18_Manager_operation",
    "_ZNSt17_Function_handlerIFvvESt5_BindIFPFviiiEiiiEEE9_M_invokeERKSt9_Any_data",
    "_ZTISt17_Weak_result_typeIPFviiiEE",
    "_ZTISt22_Weak_result_type_implIPFviiiEE",
    "_ZTISt5_BindIFPFviiiEiiiEE",
    "_ZTSSt17_Weak_result_typeIPFviiiEE",
    "_ZTSSt22_Weak_result_type_implIPFviiiEE",
    "_ZTSSt5_BindIFPFviiiEiiiEE",
})

DISASM_LINE_RE = re.compile(
    r"^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(.+?)\s*$")
HEX_TARGET_RE = re.compile(r"(?<!#)0x[0-9a-f]+(?:\s+<[^>]+>)?")
HASH_IMMEDIATE_RE = re.compile(r"#-?(?:0x[0-9a-f]+|[0-9]+)")
ADD_BASE_RE = re.compile(r"^add\s+x\d+,\s*(x\d+),\s*#")
LDR_BASE_RE = re.compile(r"^ldr\s+[^,]+,\s*\[(x\d+),\s*#")


def checked_range(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(
            f"{label} range 0x{offset:X}+0x{size:X} is outside ELF")


def read_sections(data: bytes) -> dict[str, elf_surface.ElfSection]:
    elf_header = struct.Struct("<16sHHIQQQIHHHHHH")
    section_header = struct.Struct("<IIQQQQIIQQ")
    checked_range(data, 0, elf_header.size, "ELF header")
    header = elf_header.unpack_from(data)
    ident = header[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
        raise ValueError("image is not a little-endian ELF64 file")
    if header[2] != 183:
        raise ValueError(f"ELF machine is {header[2]}, expected AArch64")

    section_offset = header[6]
    section_entry_size = header[11]
    section_count = header[12]
    names_index = header[13]
    if section_entry_size != section_header.size:
        raise ValueError("unexpected ELF section-header size")
    if section_count == 0 or names_index >= section_count:
        raise ValueError("unsupported extended ELF section table")
    checked_range(
        data, section_offset, section_entry_size * section_count,
        "ELF section table")
    raw_sections = [
        section_header.unpack_from(
            data, section_offset + index * section_entry_size)
        for index in range(section_count)
    ]

    names_header = raw_sections[names_index]
    names_offset, names_size = names_header[4], names_header[5]
    checked_range(data, names_offset, names_size, "section-name table")
    names = data[names_offset:names_offset + names_size]

    result: dict[str, elf_surface.ElfSection] = {}
    for fields in raw_sections:
        name_offset = fields[0]
        if name_offset >= len(names):
            raise ValueError("ELF section-name offset is out of range")
        name_end = names.find(b"\0", name_offset)
        if name_end < 0:
            raise ValueError("unterminated ELF section name")
        name = names[name_offset:name_end].decode("ascii")
        if name in result:
            raise ValueError(f"duplicate ELF section {name!r}")
        section = elf_surface.ElfSection(
            address=fields[3], offset=fields[4], size=fields[5])
        if fields[1] != 8:  # SHT_NOBITS has no file-backed payload.
            checked_range(data, section.offset, section.size, name or "<null>")
        result[name] = section
    return result


def read_build_id(
    data: bytes, sections: dict[str, elf_surface.ElfSection]
) -> str:
    section = sections.get(".note.gnu.build-id")
    if section is None:
        raise ValueError("missing .note.gnu.build-id")
    cursor = section.offset
    end = section.offset + section.size
    while cursor < end:
        checked_range(data, cursor, 12, "ELF note header")
        name_size, desc_size, note_type = struct.unpack_from("<III", data, cursor)
        cursor += 12
        checked_range(data, cursor, name_size, "ELF note name")
        name = data[cursor:cursor + name_size].rstrip(b"\0")
        cursor = (cursor + name_size + 3) & ~3
        checked_range(data, cursor, desc_size, "ELF note descriptor")
        descriptor = data[cursor:cursor + desc_size]
        cursor = (cursor + desc_size + 3) & ~3
        if note_type == 3 and name == b"GNU":
            return descriptor.hex()
    raise ValueError("GNU build-id note was not found")


def read_git_blob(oid: str) -> bytes:
    process = subprocess.run(
        ["git", "cat-file", "blob", oid], cwd=REPO_ROOT,
        check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if process.returncode != 0:
        raise RuntimeError(
            f"git cat-file failed for {oid}: "
            f"{process.stderr.decode(errors='replace').strip()}")
    return process.stdout


def resolve_executable(
    explicit: Path | None, candidates: tuple[Path | str, ...], label: str
) -> Path:
    paths: list[Path] = []
    if explicit is not None:
        paths.append(explicit)
    else:
        for candidate in candidates:
            if isinstance(candidate, Path):
                paths.append(candidate)
                continue
            found = shutil.which(candidate)
            if found:
                paths.append(Path(found))
    for path in paths:
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise FileNotFoundError(f"{label} executable was not found")


def disassemble(
    objdump: Path, binary: Path, start: int, end: int
) -> dict[int, str]:
    process = subprocess.run(
        [
            str(objdump), "-d", f"--start-address={start:#x}",
            f"--stop-address={end:#x}", str(binary),
        ],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if process.returncode != 0:
        raise RuntimeError(
            f"objdump failed with {process.returncode}: "
            f"{process.stderr.strip()}")
    result: dict[int, str] = {}
    for line in process.stdout.splitlines():
        match = DISASM_LINE_RE.match(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        instruction = match.group(2).split("//", 1)[0].rstrip()
        result[address] = instruction
    return result


def verify_instruction_shape(
    objdump: Path, current: Path, historical: Path
) -> tuple[int, int, int, int]:
    exact = 0
    address_only = 0
    immediate_only = 0
    instruction_count = 0
    immediate_mnemonics: Counter[str] = Counter()
    adrp_distances: Counter[int] = Counter()
    errors: list[str] = []

    for surface_name, start, end in elf_surface.SURFACES:
        current_disasm = disassemble(objdump, current, start, end)
        historical_disasm = disassemble(
            objdump, historical, start + ADDRESS_SHIFT, end + ADDRESS_SHIFT)
        for address in range(start, end, 4):
            instruction_count += 1
            current_instruction = current_disasm.get(address)
            historical_instruction = historical_disasm.get(
                address + ADDRESS_SHIFT)
            if current_instruction is None or historical_instruction is None:
                errors.append(
                    f"{surface_name}: missing instruction at 0x{address:X}")
                continue
            if current_instruction == historical_instruction:
                exact += 1
                continue

            current_address_shape = HEX_TARGET_RE.sub(
                "<addr>", current_instruction)
            historical_address_shape = HEX_TARGET_RE.sub(
                "<addr>", historical_instruction)
            if current_address_shape == historical_address_shape:
                address_only += 1
                continue

            current_shape = HASH_IMMEDIATE_RE.sub(
                "#<imm>", current_address_shape)
            historical_shape = HASH_IMMEDIATE_RE.sub(
                "#<imm>", historical_address_shape)
            if current_shape != historical_shape:
                errors.append(
                    f"{surface_name}: instruction shape mismatch at "
                    f"0x{address:X}: {current_instruction!r} != "
                    f"{historical_instruction!r}")
                continue

            immediate_only += 1
            mnemonic = current_instruction.split(maxsplit=1)[0]
            immediate_mnemonics[mnemonic] += 1
            base_match = (
                ADD_BASE_RE.match(current_instruction)
                or LDR_BASE_RE.match(current_instruction)
            )
            if base_match is None:
                errors.append(
                    f"{surface_name}: unclassified changed immediate at "
                    f"0x{address:X}: {current_instruction!r}")
                continue
            base_register = base_match.group(1)
            adrp_pattern = re.compile(
                rf"^adrp\s+{re.escape(base_register)},\s*")
            for distance in range(1, 17):
                prior = current_disasm.get(address - distance * 4, "")
                if adrp_pattern.match(prior):
                    adrp_distances[distance] += 1
                    break
            else:
                errors.append(
                    f"{surface_name}: changed immediate at 0x{address:X} "
                    f"has no {base_register} ADRP anchor")

    if errors:
        raise ValueError("\n".join(errors[:50]))
    if sum(adrp_distances.values()) != immediate_only:
        raise ValueError("not every changed immediate is ADRP-anchored")
    if set(immediate_mnemonics) - {"add", "ldr"}:
        raise ValueError(
            f"unexpected changed-immediate mnemonics: {immediate_mnemonics}")
    return instruction_count, exact, address_only, immediate_only


def read_dynamic_symbols(objdump: Path, binary: Path) -> set[str]:
    process = subprocess.run(
        [str(objdump), "-T", str(binary)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if process.returncode != 0:
        raise RuntimeError(
            f"objdump -T failed with {process.returncode}: "
            f"{process.stderr.strip()}")
    result: set[str] = set()
    for line in process.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 6 and re.fullmatch(r"[0-9a-f]{16}", fields[0]):
            result.add(fields[-1])
    return result


def verify_lineage(
    current_binary: Path, historical_data: bytes,
    dwarfdump: Path, objdump: Path,
) -> None:
    current_data = current_binary.read_bytes()
    if len(current_data) != CURRENT_SIZE:
        raise ValueError(
            f"current size is {len(current_data)}, expected {CURRENT_SIZE}")
    if len(historical_data) != HISTORICAL_SIZE:
        raise ValueError(
            f"historical size is {len(historical_data)}, "
            f"expected {HISTORICAL_SIZE}")
    if hashlib.sha256(current_data).hexdigest() != CURRENT_SHA256:
        raise ValueError("current SHA-256 mismatch")
    if hashlib.sha256(historical_data).hexdigest() != HISTORICAL_SHA256:
        raise ValueError("historical SHA-256 mismatch")

    current_sections = read_sections(current_data)
    historical_sections = read_sections(historical_data)
    if read_build_id(current_data, current_sections) != CURRENT_BUILD_ID:
        raise ValueError("current build-id mismatch")
    if read_build_id(historical_data, historical_sections) != HISTORICAL_BUILD_ID:
        raise ValueError("historical build-id mismatch")
    forbidden_sections = {
        name for name in current_sections | historical_sections
        if name.startswith(".debug") or name in {".symtab", ".gnu_debugdata"}
    }
    if forbidden_sections:
        raise ValueError(
            f"unexpected retained debug/symbol sections: "
            f"{sorted(forbidden_sections)}")

    with tempfile.NamedTemporaryFile(suffix=".so") as temporary:
        temporary.write(historical_data)
        temporary.flush()
        historical_binary = Path(temporary.name)

        current_fdes = elf_surface.read_fdes(dwarfdump, current_binary)
        historical_fdes = elf_surface.read_fdes(dwarfdump, historical_binary)
        manifest = elf_surface.read_manifest_addresses()
        fde_errors: list[str] = []
        for address in sorted(manifest):
            current_fde = current_fdes.get(address)
            historical_fde = historical_fdes.get(address + ADDRESS_SHIFT)
            if current_fde is None or historical_fde is None:
                fde_errors.append(f"missing mapped FDE for 0x{address:X}")
                continue
            if historical_fde.end != current_fde.end + ADDRESS_SHIFT:
                fde_errors.append(f"FDE end mismatch for 0x{address:X}")
            if ((current_fde.lsda is None)
                    != (historical_fde.lsda is None)):
                fde_errors.append(f"FDE LSDA-presence mismatch for 0x{address:X}")
        if fde_errors:
            raise ValueError("\n".join(fde_errors))

        current_exception = current_sections[".gcc_except_table"]
        historical_exception = historical_sections[".gcc_except_table"]
        lsda_errors: list[str] = []
        call_site_count = 0
        for address in sorted(elf_surface.EXPECTED_LSDA_STARTS):
            current_fde = current_fdes[address]
            historical_fde = historical_fdes[address + ADDRESS_SHIFT]
            current_lsda = elf_surface.parse_lsda(
                current_data, current_exception, current_fde.lsda)
            historical_lsda = elf_surface.parse_lsda(
                historical_data, historical_exception, historical_fde.lsda)
            call_site_count += len(current_lsda.call_sites)
            if (
                current_lsda.type_table_encoding
                != historical_lsda.type_table_encoding
                or current_lsda.call_sites != historical_lsda.call_sites
            ):
                lsda_errors.append(f"LSDA shape mismatch for 0x{address:X}")
        if lsda_errors:
            raise ValueError("\n".join(lsda_errors))

        instruction_counts = verify_instruction_shape(
            objdump, current_binary, historical_binary)
        current_symbols = read_dynamic_symbols(objdump, current_binary)
        historical_symbols = read_dynamic_symbols(objdump, historical_binary)
        if current_symbols - historical_symbols:
            raise ValueError(
                "unexpected current-only dynamic symbols: "
                f"{sorted(current_symbols - historical_symbols)}")
        if historical_symbols - current_symbols != HISTORICAL_ONLY_DYNSYMS:
            raise ValueError(
                "historical-only dynamic symbol set changed: "
                f"{sorted(historical_symbols - current_symbols)}")
        psbfile_markers = (
            "PSBFile", "PSBRaw", "PSBMedia", "PSBValue", "PsbArray",
            "PSB_Find", "PSB_Decode",
        )
        psbfile_symbols = {
            symbol for symbol in current_symbols | historical_symbols
            if any(marker in symbol for marker in psbfile_markers)
        }
        if psbfile_symbols:
            raise ValueError(
                "unexpected PSBFile semantic dynamic symbols: "
                f"{sorted(psbfile_symbols)}")

    instruction_count, exact, address_only, immediate_only = instruction_counts
    print("PASS")
    print(
        f"historical_commit={HISTORICAL_COMMIT} "
        f"historical_blob={HISTORICAL_BLOB}")
    print(
        f"current_sha256={CURRENT_SHA256} "
        f"historical_sha256={HISTORICAL_SHA256}")
    print(
        f"current_build_id={CURRENT_BUILD_ID} "
        f"historical_build_id={HISTORICAL_BUILD_ID}")
    print(
        f"manifest_fdes={len(manifest)} address_shift=0x{ADDRESS_SHIFT:X} "
        "fde_shape_mismatch=0")
    print(
        f"lsda_functions={len(elf_surface.EXPECTED_LSDA_STARTS)} "
        f"lsda_call_sites={call_site_count} lsda_shape_mismatch=0")
    print(
        f"instructions={instruction_count} exact={exact} "
        f"address_only={address_only} "
        f"adrp_anchored_immediate_only={immediate_only} shape_mismatch=0")
    print(
        f"historical_only_dynsyms={len(HISTORICAL_ONLY_DYNSYMS)} "
        "psbfile_semantic_dynamic_symbols=0 debug_sections=0")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--current-binary", type=Path, default=DEFAULT_CURRENT_BINARY)
    parser.add_argument("--historical-blob", default=HISTORICAL_BLOB)
    parser.add_argument("--llvm-dwarfdump", type=Path, default=None)
    parser.add_argument("--objdump", type=Path, default=None)
    args = parser.parse_args()

    current_binary = args.current_binary.resolve()
    if not current_binary.is_file():
        parser.error(f"current binary does not exist: {current_binary}")
    dwarfdump = elf_surface.resolve_dwarfdump(args.llvm_dwarfdump)
    sibling_objdump = dwarfdump.with_name("objdump")
    objdump = resolve_executable(
        args.objdump, (sibling_objdump, "llvm-objdump", "objdump"), "objdump")
    historical_data = read_git_blob(args.historical_blob)
    verify_lineage(current_binary, historical_data, dwarfdump, objdump)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
