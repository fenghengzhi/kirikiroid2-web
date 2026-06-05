---
name: layerid-dispatch-routing-d
description: layer-id alloc/release 经 Player+992 RM dispatch FuncCall 的 binary 权威证据 + 维度(d) 审计结论
metadata:
  type: project
---

layer-id 分配/释放在 libkrkr2.so 全部经 **Player+992 RM dispatch** 的 `FuncCall(slot16)`，**无任何 native 直调**。2026-06-05 审计 dev/motion 维度③(d) 改造为 ✅完全对齐（含 2 处良性偏差，非 bug）。

**Why:** 这是 CLAUDE.md「复刻 TJS dispatch 调用链而非 shortcut 到 native」的核心案例；layer-id 站点固定且证据明确，未来若改 render reuse(c)/RM ownership 会再次触碰这些站点。

**How to apply:** 审计任何 layer-id / requireLayerId / releaseLayerId 路径时直接复用以下证据，不必重新反编译。

## require 站点（3 处，FuncCall vtable slot 16 = `*(vtable)+16`）
- buildNodeTree_recursive@0x6B4A6C: 0x6b4d24→node+16(layerId1), 0x6b4dbc→node+20(layerId2)，每节点两次。dispatch 源 a1+124(qword)=byte 992。
- emitRenderItem@0x6C4E28: 0x6c51cc→item+424；门控 (drawFlag19 && drawable && item+20==0)，set item+20=1；dispatch 源 a1+992。
- RenderMotionFrame@0x6DE738: 0x6df158→v87+424；同 item+20==0 门控；dispatch 源 a7+992。
- shape: `FuncCall(0, L"requireLayerId", hint, &result, numparams=0, params=NULL, objthis=dispatch)`；result 经 5-case switch 整数化（= tTJSVariant::AsInteger 内联，default=0）。

## release 站点（resetAndReleaseNodes@0x6B56F8，slot 16）
- 0x6b58b8/0x6b58fc 对 node+16/node+20 各一次；0x6b5954 对 mask+424（若 *(node+1904) 且 *(mask+20)）。
- shape: `FuncCall(0, L"releaseLayerId", hint, result=NULL, numparams=1, {idVar(Type4 int)}, objthis=dispatch)`；dispatch 源 a1+124。

## 字符串（get_bytes 确证 UTF-16LE）
- 0x14D9014 = `"requireLayerId"`；0x14D9032 = `"releaseLayerId"`。

## dispatch 源辨析
- Player+992 = 本地单 `_resourceManager` tTJSVariant（PlayerCore.cpp:144 nativeRM() 同源 unpack via AsObjectNoAddRef）。
- +676/+716 是邻槽（width/height、key dispatch 等），**非** layer-id；layer-id 专用 +992。
- FuncCall 经 NCB（main.cpp:589-590 注册 requireLayerId/releaseLayerId）路由到 native ResourceManager::requireLayerId/releaseLayerId，返回同 id —— dispatch wrap native 是 binary 本身的架构，本地复刻它（不 shortcut）即忠实。

## 良性偏差（非 bug，勿当回归）
- result 整数化：本地 `result.AsInteger()` = binary 5-case switch 的反内联还原（源码层即 `(tjs_int)result`，default=0 一致）。
- render reuse-by-name 主路径仍 native = 独立架构差 (c)，与 (d)「把 require/release 调用点 native→dispatch」正交，不属 (d) 遗漏。

## refcount
- 本地 AsObjectNoAddRef 不加引用；`_resourceManager` variant 在 Player 生命周期持有该 dispatch，同步栈内调用无 UAF。binary 的 copy+AddRef/Release 配对是「拷 variant 到局部」的编译产物模式，本地直接借用成员 variant 不拷贝故无需配对。
