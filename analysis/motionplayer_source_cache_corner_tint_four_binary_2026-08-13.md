# SourceCache packed-corner tint 四端复核（2026-08-13）

## 范围与总论

本轮从当前 `reference/binaries/` 四个目标的 `SourceCache::bakeSource` 调用点
重新定位 packed-corner tint helper，替换此前来自旧单端目标的地址式命名与
推断。四端映射为：

| 目标 | `SourceCache::bakeSource` | tint helper |
| --- | ---: | ---: |
| Android arm64 | `0x6A3FC0` | `0x6A48F8` |
| Android armv7 | `0x57A168` | `0x57A754` |
| iOS arm64 | `0x1000FFB24` | `0x10010032C` |
| iOS armv7 | `0xFCD68` | `0xFD4B4` |

四份 helper 已统一命名为 `applyPackedCornerTint_guess`。`_guess` 是必要的：
功能和调用链已经确定，但参考文件没有保留可证明的原始 C++ 符号名。

四端共同控制流为：

```text
if all colors == 0xFF808080: return
if (colors[0] & colors[1] & colors[2] & colors[3]) == 0xFFFFFFFF: return

if !TVPIsSoftwareRenderManager():
    query __Private_Motion_GLLayer native from the Layer Variant
    discard result/status
    return

nativeLayer = tTJSNI_Layer::FromVariant(layer)  // strict helper
clip = nativeLayer.clip
pixels = nativeLayer.main-image buffer for write
pitch = nativeLayer.main-image pitch
intersect clip with the supplied rect
for every intersecting row and column:
    bilinearly interpolate the four packed corner colors
    multiply those RGBA factors into the BGRA destination pixel
```

这里的两个提前返回发生在渲染器查询之前。因此中性色或全白色不仅“不改
像素”，也不会触发 renderer 单例、Layer native query 或私有 GLL query。

## 软件/硬件分流及其缓存语义

四端 tint helper 和四端 bake 低 blend 分支都调用同一个
`TVPIsSoftwareRenderManager` helper：

| 目标 | cached renderer helper |
| --- | ---: |
| Android arm64 | `0x848BDC` |
| Android armv7 | `0x65728C` |
| iOS arm64 | `0x100323EB8` |
| iOS armv7 | `0x32930C` |

四份实现都使用线程安全的函数局部静态初始化 guard，第一次调用时取得 render
manager 并执行一次 `IsSoftware()`，以后返回已缓存的布尔值。旧端口直接调用
`TVPGetRenderManager()->IsSoftware()`，在正常“后端启动后不替换”的生命周期中
结果通常相同，但丢失了原函数只观察第一次结果的结构和边界。本轮两个 call
site 均改为现有引擎函数 `TVPIsSoftwareRenderManager()`。

非软件路径不触碰像素。它严格把 Variant 转为 Object，然后以
`__Private_Motion_GLLayer` ClassID 调用 `NativeInstanceSupport(GETINSTANCE, ...)`；
输出槽预先清零，但调用状态和最终指针都被 tint helper 丢弃。typed-null Object
仍会在 native query 内直接解引用，非 Object Variant 则在严格转换时抛出。

## 软件路径的严格 Layer 转换

软件路径不是“尝试解析 Layer，失败返回 null”。四端调用的是引擎原有的
`tTJSNI_Layer::FromVariant` / `FromObject` 链：

| 目标 | Variant wrapper | Object/native helper |
| --- | ---: | ---: |
| Android arm64 | `0xA7959C`（内联第二层） | 同一函数内 |
| Android armv7 | `0x79AFCE` | `0x79AFF0` |
| iOS arm64 | `0x10035FF10` | `0x10035FF40` |
| iOS armv7 | `0x36366C` | `0x36368C` |

精确边界如下：

1. `Variant::AsObjectNoAddRef()` 是严格转换；非 Object 会在 native query 之前
   抛出转换异常。
2. 对非空 Object，以 Object 自身而不是 closure `ObjThis` 调用
   `NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID, &layer)`。
3. 返回状态为负时立即 `TVPThrowExceptionMessage(TVPSpecifyLayer)`；不会把失败
   吞成空指针，也不会继续取得 clip。
4. typed-null Object 跳过 native query 并返回 null；“状态成功但输出仍为 null”
   也返回 null。tint helper 随后不做 null gate，立即读取 Layer clip。因此这两
   种边界不是 `TVPSpecifyLayer`，而是原生空指针解引用。

此前 tint 和 `textureFromLayerVariant` 都复用了本地 nullable resolver：它会吞掉
native query 失败，再在稍后的解引用处崩溃，改变异常类型和发生时序。本 tint
专项先只按其自身四端证据改正 tint；随后独立的 render-source texture 四端专项
证明 texture extraction 同样使用 strict Layer helper，且无条件继续
`GetMainImage()->GetTexture()`。该已证伪的 nullable 描述现已纠正，详见
`motionplayer_render_source_texture_four_binary_2026-08-13.md`。

## clip、矩形和空区间

四端读取次序一致：严格 Layer 转换后先读 clip，再取得 writable pixel buffer
和 pitch，之后才做矩形交集。传入矩形是四个有符号 32 位整数
`{x, y, width, height}`：

