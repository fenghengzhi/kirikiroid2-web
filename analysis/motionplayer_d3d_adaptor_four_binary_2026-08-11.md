# Motion.D3DAdaptor 四参考二进制共同实现（2026-08-11）

## 结论

`Motion.D3DAdaptor` 是一个脚本可见的渲染目标适配器。它持有窗口引用、逻辑宽高/中心、五个布尔状态、清屏色、一个当前渲染目标纹理，以及一个只在软件渲染器中使用的“源纹理指针 -> 静态副本纹理”有序树。

四个参考二进制给出的源级语义一致：

- `setPos`、`removeAllBg`、`removeAllCaption`、`registerBg`、`registerCaption`、`unloadUnusedTextures` 是真实空函数；
- `setSize` 只写逻辑宽高，不重建或缩放目标纹理；
- `removeAllTextures` 析构软件纹理树中每一个 mapped intrusive holder，然后清空树；
- `captureCanvas` 是有一个 `tTJSVariant` 参数的普通 NCB 方法，不是 raw callback；
- 软件渲染时，`captureCanvas` 把目标纹理按 pitch 复制进 Layer；
- GPU 渲染时，`captureCanvas` 把已渲染目标纹理交给 Layer，并尽可能回收 Layer 原来的非静态、同尺寸纹理作为下一帧目标；
- 清屏和捕获使用目标纹理的实际尺寸；普通渲染仍使用 adaptor 的逻辑宽高；
- `canvasCaptureEnabled` 是 Player-to-target 渲染门，`clearEnabled` 是显式 FillARGB 门；
  `visible`、`alphaOpAdd`、`resizable`、`clearColor` 只保留脚本写入状态，不驱动当前 native
  pipeline，其中共享 renderer 的 alpha-add 形参固定传字面量 `true`，清屏颜色来自本次调用参数；
- GNU STL 和 libc++ 只导致红黑树 header/node 布局及对象总大小不同，没有产生插件级行为差异。

因此，旧实现中“独立 CPU `_buffer`”“`setSize` 重建纹理”“捕获总是 CPU readback”“raw callback 返回传入 Layer”等行为均不是四参考二进制的实现。

## 四平台地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 成员注册器 | `0x6AA274` | `0x57CC58` | `0x1001039A4` | `0x100D94` |
| NCB 实例工厂 | `0x6AA8F8` | `0x57CEBC` | `0x100103C30` | `0x100FD4` |
| 构造函数 | `0x6AAEF0` | `0x57D0AC` | `0x100103FA8` | `0x10128C` |
| 析构函数 | `0x6AAFCC` | `0x57D12E` | `0x1001040A0` | `0x1013BC` |
| `setPos` | `0x6AAB84` | `0x57CF64` | `0x100103D3C` | `0x101128` |
| `setSize` | `0x6AAB88` | `0x57CF66` | `0x100103D40` | `0x10112A` |
| `setClearColor` | `0x6AAB90` | `0x57CF6C` | `0x100103D48` | `0x101130` |
| `setResizable` | `0x6AAB98` | `0x57CF70` | `0x100103D50` | `0x101134` |
| `removeAllTextures` | `0x6AAC98` | `0x57CF74` | `0x100103D58` | `0x101138` |
| `removeAllBg` | `0x6AACD0` | `0x57CF7A` | `0x100103D88` | `0x101154` |
| `removeAllCaption` | `0x6AACD4` | `0x57CF7C` | `0x100103D8C` | `0x101156` |
| `registerBg` | `0x6AACD8` | `0x57CF7E` | `0x100103D90` | `0x101158` |
| `registerCaption` | `0x6AACDC` | `0x57CF80` | `0x100103D94` | `0x10115A` |
| `unloadUnusedTextures` | `0x6AACE0` | `0x57CF82` | `0x100103D98` | `0x10115C` |
| `visible` get/set | `0x6AACE4` / `0x6AACEC` | `0x57CF84` / `0x57CF88` | `0x100103D9C` / `0x100103DA4` | `0x10115E` / `0x101162` |
| `alphaOpAdd` get/set | `0x6AACF8` / `0x6AAD00` | `0x57CF8C` / `0x57CF90` | `0x100103DAC` / `0x100103DB4` | `0x101166` / `0x10116A` |
| `captureCanvas` | `0x6AAD0C` | `0x57CF94` | `0x100103DBC` | `0x10116E` |
| `canvasCaptureEnabled` get/set | `0x6AAEC8` / `0x6AAED0` | `0x57D09C` / `0x57D0A0` | `0x100103F88` / `0x100103F90` | `0x10127C` / `0x101280` |
| `clearEnabled` get/set | `0x6AAEDC` / `0x6AAEE4` | `0x57D0A4` / `0x57D0A8` | `0x100103F98` / `0x100103FA0` | `0x101284` / `0x101288` |
| 释放目标纹理 helper | `0x6AB058` | `0x57D16E` | 内联 | 内联 |
| 清目标纹理 | `0x6AB08C` | `0x57D184` | `0x100104130` | `0x10149C` |
| 从 Player 渲染 | `0x6AB204` | `0x57D2CC` | `0x100104284` | `0x101680` |
| 纹理树递归清理 | `0x6D8C38` | `0x59A8CE` | `0x1001285E4` | `0x127928` |

