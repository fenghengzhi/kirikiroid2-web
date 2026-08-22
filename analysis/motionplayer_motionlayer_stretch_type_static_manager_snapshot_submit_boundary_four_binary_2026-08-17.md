# MotionLayer `StretchType` 静态参数、双 manager 快照与提交边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致把 MotionLayer mesh 与 Bezier patch 的最终 triangle 提交收敛到一个
共享 helper。两个外层 renderer 先取得第一份 render-manager 快照并从它选择 render method；
只有 method 非空时，才取得目标主图并以显式 null manager 参数调用共享 helper。helper 将
null 解析为第二份 manager 快照，用一个函数局部、C++ guard 保护的 4-byte 静态参数 ID 设置
`StretchType`，随后由捕获第二份 manager 的 callback 调用 `OperateTriangles`。

本地旧实现有两层真实偏差：

1. 用 `static manager pointer + static int ID` 按 manager identity 重新枚举参数；参考实现只有
   一个 ID 和一个 ABI guard，不缓存 manager identity，首次成功初始化后的 ID 是粘滞的；
2. 本地共享 helper 还合并了 source/target Layer 解包、第一份 manager、method 选择和
   `update` 分派；参考 helper 只负责第二份 manager、参数设置与 backend submit，backend
   Boolean 返回给外层决定是否 `update`。

本轮同时修复数据拓扑和调用链边界。命名继续保留 `_guess`，因为四库均已 strip，无法恢复
原始 C++ 标识符。

## 四端函数映射

| 目标 | 共享 submit helper | mesh renderer | Bezier renderer |
|---|---:|---:|---:|
| Android arm64 | `0x69CD80` | `0x69E0D0` | `0x69E630` |
| Android armv7 | `0x576288` | `0x576E08` | `0x577184` |
| iOS arm64 | `0x1000FA650` | `0x1000FB660` | `0x1000FBBB8` |
| iOS armv7 | `0xF77CC` | `0xF86B0` | `0xF8C00` |

每个参考库对 submit helper 都恰好有上述两个语义 caller：普通 mesh 与 Bezier patch。四端共
八个 caller 都把 helper 的 manager 实参置空；不存在第三条 MotionLayer submit caller。

## 静态数据与字符串映射

| 目标 | 4-byte 参数 ID | C++ guard | guard 大小 | ASCII `StretchType` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AB5244` | `0x1AB5248` | 8 bytes | `0x14D6B7A` |
| Android armv7 | `0x1111778` | `0x111177C` | 4 bytes | `0x5763C0` |
| iOS arm64 | `0x101B6970C` | `0x101B69710` | 8 bytes | `0x1014F22CF` |
| iOS armv7 | `0x187D43C` | `0x187D440` | 4 bytes | `0x1380DEB` |

四库都只有一个 4-byte parameter-ID object 和紧邻的 ABI guard。64 位 guard 为 8 bytes，
32 位 guard 为 4 bytes；没有第二个 pointer-sized static，也没有任何可解释为 manager identity
的 companion slot。data xref 数量会受 ADRP/ADD、literal pool、异常清理与 PC-relative
materialization 影响，不能把多条机器级 xref 误计成多个语义 static。

Android armv7 的 ASCII literal 位于 text/literal-pool 区域，另外还有无关的同字节命中；本轮
只接受由共享 helper 的 `EnumParameterID` 调用实际引用的 identity，不能用字符串文本相同来
合并无关 consumer。

## 共同调用链

四端可归一为以下源码级结构：

```text
renderMesh / renderBezierPatch:
    sourceTexture = sourceLayer.mainImage.texture
    methodManager = TVPGetRenderManager()              // 第一份快照
    method = methodManager.GetRenderMethod(opacity, holdAlpha, bitmapMethod)
    if method == null:
        return                                         // 不取 target main image

    targetBitmap = targetLayer.mainImage
    submitted = sharedSubmit(
        manager = null, targetBitmap, clip,
        sourceTexture, sourceRect, geometry, divisions,
        method, stretchType)
    if submitted:
        owner.update(full clip)
    draw method-specific debug overlays               // submitted=false 仍执行

