# Player ordinary post-draw `assignImages`（四参考二进制，2026-08-27）

## 1. 入口与完整取证

| 端 | `updateLayerAfterDraw` | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6CBBB8` | 88 | — |
| Android armv7 | `0x59327C` | 57 | — |
| iOS arm64 | `0x10011E6CC` | 59 | — |
| iOS armv7 | `0x11CF20` | 90 | `0x11D014`, 37 instructions |

四端 294 条主函数指令与四份 fresh decompile已完整读取；iOS armv7的五状态、37 条
SjLj cleanup也已完整读取。入口与 cleanup已命名/注释/bookmark，四个 IDB均已保存。

四端共同体很小且稳定：相邻 flag发布、needs早退、materialize调用、primary internal
Layer Object owner、一个 target Variant参数、`assignImages`调用和逆序清理。armv7的体积
主要来自 SjLj register/unregister与寄存器保存，不代表额外业务路径。

## 2. ready/needs发布与早退

函数第一项业务动作是无条件 `ready = needs`：

- Android arm64：Player `+0x264 <- +0x265`；
- Android armv7：`+0x19C <- +0x19D`；
- iOS arm64：`+0x1F4 <- +0x1F5`；
- iOS armv7：`+0x15C <- +0x15D`。

复制后只检查刚读出的 needs值；为零立即返回。早退路径不读取或转换 target、不访问
primary internal Layer，也不调用 materializer。函数在所有路径都不清除 producer needs。

## 3. materialize与 target边界

needs非零时，第一个调用是 `Player_materializeInternalRenderLayers(target)`。与 accurate SLA
post-draw不同，ordinary函数在这个调用前不复制 target、不调用 AsObject，也不建立任何
target Object-only owner。

这个差异带来可观察边界：

- primary internal Layer为 Void时，materializer内部会严格转换 target并读取 window；
- primary已非 Void时，materializer立即返回，ordinary post-draw本身不会再严格转换
  target；非 Object target仍可作为普通 Variant参数进入后续 `assignImages`；
- needs为零时更是完全不触碰 target。

本地原有 headless probe会在 flag发布前解析 target Layer、做 raw probe，并由 RAII析构在
离开时再次探测，因而改变上述异常和引用时序。本轮已删除这套 sidecar。

## 4. primary internal owner

materializer返回后，函数从 persistent primary `_internalRenderLayer`复制一个临时 Variant，
对临时值做严格 Object转换并取得 Object-only AddRef，随即析构临时 Variant。留下的 raw
internal owner同时作为 `assignImages` receiver和 objthis。

没有检查 work Layer、internal Layer validity或 Object-null，也没有借用 persistent Variant
内部的 raw dispatch。若 materializer留下非 Object/typed-null residue，行为停留在 ncbind
严格转换或后续 raw dispatch边界，不添加 Web侧恢复。

## 5. `assignImages(target)` ABI形状

调用前按值复制一份完整 target Variant作为唯一参数，因此保留 object与objthis可能不同
的 closure关系。调用固定为：

```text
internal.FuncCall(
    flags=0,
    member="assignImages",
    hint=process-global assignImages hint,
    result=null,
    argc=1,
    argv=[complete target Variant],
    objthis=internal)
```

HRESULT被忽略，不转换成 bool，也不改变 ready/needs。正常返回先析构 target参数 Variant，
再 Release internal Object-only owner。Android arm64 landing pad与 iOS armv7五状态 cleanup
确认异常只清理已经构造的参数/owner，然后继续 unwind；不会撤销 materializer产生的
persistent Layer状态，也不会回滚 ready发布。

## 6. 本地差异与修改

本地核心控制流和 ABI参数原本已对齐，但函数还包含两组参考中不存在的诊断行为：

- `KRKR2_WASMTIME_HEADLESS`下的 target resolve、进入/离开 raw probe、render checkpoint和
  成功后 candidate记录；
- motion-path/logo trace的路径解析、条件日志与成功检查。

这些逻辑会在原版早退/转换边界之外读取 target、motion路径和 Layer状态，也增加额外
owner与析构调用。本轮整段删除。当前函数只剩四端共同的 ready快照、needs早退、
materialize、internal owner和单参数 `assignImages`。

## 7. 验证状态

本项标记 `IMPLEMENTED`。已完成四份 fresh decompile、全部 294 条主函数指令、完整
37 条 armv7 cleanup、本地逐语句/owner/ABI对照、IDB命名/注释/bookmark/save、coverage
12列校验与 `git diff --check`。

正式 CMake/unit/Web build仍因本机缺少 CMake、Ninja、Emscripten且没有既有 build/out
目录而未运行。
