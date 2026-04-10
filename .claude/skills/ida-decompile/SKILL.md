---
name: ida-decompile
description: >
  使用 IDA Pro MCP 工具逆向工程 libkrkr2.so（Android kirikiroid2）。
  当用户要求检查 libkrkr2.so 如何实现某功能、将 Web 移植版的 C++ 代码与原始二进制对比、
  查找函数地址、追踪调用链，或理解 NCB 类注册时使用此 skill。
  在修复需要理解原始 Android 实现的 bug 时也应主动使用。
  触发关键词：libkrkr2.so、IDA、反编译、逆向工程、原始实现、
  Android kirikiroid2 二进制、"原版是怎么做的"、二进制中的 NCB 注册、
  在 .so 文件中查找函数。
---

# IDA Pro MCP — libkrkr2.so 逆向工程

此 skill 提供使用 IDA Pro MCP 工具逆向工程 Android kirikiroid2
二进制文件（`libkrkr2.so`）的模式。

## 可用工具

| 工具 | 用途 |
|------|------|
| `mcp__ida-pro-mcp__decompile` | 反编译指定地址的函数 → 伪代码 |
| `mcp__ida-pro-mcp__find` | 搜索字符串/立即数（仅 ASCII/UTF-8） |
| `mcp__ida-pro-mcp__xrefs_to` | 查找某地址的交叉引用 |
| `mcp__ida-pro-mcp__py_eval` | 运行 IDAPython 代码（用于复杂查询） |
| `mcp__ida-pro-mcp__disasm` | 获取指定地址的反汇编代码 |
| `mcp__ida-pro-mcp__list_funcs` | 列出匹配模式的函数 |
| `mcp__ida-pro-mcp__get_bytes` | 读取指定地址的原始字节 |

所有工具需要先通过 `ToolSearch` 获取（它们是延迟加载的工具）。

## 常见工作流

### 1. 通过字符串引用查找函数

```
步骤 1：搜索函数使用的已知字符串
  mcp__ida-pro-mcp__find  type="string"  targets=["the string"]

步骤 2：查找引用该字符串的代码
  mcp__ida-pro-mcp__xrefs_to  addrs="0xADDRESS"

步骤 3：反编译引用函数
  mcp__ida-pro-mcp__decompile  addr="0xFUNC_ADDR"
```

### 2. 搜索 UTF-16 字符串

`mcp__ida-pro-mcp__find` 配合 `type: "string"` 仅能找到 ASCII/UTF-8。
对于 UTF-16 字符串（在 KiriKiri 中非常常见——所有 `TJS_W(...)` 字面量），
改用 `/ida-search-string` skill，它会运行 IDAPython 脚本
同时扫描字符串列表和原始内存。

### 3. 追踪 NCB 类注册

libkrkr2.so 中的 NCB 类通过一系列函数链注册：

```
模块注册函数（如 sub_6D9B08 用于 motionplayer.dll）
  ├── sub_6DA28C(cls, L"ConstantName", value, flags)  → addConstant
  ├── sub_6FC6E8(L"SubClassName", flag)                → NCB_REGISTER_SUBCLASS
  │     └── sub_6FC84C → 注册成员（方法、属性）
  └── sub_6FEEE4(L"ClassName", flag)                   → NCB_REGISTER_CLASS
        └── sub_6FF048 → 注册成员
              └── sub_9F5AF4(cls, L"methodName", funcPtr, ...) → addMember
```

要查找某个类的注册：
1. 搜索类名字符串（如 "Player"、"SeparateLayerAdaptor"）
2. 查找该字符串的交叉引用
3. 引用函数通常就是 NCB 注册函数
4. 反编译它以查看所有注册的成员

### 4. 理解函数签名

IDA 反编译 ARM64 代码使用以下约定：
- `a1` = 第一个参数（通常是 `this` 或类指针）
- 返回值在 `x0`（整数）或 `v0`/`d0`（浮点/双精度）
- `__ldaxr`/`__stlxr` = 原子操作（引用计数、线程安全）
- `sub_A13274` = 可能是 `Release()`（引用计数递减）
- `sub_A136C0` = 可能是从宽字符串字面量创建字符串
- `sub_A13390` = 可能是 `c_str()` 或字符串数据访问
- `sub_9F538C` = 可能是函数包装器创建
- `sub_9F5AF4` = NCB `addMember`（在类上注册方法/属性）

