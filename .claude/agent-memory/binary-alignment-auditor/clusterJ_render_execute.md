---
name: clusterJ-render-execute
description: 簇J 渲染管线part1(execute/internal/dispatch)审计 — 0x6C4E28/0x6C7440/0x6C6B48/0x6CBCE4/0x6C9CA8 二进制地址↔本地映射 + 2026-06-07 对齐结论
type: project
---

簇J审计(2026-06-07,read-only,IDB已rename+save)。Player draw execute是**双函数双层**管线。

**地址↔语义(全部本对话反编译确认)**:
- 0x6C4E28 Player_emitRenderItem_requireLayer = 预走 emitter，仅在 `!priorDraw` 调用。Loop A 以 camera∩paintBox 得 float clip，valid viewport 才 floor/ceil narrow；acquire out-byte gate leaf refresh，neutralColor→Real setSize→copy。Loop B 以 group paintBox 为 seed union child paintBox；camera tuple只做 empty gate，viewport tuple驱动 composed Real W/H、argc5 fillRect 和最终 clip；child mask 在 float 差后 FCVTZS。
- 0x6C7440 Player_renderToCanvas(原_guess,已rename)=非accurate顶层submit。skip门=item+17||item+16||!item+232。setClip(argc4有效viewport/argc0无效)。priorDraw门=`player+1096 && !item+18` skip。opa=item+232(priorDraw则>>1)。blend switch(item+48&0xF):1→14,2/5→15,3→16,4→17,0/default→if(player+1144 completionType||item+264)BUFFERED(v48=2)else DIRECT。BUFFERED:ctx(player+656).bufLayer→affine/mesh/bezierCopy→走item+264祖先链alphaMask→fillRect→v370.operateRect。DIRECT:v370.operateAffine/operateMesh/operateBezierPatch(-0.5,-0.5世界偏移)。**尾部仅setClip(argc0)+release,无Update()**。
- 0x6C6B48 Player_acquireLeafLayerById(原sub_)=SLA(player+760)上Rb_tree,key=item+424;create时Layer.absolute=SLA+160 base + SLA+164 sequence，然后**++SLA+164**,hitThreshold=256,缓存map-node+40。
- 0x6CBCE4 Player_acquireComposedLayerById(原误名Player_acquireLayerById,本次重命名澄清)=Rb_tree key=item+56(=node+20);absolute=x+y(无++),仅accurate-SLA composed子用。
- 0x6C9CA8 Player_renderAccurateSLA(原sub_)=ogl_accurate_render(byte_1AB84F4)路径,建持久SLA子Layer树(assignImages/drawMeshFrame/drawBezierPatchMeshFrame/drawLine/setPos/type/visible/opacity)。与0x6C7440单层合成完全不同架构。

**关键纠正**:item+304=LEAF层(acquireLeafLayerById,key item+424);item+324=COMPOSED层(key item+56)。item+264=parent指针,在0x6C7440同时驱动 buffered路由(item+264!=0强制buffered)+alphaMask祖先walk。clipRect item+216..228是**float[4]非int**。

**2026-07-23 当前状态（取代历史快照）：**
- ⚠ J1/J7：0x6C4E28 独立 leaf/group pre-walk、SLA ordered-map/reuse、acquire refresh gate均已接入；入口/lazy-create 会 swap active/retired 树并 reset sequence，只有正常尾部才 Invalidate/clear 未复用 retired，异常 unwind 不清理。0x6C7440 buffered 使用同一次 source resolve + RM.bufLayer。仍缺 0x6C6B48 caller-local command payload 的同构专用值类型；`Player::_renderLayerStates` 只残留于 HEADLESS accurate-SLA 诊断。
- ✅ J2：direct gate 仅为 `(blend低4位==0 || >5) && completionType(+1144)==0 && item+264 parent==null`；`_clearEnabled`、`visibleAncestorIndex`、`childItems` 均不参与。
- ✅ J4/J5/J6/J9：尾部无 Update；clip/camera/viewport/Real setSize 的完整 float 消费链已恢复；SLA base+sequence absolute；raw opacity 只以 0 gate，priorDraw 用 signed `/2` 且不 clamp。
- ✅ `child+320`/`grp+340` 分别是 leaf/composed `tTJSVariant` 的 type tag，不是恒零独立字段。
- ✅ 目标 Layer 的 width/height、setClip、direct operate*、operateRect 均经同一 Layer class accessor dispatch，目标实例仅作 objthis；target 不再要求 NativeInstanceSupport。leaf/composed/buf/source 使用实例 dispatch。execute 的 descriptor→color→source→accessor owner 逆序析构及异常 unwind 已恢复。
- 🔧 J8：accurate-SLA 仍仅有 HEADLESS diagnostics checkpoint；真实持久 SLA 树架构尚缺。

**港湾**:本地无`// PLATFORM_BOUNDARY:`标注;KRKR2_WASMTIME_HEADLESS guard等价diagnostics边界(trace/SNAP*/probe)。但J8 accurate架构藏在该guard内属"缺失"非边界。

报告:analysis/audit_motionplayer_2026-06-07/clusterJ_render_execute.md
