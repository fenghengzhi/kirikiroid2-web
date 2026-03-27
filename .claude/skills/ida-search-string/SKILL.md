---
name: ida-search-string
description: Search for strings in IDA Pro across ALL encodings (UTF-8, UTF-16LE, UTF-32) using IDAPython. The IDA MCP `find` tool only matches ASCII/UTF-8 strings and misses UTF-16 encoded strings — this skill fixes that gap. Use this skill whenever the user wants to search or find strings in IDA Pro, especially when they say "搜索字符串", "search string", "find string in IDA", or when a previous `find` string search returned no results unexpectedly.
---

# IDA String Search (All Encodings)

## Why this skill exists

The `mcp__ida-pro-mcp__find` tool with `type: "string"` only searches ASCII/UTF-8 strings in IDA's string list. It silently skips UTF-16LE strings, which are extremely common in binaries that use wide characters (e.g., Kirikiri/吉里吉里 engine, Windows-origin code, Java/JNI unicode strings).

This skill uses `mcp__ida-pro-mcp__py_eval` to iterate over IDA's full string list via IDAPython, matching against all encodings.

## How to use

The user provides a search keyword as the argument. For example:
- `/ida-search-string emoteplayer.dll`
- `/ida-search-string JNI_OnLoad`

## Steps

1. Extract the search keyword from the user's argument.
2. Call `mcp__ida-pro-mcp__py_eval` with the following IDAPython code, replacing `KEYWORD` with the user's search term:

```python
import ida_strlist, ida_bytes
sl = ida_strlist.string_info_t()
keyword = b"KEYWORD"
count = ida_strlist.get_strlist_qty()
found = 0
for i in range(count):
    if ida_strlist.get_strlist_item(sl, i):
        s = ida_bytes.get_strlit_contents(sl.ea, sl.length, sl.type)
        if s and keyword.lower() in s.lower():
            stype = sl.type & 0xFF
            if stype == 1:
                enc = "UTF-16"
            elif stype == 2:
                enc = "UTF-32"
            else:
                enc = "UTF-8"
            print(f"0x{sl.ea:X} [{enc}] {s.decode('utf-8', errors='replace')}")
            found += 1
print(f"\nTotal: {found} matches out of {count} strings")
```

3. Present the results to the user. If matches are found, show each address, encoding type, and string content. If no matches are found, suggest the user try a partial keyword or check if the string might be dynamically constructed at runtime.

## Important notes

- Always escape any quotes or backslashes in the user's keyword before inserting into the Python code.
- The keyword is matched case-insensitively against the raw bytes decoded as UTF-8, which works for ASCII substrings in both UTF-8 and UTF-16 content (since `get_strlit_contents` normalizes UTF-16 to bytes).
- If the user previously tried `find` with `type: "string"` and got 0 results, proactively mention that the string was likely UTF-16 encoded, which is why `find` missed it.