所有上述函数均已在四个 IDB 中用 `D3DAdaptor_*_guess` 或明确的 `*_noop` 名称标注。构造、析构、`setSize`、`removeAllTextures`、`captureCanvas`、清屏和渲染函数也已应用保守的函数类型；定型后重新生成了四平台伪代码并保存数据库。

## 脚本成员表与 ABI 边界

四个平台的注册器都按相同次序发布 15 个成员：

1. `setPos`
2. `setSize`
3. `setClearColor`
4. `setResizable`
5. `removeAllTextures`
6. `removeAllBg`
7. `removeAllCaption`
8. `registerBg`
9. `registerCaption`
10. `unloadUnusedTextures`
11. `visible`（读写属性）
12. `alphaOpAdd`（读写属性）
13. `captureCanvas`
14. `canvasCaptureEnabled`（读写属性）
15. `clearEnabled`（读写属性）

`captureCanvas` 所用 NCB FuncCall 模板要求 `numparams >= 1`，复制第一个 `tTJSVariant` 后调用成员函数；它没有 raw-callback 的 result/numparams/param/objthis 形状，也不会把传入 Layer 写回脚本结果。

工厂共同边界：

```text
if numparams < 5:
    return TJS_E_BADPARAMCOUNT
if param[0] is not an instance of "Window":
    throw "must set Window object"

window  = param[0]
width   = integer(param[1])
height  = integer(param[2])
centerX = integer(param[3])
centerY = integer(param[4])
construct(window, width, height, centerX, centerY)
```

构造函数先保存四个整数和 raw Window、初始化属性字节和容器，再对非 null Window
dispatch 执行 `AddRef`，最后创建：

```text
CreateTexture2D(
    pixels = null,
    pitch = 0,
    width,
    height,
    format = RGBA /* 4 */,
    flags = 0)
```

四端都直接把脚本传入的有符号 `int` 位模式作为纹理宽高实参；构造体内没有 `width > 0`、`height > 0` 或创建结果检查。Android arm64 的定型伪代码明确表现为转成 `unsigned int` 后调用，其他三端寄存器传参等价。创建失败留下 null target；参考后续渲染/软件捕获并没有 null-target 保护，因此这是调用者/渲染后端必须维持的不变量，不是 adaptor 自己恢复的状态。

四端的 width/height/center 字段都在 `Window::AddRef` 前完成写入。AddRef 之后的 manager
lookup 或 texture factory 若抛出，constructor 只清理已构造的 map subobject，不调用完整
destructor，也不 Release raw Window；外层 new-expression 虽会 delete storage，该 Window
引用仍泄漏。`CreateTexture2D` 返回 null 则不是异常，constructor 会正常返回并发布 null
target。精确的 constructor/cold landing/SjLj 证据见
`motionplayer_d3d_adaptor_constructor_failure_lifecycle_four_binary_2026-08-15.md`。

