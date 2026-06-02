---
name: alpha-mask-namespace-fn
description: M6 P0 — Motion_doAlphaMaskOperation 0x6AF104 is ONE function shared by NCB namespace entry + render path; doAlphaMaskOperation/getD3DAvailable are Motion namespace free-fns NOT Player methods
metadata:
  type: project
---

`Motion_doAlphaMaskOperation` @0x6AF104 is a SINGLE function with dual role: xrefs prove it is both (a) the NCB-exposed Motion namespace free-function (data ref from motionplayer_ncb_register @0x6D9B08 at 0x6da1b0) AND (b) the internal alpha-mask compositor called from 3 render-path fns (0x6c4e28, 0x6c7440, 0x6c9ca8). There is NO separate wrapper — same address both ways.

Signature (11 args, free function, NOT this-method): (dstLayerObj, dstX, dstY, srcLayerObj, srcX, srcY, w, h, threshold=a9, maskMode=a10, op=a11). a10 maskMode: thresholdMode = (a10==0); a10==1 = non-threshold alpha ops. a11 op: 1=AlphaMask(mul), 2=AlphaMaskRev(~src), 5/6=AddAlphaMask(add). GLES-accel branch = CPU per-pixel loops; else GL shader path (sub_84B454 backend, vtbl+160 drawShader, shaders "AddAlphaMask"/"AlphaMask"/"AlphaMaskRev"/"AlphaMaskThreshold"/...Fill/...Crop). Reads dst PropGet L"clipLeft/clipTop/clipWidth/clipHeight" (default 0 if PropGet<0), then FuncCall L"fillRect"/L"update".

Local: compositor body already faithfully ported as render_detail::applyMotionAlphaMaskLike_0x6AF104 (PlayerRenderInternal.cpp:627, CPU per-pixel switch only — the web port has no GLES-shader path). NCB entry motion_doAlphaMaskOperation (main.cpp) delegates to it.

**Why:** earlier port mis-attached doAlphaMaskOperation + getD3DAvailable as NCB_METHOD on the Player subclass (wrong owner). Binary registers BOTH on the Motion namespace object via sub_6FCAAC(*Motion, name, descriptor) at 0x6da1f0/0x6da260.
**How to apply:** Motion namespace-level free fns use NCB_ATTACH_FUNCTION(name, Motion, fn) placed AFTER the NCB_REGISTER_CLASS(Motion) block in main.cpp. ncbind auto-thunks iTJSDispatch2* params (ncbind.hpp:584-586). getD3DAvailable binary = `!hasGPUAccel`; web port has no GLES/D3D split so returns true (platform boundary). MaskModeStencil(0)/MaskModeAlpha(1) constants ALSO belong on Motion namespace (currently only on D3DEmoteModule) — adjacent K-7 cleanup not yet done.
