---
name: framebuffer-alpha-presentation
description: libkrkr2.so 最终 framebuffer alpha 链——cocos clear(0,0,0,1) + DrawSprite BlendFunc::DISABLE 覆盖写 + ltOpaque CopyOpaqueImage 强制 src|0xFF000000；不透明背景区 framebuffer alpha 必为 255
metadata:
  type: project
---

Web-only "白屏渐黑" bug 根因调查：libkrkr2.so 呈现完一帧后默认 framebuffer 不透明背景区的 alpha **必为 255**。三处二进制证据：

1. **cocos clear alpha=1.0** — `Director::drawScene@0x12880fc` 每帧调 `Renderer::clear@0x12cd48c` → `glClearColor(_clearColor RGBA)` + `glClear(GL_COLOR|GL_DEPTH=16640)`。`_clearColor`(Renderer this+0..15) 在 `Renderer ctor@0x12cb7cc` 末尾 0x12cbabc 设为 `Color4F::BLACK`。`Color4F::BLACK`(static-init@sub_51E6FC 0x51e920 写 xmmword_1617C60) = bytes `00..00 00 00 80 3F` = (0,0,0,**1.0**)。`Director::setClearColor@0x1289104` 唯一 xref 是无代码数据引用 → 游戏从不改 clearColor，保持 alpha=1.0。

2. **DrawSprite blend = 覆盖写** — `TVPWindowLayer::UpdateDrawBuffer@0xAA6268` 在 0xaa63e0 调 DrawSprite vtable+1440 传 `&BlendFunc::DISABLE`。`BlendFunc::DISABLE@0x161a2e8` = bytes `01 0 0` = {GL_ONE, GL_ZERO} → final framebuffer alpha == DrawBuffer 纹理 alpha（不混合，整通道覆盖含 alpha）。承载 KAG 主图层合成结果的 cocos Sprite。

3. **ltOpaque 合成强制 alpha=0xFF** — DrawBuffer 合成时 ltOpaque/coverRect 基层走 `TVPCopyOpaqueImage_c@0x88f018`：每像素 `dst = src | 0xFF000000`(SIMD mask 0xFF000000FF000000) 强制 alpha 饱和。不透明背景图(諸注意.PNG 这类)的基层即 ltOpaque。

**结论**：Android 正常显示背景图 = libkrkr2.so 三处协同保证 framebuffer 不透明区 alpha=255（clear 预填 1.0 + Sprite 覆盖写 DrawBuffer + DrawBuffer 经 CopyOpaqueImage 写 0xFF）。Web port 若白遮罩淡出后露黑/透明 → 缺以上某步(最可能：clearColor alpha=0、或 Sprite 用了 premult blend 让 dst alpha 被冲掉、或合成未强制 opaque-base alpha)，属移植 bug 非平台边界。GPU 路径默认关(renderer=software)，CPU 路径同走 CopyOpaqueImage。