sharedSubmit(manager, ...):
    if manager == null:
        manager = TVPGetRenderManager()                // 第二份快照

    static int stretchTypeId =
        manager.EnumParameterID("StretchType")
    manager.SetParameterInt(
        stretchTypeId, unsigned-short(stretchType))

    return buildAndSubmitMeshTriangles(
        ..., callback capturing manager/method/target/clip)

callback:
    reference = targetBitmap.GetTexture()
    target = targetBitmap.GetTextureForRender(method.IsBlendTarget(), clip)
    manager.OperateTriangles(method, triangleCount, target, reference, ...)
```

外层选择 method 与 helper 执行提交不是同一份 manager snapshot。若全局 manager 在两次获取
之间变化，method 仍来自第一份，而参数设置和 `OperateTriangles` 发生在第二份；四端没有
identity check、method 重新选择或 manager 一致性修复。这个看似危险的跨快照组合是参考实现
的真实边界，不能在 portable 源码里自动“优化”为单 snapshot。

## 函数局部静态初始化

parameter ID 的初始化遵循标准 C++ 动态局部静态协议：

```text
if guard not initialized:
    if __cxa_guard_acquire(guard):
        try:
            id = submitManager.EnumParameterID("StretchType")
            __cxa_guard_release(guard)
        catch:
            __cxa_guard_abort(guard)
            rethrow