## 语义对象布局

64 位四平台在目标纹理之前的字段一致：

| 偏移 | 字段 |
|---:|---|
| `+0` | `int width` |
| `+4` | `int height` |
| `+8` | `int centerX` |
| `+12` | `int centerY` |
| `+16` | 独立 `int32` dormant state：构造写 0，插件范围无后续访问，原名未知 |
| `+20` | `bool visible` |
| `+21` | `bool canvasCaptureEnabled` |
| `+22` | `bool clearEnabled`，初值 1 |
| `+23` | `bool resizable` |
| `+24` | `bool alphaOpAdd` |
| `+32` | AddRef 持有的 Window dispatch 指针 |
| `+40` | `int clearColor` |
| `+48` | AddRef/Release 管理的目标纹理 |
| `+56` | 软件纹理副本有序树起点 |

32 位平台的对应位置是：Window `+28`、clearColor `+32`、目标纹理 `+36`、树 `+40`；前面的整数和布尔偏移保持不变。

`+16` 不是编译器 padding。四端构造均有显式零写；Android 两端把它与后续四个布尔字节
组成 packed store，iOS 两端独立写 32 位零。完整 constructor/public-member/capture/render/
Player callback/析构 consumer audit 没有构造后的命中，故本地保守命名为
`_dormantState_guess`。详细证据见
`motionplayer_d3d_adaptor_dormant_prefix_state_four_binary_2026-08-15.md`。

对象总大小反映各平台标准库的树 header 布局：

| 平台 | 对象大小 | 树实现特征 |
|---|---:|---|
| Android arm64 | `0x68` | GNU libstdc++ 红黑树 header |
| Android armv7 | `0x40` | GNU libstdc++ 红黑树 header |
| iOS arm64 | `0x50` | libc++ `__tree` header |
| iOS armv7 | `0x34` | libc++ `__tree` header |

本地 C++ 使用
`std::map<iTVPTexture2D *, TJS::tTJSRefHolder<iTVPTexture2D>>` 表达源级容器，
不尝试把任一平台的私有 STL ABI 固化进可移植结构。mapped holder 的无条件
AddRef/Release 已由后续 getter/emplace 专项复核确认，详见
`motionplayer_render_source_texture_four_binary_2026-08-13.md`。

## 软件纹理副本树

渲染项的源纹理 getter 在软件渲染器中查询 adaptor 的有序树，key 是借用的原始
源纹理指针，mapped value 是无条件 AddRef/Release 的 `tTJSRefHolder`。miss 路径
共同为：

```text
copy = CreateTexture2D(
    source.GetScanLineForRead(0),
    source.GetPitch(),
    source.GetWidth(),
    source.GetHeight(),
    source.GetFormat(),
    STATIC /* 1 */)
tree.emplace(source, copy)
return copy
```

其中工厂属于按名字 `"opengl"` 取得并以函数局部静态缓存的专用 manager，不是
process-default renderer。GPU 渲染器不经过此复制，直接返回源纹理。软件 miss 对
null source 没有 guard，
创建结果为 null 时 mapped holder 构造也没有 guard；两者都会走原生空指针解引用。
此外，map insertion AddRef 后 caller 不释放工厂 creation ref；清树只 Release
holder 的一份引用。完整引用计数证据和 GNU/libc++ duplicate insertion 差异见后续
专项文档。四端还固定 manager-first、scanline、pitch、width/height、format 的查询顺序；
若这些外部调用重入并先提交同一 key，emplace 保留旧节点而 getter 返回本次未缓存的新
copy。完整 call-site、allocator/null 边界与两套 STL candidate 生命周期见
`motionplayer_d3d_adaptor_software_texture_emplace_commit_four_binary_2026-08-15.md`。

