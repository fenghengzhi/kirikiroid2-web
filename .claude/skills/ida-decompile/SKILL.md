---
name: ida-decompile
description: >
  Reverse-engineer libkrkr2.so (Android kirikiroid2) using IDA Pro MCP tools.
  Use this skill whenever the user asks to check how libkrkr2.so implements
  something, compare our web port's C++ code against the original binary,
  find function addresses, trace call chains, or understand NCB class
  registrations. Also use it proactively when fixing bugs that require
  understanding the original Android implementation. Triggers on mentions
  of: libkrkr2.so, IDA, decompile, reverse engineer, original implementation,
  Android kirikiroid2 binary, "how does the original do X", NCB registration
  in binary, finding functions in the .so file.
---

# IDA Pro MCP — libkrkr2.so Reverse Engineering

This skill provides patterns for reverse-engineering the Android kirikiroid2
binary (`libkrkr2.so`) using IDA Pro MCP tools.

## Available Tools

| Tool | Purpose |
|------|---------|
| `mcp__ida-pro-mcp__decompile` | Decompile function at address → pseudocode |
| `mcp__ida-pro-mcp__find` | Search strings/immediates (ASCII/UTF-8 only) |
| `mcp__ida-pro-mcp__xrefs_to` | Find cross-references to an address |
| `mcp__ida-pro-mcp__py_eval` | Run IDAPython code (for complex queries) |
| `mcp__ida-pro-mcp__disasm` | Get disassembly at address |
| `mcp__ida-pro-mcp__list_funcs` | List functions matching a pattern |
| `mcp__ida-pro-mcp__get_bytes` | Read raw bytes at address |

All tools require fetching via `ToolSearch` first (they are deferred tools).

## Common Workflows

### 1. Find a function by string reference

```
Step 1: Search for a known string the function uses
  mcp__ida-pro-mcp__find  type="string"  targets=["the string"]

Step 2: Find what code references that string
  mcp__ida-pro-mcp__xrefs_to  addrs="0xADDRESS"

Step 3: Decompile the referencing function
  mcp__ida-pro-mcp__decompile  addr="0xFUNC_ADDR"
```

### 2. Search for UTF-16 strings

`mcp__ida-pro-mcp__find` with `type: "string"` only finds ASCII/UTF-8.
For UTF-16 strings (very common in KiriKiri — all `TJS_W(...)` literals),
use the `/ida-search-string` skill instead, which runs an IDAPython script
that scans both the string list and raw memory.

### 3. Trace NCB class registration

NCB classes in libkrkr2.so are registered via a chain of functions:

```
Module registration function (e.g., sub_6D9B08 for motionplayer.dll)
  ├── sub_6DA28C(cls, L"ConstantName", value, flags)  → addConstant
  ├── sub_6FC6E8(L"SubClassName", flag)                → NCB_REGISTER_SUBCLASS
  │     └── sub_6FC84C → registers members (methods, properties)
  └── sub_6FEEE4(L"ClassName", flag)                   → NCB_REGISTER_CLASS
        └── sub_6FF048 → registers members
              └── sub_9F5AF4(cls, L"methodName", funcPtr, ...) → addMember
```

To find a class's registration:
1. Search for the class name string (e.g., "Player", "SeparateLayerAdaptor")
2. Find xrefs to that string
3. The referencing function is usually the NCB registration function
4. Decompile it to see all registered members

### 4. Understand function signatures

IDA decompilation of ARM64 code uses these conventions:
- `a1` = first argument (usually `this` or class pointer)
- Return values in `x0` (integer) or `v0`/`d0` (float/double)
- `__ldaxr`/`__stlxr` = atomic operations (refcount, thread safety)
- `sub_A13274` = likely `Release()` (reference count decrement)
- `sub_A136C0` = likely string creation from wide string literal
- `sub_A13390` = likely `c_str()` or string data access
- `sub_9F538C` = likely function wrapper creation
- `sub_9F5AF4` = NCB `addMember` (registers method/property on class)

### 5. Identify TJS property/method access

TJS object member access in decompiled code looks like:
```c
// PropGet: obj->PropGet(flags, L"propertyName", hint, &result, obj)
(**(func_ptr**)(vtable + 200))(obj, flags, wide_string, hint, &result, obj);

// PropSet: obj->PropSet(flags, L"propertyName", hint, &value, obj)
(**(func_ptr**)(vtable + 208))(obj, flags, wide_string, hint, &value, obj);

// FuncCall: obj->FuncCall(flags, L"methodName", hint, &result, argc, argv, obj)
(**(func_ptr**)(vtable + 16))(obj, flags, wide_string, hint, &result, argc, argv, obj);
```

### 6. Identify image/resource handling

