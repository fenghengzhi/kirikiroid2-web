---
name: ida-decompile
description: >
  使用 Codex 原生 mcp__idalib__* 工具联合逆向 reference/binaries/ 下的四个参考二进制。
  当用户要求检查原始实现如何实现某功能、将 Web 移植版的 C++ 代码与参考二进制对比、
  查找函数地址、追踪调用链，或理解 NCB 类注册时使用此 skill。
  在修复需要理解原始实现的 bug 时也应主动使用。
  触发关键词：reference/binaries、参考二进制、IDA、反编译、逆向工程、原始实现、
  "原版是怎么做的"、二进制中的 NCB 注册、在二进制文件中查找函数。
---

# Codex 原生 idalib MCP — 四参考二进制联合逆向工程

此 skill 提供使用 Codex 原生 `mcp__idalib__*` 工具联合逆向 `reference/binaries/` 中四个参考二进制的工作模式。插件展示名与工具命名空间是两回事；只调用实际暴露的工具名，不根据 “IDA Pro MCP” 展示名拼接前缀。四个文件共同构成取证输入；单独反编译其中一个文件不能代表原始源码。

## 参考文件

| 平台 | 架构 | 二进制文件 | IDA 数据库 |
|------|------|------------|------------|
| Android | arm64-v8a | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` |
| Android | armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` |
| iOS | arm64 | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_arm64.i64` |
| iOS | armv7 | `Kirikiroid2_1.3.9_iOS_armv7` | `Kirikiroid2_1.3.9_iOS_armv7.i64` |

`.i64` 是配套 IDA 数据库，不计入四个目标二进制。`armabi-v7a` 是目录中现有文件的实际拼写。

## 四文件取证规则

1. 每轮开始核对上表的四个目标二进制和四个 `.i64`。统计目标时排除 `.i64`；任一目标或数据库缺失、不可读或当前 IDB 对应错误输入时，停止取证并报告，禁止静默只分析可用子集。
2. 对每个目标逻辑建立逐文件映射：二进制文件名、函数名/地址、架构、定位状态。地址只在所属文件内有效，禁止把一个文件的地址用于其它文件。
3. 对四个已定位函数分别执行 fresh `decompile`。若某文件中函数被内联、裁剪或暂未定位，必须保留本轮字符串、交叉引用、调用链和反汇编搜索记录，不能用另一个文件的结果代替。
4. 先提炼四者共同的控制流、数据流和生命周期，再单列差异。差异必须标注为平台、ABI、编译器、版本或尚未解释；证据不足时不得强行合并。
5. 分析报告和代码注释引用地址时使用 `二进制文件名!函数名@地址`，禁止写没有文件归属的裸地址。

建议为每个目标函数维护以下证据表：

| 二进制文件 | 函数名/地址 | 状态 | fresh 证据 | 与其余文件的差异 |
|------------|-------------|------|------------|--------------------|
| `<file-1>` | `<name>@<addr>` | 已定位/内联/缺失/待定位 | decompile/disasm/xrefs | ... |
| `<file-2>` | `<name>@<addr>` | ... | ... | ... |
| `<file-3>` | `<name>@<addr>` | ... | ... | ... |
| `<file-4>` | `<name>@<addr>` | ... | ... | ... |

## 可用工具

| 工具 | 用途 |
|------|------|
| `mcp__idalib__idb_open` / `mcp__idalib__idb_list` | 打开配套 `.i64`、获取或枚举独立 `session_id` |
| `mcp__idalib__server_health` | 核对会话对应的 `module`、`input_path`、imagebase、缓存与 Hex-Rays 状态 |
| `mcp__idalib__decompile` | 在指定 `database` 中反编译函数地址 → 伪代码 |
| `mcp__idalib__find` | 在指定 `database` 中搜索字符串/立即数/引用（字符串仅 ASCII/UTF-8） |
| `mcp__idalib__find_bytes` | 搜索原始字节模式；用于 UTF-16LE/UTF-32LE 字面量和机器码模式 |
| `mcp__idalib__xrefs_to` | 查找地址的交叉引用（参数名是 `addrs`，**不是** `address`；接受单字符串或数组）|
| `mcp__idalib__disasm` | 获取指定地址的反汇编代码 |
| `mcp__idalib__list_funcs` / `lookup_funcs` | 列出或解析匹配的函数 |
| `mcp__idalib__get_bytes` | 读取指定地址的原始字节 |
| `mcp__idalib__idb_save` | 保存对指定 IDB 的类型、名称和注释修正 |

这些工具由 Codex 原生暴露。先以本轮实际工具清单为准，不要沿用旧文档缓存或根据插件展示名猜工具名。`mcp__idalib__idb_open` 返回的 `session_id` 是后续调用的 `database`；每个 IDB 工具调用都必须显式传入它，不存在隐式“当前 IDB”。若当前工具清单中没有 `mcp__idalib__*`，停止取证并报告，禁止用 Python MCP 客户端、手工 JSON-RPC、IDA CLI 或其它旁路代替原生工具调用。

## 常见工作流

### 1. 通过字符串引用查找函数

```
对 reference/binaries/ 的每个文件重复以下步骤：

