---
name: IDA UTF-16 string misidentification trap
description: IDA often labels UTF-16LE strings as ASCII C strings, showing only the first character — MUST verify raw bytes for all string operands in ARM64 code
type: feedback
---

IDA frequently mislabels UTF-16LE strings as ASCII (string type 0) in ARM64 binaries. When a UTF-16LE string like `"zx"` (bytes: `7a 00 78 00 00 00`) is classified as ASCII, IDA sees `7a 00` as `"z"` + null terminator, hiding the second character.

**Why:** In sub_692AB0 (0x692AB0), IDA pseudocode showed `sub_662668(&v143, "z", ...)` but raw bytes at the string address (0x14D868E) proved it was UTF-16LE `"zx"`. This caused the local code to use wrong PSB dictionary keys ("z" instead of "zx", "s" instead of "sx"), making scaleX/slantX always return default 1.0 since PSB has "zx"/"sx" but not "z"/"s".

**How to apply:** For ANY string operand in IDA pseudocode, especially single-character strings, ALWAYS verify raw bytes at the string address using `ida_bytes.get_bytes()` or `get_bytes` MCP tool. Decode as UTF-16LE to see the real content. Fix with `ida_bytes.create_strlit(addr, size, STRTYPE_C_16)`.

Complete mapping of misidentified strings found in sub_692AB0:
- `"z"` → `"zx"` (scaleX) at 0x14D868E
- `"s"` → `"sx"` (slantX) at 0x14D869A
- `"b"` → `"bm"` (blendMode) at 0x14D867A
- `"t"` → `"ti"` (timeline) at 0x14D86A6
- `"i"` → `"icon"` at 0x14D866A
- `"c"` → `"coord"` at 0x14D7186
- `"f"` → `"fmax"` (prt) at 0x14D87B6
- `"v"` → `"vmax"` (prt) at 0x14D87CA
- `"a"` → `"amax"` (prt) at 0x14D87DE
- `"z"` → `"zmax"` (prt) at 0x14D87F2
- `"o"` → `"obj"` (mesh) at 0x14D86DA
- `"m"` → `"mcc"` (mesh) at 0x14D86E2
- `"f"` → `"fov"` (camera) at 0x14D880A
