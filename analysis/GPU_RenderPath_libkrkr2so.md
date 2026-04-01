# libkrkr2.so GPU Rendering Path Analysis

通过反编译libkrkr2.so分析KAG的GPU渲染路径。

## GPU vs CPU路径选择

libkrkr2.so中通过`ogl_accurate_render`配置项（字符串`preference_ogl_accurate_render`在0x155B714）决定是否使用GPU路径。

- GPU路径: `IsGPU()` = true → `InternalComplete2_GPU` → `Draw_GPU`
- CPU路径: `IsGPU()` = false → `InternalComplete2` → `Draw`

`ogl_accurate_render`的值从`sub_AC4A90`和`sub_AD5F74`中读取（大型配置函数）。

## GPU渲染路径完整链条

```
1. Layer像素被修改（通过Motion Player或其他方式）
    │
    ▼
2. Layer::InternalUpdate → Parent::UpdateChildRegion
   向上传播到root → Manager::AddUpdateRegion
    │
    ▼
3. Manager::NotifyWindowInvalidation
   → LayerTreeOwner::NotifyLayerImageChange
   → DrawDevice::NotifyLayerImageChange (vtable+88)
   → Window::RequestUpdate → TVPPostWindowUpdate（事件队列）
    │
    ▼
4. TVPDeliverWindowUpdateEvents → UpdateContent
   → DrawDevice::Update → Manager::UpdateToDrawDevice
   → Primary::CompleteForWindow
    │
    ▼
5. CompleteForWindow → InternalComplete2_GPU(Rect, drawable)
   drawable = LayerManager
    │
    ▼
6. InternalComplete2_GPU:
   调用 Draw_GPU(drawable, 0, 0, updateregion, false)
    │
    ▼
7. Draw_GPU 递归合成:
   对每个可见子Layer:
     - DrawSelf(target, rctar, rect) → 通过GPU render methods合成
     - child->Draw_GPU(target, ...) → 递归
     - target->DrawCompleted(rctar, bitmap, rect, type, opacity)
    │
    ▼
8. LayerManager::DrawCompleted (drawable回调):
   if (!DrawBuffer) DrawBuffer = new tTVPDestTexture(w, h);
   DrawBuffer->Blt(destrect, bmp, cliprect, type, opacity, holdAlpha);

   Blt使用GPU render methods（如CopyOpaqueImage at 0x150BFA0）
   通过OpenGL shader直接操作GPU纹理。
    │
    ▼
9. DrawBuffer的纹理 = iTVPTexture2D，底层是Cocos2D Texture2D
   Cocos2D Sprite已经引用了这个Texture2D（在TVPWindowLayer::UpdateDrawBuffer中设置）
    │
    ▼
10. Cocos2D场景图渲染时，Sprite自动使用最新的纹理内容
    → OpenGL ES渲染 → 屏幕
```

## 关键发现

### 1. GPU路径不需要显式纹理上传
在CPU路径中，需要Show() → UpdateDrawBuffer → Texture2D::updateWithData → glTexSubImage2D
来把CPU内存中的像素上传到GPU。

在GPU路径中，DrawBuffer本身就是GPU纹理（iTVPTexture2D/Cocos2D Texture2D），
Blt操作通过OpenGL shader直接在GPU上执行。Cocos2D Sprite已经引用了这个纹理，
所以下一帧渲染时自动使用最新内容。**不需要Show()调用。**

### 2. GPU Render Methods
libkrkr2.so在`sub_84C724`中注册了大量GPU渲染方法（通过`sub_84AE48`注册）：
- CopyOpaqueImage
- ConstAlphaBlend / ConstAlphaBlend_d / ConstAlphaBlend_a
- AlphaBlend_SD
- Copy / CopyColor / CopyMask
- FillARGB / FillColor / FillMask
- ApplyColorMap / ApplyColorMap_d / ApplyColorMap_a
- RemoveOpacity / RemoveConstOpacity
- AdjustGamma / AdjustGamma_a
- UnivTransBlend / UnivTransBlend_d / UnivTransBlend_a
- ConstAlphaBlend_SD / ConstAlphaBlend_SD_a / ConstAlphaBlend_SD_d
- ConstColorAlphaBlend / ConstColorAlphaBlend_d / ConstColorAlphaBlend_a

这些方法由GPU RenderManager管理，通过OpenGL ES 2.0 shader执行。

### 3. DrawDevice vtable分析

#### vtable1 (0x1A27308) — 可能是tTVPBasicDrawDevice的第一接口
| Index | Offset | Function | 推测名称 |
|-------|--------|----------|----------|
| 0 | 0x00 | sub_84C314 | destructor? |
| 1 | 0x08 | sub_850304 | Present (calls vtable[7]) |
| 8 | 0x40 | 0x850528 | DrawDevice_UploadLayerToTexture_guess |
| 15 | 0x78 | 0x849868 | DrawDevice_RequestRedraw_guess |