`removeAllTextures` 和析构函数都递归遍历所有节点，对 mapped texture 调用 `Release`，删除
节点，并恢复空树 header。key 只是非拥有的身份值，不执行 `AddRef`/`Release`。析构的源级
顺序是：清软件纹理树、释放并清零当前目标纹理、释放但不清零 Window raw slot、销毁空树。
GNU 两端按 descending key 释放节点，libc++ 两端按 left-right-root postorder；header 只在全部
节点清理返回后发布为空。完整 traversal、重入和 terminate 边界见
`motionplayer_d3d_adaptor_destructor_texture_map_four_binary_2026-08-15.md`。

## `setSize` 的非直觉边界

四平台的定型后伪代码完全等价：

```cpp
void setSize(void *self, int width, int height) {
    self->width = width;
    self->height = height;
}
```

它不比较旧值，不释放目标纹理，不创建新纹理，也不调整中心点。因此构造为 `1024 x 768` 后调用 `setSize(320, 200)`，逻辑字段变成 `320 x 200`，但现有目标纹理仍是 `1024 x 768`。这种逻辑尺寸与实际纹理尺寸的分离是原始边界，不应由本地实现“修正”。

## `captureCanvas` 数据流

第一步把传入 variant 转为原生 Layer。非 Layer 参数走核心 Layer 转换错误路径。

### 软件渲染器

共同伪代码：

```text
textureWidth  = targetTexture.GetWidth()
textureHeight = targetTexture.GetHeight()
layer.SetImageSize(textureWidth, textureHeight)

src      = targetTexture.GetScanLineForRead(0)
srcPitch = targetTexture.GetPitch()
dst      = layer.GetMainImagePixelBufferForWrite()
dstPitch = layer.GetMainImagePixelBufferPitch()

if srcPitch == dstPitch:
    memcpy(dst, src, srcPitch * textureHeight)
else:
    repeat textureHeight rows:
        memcpy(dstRow, srcRow, textureWidth * 4)
```

这里使用的是目标纹理实际宽高，不是 adaptor 逻辑宽高。函数没有显式 result 写回，也没有在复制结束后额外调用 Layer `Update`；Layer 自身的尺寸/写缓冲 API 仍可产生其固有副作用。

### GPU 渲染器

共同伪代码和引用计数顺序：

```text
layer.IndependMainImage()
candidate = layer.MainImage.texture
replacement = null

if !candidate.IsStatic()               // candidate 无 null guard
   && candidate.width  == targetTexture.width
   && candidate.height == targetTexture.height:
    candidate.AddRef()
    replacement = candidate

layer.AssignTexture(targetTexture)  // Layer AddRef 新纹理，释放旧纹理
targetTexture.Release()             // 无 null guard；adaptor 放弃刚交给 Layer 的引用
targetTexture = replacement

if replacement was not accepted:
    targetTexture = CreateTexture2D(
        null, 0, adaptor.width, adaptor.height, RGBA, 0) // 无尺寸/结果 guard
```

这不是“把 GPU 纹理读回 Layer”的路径，而是所有权交换与 render-target recycling。只有 Layer 原纹理同时满足“非静态、实际宽高与刚渲染目标一致”时才复用；格式没有参与接受条件。创建替代目标时反而使用 adaptor 逻辑宽高，因此 `setSize` 的字段写入会在下一次无法复用 Layer 旧纹理时生效。

该交换不是事务：candidate 的保护性 AddRef 没有 RAII rollback；`AssignTexture` 后先释放并
发布 candidate/null，才在 no-reuse 路径创建新 target。创建抛出或返回 null 时 Layer 已持有
旧 target 而 adaptor 保持 null。software 路径也保留 signed-height gate、equal-pitch 的
32 位乘法后符号扩展及严格 source-before-destination getter 顺序。完整提交/异常证据见
`motionplayer_d3d_adaptor_capture_commit_boundaries_four_binary_2026-08-15.md`。

## 清屏和从 Player 渲染

