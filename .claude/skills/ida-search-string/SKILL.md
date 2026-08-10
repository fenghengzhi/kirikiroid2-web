---
name: ida-search-string
description: 在 IDA Pro 中跨 ASCII/UTF-8、UTF-16LE 和 UTF-32LE 搜索字符串，使用 Codex 原生 mcp__idalib__find/find_bytes 工具。当普通 find 字符串搜索返回空、字符串可能来自 TJS_W 宽字面量，或用户要求在四个参考二进制中搜索字符串时使用。
---

# IDA 字符串搜索（全编码）

## 为什么需要这个 skill

`mcp__idalib__find` 配合 `type: "string"` 主要查询 IDA 字符串列表中的 ASCII/UTF-8 字符串，可能遗漏 UTF-16LE/UTF-32LE 字面量。Kirikiri 中的 `TJS_W(...)` 使用宽字符，这个缺口尤其常见。

IDA 也可能把紧密排列的宽字符串识别在错误的起始地址，例如把 `"PackinOne.dll"` 显示成 `"ackinOne.dll"`。因此不能把一次 `find` 空结果当成字符串不存在；必须补做原始字节搜索。

当前 `idalib` MCP 不提供旧版 `py_eval`。本 skill 只使用 Codex 原生 `mcp__idalib__*` 工具，不运行 Python MCP 客户端、手工 JSON-RPC 或 IDA CLI 旁路。

## 会话前置条件

1. 使用 `mcp__idalib__idb_open` 打开目标配套 `.i64`，保存返回的 `session_id`。
2. 使用 `mcp__idalib__server_health(database=<session_id>)` 核对 `module` 与 `input_path`。
3. 后续每次调用都显式传入该 `database`；不得把一个二进制的地址或会话用于其它二进制。
4. 四参考二进制任务必须分别搜索四个 `database`，不能以一个文件的结果代表其余三个。

## 搜索步骤

### 1. 先查询 IDA 字符串列表

```
mcp__idalib__find
  database="<session_id>"
  type="string"
  targets=["KEYWORD"]
```

若结果为空或关键词可能是宽字符串，继续执行原始字节搜索。

### 2. 生成编码后的精确字节模式

把查询词分别编码为 UTF-8、UTF-16LE 和 UTF-32LE，并以空格分隔的十六进制字节表示。这里只生成查询模式，不读取或分析二进制。

例如 `PSBFile`：

```
UTF-8:    50 53 42 46 69 6C 65
UTF-16LE: 50 00 53 00 42 00 46 00 69 00 6C 00 65 00
UTF-32LE: 50 00 00 00 53 00 00 00 42 00 00 00 46 00 00 00
           69 00 00 00 6C 00 00 00 65 00 00 00
```

字节搜索是大小写敏感的。需要不区分大小写时，为实际可能出现的大小写形式分别生成模式；不要使用会改变非 ASCII Unicode 语义的简单逐字节大小写转换。

### 3. 通过原生 MCP 搜索原始内存

```
mcp__idalib__find_bytes
  database="<session_id>"
  patterns=[
    "50 53 42 46 69 6C 65",
    "50 00 53 00 42 00 46 00 69 00 6C 00 65 00",
    "50 00 00 00 53 00 00 00 42 00 00 00 46 00 00 00 69 00 00 00 6C 00 00 00 65 00 00 00"
  ]
```

如结果分页，按返回的 `cursor.next` 继续设置 `offset`，直到 `cursor.done=true`；禁止只读取第一页后宣称没有更多匹配。

### 4. 验证边界并追踪引用

对每个匹配地址：

1. 使用 `mcp__idalib__get_bytes(database=<session_id>, regions=[{addr, size}])` 读取匹配前后足够字节，按对应编码确认完整字符串、终止符和真实起始地址。
2. 若匹配落在字符串中部，按 UTF-16LE 的 2 字节或 UTF-32LE 的 4 字节 code unit 向前核对，不要直接把中间地址当作字符串起点。
3. 对确认后的真实地址调用 `mcp__idalib__xrefs_to(database=<session_id>, addrs=[...])`。
4. 反编译引用函数时继续使用同一个 `database`，并在证据中写成 `二进制文件名!函数名@地址`。

## 结果报告

报告每个匹配时至少包含：

- 二进制文件与 `database` 会话；
- 匹配地址和确认后的字符串起始地址；
- 编码类型；
- 字节边界验证结果；
- 交叉引用函数；
- `find` 与 `find_bytes` 是否产生不同结果。

如果所有编码都没有匹配，只能报告“本轮在已核对的 IDB 中未找到静态字面量”。随后应尝试部分关键词、大小写变体、相邻稳定子串或调用链搜索；不得直接断言该字符串或相关机制不存在。
