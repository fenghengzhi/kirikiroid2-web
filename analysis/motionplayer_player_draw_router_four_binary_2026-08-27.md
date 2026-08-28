# Player.draw 顶层路由与 owner 边界（四参考二进制，2026-08-27）

## 1. endpoint 与 fresh 完整取证

| 端 | `Player::draw(tTJSVariant)` | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6D3398` | 371 | — |
| Android armv7 | `0x597864` | 293 | — |
| iOS arm64 | `0x100123C84` | 270 | — |
| iOS armv7 | `0x122F28` | 423 | `0x123368`, 146 instructions |

四端均已 fresh full decompile，并分页读取全部 1357 条 `draw` body 指令。iOS armv7
另有独立 SjLj cleanup dispatcher，亦已 fresh decompile 并读取全部 146 条指令。四端
入口、六组直接 helper 已统一命名；入口和 prepare call site 已注释、bookmark，四个
IDB 均已保存。

四端体积差异主要来自 32/64 位 Variant/STL ABI、AArch64 对 SLA 双树旋转的内联、
iOS armv7 SjLj 状态机，以及 shared D3D adaptor 的平台字段差异，不是不同的顶层
算法。

## 2. 共同源代码结构

四端共同控制流可归约为：

```cpp
void Player::draw(tTJSVariant target) {
    auto *d3d = D3DAdaptor::GetNativeInstance(
        target.AsObjectNoAddRef(), false);
    if (d3d) {
        useD3D = true;
        renderToD3DAdaptor(d3d);
        return;
    }

    auto *sla = SeparateLayerAdaptor::GetNativeInstance(
        target.AsObjectNoAddRef(), false);
    if (sla) {
        renderToSeparateLayerAdaptor(sla);
        return;
    }

    PreparedRenderItemList mainList;
    PreparedRenderItemList auxList;
    if (!prepareRenderItems(mainList, auxList)) return;

    if (useD3D) {
        // 参考实现把此块内联在 draw；本地按已恢复结构抽成 helper。
        renderViaSharedD3DAdaptor(target, mainList);
        return;
    }

    applyPreparedRenderItemProjection(mainList);
    renderToCanvas(tTJSVariant(target), mainList, auxList);
    updateLayerAfterDraw(target);
}
```

关键顺序在四端完全一致：D3D class-ID probe、sticky byte 写入、直接 D3D；第二次
Object 转换、SLA class-ID probe、直接 SLA；两条空 pointer-vector 构造、prepare；
sticky shared-D3D 或普通 projection/canvas/post-draw。没有进入 `draw` 后的 lazy
load，也没有任何日志、motion path 查询、TJS 栈回溯或 trace scope。

## 3. target 类型与 NCB class-ID 边界

两个 adaptor probe 各自重新检查 Variant type tag 是否为 Object。四端在 type tag
不是 1 时均调用严格转换 helper：

- Android arm64：`0x6D3434`、`0x6D349C`；
- Android armv7：`0x5978BA`、`0x5978FA`；
- iOS arm64：`0x100123D20`、`0x100123D88`；
- iOS armv7：`0x122FDC`、`0x123034`。

所以 Void、Integer、String 等不是“无 target 的成功 no-op”，而是在第一个 D3D
probe 前抛正常 Variant-to-Object 异常。Object-null 则不同：转换成功，两个
`NativeInstanceSupport(TJS_NIS_GETINSTANCE, classId, scratch)` probe 都因 raw dispatch
为空而 miss，随后仍构造两个 list 并调用 `prepareRenderItems`。

每个 probe 使用 borrowed raw dispatch 和 borrowed native pointer，不增加 target 的
引用计数；`NativeInstanceSupport` 的负 HRESULT、空 adaptor、空 native payload 都是
普通 miss，不抛 `Invalid instance type`，因为 `err=false`。D3D 与 SLA class ID 的
先后次序可观察：一个同时暴露多 class ID 的 dispatch 总是先走 D3D。

## 4. `useD3D` 的提交时机与三类路由

直接 D3D 命中后，四端先把 Player 的 sticky byte 写成 1，再调用直接 D3D helper：

| 端 | sticky byte offset | direct D3D helper | direct SLA helper |
|---|---:|---:|---:|
| Android arm64 | `+0x38D` | `0x6D2F70` | `0x6D2A38` |
| Android armv7 | `+0x275` | `0x59761C` | `0x597328` |
| iOS arm64 | `+0x31D` | `0x100123844` | `0x1001233C8` |
| iOS armv7 | `+0x235` | `0x122AAC` | `0x12257C` |

helper 抛异常或内部 prepare 返回 false 都不会回滚 sticky byte。后续非 D3D target
也不会隐式清零；公开 `useD3D` setter 才会覆盖它。直接 D3D/SLA 两路发生在顶层
list 构造之前，它们各自在自己的 helper 内准备和投影 render items。

普通 Object target 在 prepare 成功后才检查 sticky byte：

- false：projection → 按值复制 target 给 `renderToCanvas` → 销毁该临时 Variant →
  用原始 target 调用 post-draw；
- true：确保进程级 shared D3D adaptor 存在，然后选择实际 target、设置 size/visible、
  渲染并 capture。SLA target 在这个分支会再次按 class ID 解析，并执行 tree pass、
  ordinal 0 target 保留、retired tree invalidation 和 clear。

shared adaptor 的平台对象大小分别为 `0x68/0x40/0x50/0x34`；这是后端字段布局差异。
四端均在后续 SLA 解析、target 属性调用或 capture 之前发布全局指针，因此中途异常
不会撤销全局 adaptor。

## 5. prepare gate 与容器生命周期

顶层 `draw` 没有独立 `hasMotionContent()` 短路。它总是先 default-construct
`mainList`、`auxList`，再调用 `prepareRenderItems`；当前本地 prepare helper 自己以
motion-content type tag 决定 false，因此正常空内容的外部结果相同，但额外前置 gate
会改变可插桩调用序列、异常点和源代码结构，已删除。

两个 list 都是 pointer-vector：vector 只拥有连续指针缓冲区，不拥有指针指向的
PreparedRenderItem。正常 return 会释放各自缓冲区；普通路线的 by-value target
临时值在 post-draw 之前销毁。Android arm64 在函数尾内联了各 Variant/dispatch
owner 与两条 vector 的 landing pads；iOS armv7 的 `0x123368` 按 SjLj call-site 0..26
销毁仍 live 的 Variant、临时 Object dispatch owner和可能已分配的 shared adaptor
构造内存，最后无条件释放 aux/main vector storage 并 resume unwind。析构期间再次
抛出的 case 转入 terminate。

本切片闭合顶层 owner 顺序与 list cleanup 形状；六个深 helper 内部每个图层、纹理、
stencil 和 TJS 属性调用的逐 call-site EH ledger 仍由各自渲染切片负责。

## 6. 本地差异与修改

原本 `PlayerDrawDispatch.cpp` 有三项非参考行为：

1. 以 `target.Type() == tvtObject ? ... : nullptr` 把非 Object 静默当作无 target；
2. `paramObj == nullptr` 时提前返回，使 Object-null 不进入 prepare；
3. 在路由器中构造 motion path、stack trace、logo trace 与 headless draw scope，并在
   多个分支写 trace 状态；另有独立 `hasMotionContent()` 前置 gate。

本轮已把函数恢复为四端共同控制流：两次严格 `AsObjectNoAddRef`、D3D/SLA 两路、
两 list prepare、sticky shared-D3D 或普通三调用；同时移除 draw 的全部 trace/log/
stack sidecar 和不再需要的 include。`setDrawAffineTranslateMatrix` 先前已按独立四端
切片去除相同类别的 trace，本文件现在不再依赖 `MotionTraceWeb.h`/`tjsDebug.h`。

## 7. 测试与验证状态

测试在 `tests/unit-tests/plugins/motionplayer-dll.cpp` 的既有 typed `draw`/`useD3D`
case 上扩展：

- direct D3D 后用 Void 调用必须抛 `eTJSError`，且 sticky byte 不回滚；
- Object-null 不抛，两个 class-ID probe miss 后由空 motion prepare 返回 false；
- typed method 的 Integer 必需参数在 result 已清为 Void 后抛转换异常；
- 既有 receiver 优先级、argc 下界、surplus argv 不转换、by-value target 不被替换、
  direct D3D sticky 路由继续保留。

`git diff --check` 与覆盖表结构校验通过。正式 CMake/unit/Web build 仍因本机没有
CMake、Ninja 和 Emscripten，且不存在既有 build/out 目录而无法执行。

本覆盖项仅标记 `Player::draw` 顶层协调器为 `IMPLEMENTED`。下一步应依次闭合
`renderToD3DAdaptor`、`renderToSeparateLayerAdaptor`、shared-D3D inline/helper、
`prepareRenderItems`、projection、canvas 和 post-draw 深层实现；不能用本项替代这些
callee 的四端审计。
