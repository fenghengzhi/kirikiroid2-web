---
name: render-subsystem-fresh-verdict
description: 2026-06-05 fresh-decompile 渲染子系统(draw/render items/targets/alpha-mask)全维裁决; 12原语全经FuncCall(vtbl+16)UTF-16派发✅; setClip半迁移; colorBytes经0x6A7518 CPU bake非draw路径(per-vertex边界再次证伪); alpha-mask shader→CPU平台边界
metadata:
  type: project
---

2026-06-05 对 motionplayer 渲染路径做 fresh-decompile 独立复核(0x6C7440/0x6C4E28/0x6C715C/0x6AF104/0x6A7518/0x6C6B48)。

**地址↔文件映射:**
- draw 主体 sub_6C7440 = renderToCanvasLike_0x6C7440 @PlayerRenderTargets.cpp:1087 (入口) + 执行主体 @PlayerRenderExecute.cpp:1020 + 12原语 dispatch helper @PlayerRenderInternal.cpp:48-289
- emitRenderItem_requireLayer sub_6C4E28 = build loop @PlayerRenderExecute.cpp:59-234 + SeparateLayerAdaptor.cpp resolveLayerNodeLike_0x6C6B48
- vertex builder sub_6C715C = buildMeshPointTJSArrayLike_0x6C715C @PlayerRenderInternal.cpp:171
- alpha-mask sub_6AF104 = applyMotionAlphaMaskLike_0x6AF104 @PlayerRenderInternal.cpp:822
- 4-corner color bake sub_6A7518 = applyPackedCornerTintLike_0x6A7518 @SourceCache.cpp:82 (消费点 :732)
- render target map sub_6C6B48 = NativeSLAOrderedMapLike_0x6C6B48 @SeparateLayerAdaptor.cpp

**核心裁决:**
- TJS-dispatch ✅: binary 12 draw 原语全经 `(*(...)(*_QWORD*vobj+16LL))(L"...")` 即 iTJSDispatch2::FuncCall(vtbl+16)+UTF-16LE 键。本地 callLayer*Like_0x6C7440 全部 FuncCall(0,TJS_W(...)) argc(15/14/11/10/9)逐项对齐。
- 12 原语: operateAffine/affineCopy/operateMesh/operateBezierPatch/meshCopy/bezierPatchCopy/operateRect/fillRect 全 ✅; setClip 🟡半迁移; drawLine/drawMeshFrame/drawBezierPatchFrame/drawBezierPatchMeshFrame ❌(debug overlay,a1+1048||1068门控,logo不走,oracle-inert)。
- colorBytes(node+100) 真实消费点 = sub_6A7518 CPU bilinear bake 进 source bitmap(非 draw 路径; 0x6C7440/0x6C4E28 全文无 node+96..112 读取)。divisor 128(GPU half-alpha)/255 本地对齐。**per-vertex color 平台边界论据再次证伪**(color 不走 GPU 顶点色,而是 draw 前 CPU bake)。
- sub_6C715C 仅 append 顶点位置(x+off,y+off),无颜色 ✅。

**Open 项(均 oracle-inert,非平台边界,应实装):**
- D-1(P2) setClip 半迁移: binary FuncCall(primaryLayer/v370,L"setClip",4或0 args); 本地 renderLayer->SetClip()/ResetClip() 原生 C++(PlayerRenderExecute.cpp:778/782/1293)。同一 TJS Layer 对象上 dispatch 与原生混用。renderLayer 是真实 TJS Layer 可派发,非平台边界。
- D-2(P3) alpha-mask 非GPU边缘提交: binary 非GPU分支边缘走 FuncCall("fillRect")+FuncCall("update"); 本地 FillMask 直写。CPU 像素核已对齐。
- D-3(P3) debug overlay 4 原语缺失(drawLine 等)。
- D-4(P3) mesh points 多一层 std::vector<float> 中转再转 TJS Array。

**平台边界(明确技术原因):** alpha-mask GPU shader 分支(0x6AF104, 5种 GLProgram: AddAlphaMask/AlphaMask/AlphaMaskRev/AlphaMaskThreshold{,Fill,Crop})——本地无 GLProgramObject 自定义 fragment shader 提交能力(binary 经 sub_84B160 编 GLSL+vtbl+160 draw)。CPU 像素核语义对齐非GPU分支,可接受。

render target sub_6C6B48 ✅: red-black tree map(ordinal)+active/retired 双链 swap+sub_6DCB2C(恒返1)+CreateNew"Layer"+PropSet absolute/hitThreshold=256。本地 try_emplace/find/erase/swapWith 对齐。

