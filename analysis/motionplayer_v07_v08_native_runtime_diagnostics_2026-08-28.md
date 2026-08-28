# Motionplayer MP-V07 / MP-V08 原生运行与诊断非回归（2026-08-28）

## 最终结论

MP-V07 与 MP-V08 已完成。所有相关产品 translation unit 和测试目标均重新编译、链接并以
ad-hoc 签名执行；最终不是沿用修复前结果。

```text
motionplayer-dll:
  test cases: 357 | 356 passed | 1 expected headless-OpenGL skip
  declared-order assertions: 23259 | 23259 passed
  random seed 2862347432 assertions: 23260 | 23260 passed

motionplayer-ttstr-hash-test:
  test cases: 23 | 23 passed
  assertions: 150 | 150 passed
```

唯一 skip 是必须由完整应用提供 live OpenGL context 的 integration case；headless D3D
cache/target/脚本表面路径通过未注册 test seam 使用内存 texture 和无操作 composite manager 覆盖，
不把缺少窗口上下文伪装成产品失败。

最终复跑还主动验证了测试独立性：`__Private_Motion_GLLayer uses private ClassID only` 单独执行时
曾因复用前序 case 留下的全局 TJS script dispatch 而崩溃。该 case 现自行持有
`ScopedCoreScriptEngine`；单独执行 73 assertions 全过，declared order 与原失败随机 seed
`2862347432` 均全过。此修复只改变测试 fixture 生命周期，不改变产品语义。

## 三个原失败的裁决

原生 IDA transport 已恢复；四个配套 IDB 均重新打开、健康检查、fresh decompile/disassembly、
注释、书签、保存并正常关闭。复核结果与修复如下：

1. DrawDeviceD3D `IsVisible()`：四端都走 Variant 的完整真值转换。本地 non-const overload
   resolution 曾误选 Object conversion，现显式调用 `value.operator bool()`。
2. EmotePlayer Boolean trigger：四端 typed wrapper 都把缺参/Void 转为 false 后调用 one-way
   trigger；本地通用 NCBind 本来正确。失败来自 selector fixture 未建立 metadata owner，现先执行
   `resetMetadataState()`，未增加产品 null fallback，也撤回了诊断期的全局 converter。
3. `Motion.EmotePlayer` typed Factory：四端要求一个脚本可见 arg0，同时允许单个 Void sentinel 并
   忽略 surplus args。Factory 增加无名 `iTJSDispatch2 *` receiver formal，恢复 NCBind 的参数计数，
   native allocation 和 rmDispatch 数据流不变。

详细四端地址与共同控制流见
`analysis/motionplayer_runtime_reconciliation_four_binary_2026-08-28.md`。

## DrawDeviceD3D headless 非回归

`drawvisible` 原失败点通过后，旧 fixture 在 `capture()` 后段触发 SIGSEGV。macOS crash report 栈为：

```text
glGetIntegerv
TVPRenderManager_OpenGL::InitGL
GetD3DRenderManager
capture
```

这定位为 headless OpenGL context 问题，不是 `AssignTexture` 生命周期问题。测试现注入
`HeadlessD3DRenderManager` / exact-size `HeadlessD3DTexture`；override 默认是 null，null 时继续走
原有缓存 named `"opengl"` manager。公开 D3DLayer/D3DImage `Class.CreateNew` 的 non-object 和
Void-plus-surplus 断言也按真实 wrapper 行为校正为 `TJS_E_NOTIMPL`：raw factory 的
`TJS_E_INVALIDTYPE` 会触发 `TJSDefaultFuncCall` property retry，内部 raw contract 未被修改。

定向结果：Boolean property 1 case / 61 assertions，typed Factory 2 cases / 148 assertions，
DrawDeviceD3D 七类表面 1 case / 456 assertions，全部通过。

## 诊断输出与 patch 完整性

最终扫描结果：

- `PRTDIAG`：0；
- `std::cerr/std::clog`、`fprintf(stderr)`、普通 `printf/puts`：0；
- `OutputDebugString/qDebug/NSLog`：0；
- `TVPAddLog`：6。

六个 `TVPAddLog` 位于 `PlayerUpdateLayersInternal.h:794`、`PlayerTimeline.cpp:129`、
`MotionBezierPatch.h:108`、`EmoteEngine.cpp:2936`、`EmoteEngine.cpp:3074` 和 `PlayerCore.cpp:636`；
它们是参考或未修改的既有 error path，不是 recovery-only trace。`git diff --check` 最终返回零。

## 可重复命令

```sh
out/macos/debug/tests/unit-tests/plugins/motionplayer-dll --reporter compact
out/macos/debug/tests/unit-tests/plugins/motionplayer-ttstr-hash-test --reporter compact

rg -n 'PRTDIAG|std::(cerr|clog)|fprintf\\s*\\(\\s*stderr|\\b(printf|puts)\\s*\\(|OutputDebugString|qDebug|NSLog' \
  cpp/plugins/motionplayer cpp/plugins/DrawDeviceD3D.cpp
rg -n 'TVPAddLog' cpp/plugins/motionplayer cpp/plugins/DrawDeviceD3D.cpp
git diff --check
```
