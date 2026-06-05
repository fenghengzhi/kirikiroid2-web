---
name: sub6C7440-setclip-dispatch
description: sub_6C7440 setClip goes through FuncCall(vtbl+16) L"setClip" on work-layer v370, NOT native Layer->SetClip; argc=4 set / argc=0 reset; 3 sites
metadata:
  type: project
---

sub_6C7440 (Player render-to-canvas walk) dispatches clip via TJS `FuncCall`, not a native `tTJSNI_BaseLayer::SetClip` call. Aligned 2026-06-06.

**3 派发点** (all `(*(*v370+16))(v370, 0, L"setClip", &dword_1AB8464, 0, <argc>, <argv>, v9)`):
- 0x6c78dc: argc=4, argv=[v31(clipL), v32(clipT), v36-v31(width), v37-v32(height)] — viewport-clip set
- 0x6c7620: argc=0 — reset (else-branch when no viewport clip)
- 0x6c8fcc: argc=0 — post-walk reset after top-level item loop

v370 = render target work-layer = ALL draw prims dispatch object (operateRect 0x6c8558/0x6c8b74, operateAffine 0x6c8d74, setClip ...) all via vtbl+16. Maps to local `renderLayerObject` (executeLayerRenderCommands formal param, PlayerRenderExecute.cpp:243). String literal IDA-confirmed full `L"setClip"` (7-char, not truncated), matches local registration `setClip // not setClipRect` @ LayerIntf.cpp:8875.

**Local registration contract** (LayerIntf.cpp:8875-8895): numparams==0 -> ResetClip(); numparams>=4 -> SetClip((int)p0..p3). So argc=0 dispatch correctly routes to reset, argc=4 to set.

**Local impl**: callLayerSetClipLike_0x6C7440 / callLayerResetClipLike_0x6C7440 in PlayerRenderInternal.cpp (decl PlayerRenderInternal.h). Replaced prior native `renderLayer->SetClip()/ResetClip()` bypass at PlayerRenderExecute.cpp ~775 (set/reset branch) and ~1291 (post-walk reset). After dispatch, local re-reads renderLayer->GetClip() to fill outRect (clip clamped by Layer).

**Oracle gap**: structural diff (run_motion_playback_wasmtime.py --only-structural) is INERT to this — it sees Motion state fields, not draw_dispatch events. m2logo(93f)/yuzulogo(243f) PASS = non-regression guard only. The draw_dispatch event stream that WOULD observe setClip dispatch needs --record-render-stages (heavy lldb) + frida oracle draw_dispatch trace; not in lightweight suite. Change kept on decompile evidence per CLAUDE.md oracle-inert rule.

**Verified**: guest wasm rebuilt (out/wasmtime/debug), confirmed callLayerSetClipLike_0x6C7440 symbol present; web debug build 24/24 OK.
