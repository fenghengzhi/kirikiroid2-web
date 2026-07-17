# libkrkr2.so GPU Rendering Path Analysis

通过反编译libkrkr2.so分析KAG的GPU渲染路径。

> 2026-07-18 纠正：旧版文档把 `0x850528` 误命名为
> `DrawDevice_UploadLayerToTexture_guess`，并据此推断 DrawDeviceD3D 的
> `Show()` 没有调用 `UpdateDrawBuffer`。新反编译证明 `0x850528` 是软件纹理
> 的 `GetAdapterTexture` 虚函数；DrawDeviceD3D 的 `sub_5314B0@0x5314B0`
> 在普通与转场分支末尾都会把 back target 交给 window form 的
> `UpdateDrawBuffer`。下文已按新证据纠正。

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

### 3. 软件纹理 adapter vtable 分析

`off_1A272C8/off_1A27370` 的 adapter-texture 槽 `+0x80` 指向
`SoftwareTexture2D_GetAdapterTexture_guess@0x850528`。它属于软件纹理对象，
不是 DrawDevice：同尺寸调用 `Texture2D::updateWithData`，尺寸变化则
`new Texture2D → autorelease → initWithData`。

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

DrawDeviceD3D 的软件链已由 `sub_5314B0@0x5314B0` 确认：manager 的 CPU
draw buffer 经 `sub_532B1C` 全量上传到每-manager软件纹理，再经
`sub_5328F4` 合成到 back target，最后调用 window form 的
`UpdateDrawBuffer`。因此“Show 没有调用 UpdateDrawBuffer”的旧结论已被证伪。

## DrawDeviceD3D 第二批源码结构复原（2026-07-18）

本轮在 `cpp/plugins/DrawDeviceD3D.cpp` 继续恢复了 Show 外围的对象图，证据与
本地结构对应如下：

| libkrkr2.so | 原版行为 | 本地复原 |
|---|---|---|
| `0x530E94`, `0x531ECC` | root 注册独立 `D3DLayerBase` native adaptor | `D3DLayerBaseNativeInstance`，与 NCB class adaptor 分离 |
| `0x533010`, `0x532D64` | child 注册 `D3DLayerObjectNativeInstance`；公共基类持有 owner/parent/双索引/listener | `D3DLayerObject` + 独立 owning adaptor |
| `0x52991C`, `0x529B98`, `0x529DAC` | front/back 两棵允许重复键的 RB tree；删除按键范围内精确指针匹配 | 两个 `std::multimap<int,D3DLayerObject*>`，保留重复项与单项删除边界 |
| `0x529038` | `children` 仅枚举 front tree，过滤失效 owner | TJS Array getter，同序同过滤 |
| `0x5297BC`, `0x530DA4`, `0x530DE8` | `onUpdate(state)` + listener 链表传播 | `std::list` 追加/全匹配删除；Show/capture 复用传播链 |
| `0x529248`, `0x529670` | transition 读取 method/rule/vague 并维护纹理引用 | `startTransition/stopTransition`，默认 vague=64、state 1→0 |
| `0x52CF28`–`0x533310` | D3DImage 构造、矩阵、clip、transform、listener draw | 完整 NCB surface 与 child 生命周期 |
| `0x52D5AC`–`0x533420` | D3DPicture 跟踪于 root pointer-set；Software load 复制纹理 | `D3DPicture` + texture reference holder；保留重复 load 覆盖旧 holder 的边界 |
| `0x52B5F8 → 0x531088` | capture 合成 front plane 并回写目标 Layer | Software scanline copy，支持 frontIndex limit |

### ncbind 对象所有权

二进制给 D3DImage 同时安装 class adaptor 与公共 child adaptor；公共 adaptor 的
`Destruct` 调用 native deleting destructor，而 `Invalidate` 是 no-op。Web 本地
`ncbInstanceAdaptor` 默认会拥有 factory 返回值，因此 factory 将 class adaptor
设为 sticky，由公共 adaptor 保持唯一所有权，避免双重析构。这不是渲染行为
workaround，而是对二进制 adaptor 生命周期在本仓库 ncbind 实现中的同构表达。

### 平台边界

直到 `CurrentTarget` 为止，容器、更新传播、Software texture copy 和合成顺序均按
反编译复原。最终 `UpdateDrawBuffer` 仍进入 Emscripten/Cocos2D 的 WebGL texture
adapter；这是 Android GL 与浏览器 WebGL 的不可避免平台边界，没有在 child/transition
链中加入额外缓存失效或强制刷新步骤。

## IDA已重命名函数

| 地址 | 名称 | 确认程度 |
|------|------|----------|
| 0x84C724 | SoftwareRenderMethod_Init_guess | guess（由 CPU render-method 对象/vtable 交叉确认） |
| 0x84AE48 | RenderManager_RegisterMethod_guess | guess |
| 0x850528 | SoftwareTexture2D_GetAdapterTexture_guess | guess（vtable 槽与上传行为确认） |
| 0x5314B0 | DrawDeviceD3D_Show_guess | guess（NCB/embedded draw-device 调用链确认） |
