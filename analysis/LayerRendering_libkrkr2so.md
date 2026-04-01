# libkrkr2.so Layer Rendering Pipeline Analysis

反编译libkrkr2.so分析KAG Layer如何通过Cocos2D显示到屏幕上。

## 完整渲染管线（无gap）

```
1. Player写像素到Layer缓冲区 (sub_6DE738)
    │
    ▼
2. Layer_UpdateRect_guess (0x800F4C)
   构造dirty rect = {0, 0, clipW, clipH}
    │
    ▼
3. Layer_NotifyUpdate_guess (0x8144E8)
   调用 Rect_Intersect (0x7E1F68) 检查dirty rect与layer rect是否相交
   如果不相交 → 中断（返回0）
    │
    ├─ 有子Layer (offset+64) →
    │   Layer_PropagateUpdateToChildren_guess (0x81437C)
    │   递归遍历子Layer树
    │   到达叶节点时 →
    │       Window_NotifyLayerImageChange_guess (0x838844)
    │       存储dirty rect到 DirtyRectList (offset+176)
    │       调用 DrawDevice->NotifyLayerImageChange (vtable+88)
    │
    ├─ 无子Layer，有windowObject (offset+56) →
    │   Layer_NotifyLayerImageChange_guess (0x8388EC)
    │   存储dirty rect → DrawDevice->NotifyLayerImageChange (vtable+88)
    │
    └─ 向上传播到父Layer (offset+632)
       递归调用 Layer_NotifyUpdate_guess
    │
    ▼
4. DrawDevice->NotifyLayerImageChange (vtable[11], offset 88)
   标记DrawDevice需要重绘
   调用 DrawDevice_RequestRedraw_guess (0x849868)
   将DrawDevice添加到全局 g_pendingDrawDevices 列表
    │
    ▼
5. DrawDevice_FlushAllPending (0x849808)
   由 ContinuousHandler 定期调用（注册于 DrawDevice_InitRenderLoop_guess 0x42F644）
   遍历 g_pendingDrawDevices，对每个调用 vtable[1] (Present)
    │
    ▼
6. DrawDevice->Present (vtable+8)
   调用 vtable[7] (GetSrcSize/ShouldRedraw?)
   最终调用 DrawDevice_UploadLayerToTexture_guess (0x850528)
   从Layer像素缓冲区创建/更新 Cocos2D Texture2D
    │
    ▼
7. Texture2D::updateWithData → glTexSubImage2D
   像素数据上传到GPU
    │
    ▼
8. TVPWindowLayer::UpdateDrawBuffer (0xAA6268)
   更新Cocos2D Sprite的纹理引用
   调用 ResetDrawSprite (0xAA7D70)
    │
    ▼
9. Cocos2D场景图渲染 → OpenGL ES → 屏幕
```

## 关键发现

### NotifyLayerImageChange 是核心
DrawDevice vtable+88 对应的是 `iTVPDrawDevice::NotifyLayerImageChange`
（ARM64 vtable layout包含2个destructor slot，所以offset 88 = slot 11 = NotifyLayerImageChange）

### 异步渲染模型
libkrkr2.so采用**异步渲染**：
- Layer::Update() 不直接上传纹理
- 它只是将DrawDevice标记为"需要重绘"（添加到 g_pendingDrawDevices）
- `DrawDevice_FlushAllPending` 由 ContinuousHandler（定时器）周期性调用
- 在FlushAllPending中才真正执行纹理上传（Present → UploadTexture → glTexSubImage2D）

### Player SLA draw路径的完整链
```
Player_DrawSLA_guess (0x6D5658)
  ├─ prepareMotionData (sub_6D5164)
  ├─ setupRenderState (sub_6D5264)
  ├─ ResolveSLATarget (0x6D5948) → 获取SLA底层native Layer
  ├─ RenderMotionFrame (0x6DE738) → 往Layer像素缓冲区写motion帧
  └─ Layer_UpdateRect_guess (0x800F4C) → 触发dirty通知
      └─ ... → NotifyLayerImageChange → RequestRedraw → 添加到pending list
```
渲染在下一次 FlushAllPending 时上屏。

## 架构概览（旧版，保留参考）

