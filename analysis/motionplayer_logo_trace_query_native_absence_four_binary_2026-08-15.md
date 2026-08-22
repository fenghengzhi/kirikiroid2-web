# MotionPlayer logo-chain Web sidecar / native query absence 四端复核（2026-08-15）

## 1. 目的与结论

`RuntimeSupport.cpp::logoTraceQueryEnabled()` 的 non-Emscripten 注释仍引用旧 Android
`libkrkr2.so` 的单端函数地址、单端命令行 helper 名和两编码扫描。为避免把过时目标
继续当作原版身份，本轮重新对 `reference/binaries/` 的四个当前参考执行三编码搜索，
并 fresh 复核 EmoteObject 构造/load 入口与 Player play/load 入口。

四端共同结论：

- native 参考中不存在 logo-chain trace 或 snapshot 开关的任何对应字符串；
- EmoteObject 构造/load 和 Player play/load 的完整反编译没有 trace、logo、snapshot、
  path formatting、额外 logger 或命令行-query 数据流；
- 本地 logo-chain/snapshot 设施是 Web 调试 sidecar，不是待恢复的 native 插件成员；
- 非 Emscripten 必须恒定返回 false，Web 也只通过显式 global/query 参数 opt-in。

所以运行逻辑保持不变，只把旧单端地址式注释迁移成四端共同、无绝对地址的源码说明。

## 2. 三编码四端排除检索

使用 `ida-search-string` 工作流，对每份 recovery IDB 的全部 segments 同时搜索
ASCII/UTF-8、UTF-16LE 与 UTF-32LE byte pattern。检索词为：

- `tracelogochain`；
- `traceLogoChain`；
- `-tracelogochain`；
- `snaplogo`；
- `logoChain`。

| 目标 | 5 词 × 3 编码结果 |
|---|---:|
| Android arm64 | 0 matches |
| Android armv7 | 0 matches |
| iOS arm64 | 0 matches |
| iOS armv7 | 0 matches |

大小写两种 trace spelling、命令行前导 `-`、snapshot 词和共享 `logoChain` stem 均独立
检索。结果不是依赖 IDA 是否把宽字面量识别成 string：UTF-16LE/UTF-32LE 使用原始 byte
pattern，因此可排除普通 string-list 识别噪声。

## 3. fresh production-body 复核

### 3.1 EmoteObject construct/load

| 目标 | `EmoteObject_init_guess` | size |
|---|---:|---:|
| Android arm64 | `0x67AF8C` | `1632` |
| Android armv7 | `0x5604B8` | `660` |
| iOS arm64 | `0x1001B4984` | `920` |
| iOS armv7 | `0x1B4500` | `926` |

四份完整 decompile 的 code 与 refs 均重新筛查：`trace`、`logo`、`logger`、`logf`、
`printf`、`commandline`、`matchedMotion`、`snap` 全部零匹配。函数只保留
ResourceManager/dispatch owner、Engine 构造与异常 unwind 等 production 生命周期。

### 3.2 Player play/load

| 目标 | `Player_playImpl_guess` |
|---|---:|
| Android arm64 | `0x6AF664` |
| Android armv7 | `0x580158` |
| iOS arm64 | `0x100107540` |
| iOS armv7 | `0x104AE8` |

fresh 完整 decompile/ref scan 同样没有 trace/logo/snapshot/path-format/query refs。函数中的
唯一业务日志仍是 load 失败的 production `motion not found` 路径；不能据此推导出一个
隐藏 diagnostic logger。旧恢复 IDB 已有的 diagnostic-isolation entry comment 也明确
把 portable trace 限制在显式 opt-in gate 后。

这与先前逐函数审计互相印证：

- `analysis/motionplayer_motion_load_pipeline_diagnostic_isolation_four_binary_2026-08-14.md`；
- `analysis/motionplayer_draw_entry_diagnostic_isolation_four_binary_2026-08-14.md`；
- `analysis/motionplayer_player_progress_diagnostic_isolation_four_binary_2026-08-14.md`；
- 其他 render/update diagnostic-isolation 专题。

## 4. 本地 sidecar 的开关与默认路径

本地 Web-only 开关是：

```text
trace:
  window.__KRKR_TRACE_LOGO_CHAIN__
  ?traceLogoChain
  ?trace=logo | logo-chain | 1

snapshot:
  ?snap=1 | logo
  ?trace=snap | logo-snap
```

两个查询结果都由 function-local `static const bool` 缓存，只在首次调用求值。对于
non-Emscripten，查询 helper 编译为恒 false；对于 Web，没有显式 query/global 时也为
false。各 production call site 已在更早纵切中把 path conversion、format 和 diagnostic
argument materialization 移到 enable gate 内，所以默认 Web 路径也不添加 native 不存在的
字符串 owner、format 调用或异常点。

`yuzulogo.mtn` / `m2logo.mtn` 只在 sidecar 已启用后用于缩小诊断目标，不是 native
motion 内容识别分支。

## 5. 注释迁移与 IDB

`RuntimeSupport.cpp` 的 non-Emscripten 注释现只保留共同语义：四个当前 native 参考无
对应 switch/data flow，sidecar 必须 Web-only，native 恒 false。已删除：

- 旧 `libkrkr2.so` 单端身份；
- 旧 EmoteObject 地址与函数大小硬编码；
- 旧单端 command-line helper 名/地址；
- 只声称 UTF-8/UTF-16 而遗漏 UTF-32 的扫描描述。

四份 recovery IDB 在 EmoteObject construct/load 与 Player play/load 入口补充本轮
三编码零命中和无 sidecar 数据流说明，并保存数据库。绝对地址继续只存在于本文/IDB，
不回流到编译源码注释。