```

因此：

- method 为空时不会进入 helper，也不会初始化 ID；
- 首次 `EnumParameterID` 抛出时 guard 被 abort，下一次调用可以重试；
- 首次成功枚举所得到的 ID 对进程/模块余生粘滞，不随后续 manager identity 改变；
- `SetParameterInt` 位于 guard release 之后；它抛出不会撤销 guard，也不会让 ID 重新枚举；
- ID 初始化只由 mesh/Bezier 共用 helper 完成，不是两个 renderer 各持一份 static。

部分反编译输出会把 guard 周围的局部 manager SSA 合并显示成“manager 不同时可覆盖 ID”的
伪分支。八个真实 caller 都传 null，而 helper 把解析出的同一 manager 写入后续局部并用于
initializer；四端 data layout 也不存在 identity static。因此可达 native 路径支持的是标准
单-static/单-guard 语义，而不是旧本地实现的 per-manager cache。

## 参数与返回值边界

`stretchType` 在调用 `SetParameterInt` 前先转换为 `unsigned short`，再作为 integer 参数传递。
其边界等价于保留低 16 bits：负数和大于 65535 的输入都不会原样传给 manager。该转换发生
在 geometry backend 之前，所以即使 backend 随后返回 false 或抛出，manager parameter 的
写入副作用已经发生。

共享 helper 的 Boolean 返回值是 geometry backend 的真实返回，不是“method 是否存在”：

- method null：外层不调用 helper、不设置参数、不取 target main image、不 update、不画 debug；
- method non-null、backend false：不 update，但仍画 mesh debug；Bezier 路径还会画 control
  frame debug；
- method non-null、backend true：先用完整原始 clip 调用 owner `update`，再画 debug；
- backend/manager/texture 调用抛出：异常直接展开，不 update，也不继续 debug；
- `update` 抛出：triangle 提交已发生，但后续 debug 被跳过；
- debug 抛出：提交与可能的 update 均已提交，不回滚。

backend 使用一个局部 `computedBounds` 副本完成 triangle 构建；外层 `update` 使用原始完整
clip，而不是 backend 修改后的 bounds。这与 V172 已闭合的 shared update member hint 一致。

## callback 与对象生命周期

Android arm64 `0x6E1BF4` 和 Android armv7 `0x5A2858` 的 callable body 都从 closure 捕获
区取出 helper 的 manager 并调用 `OperateTriangles`，直接证明 callback 使用第二份 snapshot。
iOS arm64 `0x1000FA5F0` 与 iOS armv7 `0xF7780` 是通用 `std::function` trampoline，不应误命名
为 callable body；iOS helper 的 closure 布局、manager 保存/回取和完整数据流仍与 Android
两端一致。

method、两个 manager、source texture、target bitmap 都是 borrowed raw pointers：此路径
不 AddRef manager/method，也不建立 manager identity owner。callback closure 只在同步 backend
调用范围内持有这些 raw pointer/capture，并在正常返回或异常展开时销毁；它不把 manager 或
Layer 发布到持久容器。source/target Layer 的脚本/native owner 仍由外层调用栈现有 accessor 与
Variant 生命周期维持，参数 ID/guard 只持有 integer state，不拥有任何对象。

callback 每次执行时重新从 target bitmap 获取 reference texture 和 render target。method 的
`IsBlendTarget()` 仍在 callback 中查询；若 callback 重入或多 triangle batch 重复调用，这些
texture 查询也会重复，不能提前缓存到 helper 外层而改变可观察顺序。

## portable 源码修复

`MotionLayerExtensions.cpp` 完成两组修改：

1. 删除 `stretchTypeManager` 与 `stretchTypeId = -1` 的 manager-identity cache，改为一个动态
   初始化的 `static int stretchTypeParameterId_guess`；
2. 将 `submitLayerMesh_guess` 收窄为参考共享 helper 的职责：接收 target bitmap、source
   texture、method 与可空 manager，内部解析第二份 snapshot、设置参数并原样返回 backend
   Boolean；两个外层 renderer 各自负责第一份 manager/method、method-null gate、target main
   image、conditional update 和 unconditional-on-method debug。

两个调用点都显式传 `nullptr`，compiled-source 注释只记录语义，不含任何参考库绝对地址。
定向源码审计结果：

- manager-identity static：0；
- 动态 parameter-ID static：1；
- helper token：3（一个定义、两个调用）；
- null-manager caller：2；
- helper 到两个 renderer 的 manager snapshot：3（两个第一快照、一个 helper 第二快照）；
- 外层 update 分派：2。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 四端共享 helper 命名为 `MotionLayer_submitLayerMesh_guess`；
- 四端 parameter ID 建为 size-4 `int` data item，guard 按 64/32 位 ABI 分别建为 8/4 bytes；
- parameter ID、guard、helper 与两个 caller 每库写入语义注释，共 20 处；
- ID、guard、helper 每库建立 bookmark，共 12 个；
- Android 两端各 force-recompile 三个核心函数；iOS 两端另含 guard EH cleanup，共重编译
  14 个函数；
- 12 个核心函数 readback 均显示 V178 名称、注释、guard/static 与双 snapshot 边界；
- 四份 recovery IDB 原位保存成功。

绝对地址只保留在本分析文件与 recovery IDB，不写回可编译 C++ 注释。

## 验证

- 结构重构后 ordinary Emscripten syntax-only：通过；
- 结构重构后 `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,616 bytes，539 imports / 69 exports；
- Headless wasm：84,995,757 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 相较 V177，两份 wasm 都增加 1,207 bytes；import/export ABI 表面不变；
- 两个 build tree 的 CTest 均报告 `No tests were found`。本轮没有虚报 runtime CTest；相关
  unit-test TU 仅由 ordinary/headless syntax-only 覆盖；
- 定向结构计数全部符合上述预期；
- `git diff --check` 无 whitespace error，只有 dirty worktree 既有 LF/CRLF 提示。

构建仅出现项目既有 `_tss`、pthread memory-growth、JSPI 与 JS-library 警告，没有新增
motionplayer 编译或链接错误。

## 结论边界

本轮闭合的是 MotionLayer mesh/Bezier 两条 renderer 到共享 submit helper 的 manager/static/
update 边界，不把 `StretchType` 推广为整个引擎所有 render helper 的全局参数协议。同名 ASCII
literal、相邻 BSS 或相似 `SetParameterInt` 调用都不足以共享本 static；后续发现其他 consumer
时仍需以四端函数 caller、data identity、guard 与 closure capture 共同消歧。
