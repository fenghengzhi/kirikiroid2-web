---
name: sub6C7440-setclip-dispatch
description: sub_6C7440 setClip 通过 Layer class receiver + target objthis；argc=4 set / argc=0 reset；无 native readback
metadata:
  type: project
---

sub_6C7440 (Player render-to-canvas walk) dispatches clip via TJS `FuncCall`, not a native `tTJSNI_BaseLayer::SetClip` call. Aligned 2026-06-06.

**2026-07-23 fresh receiver correction：**三个调用点都在 global Layer class
accessor 上 `FuncCall`，target/work-layer 是 `objthis`，不是 receiver。
- 0x6c78dc: argc=4, argv=[v31(clipL), v32(clipT), v36-v31(width), v37-v32(height)] — viewport-clip set
- 0x6c7620: argc=0 — reset (else-branch when no viewport clip)
- 0x6c8fcc: argc=0 — post-walk reset after top-level item loop

同一 receiver/objthis 合约还用于 target width/height、operateRect 与 direct
operate*。String literal 是完整 `L"setClip"`，不是截断字符串。

**Local registration contract** (LayerIntf.cpp:8875-8895): numparams==0 -> ResetClip(); numparams>=4 -> SetClip((int)p0..p3). So argc=0 dispatch correctly routes to reset, argc=4 to set.

**Local impl**: callLayerSetClipLike_0x6C7440 /
callLayerResetClipLike_0x6C7440 接受 `(layerClassObject, renderLayerObject)`；
dispatch 后不再 native `GetClip()` 回读，outRect 保留 caller 构造的 Real tuple。

**Oracle gap**: structural diff (run_motion_playback_wasmtime.py --only-structural) is INERT to this — it sees Motion state fields, not draw_dispatch events. m2logo(93f)/yuzulogo(243f) PASS = non-regression guard only. The draw_dispatch event stream that WOULD observe setClip dispatch needs --record-render-stages (heavy lldb) + frida oracle draw_dispatch trace; not in lightweight suite. Change kept on decompile evidence per CLAUDE.md oracle-inert rule.

文末旧 Web/Wasmtime 数字只属于当时 checkpoint；后续修改必须以当前验证记录为准。