```
TJS2 Game Scripts (KAG)
    │
    ▼
tTJSNI_BaseLayer (KAG Layer tree)
    │ pixels stored in internal bitmap buffer
    │
    ▼ Layer::Update() → dirty rect notification
    │
iTVPDrawDevice (PassThroughDrawDevice)
    │ vtable+0x58: NotifyBitmapCompleted_guess
    │ vtable+0x40: DrawDevice_UploadLayerToTexture_guess (0x850528)
    │
    ▼ uploads pixel data to Cocos2D Texture2D
    │
TVPWindowLayer (extends cocos2d::ScrollView → cocos2d::Node)
    │ UpdateDrawBuffer(iTVPTexture2D*) at 0xAA6268
    │ ResetDrawSprite() at 0xAA7D70
    │
    ▼ sets texture on internal Cocos2D Sprite
    │
TVPMainScene (extends cocos2d::Scene)
    │ addLayer(TVPWindowLayer*) at 0xAA0AD8
    │ update(float) at 0xAA0718
    │
    ▼ Cocos2D scene graph rendering (OpenGL ES)
    │
Screen
```

## 关键数据流

### 1. Layer像素写入

Motion Player的`sub_6D5658`（SLA draw handler）通过`sub_6DE738`直接往Layer的像素缓冲区写入渲染结果。Layer对象结构（从反编译推断）：

| Offset | Type | 描述 |
|--------|------|------|
| +24 | ptr | DrawDevice指针 |
| +32 | ptr | 像素数据指针 |
| +56 | ptr | 窗口/父对象指针 |
| +64 | ptr | 子Layer链表 |
| +176 | rect | dirty rect |
| +180-192 | int×4 | clip rect (left, top, right, bottom) |
| +632 | ptr | 父Layer指针（用于递归更新） |

### 2. 脏区域通知 — Layer_UpdateRect_guess (0x800F4C)

```c
void Layer_UpdateRect_guess(Layer *layer) {
    // 计算脏区域 = clip rect size
    rect.width = layer->clipRight - layer->clipLeft;    // offset 47-45
    rect.height = layer->clipBottom - layer->clipTop;    // offset 48-46
    Layer_NotifyUpdate_guess(layer, rect, false);
}
```

被`sub_6D5658`（SLA draw）在渲染完成后调用，通知系统Layer内容已变化。

### 3. 更新传播 — Layer_NotifyUpdate_guess (0x8144E8)

```c
void Layer_NotifyUpdate_guess(Layer *layer, rect dirtyRect, bool flag) {
    // 合并脏区域
    if (!mergeRect(&layer->accumulatedDirty, &dirtyRect))
        return;

    // 如果有缓存处理...
    if (layer->cacheEnabled) {
        updateCache(layer, dirtyRect);
    }

    // 路径A: 有子Layer → 处理子Layer链
    if (layer->children) {
        // 遍历子Layer链表，找到需要更新的
        // 调用 sub_81437C 处理
    }
    // 路径B: 无子Layer → 通知DrawDevice
    else if (layer->windowObject) {
        Layer_NotifyDrawDevice_guess(layer->windowObject, &dirtyRect);
    }

    // 递归向上传播到父Layer
    if (layer->parentLayer) {
        if (parentLayer->needsUpdate) {
            // 计算父坐标系中的脏区域
            Layer_NotifyUpdate_guess(parentLayer, parentRect, true);
        }
    }
}
```

### 4. DrawDevice通知 — Layer_NotifyDrawDevice_guess (0x8388EC)

```c
void Layer_NotifyDrawDevice_guess(WindowObject *window, rect *dirtyRect) {
    // 存储脏区域到window对象
    storeDirtyRect(window + 176, dirtyRect);

    // 调用DrawDevice的虚方法通知更新
    DrawDevice *dd = window->drawDevice;   // offset +24
    if (dd) {
        dd->vtable[11](dd, window);  // vtable+0x58: OnLayerDirty
    }
}
```

### 5. 纹理上传 — DrawDevice_UploadLayerToTexture_guess (0x850528)

DrawDevice的vtable+0x40方法，将Layer像素上传到Cocos2D Texture2D：

```c
Texture2D* DrawDevice_UploadLayerToTexture(LayerBitmap *bitmap, Texture2D *existing) {
    if (existing && existing.width == bitmap.width && existing.height == bitmap.height) {
        // 更新现有纹理（快速路径）
        existing.updateWithData(bitmap.pixelData, 0, 0, bitmap.pitch/4, bitmap.height);
        return existing;
    } else {
        // 创建新纹理
        Texture2D *tex = new Texture2D();
        tex.initWithData(bitmap.pixelData, bitmap.height * bitmap.pitch,
                         RGBA8888, bitmap.pitch/4);
        return tex;
    }
}
```