---
**2026-06-06 fresh 复核 RM/SourceCache/layer-id 容器子系统(P3-A/P3-B 提交 94f205f/3084723/7516a64/2bd16ca/56610cb/c46dd99)。地址↔文件:**
- RM findSource sub_6AAB3C = ResourceManager::findSource @ResourceManager.cpp:231; loadSource sub_6A7BA8 / SourceCache registrar sub_6A85A8(3员:loadSource/clearCache/bufLayer)
- RM ctor sub_6A88CC = make_shared<State>(); requireLayerId sub_6AB694 / releaseLayerId sub_6AB750 = ResourceManager.cpp:302/322; RM dispatch-in NCB_CONSTRUCTOR((tTJSVariant)) @main.cpp:144
- color resolver sub_6C1B70 = SourceCache loadRenderSourceByName 路径(2 分支: assignImages+bake vs FuncCall("loadSource",player+656))

**裁决(7点):**
1. draw 原语全经 vtbl+16 FuncCall+UTF-16 ✅(同上,本次再确认 setClip/setSize/operate*/copy*/fillRect/operateRect 全派发;item+280 选 0=affine/1=bezier/2=mesh, item+48&0xF 选 bufLayer 合成; 4-corner color 经 vtbl+56 PropSetByNum idx0..3 from item+168/172/176/180 onto player+716)
2. anchor blend 源 ✅: binary `*(v17+536*v19+364)`(v19=*(node+1392) slot index, 读点 0x6c0a80/0x6c0aac)= per-slot;本地 an.activeSlot().blendMode(PlayerUpdateAnchor.cpp:151)读 active-slot 非 node 级缓存,已对齐(早期 memory 说"single-cache gap"已被 activeSlot() 修正)。注:偏移是 +364 非旧注释的 +44。qword_14D7C50 字节再验={255.0(idx0),128.0(idx1)},`[(blend&0xF0)==16]`✅
3. per-vertex→单 scalar bake = 正当平台边界 ✅: 消费点 sub_6A7518(@0x6c218c 经 0x6C1B70 assignImages 分支)= per-PIXEL bilinear MULTIPLY(divisor (blend&0xF0)==16?128:255),sub_6C715C 仅 push (x,y) type-5。本地 applyPackedCornerTintLike_0x6A7518(SourceCache.cpp:82)忠实。技术原因:本地渲染栈 color 仅单 scalar RGBA 无 per-vertex 属性。
4. RM HashMap A ✅容器选型对齐: ctor sub_6A88CC a1+96=_M_next_bkt(10)/a1+88=operator new(8*bkt)=libstdc++ unordered_map;FNV functor(ttstr+68 缓存)。本地 std::unordered_map<ttstr,V,ttstr_hash,ttstr_equal>=loadedModules ✅(findSource 仍 keyed by path 而非 motion-name,M9 已记)
5. SourceCache intrusive list 🟡: binary this+72 = 真 intrusive 双向链(sub_146359C splice/sub_14635B8 unlink, 节点 inline 96B);本地 std::list<Entry>(SourceCache.cpp:113, splice-to-front LRU)。容器选型为 std::list 非 intrusive,语义对齐但实现型号偏离(低危,可后续换 intrusive)
6. RM ownership dispatch-in ✅: NCB_CONSTRUCTOR((tTJSVariant)) 单参 @main.cpp:144;layer-id 经 rm->FuncCall(L"requireLayerId",numparams=0)@PlayerResource.cpp:106 dispatch-in ✅
7. layer-id 无 name 签名 ✅: requireLayerId() 无参(sub_6AB694 单参=this), releaseLayerId(id) 擦除 std::set;本地 usedLayerIds=std::set<tjs_int>(RB-tree 选型对齐),NodeTree.cpp:105 dispatchRequireLayerId() 无 by-name reuse(已删 requireLayerIdForName, 0 hits 已证)。clearCache 不清 layer-id ✅, unloadAll 清 set 但不重置 nextId ✅

**本次新增 open 偏差(均 oracle-inert,非平台边界):**
- C-1(P3) requireLayerId 算法偏离: binary sub_6AB694 = 纯单调计数器(*(a1+216))插入 RB-tree 后返回再 +1(不搜空位);本地 requireLayerId()(ResourceManager.cpp:302-315)循环搜 usedLayerIds 跳占用槽找最低空 id。无 release 交错时等价,有 release 后语义分叉(binary 永远单调递增不复用 released id, 本地会复用)。**实为真实算法分歧**,应改为纯计数器以匹配 binary 单调性。
- C-2(P3) SourceCache 缓存键粒度: binary loadSource 按 (key,blendMode) 匹配单条目, 命中后 color 变则原地 UPDATE color[4](node+68..80)+re-splice;本地 findEntry 按 (key|resolvedKey, blendMode, packedColors) 三元组匹配 → 每 color 一条目而非原地改。容器拓扑分叉(N entries/(key,blend,color) vs 1 mutable entry/(key,blend)),低危。
- C-3(P3) ObjSource/findSource 路径 oracle-inert(仅 TJS NCB 可达, 单测走 Player::findSource), 但实现已忠实 dict facade(operator new(0x18) 语义)。