步骤 1：打开配套 IDB，保存返回的 session_id
  mcp__idalib__idb_open
    input_path="<absolute path to matching .i64>"
    preferred_session_id="<platform_arch>"

已有会话时可先枚举；不要猜测或复用其它任务中的 session_id
  mcp__idalib__idb_list

步骤 2：核对会话对应的二进制输入
  mcp__idalib__server_health  database="<session_id>"

步骤 3：搜索函数使用的已知字符串
  mcp__idalib__find
    database="<session_id>"  type="string"  targets=["the string"]

步骤 4：查找引用该字符串的代码（参数名 addrs，可批量传数组）
  mcp__idalib__xrefs_to
    database="<session_id>"  addrs=["0xADDRESS"]
  # 一次查多个：addrs=["0xA", "0xB", "0xC"]；单个也可 addrs="0xADDRESS"

步骤 5：反编译引用函数并写入四文件证据表
  mcp__idalib__decompile
    database="<session_id>"  addr="0xFUNC_ADDR"
```

### 2. 搜索 UTF-16 字符串

`mcp__idalib__find` 配合 `type: "string"` 仅能找到 ASCII/UTF-8。
对于 UTF-16 字符串（在 KiriKiri 中非常常见——所有 `TJS_W(...)` 字面量），
改用 `/ida-search-string` skill：把查询词编码成 UTF-16LE/UTF-32LE 字节模式，
通过原生 `mcp__idalib__find_bytes` 搜索，再用 `xrefs_to` 追踪引用。

### 3. 追踪 NCB 类注册

参考二进制中的 NCB 类通常通过以下逻辑链注册。具体函数名和地址必须在四个文件中分别定位：

```
模块注册函数
  ├── addConstant(cls, L"ConstantName", value, flags)
  ├── NCB_REGISTER_SUBCLASS(L"SubClassName", flag)
  │     └── 注册成员（方法、属性）
  └── NCB_REGISTER_CLASS(L"ClassName", flag)
        └── addMember(cls, L"methodName", funcPtr, ...)
```

要查找某个类的注册：
1. 在四个文件中分别搜索类名字符串（如 "Player"、"SeparateLayerAdaptor"）
2. 分别查找该字符串的交叉引用
3. 定位各文件中的 NCB 注册函数
4. 反编译并交叉核对所有注册成员；名字或绑定目标不一致时逐文件记录

### 4. 理解函数签名

先确认每个参考二进制的处理器、位数和 ABI，禁止把一个文件的寄存器约定、对象偏移或 helper 地址套到其它文件。

对于 ARM64 目标：
- `a1` 通常对应第一个参数（常见为 `this` 或类指针）
- 返回值通常在 `x0`（整数/指针）或 `v0`/`d0`（浮点/双精度）
- `__ldaxr`/`__stlxr` 常见于原子操作（引用计数、线程安全）

helper 的语义必须根据每个文件中的调用链、字符串和数据流分别确认。即使四个函数作用相同，它们的地址和反编译形态也可能不同。

### 5. 识别 TJS 属性/方法访问

反编译代码中的 TJS 对象成员访问看起来像：
```c
// PropGet: obj->PropGet(flags, L"propertyName", hint, &result, obj)
(**(func_ptr**)(vtable + PROP_GET_SLOT_OFFSET))(obj, flags, wide_string, hint, &result, obj);

// PropSet: obj->PropSet(flags, L"propertyName", hint, &value, obj)
(**(func_ptr**)(vtable + PROP_SET_SLOT_OFFSET))(obj, flags, wide_string, hint, &value, obj);

