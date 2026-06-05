---
name: d3demotemodule-ncb-0x52DFA8
description: D3DEmoteModule registrar sub_52DFA8 verified 7-member list + instance field map; pixelateDivision exists on BOTH D3DEmoteModule(+20) AND Player(+912) with different property bodies
metadata:
  type: project
---

D3DEmoteModule NCB registrar = sub_52DFA8 @0x52dfa8 (ctor sub_52DF94 @0x52df94 immediately precedes it).

**7 members, binary order** (member#0 is the dynamic ctor key, off_1A02790 Function):
1. maskMode (int) — get sub_52E3F0 / set sub_52E3F8 → instance +8
2. maskRegionClipping (bool) — get sub_52E400 / set sub_52E408 → +12
3. mipMapEnabled (bool) — get sub_52E414 / set sub_52E41C → +13
4. alphaOp (int) — get sub_52E428 / set sub_52E430 → +16
5. protectTranslucentTextureColor (bool) — get sub_52E438 / set sub_52E440 → +14
6. **pixelateDivision (int)** — get sub_52E44C / set sub_52E454 → **+20**
7. setMaxTextureSize (Function) — sub_52E45C writes w→+24, h→+28

Note offset order is NOT registration order: register order maskMode/maskRegionClipping/mipMapEnabled/alphaOp/protectTranslucent/pixelateDivision but offsets are +8/+12/+13/+16/+14/+20 (protectTranslucent=+14 sits between mipMapEnabled=+13 and alphaOp=+16).

**ctor sub_52DF94 zeroes**: +0(byte), +8(qword=+8..15), +16(dword=+16..19), +24(qword=+24..31). **Leaves +20 (pixelateDivision) UNWRITTEN** → faithful default 0 (technically uninitialized; alloc not pre-zeroed since ctor bothers to zero siblings).

**Cross-class pixelateDivision (CONFIRMED, was a real audit point 2026-06-06)**: string "pixelateDivision" @0x14c1e50 has EXACTLY 2 xrefs:
- 0x52e310 → D3DEmoteModule sub_52DFA8, body sub_52E44C/sub_52E454 → module+20 (default 0)
- 0x6d86d8 → Player_ncb_registerMembers @0x6d8690, body Player_getPixelateDivision(sub_6D992C)/Player_setPixelateDivision(sub_6D9934) → Player+912 (default 100, set by Player ctor)
Two INDEPENDENT fields, different property bodies. Earlier local comment "binary puts it at Player+912 NOT D3DEmoteModule" was WRONG — it's on both. R-pixelate phase-2 removal was over-deletion; restored (D3DEmoteModule.h `inline static int _pixelateDivision=0` + main.cpp NCB_PROPERTY). Local models single-instance module via inline-static (same as sibling fields).

**Out-of-scope divergence found**: binary ctor zeroes +8 → binary maskMode default = 0 (MaskModeStencil), but local D3DEmoteModule.h has `_maskMode=1` (MaskModeAlpha). Pre-existing, unverified-origin local default; NOT changed (separate field, regression risk). Flagged for future review.
