# MotionPlayer draw-entry diagnostic isolation（四参考二进制，2026-08-14）

## 1. 结论

四个当前参考二进制的 `Player_draw_guess`（旧 recovery 名
`Player_drawCompat_guess`）是脚本直接注册的纯 native render dispatcher：解析传入
Variant/object，依次识别 D3DAdaptor、SeparateLayerAdaptor 与普通 target，管理 prepared
render-item vectors，执行 render/post-draw，并按 native unwind 规则析构临时 owner。四份
完整函数均为 0 string references，也没有 motion context/path、TJS stack trace、fmt/logger
或 diagnostic-check 调用。

六参数 `Player_setDrawAffineTranslateMatrix_guess` 同样是 0 string references、无 callee 的
字段写入/identity-compare/Boolean-return body。它不读取 motion lookup context。

本地旧 Web sidecar 在这两个 render entry 上泄漏到默认路径：

- setter 每次无条件 `matchedMotionPath()`；
- draw 每次无条件 `matchedMotionPath()` 和 `shortTJSStackTrace()`；
- draw 各路由无条件调用 `logoChainTraceCheck`。该 helper 的 expected/actual/root-cause 参数
  是 `const std::string&`，因此长字符串字面量会在进入 helper 内部 enable check 之前先
  构造临时 `std::string`；
- 一个 `if(false)` 的 matrix-log lambda 和四个 call site 是完全不可达的旧诊断 scratch。

本轮把所有 path/stack/check argument materialization 移入 cached path-specific opt-in gate，
并删除 dead matrix-log lambda。默认 draw/setter 不再新增 Variant conversion、stack walk、
字符串分配或相应异常点；render 路由和 owner 生命周期没有变化。

## 2. 四端函数映射

| 目标 | `Player_draw_guess` | size | affine setter | size / 布局 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D3398` | `0x5D8` | shared Player chunk `0x6D22F4` | remote chunk owned by wrapper func |
| Android armv7 | `0x597864` | `0x2D2` | `0x596C40` | `0x92` |
| iOS arm64 | `0x100123C84` | `0x45C` | `0x100122D54` | `0x6C` |
| iOS armv7 | `0x122F28` | `0x440` | `0x121D90` | `0x88` |

Android arm64 的 Player setter 从 `0x6D22F4` 起，是 IDA 归到
`EmotePlayer_setDrawAffineTranslateMatrix_guess @ 0x67F2C8` 的远端共享 chunk：EmotePlayer
入口先取 inner Player 再跳进该块，Player NCB 回调直接指向该块。这个函数组织差异已由
先前 affine 专项确认，不是新的两个 setter。

本轮 fresh decompile 对上述八个 native bodies 逐一执行 keyword scan：没有
`log/trace/stack/motionPath/ToString/AsString/fmt/fprintf/narrow` 表达式；fresh function
analysis 的 string reference count 全部为 0。

## 3. draw dispatcher 的 native 数据流

四端共同高层结构仍为：

```text
targetObject = targetVariant is object ? targetVariant.object : null
if targetObject == null:
    return

if targetObject unwraps as D3DAdaptor:
    player.d3dDrawMode = true
    renderToD3DAdaptor(adaptor)
    return

if targetObject unwraps as SeparateLayerAdaptor:
    renderToSeparateLayerAdaptor(targetObject)
    return

if player has no motion content:
    return

mainList = vector<PreparedRenderItem*>()
auxList = vector<PreparedRenderItem*>()
if !prepareRenderItems(mainList, auxList):
    destroy vectors
    return

if player.d3dDrawMode:
    render through process-shared D3DAdaptor
    destroy vectors
    return

