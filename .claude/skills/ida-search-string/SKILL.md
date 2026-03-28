---
name: ida-search-string
description: Search for strings in IDA Pro across ALL encodings (UTF-8, UTF-16LE, UTF-32) using IDAPython. The IDA MCP `find` tool only matches ASCII/UTF-8 strings and misses UTF-16 encoded strings — this skill fixes that gap. Use this skill whenever the user wants to search or find strings in IDA Pro, especially when they say "搜索字符串", "search string", "find string in IDA", or when a previous `find` string search returned no results unexpectedly.
---

# IDA String Search (All Encodings)

## Why this skill exists

The `mcp__ida-pro-mcp__find` tool with `type: "string"` only searches ASCII/UTF-8 strings in IDA's string list. It silently skips UTF-16LE strings, which are extremely common in binaries that use wide characters (e.g., Kirikiri/吉里吉里 engine, Windows-origin code, Java/JNI unicode strings).

Additionally, IDA's string list can misdetect the start address of tightly packed UTF-16 strings (off by 2 bytes), causing the first character to be truncated. For example, `"PackinOne.dll"` may appear as `"ackinOne.dll"` in the string list, making keyword searches miss it.

This skill uses `mcp__ida-pro-mcp__py_eval` to search both IDA's string list AND raw memory, matching against all encodings.

## How to use

The user provides a search keyword as the argument. For example:
- `/ida-search-string emoteplayer.dll`
- `/ida-search-string JNI_OnLoad`

## Steps

1. Extract the search keyword from the user's argument.
2. Call `mcp__ida-pro-mcp__py_eval` with the following IDAPython code, replacing `KEYWORD` with the user's search term:

```python
import ida_strlist, ida_bytes, ida_segment

keyword = b"KEYWORD"
kw_lower = keyword.lower()

# ── Phase 1: Search IDA string list (fast) ──
sl = ida_strlist.string_info_t()
count = ida_strlist.get_strlist_qty()
found_addrs = set()
found = 0

for i in range(count):
    if ida_strlist.get_strlist_item(sl, i):
        s = ida_bytes.get_strlit_contents(sl.ea, sl.length, sl.type)
        if s and kw_lower in s.lower():
            stype = sl.type & 0xFF
            enc = {1: "UTF-16", 2: "UTF-32"}.get(stype, "UTF-8")
            print(f"0x{sl.ea:X} [{enc}] {s.decode('utf-8', errors='replace')}")
            found_addrs.add(sl.ea)
            found += 1

# ── Phase 2: Raw memory scan for UTF-16LE (catches misdetected strings) ──
kw_utf16 = keyword.lower().decode('ascii', errors='ignore').encode('utf-16-le')

raw_found = 0
CHUNK = 0x100000
MAX_CONTEXT = 64

def is_printable_utf16le(pair):
    lo, hi = pair[0], pair[1]
    if hi == 0:
        return 0x20 <= lo <= 0x7E
    return hi < 0x10

seg = ida_segment.get_first_seg()
while seg:
    ea = seg.start_ea
    seg_end = seg.end_ea
    while ea < seg_end:
        chunk_size = min(CHUNK, seg_end - ea)
        seg_bytes = ida_bytes.get_bytes(ea, int(chunk_size))
        if seg_bytes:
            lowered = seg_bytes.lower()
            pos = 0
            while True:
                idx = lowered.find(kw_utf16, pos)
                if idx == -1:
                    break
                match_ea = ea + idx
                if match_ea not in found_addrs:
                    start = idx
                    for _ in range(MAX_CONTEXT):
                        if start < 2:
                            break
                        pair = seg_bytes[start-2:start]
                        if pair == b'\x00\x00' or not is_printable_utf16le(pair):
                            break
                        start -= 2
                    end = idx + len(kw_utf16)
                    for _ in range(MAX_CONTEXT):
                        if end + 1 >= len(seg_bytes):
                            break
                        pair = seg_bytes[end:end+2]
                        if pair == b'\x00\x00' or not is_printable_utf16le(pair):
                            break
                        end += 2
                    raw_str = seg_bytes[start:end].decode('utf-16-le', errors='replace')
                    actual_ea = ea + start
                    print(f"0x{actual_ea:X} [UTF-16/raw] {raw_str}")
                    found_addrs.add(match_ea)
                    raw_found += 1
                pos = idx + 2
        ea += chunk_size
    seg = ida_segment.get_next_seg(seg.start_ea)

print(f"\nPhase 1 (string list): {found} matches")
print(f"Phase 2 (raw memory):  {raw_found} additional matches")
print(f"Total: {found + raw_found} matches")
```

3. Present the results to the user. If matches are found, show each address, encoding type, and string content. Phase 1 results come from IDA's string list; Phase 2 (marked `[UTF-16/raw]`) are from raw memory scanning, which catches strings that IDA misdetected or missed entirely. Note that Phase 2 results may include 1-2 extra characters at boundaries due to adjacent data resembling valid UTF-16. If no matches are found, suggest the user try a partial keyword or check if the string might be dynamically constructed at runtime.

## Important notes

- Always escape any quotes or backslashes in the user's keyword before inserting into the Python code.
- The keyword is matched case-insensitively against the raw bytes decoded as UTF-8, which works for ASCII substrings in both UTF-8 and UTF-16 content (since `get_strlit_contents` normalizes UTF-16 to bytes).
- Phase 2 raw memory scan handles cases where IDA's string list has wrong start addresses for tightly-packed UTF-16 strings (e.g., `"PackinOne.dll"` detected as `"ackinOne.dll"`).
- Phase 2 uses `is_printable_utf16le()` to determine string boundaries, which may include 1-2 stray chars from adjacent ASCII data that happens to look like valid UTF-16LE.
- If the user previously tried `find` with `type: "string"` and got 0 results, proactively mention that the string was likely UTF-16 encoded, which is why `find` missed it.