纹理最终通过 `Texture2D::updateWithData` → `glTexSubImage2D` 上传到GPU。

### 6. TVPWindowLayer 显示更新 — UpdateDrawBuffer (0xAA6268)

```c
void TVPWindowLayer::UpdateDrawBuffer(iTVPTexture2D *texture) {
    if (!texture) return;

    // 获取当前sprite的纹理
    Texture2D *current = sprite->getTexture();

    // 调用纹理的update方法
    Texture2D *updated = texture->Upload(current);  // vtable+128

    if (updated != current) {
        // 纹理变了 → 更新sprite
        sprite->setTexture(updated);

        // 计算缩放
        float scaleX = windowWidth / texture.srcWidth;
        float scaleY = windowHeight / texture.srcHeight;

        // 设置sprite显示区域
        sprite->setTextureRect(Rect(0, 0, scaledWidth, scaledHeight));
        sprite->setBlendFunc(BlendFunc::DISABLE);

        ResetDrawSprite();
    }
}
```

## Motion Player SLA Draw路径 — sub_6D5658

```c
void Player_DrawSLA(Player *self, SLA *sla) {
    // 1. 准备渲染数据
    if (!prepareMotionData(self, &textures, &sources))
        return;
    setupRenderState(self, textures);

    // 2. 获取SLA底层的渲染目标
    RenderTarget *target = ResolveSLATarget(sla);  // sub_6D5948
    // target 包含 Cocos2D GL纹理引用

    // 3. 设置渲染目标尺寸
    target->setSize(sla.clipWidth, sla.clipHeight);

    // 4. 渲染motion到目标
    // sub_6DE738: 直接往target的像素缓冲区写入motion帧
    target->renderResult = RenderMotionFrame(target, textures, sources, self);

    // 5. 触发显示更新
    // sub_800F4C (Layer_UpdateRect_guess): 通知脏区域
    if (!isOffscreen)
        Layer_UpdateRect_guess(target, 0);
    // ↑ 这会触发: NotifyUpdate → NotifyDrawDevice → UploadTexture → glTexSubImage2D
}
```

## 关键结论

### 为什么Motion Logo在web端不显示

1. **libkrkr2.so的SLA draw路径**直接通过C++操作底层渲染：
   - `sub_6D5948` 获取SLA底层的GL渲染目标（不是TJS Layer对象）
   - `sub_6DE738` 直接往渲染目标写像素
   - `Layer_UpdateRect_guess` 通知DrawDevice脏区域
   - DrawDevice通过 `glTexSubImage2D` 更新GPU纹理
   - Cocos2D在下一帧自动重绘

2. **web端的问题**：
   - 我们用renderToLayer写像素到SLA的owner Layer
   - 但这个Layer不在KAG的主显示图层树中
   - 即使写了正确的像素，DrawDevice没有收到脏区域通知
   - TVPWindowLayer的Sprite没有更新纹理

3. **正确的修复方向**：
   - 不应该写到SLA的owner Layer
   - 应该找到KAG窗口的primary Layer（主显示图层）
   - 渲染motion到primary Layer的子图层上
   - 调用该子图层的Update()触发DrawDevice → Texture2D → glTexSubImage2D链

### IDA中已重命名的函数

| 地址 | 名称 | 确认程度 |
|------|------|----------|
| 0x800F4C | Layer_UpdateRect_guess | guess |
| 0x8144E8 | Layer_NotifyUpdate_guess | guess |
| 0x8388EC | Layer_NotifyDrawDevice_guess | guess |
| 0x850528 | DrawDevice_UploadLayerToTexture_guess | guess |
| 0x8513F0 | DrawDevice_UploadLayerToTexture2_guess | guess |
| 0x8517D4 | DrawDevice_UploadLayerToTexture3_guess | guess |
| 0xAA0AD8 | TVPMainScene::addLayer | 100% (symbol) |
| 0xAA0718 | TVPMainScene::update | 100% (symbol) |
| 0xAA6268 | TVPWindowLayer::UpdateDrawBuffer | 100% (symbol) |
| 0xAA7D70 | TVPWindowLayer::ResetDrawSprite | 100% (symbol) |
| 0x6D5658 | Player_DrawSLA_guess | guess |
| 0x6D5948 | Player_ResolveSLATarget_guess | guess |
| 0x6DE738 | Player_RenderMotionFrame_guess | guess |