applyPreparedRenderItemProjection(mainList)
targetCopy = copy(targetVariant)
renderToCanvas(targetCopy, mainList, auxList)
destroy targetCopy
updateLayerAfterDraw(original targetVariant)
destroy vectors
```

四端对 vector layout/cleanup、Variant helper/inlining 和 shared-D3D creation 的 ABI 表现
不同，但没有任一分支读取 motion path 或构造错误描述字符串。错误/false 分支是普通控制
流，不带 native diagnostic payload。

本轮没有改动 native 路由顺序、`_d3dDrawMode` 的 sticky publication、目标 Variant 的 copy
时机、render result 与 post-draw 的既有边界，也没有给 native owner cleanup 增加 RAII。

## 4. affine setter 的 native 数据流

四端 setter 继续严格执行：

```text
store m11, m12, m21, m22 as double
store float(m14), float(m24)
nonIdentity =
    m11 != 1.0 || m21 != 0.0 || m12 != 0.0 ||
    m22 != 1.0 || m14 != 0.0 || m24 != 0.0
store nonIdentity byte
return nonIdentity
```

identity comparison 使用六个原始 double，translation 的 float 窄化只用于存储。signed zero
仍为 identity，任一 NaN 使比较结果 nonidentity。setter 在任何字段 store 前后都没有
path conversion、logger 或 string allocation。

本地现在只在 `logoChainTraceEnabled()` 为 true 时才物化 path 并调用 trace template；
trace 默认关闭时，字段写入/比较/return 之间不再插入其他可抛操作。

## 5. C++ eager-argument 问题

`logoChainTraceLogf` template 自己会先 gate，之后才在函数体内 `fmt::format`；但调用表达式
里的 `shortTJSStackTrace()` 必须在进入 template 前求值，所以内部 gate 无法避免 stack
walk。

`logoChainTraceCheck` 更直接：接口接收三个 `const std::string&` 文本。如下调用即使 helper
第一行返回，也必须先把长字面量/ternary C string 转为 temporary `std::string`：

```cpp
logoChainTraceCheck(path, stage, func, time,
                    "a long expected route",
                    ok ? "success" : "failure",
                    ok,
                    "a long likely root cause");
```

因此正确 sidecar 边界必须位于调用表达式外：

```cpp
if(traceForPath) {
    logoChainTraceCheck(...);
}
```

仅依赖 helper 内部的 enable check 不足以复原 native allocation/exception behavior。

## 6. 修复后的 Web sidecar 控制域

draw 入口现在：

```cpp
const bool traceEnabled = logoChainTraceEnabled();
std::string motionPath;
bool traceForPath = false;
if(traceEnabled) {
    motionPath = matchedMotionPath();
    traceForPath = logoChainTraceEnabledForPath(motionPath);
}
```

随后 stack trace、所有 route logs 和五个 route checks 都在 `traceForPath` block 内。path
filter 只计算一次。Wasmtime 的 `MotionTraceRenderDrawScope` 仍由自己的
`traceRequested()` 控制，不与 logo path sidecar 混合。

原 `logDrawMatrix` lambda 第一条语句恒为 `if(false) return`，所有 call site 都没有任何
可观察效果；本轮将 lambda 与 call sites 一并删除，避免把不可达临时诊断误认为 native
render 阶段。

显式开启 logo trace 时，已有 route/stack/check 输出保持；默认关闭时不读取
`_findMotionContextVariant`、不走 TJS stack、也不构造 check temporary strings。

## 7. IDB 回写

四份 recovery IDB 已在 draw dispatcher 与 affine setter/共享 chunk 添加注释：

- 完整 native function 为 0 string references；
- draw 只含 target routing、prepared-list ownership、render/post-draw 和 cleanup；
- setter 只含六字段 store、exact identity compare、flag publication/return；
- Web diagnostic arguments 必须在构造前 gate。

四份 IDB 已原位保存。

## 8. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerDrawDispatch.cpp`，成功链接
  `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 源码检索确认 `shortTJSStackTrace()` 仅出现在 path-specific gate 内，五处
  `logoChainTraceCheck` 均有调用表达式外 gate，`logDrawMatrix` 与 `if(false)` 无残留；
- setter 的 `matchedMotionPath()` 位于全局 trace gate 内，draw 的 path conversion 与 path
  filter 只在 trace 开启时执行。

既有 motionplayer 测试覆盖 affine identity/nonidentity、draw target routing、D3D/SLA/普通
target 及 render owner 边界；完整 TU 编译和 Web 链接通过。本轮没有伪称运行 Catch2
runtime target，因为当前 preset 仍只提供该完整翻译单元的 syntax gate。