```text
left   = max(clip.left,   rect.x)
top    = max(clip.top,    rect.y)
right  = min(clip.right,  wrap32(rect.x + rect.width))
bottom = min(clip.bottom, wrap32(rect.y + rect.height))
```

矩形右/下边界加法使用低 32 位回绕。实现只对垂直交集做外层 gate：

- `top >= bottom`：在解包颜色和计算起始像素地址之前返回，但 native Layer、
  clip、buffer 与 pitch 已经读取；
- `top < bottom && left >= right`：仍建立起始 row 并逐行增加 pitch，只跳过每行
  的颜色插值和像素写入；
- `top < bottom && left < right`：进入完整像素循环。

起始位置保留参考实现的 32 位乘法边界：`top * pitch` 和 `left * 4` 分别先取
低 32 位并解释为有符号数，再加到 buffer 指针；不是先提升成无限精度/64 位
乘法。

## 四角映射、插值与像素通道

packed color 的字节顺序为低到高 `R, G, B, A`，四个元素的角映射为：

```text
colors[0] = top-left
colors[1] = top-right
colors[2] = bottom-right
colors[3] = bottom-left
```

先以 `rowPosition = y - rect.y` 在左右两条竖边插值，再以
`columnPosition = x - rect.x` 在该行左右结果之间插值。所有减、乘、加都使用
有符号 32 位低字回绕。每次插值等价于：

```text
from + wrap32(position * wrap32(to - from)) / span
spanY = wrap32(rect.height - 1)
spanX = wrap32(rect.width  - 1)
```

目标 buffer 是 BGRA。参考循环把指针定位到每个像素的 G 字节，然后写：

| 指针偏移 | 目标通道 | tint packed byte | 除数 |
| ---: | --- | --- | ---: |
| `+1` | R | byte 0 | `halfAlphaBlend ? 128 : 255` |
| `0` | G | byte 1 | `halfAlphaBlend ? 128 : 255` |
| `-1` | B | byte 2 | `halfAlphaBlend ? 128 : 255` |
| `+2` | A | byte 3 | `255` |

乘色先用无符号 32 位乘法和除法，再把大于等于 255 的结果钳到 255。alpha 永远
除以 255；高 blend nibble 非零只把 RGB 除数切到 128，不改变 alpha。

## 除零与溢出边界

插值源表达式使用有符号整数 `/`。两个 64 位目标直接发出 AArch64 `SDIV`；
零除数结果为 0，`INT_MIN / -1` 保留低字 `INT_MIN`。两个 32 位目标分别调用
动态链接的 `__aeabi_idiv`（Android）与 `___divsi3`（iOS），具体除零处理落在
目标运行时而不在这四个文件内；参考文件本身没有足够证据把它声明成与
AArch64 完全相同的可移植 C++ 行为。这也反证共享源码最可能就是普通有符号
除法，零除数在 C++ 源级没有定义。

实际是否到达除法还受循环 gate 控制：空垂直区间直接返回，空水平区间逐行但
不插值；只有至少一个实际像素时才执行 `spanY`/`spanX` 除法。因此例如有效的
`width == 1` 或 `height == 1` 着色矩形会触发该平台相关边界。Web 端现有
`divideSignedW32LikeArm` 明确采用 AArch64 的 0/`INT_MIN` 结果，避免 Wasm
整数除零 trap；这是有记录的移植选择，不冒充四个 native runtime 在源级共同
保证。

## 源码与回归落点

- `cpp/plugins/motionplayer/SourceCache.cpp`
  - 清除 `0x6A7518` 旧单端地址式命名，改为
    `TintRect_guess`、`lerpTintChannel_guess`、
    `multiplyTintChannel_guess`、`applyPackedCornerTint_guess`；
  - tint 与 bake 统一使用 cached renderer helper；
  - software tint 改用严格 `tTJSNI_Layer::FromVariant`；
  - 清除像素循环中旧目标的绝对地址注释，保留跨四端可证明的语义注释。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 新增 software tint strict-conversion 回归；
  - 使用非 neutral/non-white 四角色确保越过提前返回；
  - 让 fake Layer 的 native query 返回失败，验证抛出 `eTJSError`；
  - 同时锁定 `drawLayer -> width/height -> native query -> exception` 顺序、候选
    Layer 在入链前析构以及缓存仍为空。

验证：Wasmtime headless 的 `SourceCache.cpp` object 重新编译成功；完整
`tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实 Emscripten
defines、include、ABI 参数与既有 Catch2/test config 做 `-fsyntax-only` 成功，
唯一诊断是仓库既有 `_tss` literal-operator 弃用警告。当前配置没有可直接运行
的原生 Catch2 executable，因此这里只报告翻译单元编译验证，不冒充测试已在
运行时执行。

## IDB 改进

四份 tint helper 已统一命名为 `applyPackedCornerTint_guess`，添加四端语义注释
和 bookmark。原有 `TJSNI_Layer_FromVariant_guess`、
`TVPIsSoftwareRenderManager_guess` 与 `SourceCache_bakeSource_guess` 名称经本轮
重新核对仍成立。四份 IDB 均在本轮改名、注释后保存。
