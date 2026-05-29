---
name: texture-upload-path
description: libkrkr2.so 纹理上传完整路径分析——确认 100% 同步 glTexImage2D/glTexSubImage2D 主线程上传，无 PBO、无 GL 共享 context 线程、无 glBufferData-for-texture
type: project
---

# libkrkr2.so 纹理上传路径（最终结论）

## 同步性
- **完全同步**：所有 Bitmap→GLTexture 路径都在主渲染线程上直接调用 `glTexImage2D` / `glTexSubImage2D`。
- **没有 PBO**：
  - `glMapBufferRange` 在 libkrkr2.so 中根本不存在（lookup_funcs 返回 Not found）
  - `GL_PIXEL_UNPACK_BUFFER (0x88EC)`、`GL_DYNAMIC_DRAW (0x88E8)`、`GL_STREAM_DRAW (0x88E0)` 作为立即数 0 次出现
  - 所有 `glBufferData` 调用（在 cocos2d 内部）都用于 VBO/IBO（DrawNode、ParticleSystemQuad、Renderer、TextureAtlas、VertexBuffer、IndexBuffer），与 texture upload 无关
- **没有 GL 上传线程**：
  - `eglMakeCurrent` / `eglCreateContext` 在 libkrkr2.so 中不存在
  - `glFenceSync` 不存在
  - 因此任何工作线程都不可能调 GL
- **实际线程总数**：5 个 pthread_create 调用点：
  1. `tTVPThread::StartTread @ 0xA35DC0` (krkr 自家线程类：timer、async file IO 解码 worker、scripted thread 等共6个实例)
  2. `tTVPThreadPool worker @ 0xD76ADC` (boost-style thread pool)
  3. `althrd_create @ 0xF7B1D4` (al-lib 音频)
  4. WebP decode worker @ 0x14272EC
  5. `std::thread` ctor @ 0x14A4F6C (cocos2d TextureCache 用此)

## cocos2d::TextureCache::addImageAsync 行为（@ 0x12D6DF4）
- 启动一个 `std::thread`，工作线程仅做 `cocos2d::Image::initWithImageFileThreadSafe`（PNG/JPEG decode）
- decode 完成后 push 到 response queue
- `addImageAsyncCallBack @ 0x12D7818` 由主线程 Scheduler 调度，在主线程上调 `Texture2D::initWithImage` → `initWithMipmaps` → **同步 `glTexImage2D`**
- **krkr 没用这条 cocos 异步链**：`addImageAsync` 在 .so 中无代码 xref，只剩 vtable 符号

## krkr 自己的 Texture 类（关键函数）
位于 0xA4E000-0xA65000 区间，命名按本地代码 `iTVPTexture2D` 接口：

| 地址 | 函数 | GL 行为 |
|---|---|---|
| 0xA5F718 | tTVPTexture2D_Create_guess | `glGenTextures` + `glTexImage2D` (NULL data 占位 + 后续 Update) |
| 0xA5F29C | tTVPTexture2D_SetData | `glPixelStorei(UNPACK_ALIGNMENT)` + `glTexImage2D`（全量） |
| 0xA5F3A8 | tTVPTexture2D_Update_guess | `glTexSubImage2D`（局部）—— GL_UNPACK_ROW_LENGTH (0x88EC 不存在，用 3314 = UNPACK_ROW_LENGTH ES3) 路径或 staging memcpy |
| 0xA64C84 | tTVPTexture2D_TVPInitFromBitmap_guess | `glTexImage2D` 或 `glTexImage2D+glTexSubImage2D`（按 width 是否 POT 分支） |
| 0xA6475C | tTVPTexture2D_TVPInitFromBitmapScaled_guess | `cv::resize` 缩放后 `glTexImage2D` |
| 0xA4E658 | tTVPTexture2D_RecreateScaled_guess | `glTexImage2D` 占位 + FBO 内 blit（GPU 内部 copy，**仍然不是 PBO**） |
| 0xA5EF18 | tTVPTexture2D_SetDataWithCopy | `glTexImage2D` 占位 + 调用 Update 写入 |
| 0xA5FD18 | tTVPTexture2D_dtor | `cocos2d::GL::deleteTexture` |

## cocos2d::Texture2D 上传函数
- `initWithData @ 0x12D0DFC` → `initWithMipmaps @ 0x12D0E54` → `glTexImage2D` 同步
- `updateWithData @ 0x12D1394` → `glBindTexture` + `glTexSubImage2D` 同步
- 没有任何 PBO / orphaning 路径

## KAG @bg 完整调用链（Android）
1. KAG `@bg storage=xxx` (TJS) → Layer.loadImages (TJS NCB)
2. → krkr 自家 GraphicsLoader (`GraphicsLoaderIntf.cpp`，路径串 @ 0x15069E8)
3. → Image decode（PNG/JPEG/WebP，可在 worker thread 上）→ bitmap in CPU memory
4. → 主线程：`tTVPTexture2D_TVPInitFromBitmap_guess @ 0xA64C84`
5. → `glTexImage2D` / `glTexSubImage2D` **同步**

## Web 移植版差异
**无**：libkrkr2.so 也是 100% 同步主线程上传，Web 移植版的两份实现（cocos 路径 + ogl 路径）行为均与 Android 一致。

可以删除的"假优化空间"：
- 不需要给 Web 移植版加 PBO，因为原版没有
- 不需要给 Web 移植版加 GL 上传线程，因为原版没有
- 不需要 staging buffer 双缓冲

## Web 端唯一真正的架构差异（与本任务无关，但顺便记录）
RenderManager.cpp 和 RenderManager_ogl.cpp 两条上传路径**共存**——Android 端只有 ogl 那一条；cocos2d 的 initWithData/updateWithData 在 Android 端 krkr 也调用了（line 557、563 处的代码同样存在于 libkrkr2.so）。两条路径都是 krkr 原版就有的（用于不同类型的 Layer/Bitmap），不是 Web 移植引入的冗余。