For PSB/MTN resource handling:
- `sub_5996E4` = get PSB resource data (returns pointer + sets size)
- `TVPReverseRGB` = swap R↔B in RGBA pixel data (for BGRA layer format)
- Resource pixel format is identified by string: "RGBA8", "A8L8"
- Resources are raw pixel data (not RL-compressed) after PSB chunk loading

### 7. Rename confirmed functions

When you can **100% confirm** that a `sub_XXXXXX` corresponds to a known
function in the project, rename it immediately using `mcp__ida-pro-mcp__rename`.
This makes all future decompilation output readable.

**When to rename:**
- The function's string references, call pattern, and behavior exactly match
  a known function in our C++ codebase or a well-known library function
- You have cross-referenced from multiple directions (string refs, callers,
  callees, parameter count) and there is no ambiguity

**When NOT to rename:**
- You're only guessing based on a single clue (e.g., one string reference)
- The function might be an inlined/merged variant
- You're unsure whether it's the exact function or a wrapper around it

**Naming conventions:**
- NCB registration: `ClassName_ncb_register`, `ClassName_ncb_members`
- NCB infrastructure: `ncb_addMember`, `ncb_addConstant`, `ncb_classInit`
- TJS runtime: `tTJSVariant_Release`, `ttstr_createFromWide`, `ttstr_c_str`
- Module-level: `modulename_entry`, `modulename_static_init`
- Class methods: `ClassName_methodName`

**Example:**
```
mcp__ida-pro-mcp__rename  batch={
  "func": [
    {"addr": "0x6D9B08", "name": "motionplayer_ncb_register"},
    {"addr": "0xA13274", "name": "tTJSVariant_Release"}
  ]
}
```

Use `"dry_run": true` first if you want to verify before committing.

## Named Functions (already renamed in IDA)

| Address | Name | Description |
|---------|------|-------------|
| `0x6D9B08` | `motionplayer_ncb_register` | motionplayer.dll NCB module registration |
| `0x6948E8` | `Motion_Player_findSource` | Motion findSource (texture/resource loading) |
| `0x42EB00` | `emoteplayer_static_init` | emoteplayer.dll static init registration |
| `0x682528` | `emoteplayer_entry` | emoteplayer.dll entry (loads motionplayer.dll) |
| `0x54242C` | `ncb_addMember` | NCB addMember |
| `0x52FA58` | `ncb_addConstant` | NCB addConstant |
| `0x6DA28C` | `ncb_addConstant_wrapper` | NCB addConstant wrapper (module-level) |
| `0x9F5AF4` | `ncb_registerMember` | NCB registerMember (method/property on class) |
| `0x9F538C` | `ncb_createFuncWrapper` | NCB function wrapper creation |
| `0x9F5858` | `ncb_classInit` | NCB class init (tTJSNativeClass setup) |
| `0xA13274` | `tTJSVariant_Release` | Reference count decrement / Release |
| `0xA136C0` | `ttstr_createFromWide` | Create ttstr from wide string literal |
| `0xA13390` | `ttstr_c_str` | Get C string pointer from ttstr |
| `0x5996E4` | `PSB_getResourceData` | Get PSB resource data pointer + size |
| `0x695D04` | `Motion_createTextureFromPixels` | Create texture object from raw pixels |
| `0x6FEEE4` | `SeparateLayerAdaptor_ncb_register` | SLA NCB class registration |
| `0x6FF048` | `SeparateLayerAdaptor_ncb_members` | SLA NCB member registration |
| `0x6ABF98` | `SeparateLayerAdaptor_registerProps` | SLA property registration |
| `0x6FC6E8` | `Motion_Point_ncb_register` | Motion.Point NCB registration |
| `0x6FDD04` | `Motion_Player_ncb_register` | Motion.Player NCB registration |
| `0x6FE124` | `Motion_SourceCache_ncb_register` | Motion.SourceCache NCB registration |
| `0x6FE610` | `Motion_ObjSource_ncb_register` | Motion.ObjSource NCB registration |
| `0x6FEAC4` | `Motion_ResourceManager_ncb_register` | Motion.ResourceManager NCB registration |
| `0x6FF2F8` | `Motion_D3DAdaptor_ncb_register` | Motion.D3DAdaptor NCB registration |

## Tips

- If `decompile` fails at an address, try nearby addresses (IDA may have
  merged functions). Look for `SUB SP` prologues at `loc_` labels.
- NCB module loading (`LoadModule`) is case-insensitive (lowercases before lookup).
- String comparisons like `strcmp(v57, "RGBA8")` indicate format-dependent
  code paths — decompile the full function to understand all branches.
- When tracing a call chain, decompile each function in the chain and
  annotate what it does before moving to the next. This avoids losing context.
- Use `xrefs_to` on function addresses to find callers (trace upward).
- Use `py_eval` for batch operations like scanning all strings or dumping
  struct layouts.
