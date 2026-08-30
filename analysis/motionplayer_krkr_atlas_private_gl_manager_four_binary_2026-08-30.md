# KRKR 图集私有 OpenGL manager 与 Web 间接调用越界四参考复核

日期：2026-08-30

## 1. 运行时结论

`千恋＊万花` 在年龄界面后进入 MotionPlayer 场景时，WebAssembly 抛出
`RuntimeError: table index is out of bounds`。失败点是
`TVPRenderManager_OpenGL::OperateTriangles` 对源纹理调用 `SyncPixel()` 的间接调用。

故障时源对象地址为 `0x115383c0`，虚表指针为 `0x4dc9f8`。Wasm 符号表把
`0x4dc9f0` 标为 `tTVPSoftwareTexture2D` 虚表；`SyncPixel` 所在的第 19 个槽读到
`0x4b28c8`，而函数表长度只有 `30660`。所以这不是悬空指针或批处理生命周期问题，
而是 OpenGL 路径把有效的软件纹理当成 `tTVPOGLTexture2D` 调用了不存在的虚函数。

数据流为：KRKR atlas producer 用进程默认 software manager 创建纹理，source getter
通过 atlas fast path 原样返回，Motion 私有 OpenGL renderer 随后消费该纹理。

## 2. Fresh 四端函数与调用点

本次对 loader 和 source getter 都重新执行了 decompile，并对私有 manager 取值函数重新
读取 xref。四端 loader 内的调用点如下：

| 参考二进制 | KRKR loader | 私有 OpenGL manager 调用点 |
|---|---:|---:|
| Android arm64 `libmotionplayer.so` | `Player_loadKrkrAtlasSource_guess@0x6931C8` | `0x693CB4` |
| Android armv7 `libmotionplayer.so` | `Player_loadKrkrAtlasSource_guess@0x570F54` | `0x5717B8` |
| iOS arm64 `motionplayer` slice | `Player_loadKrkrAtlasSource_guess@0x1000F4098` | `0x1000F4C4C` |
| iOS armv7 `motionplayer` slice | `Player_loadKrkrAtlasSource_guess@0xF0BE4` | `0xF1614` |

对应 source getter 为 Android arm64 `motion_D3DSourceTextureGetter_invoke@0x6EE440`、
Android armv7 `@0x5AC518`、iOS arm64 `@0x10014019C`、iOS armv7 `@0x1414C0`。

## 3. 四端共同伪代码

```text
loadKrkrAtlasSource(...):
    enumerate and decode all KRKR source records
    bins = ImagePacker.pack(records, maxTextureSize)
    for bin in bins:
        manager = getPrivateOpenGLRenderManager()
        texture = manager.CreateTexture2D(
            null, bin.width * 4, bin.width, bin.height,
            RGBA, STATIC)
        upload every packed record into texture
        publish texture in the persistent KRKR source map
        texture.Release()

getD3DSourceTexture(item):
    if item.sourceState.texture != null:
        return item.sourceState.texture       // borrowed atlas fast path
    if loadKrkrAtlasSource(...) && source.texture != null:
        return source.texture                 // borrowed atlas fast path
    layerTexture = resolve ordinary Layer fallback
    if process renderer is software:
        return cached/private-GL copy(layerTexture)
    return layerTexture
```

因此 source getter 的两个 atlas fast path 必须保留；原版通过 producer 保证 atlas 本身就属于
私有 OpenGL manager。把转换塞到 getter 会偏离四端共同实现，并增加不必要的复制缓存。

## 4. ABI 差异与共同语义

- Android arm64/iOS arm64 通过 64 位对象和虚表偏移调用纹理 factory；armv7 使用 Thumb
  调用序列和 32 位对象布局。
- Android 使用 libstdc++ 容器实现，iOS 使用 libc++；map/vector 的遍历和异常清理形状不同。
- 上述差异不改变 manager receiver、atlas fast path、静态 RGBA 创建参数或引用所有权。

## 5. 本地对照与修复

故障版本的 `cpp/plugins/motionplayer/PlayerResource.cpp` 在 KRKR page loop 中调用：

```text
TVPGetRenderManager()->CreateTexture2D(...)
```

Web 默认 renderer 是 software，所以这里发布了 `tTVPSoftwareTexture2D`。修复后改为：

```text
render_backend_guess::getPrivateOpenGLRenderManager_guess()
    ->CreateTexture2D(...)
```

这把对象的创建域与后续 `OperateTriangles` 的消费域重新对齐，并与四个参考二进制一致。
现有 production-atlas 单元测试同时加入私有 manager probe，防止回退到默认 manager。

## 6. ASAN 旁路说明

ASAN 构建在目标界面之前报告 `tTJSString::IndexOf` 构造单字符临时串时的
stack-buffer-overflow。该路径的显式长度和额外读取也能在原版实现中观察到，与本次纹理
类型错误无关。由于 ASAN 会在这类报告处终止进程，本次目标故障按允许的流程改用非 ASAN
构建复现和验证；ASAN 仍用于确认这不是同一处内存破坏的直接报告。