清屏函数真实签名为 `clearTargetTexture(int color)`。它自己检查 `clearEnabled`；false 时立即返回且不触碰静态 guard。true 时通过两个独立 guard 惰性缓存 raw `FillARGB` 方法指针和 `color` 参数 ID，写入**本次调用参数 `color`**，并以当前目标纹理同时作为 destination/reference target，对其实际宽高矩形执行操作。此前把函数第二参数误认成成员 `clearColor`，是未给函数补齐原型造成的反编译假象。

从 Player 渲染函数的真实返回类型为 `void`，共同顺序是：

```text
if !canvasCaptureEnabled:
    return
bind targetTexture as render target
render prepared items into targetTexture
```

`D3DAdaptor_renderFromPlayer` 除 capture 开关外没有 adaptor/null、逻辑尺寸、motion-content、source-cache 或 target-texture guard。四端两个直接 caller 都先走 Player 的 render-item prepare gate；adaptor 构造和 GPU capture 尾部负责维持 target。函数因此直接绑定 `targetTexture`，把逻辑宽高交给共享 renderer，并忽略 renderer 的内部结果。它也不调用清屏 helper。对 Android arm64 的两个 `std::function` capture 继续下钻后可排除“清屏藏在闭包里”：`{adaptor, player}` 闭包只取得源 Layer 纹理并访问 adaptor 的软件纹理 `std::map`，`{adaptor}` 闭包只返回目标纹理；两者均不读取 `clearColor`。共享 renderer 中按需执行的 clear 只针对 GL stencil buffer，也不是 `FillARGB` 颜色清屏。

颜色清理是在 `Player.clear` 的 D3DAdaptor 快速路径中发生，颜色来自该次 clear 的 fill Variant；D3D 命中后立即返回，不递归 child Player。渲染调用携带 adaptor 逻辑宽高和软件源纹理 getter，但共享 renderer 给方法选择器的 `alphaOpAdd` 形参固定为字面量 `true`，不会读取 adaptor 的同名属性；这与显式清屏/捕获使用目标纹理实际尺寸的行为有意不同。六个状态字段的完整 consumer 证据见
`motionplayer_d3d_adaptor_state_consumers_four_binary_2026-08-15.md`。

## 本地复原变更与验证

本轮对本地实现作了以下修正：

- 删除旧 `libkrkr2.so` 地址注释和错误的 `_buffer` 状态；
- 恢复以 `tTJSRefHolder` 为 mapped value 的 `std::map` 软件纹理副本所有权与
  `removeAllTextures`；
- 把 `setSize` 改为纯字段写入；
- 把 `captureCanvas` 从 raw callback 改为普通 `NCB_METHOD`；
- 恢复软件逐行复制和 GPU 纹理交换/复用；
- 清屏改为显式接收本次颜色参数，使用实际纹理尺寸、target-as-reference，并让函数内部处理 `clearEnabled`；
- 删除普通 Player-to-D3D 渲染路径中四参考二进制不存在的额外清屏调用；
- 构造和 GPU capture 的 replacement-miss 路径改为无条件创建目标纹理，不再用正尺寸 guard 改写无效尺寸/创建失败边界；
- `renderFromPlayer` 恢复为只检查 `canvasCaptureEnabled` 的 `void` helper，prepare gate 留在两个上层 caller，移除本地额外的 null、尺寸、motion/source-cache/target 和递归保护；
- Window storage 改为单 dispatch 指针的 AddRef/Release，并按原生字段顺序重排本地类；
- 将 Player 的 D3D 源纹理 getter 接到 adaptor 软件副本表；
- 更新构造/尺寸、pitch 复制和软件缓存单测。

验证结果：

- 四个 IDB 定型后的 `setSize`、`removeAllTextures`、`captureCanvas` 逐平台复核一致；
- Web Debug Build 通过；
- Wasmtime Headless Debug Build 通过；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web 编译参数执行
  Emscripten `-fsyntax-only` 通过，只有仓库既有的 `_tss` 字面量声明弃用警告；
- `git diff --check` 通过时只报告工作区既有的 LF/CRLF 转换提醒。