#### vtable1[1] (Present) 行为
```c
sub_850304(DrawDevice *dd) {
    dd->internal[5]->byte_56 = 0;  // 清除"需要重绘"标记
    return dd->vtable[7](dd);       // 返回ShouldRedraw标记
}
```

Present方法只是清标记——**不做实际渲染**。这进一步证实GPU路径中
不需要通过Present/Show来推送像素到屏幕。

### 4. DrawDevice_FlushAllPending (0x849808) 的真实作用
```c
void DrawDevice_FlushAllPending() {
    for (each dd in g_pendingDrawDevices) {
        dd->vtable[1](dd);  // Present → 只是清标记
    }
    g_pendingDrawDevices.clear();
}
```

在GPU路径中，FlushAllPending只是清除pending标记，不做纹理上传。
真正的纹理更新在`CompleteForWindow` → `Draw_GPU` → `DrawCompleted` → `Blt`
中通过GPU shader完成。

### 5. DrawBuffer创建和关联

DrawBuffer在`LayerManager::DrawCompleted`中懒创建：
```c
if (!DrawBuffer) {
    DrawBuffer = new tTVPDestTexture(w, h);
    DrawBuffer->Fill(rect, 0xFF000000);  // 黑色填充
}
DrawBuffer->Blt(destrect, bmp, cliprect, type, opacity, holdAlpha);
```

DrawBuffer的纹理首次通过 `TVPWindowLayer::UpdateDrawBuffer(DrawBuffer->GetTexture())`
传给Cocos2D Sprite。之后DrawBuffer的GPU纹理内容通过OpenGL直接更新，
Sprite自动反映最新内容。

### 6. TVPWindowLayer::UpdateDrawBuffer (0xAA6268)
```c
void TVPWindowLayer::UpdateDrawBuffer(iTVPTexture2D *tex) {
    Texture2D *current = DrawSprite->getTexture();
    Texture2D *newtex = tex->GetAdapterTexture(current);
    if (newtex != current) {
        DrawSprite->setTexture(newtex);
        DrawSprite->setTextureRect(Rect(0, 0, sw, sh));
        DrawSprite->setBlendFunc(BlendFunc::DISABLE);
        ResetDrawSprite();
    }
}
```

只在纹理对象改变时（首次创建或resize）更新Sprite的纹理引用。
GPU路径中纹理内容的更新不需要调用此函数。

## libkrkr2.so中IsGPU()的实际值

### 事实确认（反编译证据）

1. `sub_84B454`（获取renderer）读取配置 `"renderer"`，**默认值 = `"software"`**
2. `sub_84B7FC`（IsGPU检查）调用renderer的vtable[8]判断是否software
3. Android kirikiroid2支持两种renderer：`"software"` 和 `"hardware"`
   - `"hardware"` 字符串在0x1610140，被70+个GPU render method函数引用
   - `"preference_renderer_opt"` 在0x155B3A8，从Android SharedPreferences读取
4. **默认配置下，renderer = "software"，IsGPU() = false，走CPU路径**

### Player SLA draw的两条路径

在`Player_DrawSLA_guess`(0x6D5658)中：
```c
isSoftware = sub_84B7FC();  // 检查renderer是否是software
if (isSoftware) {
    byte_1AB84F4 = 1;  // 不检查ogl_accurate_render
} else {
    byte_1AB84F4 = GetValue<bool>("ogl_accurate_render", false);
}

if (!byte_1AB84F4) {
    // GPU路径（hardware renderer + ogl_accurate_render=false）
    // → 使用GPU纹理直接渲染
} else {
    // CPU路径（software renderer 或 ogl_accurate_render=true）
    // → sub_6C9CA8: 软件合成到Layer像素缓冲区
    // → sub_6CE938: 后处理
}
```

### 结论
**libkrkr2.so默认走CPU路径（software renderer）。**
在CPU路径中，Layer像素通过标准的KAG图层合成（InternalComplete2 → Draw → DrawCompleted）
进入DrawBuffer，然后通过Show() → UpdateDrawBuffer上传到Cocos2D纹理。

这意味着web端的问题**不在GPU/CPU路径选择上**，而在于：
- DrawDeviceD3D的Show()没有调用UpdateDrawBuffer
- 或者DrawDevice的Update链路在DrawDeviceD3D代理模式下断裂

## IDA已重命名函数

| 地址 | 名称 | 确认程度 |
|------|------|----------|
| 0x84C724 | GPU_RenderMethod_Init_guess | guess |
| 0x84AE48 | RenderManager_RegisterMethod_guess | guess |
