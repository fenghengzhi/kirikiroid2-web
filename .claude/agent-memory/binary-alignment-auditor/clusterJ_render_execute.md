---
name: clusterJ-render-execute
description: 簇J 渲染管线part1(execute/internal/dispatch)审计 — 0x6C4E28/0x6C7440/0x6C6B48/0x6CBCE4/0x6C9CA8 二进制地址↔本地映射 + 2026-06-07 对齐结论
type: project
---

簇J审计(2026-06-07,read-only,IDB已rename+save)。Player draw execute是**双函数双层**管线。

**地址↔语义(全部本对话反编译确认)**:
- 0x6C4E28 Player_emitRenderItem_requireLayer = 预走emitter,在0x6C7440 pre-walk调用(仅!preview)。LoopA(mainList):per leaf item门控item+19(drawFlag),drawable则item+21=1/写clip item+216..228(**float[4]**)/requireLayerId(numparams=0)→item+424+item+20=1/acquireLeafLayerById→leaf item+304/setSize/affineCopy|meshCopy|bezierPatchCopy**直接在leaf层发**。LoopB(groupList):union子clip,建composed item+324(门控item+340),fillRect,子Motion_doAlphaMaskOperation。
- 0x6C7440 Player_renderToCanvas(原_guess,已rename)=非accurate顶层submit。skip门=item+17||item+16||!item+232。setClip(argc4有效viewport/argc0无效)。preview门=preview&&!item+18 skip。opa=item+232(preview则>>1)。blend switch(item+48&0xF):1→14,2/5→15,3→16,4→17,0/default→if(player+1144 clearEnabled||item+264)BUFFERED(v48=2)else DIRECT。BUFFERED:ctx(player+656).bufLayer→affine/mesh/bezierCopy→走item+264祖先链alphaMask→fillRect→v370.operateRect。DIRECT:v370.operateAffine/operateMesh/operateBezierPatch(-0.5,-0.5世界偏移)。**尾部仅setClip(argc0)+release,无Update()**。
- 0x6C6B48 Player_acquireLeafLayerById(原sub_)=SLA(player+760)上Rb_tree,key=item+424;create时Layer.absolute=node+160+node+164(x+y)然后**++node+164**,hitThreshold=256,缓存map-node+40。
- 0x6CBCE4 Player_acquireComposedLayerById(原误名Player_acquireLayerById,本次重命名澄清)=Rb_tree key=item+56(=node+20);absolute=x+y(无++),仅accurate-SLA composed子用。
- 0x6C9CA8 Player_renderAccurateSLA(原sub_)=ogl_accurate_render(byte_1AB84F4)路径,建持久SLA子Layer树(assignImages/drawMeshFrame/drawBezierPatchMeshFrame/drawLine/setPos/type/visible/opacity)。与0x6C7440单层合成完全不同架构。

**关键纠正**:item+304=LEAF层(acquireLeafLayerById,key item+424);item+324=COMPOSED层(key item+56)。item+264=parent指针,在0x6C7440同时驱动 buffered路由(item+264!=0强制buffered)+alphaMask祖先walk。clipRect item+216..228是**float[4]非int**。

**本地偏差(PlayerRenderExecute.cpp)**:
- 🔧 J1/J7:本地把leaf-copy折进executeLayerRenderCommands的buildItemOutput递归,未复刻0x6C4E28独立pre-walk pass+leaf/composed双层Rb_tree(用_renderLayerStates近似)。延续cluster-I build/execute+STL分歧,现确认更结构性(分歧在独立函数pass非仅独立loop)。
- ❌ J4:本地executeLayerRenderCommands尾部调renderLayer->Update(false)(line~1293),二进制0x6C7440无Update(Update在0x6CE7D8/SLA)。
- ❌ J6:本地ensureLeafItemLayer absolute=_nextLayerAbsolute++(纯计数器),二进制=node.x+y。
- ⚠ J5:本地clipRect std::array<int,4>,二进制float[4]。
- ⚠ J9:本地opacity未在preview下减半,二进制opa>>=1。
- 🔧 J8:accurate-SLA仅HEADLESS下diagnostics checkpoint(renderAccurateSlaPostDrawCandidateLike_0x6C9CA8),真实持久SLA树架构缺失。
- ✅对齐:blend map(resolveBlendOperationModeLike),direct门(shouldUseDirectRenderPathLike: !clearEnabled&&visibleAncestorIndex<0&&nibble(0或>5)),mesh array走TJS dispatch(buildMeshPointTJSArrayLike,非std::vector),alphaMask(applyMotionAlphaMaskLike itemFlags1/2/5/6),drawCompat三路分发。

**港湾**:本地无`// PLATFORM_BOUNDARY:`标注;KRKR2_WASMTIME_HEADLESS guard等价diagnostics边界(trace/SNAP*/probe)。但J8 accurate架构藏在该guard内属"缺失"非边界。

报告:analysis/audit_motionplayer_2026-06-07/clusterJ_render_execute.md
