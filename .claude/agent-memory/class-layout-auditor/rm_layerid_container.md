---
name: rm-layerid-container
description: motion::ResourceManager layer-id allocator 容器选型权威 — std::set<uint>+counter,无 name maps,requireLayerIdForName 是本地发明
metadata:
  type: project
---

motion::ResourceManager 的 layer-id 分配器在 libkrkr2.so 的权威实现。

**Why:** P3-B (2026-06-05) 容器选型审计;之前 port 用 unordered_set + 两个 name<->id map,全部偏离 binary。

**How to apply:** RM layer-id 相关改动以此为准,勿再引入 name-keyed 路径。

- ctor `sub_6A88CC@0x6A88CC`:layer-id 容器 = `std::_Rb_tree<unsigned int,_Identity,std::less>` (即 std::set<unsigned int>) 对象@+168 / header@+176;next-id 计数器@+216 起始 1 (写 `0x100000001` = +216=1,+220=1)。ctor 末尾 `operator new(0x28)` 插入 id 0 (node+32=0) 预占。
- `requireLayerId@0x6AB694` 无参:`while(set 含 *cnt) ++*cnt; set.insert(*cnt); r=*cnt; *cnt=r+1; return r;`。
- `releaseLayerId@0x6AB750(id)`:`set.erase(id)`,**无 name 联动**,返回 erase 计数。
- clearCache `Motion_ResourceManager_clearCache@0x6A8438`:只清 +72 layer-list (释放各 Layer image via vtable+112 + free 节点),**不碰 set/counter**。
- binary **无 name<->id 映射**。`"requireLayerId"` UTF-16LE 仅 1 处@0x14D9014;`"releaseLayerId"`@0x14D9032;`"ForName"` 后缀全 binary 0 命中 → `requireLayerIdForName` 是纯本地发明。
- NCB 注册 `Motion_ResourceManager_ncb_registerMembers@0x6AB8BC`:绑 requireLayerId + releaseLayerId。
- 3 个 native 调用站点全经 TJS dispatch FuncCall (vtable+16),numparams=0:buildNodeTree@0x6B4A6C (写 node+16 layerId1)、emitRenderItem@0x6C4E28 (item+20 latch + item+424 id,**已分配则不重分配**)、RenderMotionFrame@0x6DE738 (写 +0x1A8)。release 经 resetAndReleaseNodes@0x6B56F8。
- port:ResourceManager.h `std::set<tjs_int> usedLayerIds` + `nextLayerId=1`;Player::requireLayerId(name) fallback 走无参 RM::requireLayerId。
- **未对齐(已知 defer)**:(a) ctor 未预占 id 0;(b) port clearCache/unloadAll 仍清 usedLayerIds+重置 counter (binary clearCache 不碰);(c) render 侧 reuse-by-name → allocate-fresh+item latch;(d) 调用方未经 dispatch FuncCall(L"requireLayerId")。