### 5. 识别 TJS 属性/方法访问

反编译代码中的 TJS 对象成员访问看起来像：
```c
// PropGet: obj->PropGet(flags, L"propertyName", hint, &result, obj)
(**(func_ptr**)(vtable + 200))(obj, flags, wide_string, hint, &result, obj);

// PropSet: obj->PropSet(flags, L"propertyName", hint, &value, obj)
(**(func_ptr**)(vtable + 208))(obj, flags, wide_string, hint, &value, obj);

// FuncCall: obj->FuncCall(flags, L"methodName", hint, &result, argc, argv, obj)
(**(func_ptr**)(vtable + 16))(obj, flags, wide_string, hint, &result, argc, argv, obj);
```

### 6. 识别图像/资源处理

对于 PSB/MTN 资源处理：
- `sub_5996E4` = 获取 PSB 资源数据（返回指针 + 设置大小）
- `TVPReverseRGB` = 在 RGBA 像素数据中交换 R↔B（用于 BGRA 图层格式）
- 资源像素格式通过字符串识别："RGBA8"、"A8L8"
- PSB 块加载后，资源是原始像素数据（非 RL 压缩）

### 7. 重命名已确认的函数

**核心原则：重命名必须以本地代码为依据。** 禁止从二进制行为推断命名。
必须先 grep 本地项目找到对应的类名::方法名，再用 `ClassName_MethodName` 格式重命名。
无法在本地代码中找到对应标识符时，必须加 `_guess` 后缀。

**何时重命名：**
- 已在本地 C++ 代码中通过 grep 找到精确匹配的类名和方法名
- 从多个方向交叉验证过（字符串引用、调用者、被调用者、参数数量）且没有歧义

**何时不要重命名（或加 `_guess` 后缀）：**
- 仅从反编译行为推断名称，未在本地代码中验证（如把 `StartProcess` 猜成 `Process`）
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
mcp__ida-pro-mcp__rename  batch={
  "func": [
    {"addr": "0x6D9B08", "name": "motionplayer_ncb_register"},
    {"addr": "0xA13274", "name": "tTJSVariant_Release"}
  ]
}
```

提交前如果想先验证，使用 `"dry_run": true`。

## 基础设施锚点函数（反编译中高频出现）

业务函数已在 IDA 数据库中命名，用 `lookup_funcs` / `list_funcs` 查询。以下仅列出反编译时识别模式用的基础设施函数：

| 地址 | 名称 | 说明 |
|------|------|------|
| `0x54242C` | `ncb_addMember` | NCB addMember |
| `0x52FA58` | `ncb_addConstant` | NCB addConstant |
| `0x9F5AF4` | `ncb_registerMember` | NCB registerMember（类上的方法/属性） |
| `0x9F538C` | `ncb_createFuncWrapper` | NCB 函数包装器创建 |
| `0x9F5858` | `ncb_classInit` | NCB 类初始化（tTJSNativeClass 设置） |
| `0xA13274` | `tTJSVariant_Release` | 引用计数递减 / Release |
| `0xA136C0` | `ttstr_createFromWide` | 从宽字符串字面量创建 ttstr |
| `0xA13390` | `ttstr_c_str` | 从 ttstr 获取 C 字符串指针 |
| `0x5996E4` | `PSB_getResourceData` | 获取 PSB 资源数据指针 + 大小 |

## 技巧

- 如果 `decompile` 在某地址失败，尝试相邻地址（IDA 可能合并了函数）。在 `loc_` 标签处查找 `SUB SP` 序言。
- NCB 模块加载（`LoadModule`）不区分大小写（查找前转为小写）。
- 类似 `strcmp(v57, "RGBA8")` 的字符串比较表示格式相关的代码路径——反编译完整函数以理解所有分支。
- 追踪调用链时，逐个反编译链中的每个函数并标注其功能，然后再处理下一个。这可以避免丢失上下文。
- 对函数地址使用 `xrefs_to` 查找调用者（向上追踪）。
- 使用 `py_eval` 进行批量操作，如扫描所有字符串或转储结构体布局。