// FuncCall: obj->FuncCall(flags, L"methodName", hint, &result, argc, argv, obj)
(**(func_ptr**)(vtable + FUNC_CALL_SLOT_OFFSET))(obj, flags, wide_string, hint, &result, argc, argv, obj);
```

这里表达的是虚表槽位语义，不是公共字节偏移。arm64 与 armv7 的指针宽度不同，必须在四个文件中分别确认实际 offset。

### 6. 识别图像/资源处理

对于 PSB/MTN 资源处理：
- raw-node 资源 helper 通常返回借用指针并写出大小；若二进制没有保留精确源码名，必须使用 `_guess` 后缀，并在四个文件中分别建立映射
- `TVPReverseRGB`、"RGBA8"、"A8L8" 可作为颜色格式路径的搜索锚点
- 必须逐文件确认解压、palette 展开与 R/B 交换发生在哪一层；不得把一个二进制中的处理阶段当作四文件共同结论

### 7. 重命名已确认的函数

**核心原则：命名权威是四个参考二进制自身保留的名字证据。** NCB 注册字符串、
RTTI/typeinfo、导出符号和字符串常量优先于本地代码；本地代码只能作交叉参照，
不能反向证明二进制里的源码名。四个文件间的名字证据冲突时必须保留冲突并继续追踪，不能选择最接近本地代码的名字。二进制和本地都没有精确名字证据时必须加
`_guess` 后缀，禁止仅从行为猜名字。

**何时重命名：**
- 二进制中存在精确的注册字符串、RTTI/typeinfo 或导出符号
- 或二进制没有名字信号，但本地标识符与调用链、参数和行为多向一致；这种情况仍需记录本地只是交叉参照

**何时不要重命名（或加 `_guess` 后缀）：**
- 仅从反编译行为推断名称（如把 `StartProcess` 猜成 `Process`）
- 仅基于单一线索推测（如一个字符串引用）
- 该函数可能是内联/合并的变体
- 你不确定它是精确的函数还是它的包装器

**命名约定：**
- NCB 注册：`ClassName_ncb_register`、`ClassName_ncb_members`
- NCB 基础设施：`ncb_addMember`、`ncb_addConstant`、`ncb_classInit`
- TJS 运行时：`tTJSVariant_Release`、`ttstr_createFromWide`、`ttstr_c_str`
- 模块级别：`modulename_entry`、`modulename_static_init`
- 类方法：`ClassName_MethodName`（必须与本地代码中 `Class::Method` 一致）

**示例：**
```
mcp__idalib__rename  database="<session_id>"  batch={
  "func": [
    {"addr": "0xFUNC_ADDR", "name": "motionplayer_ncb_register"},
    {"addr": "0xHELPER_ADDR", "name": "tTJSVariant_Release"}
  ]
}
```

提交前如果想先验证，在 `batch` 内使用 `"dry_run": true`。该重命名只作用于当前 IDB；其它三个文件必须在各自数据库中独立确认和执行。

## 基础设施锚点函数（必须逐文件定位）

业务函数用 `lookup_funcs` / `list_funcs` 查询。以下逻辑角色在反编译中高频出现，但名称和地址都不能跨二进制复用：

| 逻辑角色 | 识别依据 |
|----------|----------|
| `ncb_addMember` / `ncb_registerMember` | 成员名字符串、函数指针、flags 和类对象调用链 |
| `ncb_addConstant` | 常量名字符串、值和类对象调用链 |
| `ncb_createFuncWrapper` | 包装原生函数指针并交给成员注册链 |
| `ncb_classInit` | 初始化 `tTJSNativeClass` 及类注册结构 |
| `tTJSVariant_Release` | variant 引用计数递减与对象释放路径 |
| `ttstr_createFromWide` / `ttstr_c_str` | 宽字符串构造与数据指针访问模式 |
| `PSBRawNode_GetResource_guess` | raw-node 资源借用指针 + 写出大小；精确源码名未知时保留 `_guess` |

## 技巧

- 如果 `decompile` 在某地址失败，按当前二进制的架构检查相邻地址和函数序言；不要假定所有文件都使用 ARM64 `SUB SP` 序言。
- NCB 模块加载（`LoadModule`）不区分大小写（查找前转为小写）。
- 类似 `strcmp(v57, "RGBA8")` 的字符串比较表示格式相关的代码路径——反编译完整函数以理解所有分支。
- 追踪调用链时，在一个二进制内逐个反编译链中的每个函数并标注功能，再映射到其余三个文件；不要在未标明文件时混用调用链节点。
- 对函数地址使用 `xrefs_to` 查找当前二进制内的调用者（向上追踪）。
- 复杂查询优先组合 `find`、`find_bytes`、`search_text`、`analyze_function`、`read_struct` 等原生 `mcp__idalib__*` 工具；当前接口不提供旧版 `py_eval`。
