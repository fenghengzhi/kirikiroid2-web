---
name: ida-search-string
description: 在 IDA Pro 中跨所有编码（UTF-8、UTF-16LE、UTF-32）搜索字符串，使用 IDAPython。IDA MCP 的 `find` 工具仅匹配 ASCII/UTF-8 字符串，会遗漏 UTF-16 编码的字符串——此 skill 修补了这个缺口。当用户要搜索或查找 IDA Pro 中的字符串时使用，尤其是当用户说"搜索字符串"、"search string"、"find string in IDA"，或之前的 `find` 字符串搜索意外返回空结果时。
---

# IDA 字符串搜索（全编码）

## 为什么需要这个 skill

`mcp__ida-pro-mcp__find` 工具配合 `type: "string"` 仅搜索 IDA 字符串列表中的 ASCII/UTF-8 字符串。它会静默跳过 UTF-16LE 字符串，而这类字符串在使用宽字符的二进制中极其常见（如 Kirikiri/吉里吉里引擎、Windows 来源的代码、Java/JNI unicode 字符串）。

此外，IDA 的字符串列表可能错误检测紧密排列的 UTF-16 字符串的起始地址（偏移 2 字节），导致第一个字符被截断。例如，`"PackinOne.dll"` 在字符串列表中可能显示为 `"ackinOne.dll"`，使得关键词搜索无法匹配。

此 skill 使用 `mcp__ida-pro-mcp__py_eval` 同时搜索 IDA 的字符串列表和原始内存，匹配所有编码。

## 使用方法

用户提供搜索关键词作为参数。例如：
- `/ida-search-string emoteplayer.dll`
- `/ida-search-string JNI_OnLoad`

## 步骤

1. 从用户参数中提取搜索关键词。
2. 调用 `mcp__ida-pro-mcp__py_eval`，使用以下 IDAPython 代码，将 `KEYWORD` 替换为用户的搜索词：

```python
import ida_strlist, ida_bytes, ida_segment

keyword = b"KEYWORD"
kw_lower = keyword.lower()

# ── 阶段 1：搜索 IDA 字符串列表（快速）──
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

# ── 阶段 2：原始内存扫描 UTF-16LE（捕获未正确检测的字符串）──
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

print(f"\n阶段 1（字符串列表）：{found} 个匹配")
print(f"阶段 2（原始内存）：  {raw_found} 个额外匹配")
print(f"总计：{found + raw_found} 个匹配")
```

3. 向用户展示结果。如果找到匹配，显示每个地址、编码类型和字符串内容。阶段 1 的结果来自 IDA 的字符串列表；阶段 2（标记为 `[UTF-16/raw]`）来自原始内存扫描，可以捕获 IDA 未正确检测或完全遗漏的字符串。注意阶段 2 的结果在边界处可能包含 1-2 个额外字符，因为相邻数据恰好看起来像有效的 UTF-16。如果没有找到匹配，建议用户尝试部分关键词或检查该字符串是否可能在运行时动态构造。

## 重要说明

- 插入 Python 代码之前，始终转义用户关键词中的引号和反斜杠。
- 关键词以不区分大小写的方式与解码为 UTF-8 的原始字节进行匹配，这对 UTF-8 和 UTF-16 内容中的 ASCII 子字符串都有效（因为 `get_strlit_contents` 会将 UTF-16 标准化为字节）。
- 阶段 2 原始内存扫描处理 IDA 字符串列表中紧密排列的 UTF-16 字符串起始地址错误的情况（如 `"PackinOne.dll"` 被检测为 `"ackinOne.dll"`）。
- 阶段 2 使用 `is_printable_utf16le()` 来确定字符串边界，可能包含来自相邻 ASCII 数据的 1-2 个杂散字符，因为它们恰好看起来像有效的 UTF-16LE。
- 如果用户之前尝试过 `find` 配合 `type: "string"` 且得到 0 个结果，主动说明该字符串可能是 UTF-16 编码的，这就是 `find` 遗漏它的原因。
