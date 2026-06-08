---
name: drawtext-freetype-chain-verdict
description: Layer.drawText 文字渲染链(FreeType glyph→CharacterData→GPU blend)与 libkrkr2.so 全维对照裁决+函数地址映射
metadata:
  type: project
---

2026-06-08 fresh full-decompile of core Layer.drawText text-rendering chain. 整条链是 kirikiroid2 upstream KiriKiri2 源码的忠实 1:1 移植，**包括 blend 走 GPU OperateRect render-method 架构**(不是 Web 简化)。logo 差分覆盖不到此路径(标题/正文文字)，但反编译证据充分。

**函数地址映射(libkrkr2.so ↔ 本地):**
- `sub_7EA498` = `FreeTypeFontRasterizer::GetBitmap` (cpp/core/visual/FreeTypeFontRasterizer.cpp:166)。AA→clear/set 0x40000(NO_ANTIALIASING); Hinting→clear/set 0x20000(NO_HINTING); 4 次 GetGlyph(main/fallback sub_7E9E40/default/first FT_Get_First_Char); angle 0/2700/其他三分支; blur trigger if(+37 Blured) sub_7E133C. 全 ✅
- `sub_7E9038` = `tFreeTypeFace::GetGlyphFromCharcode` (FreeType.cpp:420)。调 sub_7E9670(load) + FT_Render_Glyph(format!=BITMAP时) + **gamma/AA-level 归一**(num_grays!=256 时 `v21=0x3FFFFFFF/(num_grays-1)`, `*p=(p*v21)>>22`, NEON 批处理)与本地 `(1<<30)-1` 完全一致 ✅; baseline = ascender*y_ppem/units_per_EM - bitmap_top; +0x400→underline +0x800→strikeout 走 sub_7E1D88(AddHorizontalLine,inline 算 pos/thickness 而非本地 GetUnderline/GetStrikeOut——功能等价)
- `sub_7E9670` = `tFreeTypeFace::LoadGlyphSlotFromCharcode` (FreeType.cpp:640)。load flag: NO_AA→0x20000(TARGET_MONO) else 8(NO_BITMAP); NO_HINTING→|0x8002(NO_HINTING|NO_AUTOHINT)|(0x20000>>12=0x20 FORCE_AUTOHINT); bold→FT_GlyphSlot_Embolden(+0x200) italic→Oblique(+0x100). 全 ✅(注:header 里 NO_HINTING==FORCE_AUTO_HINTING==0x20000 碰撞，本地两 if 合并效果与 binary 单表达式一致)
- `sub_7E0F04` = `tTVPCharacterData::tTVPCharacterData(8-arg ctor)` (CharacterData.cpp:9)。Gray=65; pitch fullcolor `(4w+7)&~7` else `(w+7)&~7`(=本地 `(((w-1)>>3)+1)<<3`); per-row memcpy. ✅
- `sub_7E1D88` = `tTVPCharacterData::AddHorizontalLine(pos,thickness,value)` (CharacterData.cpp:435)
- `sub_7E133C` = `tTVPCharacterData::Blur()` → `sub_7E11D4` = `Blur(blurlevel,blurwidth)` (CharacterData.cpp:118)。guard `*Data && (lvl!=255||width)`; width==0→ Gray==256?TVPChBlurMulCopy:65, `BlurLevel<<10`; width!=0→ TVPChBlurCopy/65, newpitch `(w+3)&~3`, OriginX/Y-=blurwidth. ✅
- `sub_A76688` = `tTVPNativeBaseBitmap::InternalBlendText` (LayerBitmapImpl.cpp:786)。**GPU 架构匹配**: fastGPURoute=hasGPUAccel && !cfg("ogl_accurate_render"); bltmode==bmAlphaOnAlpha(3)&&opa>0&&fastRoute→ AlphaBlend_a + TVPConvertAlphaToAdditiveAlpha + RGBA tex(qword_1B4CB90 cache); else 按 bltmode 选 ApplyColorMap_d(AoA opa>0)/RemoveOpacity(AoA opa<=0)/ApplyColorMap_a(AddAlpha=14)/ApplyColorMap(其他) + Gray tex(qword_1B4CC80 cache) + SetParameterOpa/Color4B + OperateRect(vtbl+160). 全 ✅
- 本地 `tTJSNI_BaseLayer::DrawText` (LayerIntf.cpp:4506) / TJS drawText reg (LayerIntf.cpp:8928, defaults opa=255 aa=true shlevel=0 shcolor=0 shwidth/ofsx/ofsy=0) / DrawText dispatcher (LayerBitmapImpl.h:214, len>=2→Multiple else Single) / DrawTextSingle(1126)/Multiple(1297)/InternalDrawText(919)/TVPGetCharacter(244 font cache+prerendered+GetBitmap) 均标准 KiriKiri 模式，未单独反编译 binary 端但属上游已知 1:1 结构。

**结论: 文字渲染链无对齐偏差，未发现可解释"渲染不完全正确"的真实 diff。** 本地各 cpp 文件里的 `unimplemented:` STUB(CharacterData Expand/Blur-for-FullColored/Bold/Resample4/8/copy-ctor)只在 FullColored(彩色 emoji/8bpp)路径触发，正常 grayscale 文字不走。若"渲染不完全正确"真实存在，根因更可能在: (a) tvpgl 软件函数 TVPChBlurCopy/TVPConvertAlphaToAdditiveAlpha 等的 wasm32 移植, (b) render-method shader(ApplyColorMap_d 等 GLSL)的 Web 实现, (c) GetTextureForRender/OperateRect 的 Cocos 合成层 — 均不在本次 drawText 逻辑链内，需另查。属平台边界候选而非 drawText 逻辑 diff。
