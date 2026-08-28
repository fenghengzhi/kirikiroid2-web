# ObjSource getter / clip / draw / 解码 / 纹理生命周期四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `Motion.ObjSource` 的 6 个非构造 NCB callback：`originX`、`originY`、
`width`、`height`、`clip` 和 `drawLayer`。`drawLayer` 的闭包继续向下覆盖：

- lazy `ensureTexture`；
- raw PSB `width/height/compress/pixel/pal` 数据流；
- RL8 / RL32 解码、palette 展开、RGB/BGRA 转换；
- aligned raw buffer、palette vector、Bitmap、Texture 的发布与异常 owner；
- adaptor 到 `ObjSource` 的销毁链，以及 Texture 先于 PSBRawNode owner 释放的成员逆序；
- `clip` 返回 Dictionary 的完整 UTF-16LE 成员名，排除 Hex-Rays 的 `r/b` 截断误导。

四个参考二进制共同构成权威。本地 `SourceCache.cpp/.h` 仅在四端 fresh map、完整反编译/
反汇编和共同伪代码完成后逐行对照。

## 2. 六个 callback 地址与 fresh 指令量

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `originX` | `0x69A3F4` / 48 | `0x57511C` / 31 | `0x1000F8E88` / 18 | `0xF5D4C` / 50 |
| `originY` | `0x69A4B8` / 48 | `0x575180` / 31 | `0x1000F8EEC` / 18 | `0xF5E04` / 50 |
| `width` | `0x69A57C` / 55 | `0x5751E4` / 38 | `0x1000F8F50` / 25 | `0xF5EBC` / 58 |
| `height` | `0x69A65C` / 55 | `0x575258` / 38 | `0x1000F8FD0` / 25 | `0xF5F8C` / 58 |
| `clip` | `0x69A73C` / 172 | `0x5752CC` / 154 | `0x1000F9050` / 124 | `0xF605C` / 225 |
| `drawLayer` | `0x69AAB8` / 24 | `0x5754E4` / 20 | `0x1000F930C` / 24 | `0xF63C0` / 21 |

表中斜线后的数字是 fresh 完整反汇编指令数；所有 cursor 均到 `done=true`。四端每个
callback 同时完成 fresh decompile、调用目标和字符串/xref 对照。

## 3. 四个简单 getter 的共同源码

```text
getOriginX():
    return source.GetDictionaryValueStrict("originX").GetInt()

getOriginY():
    return source.GetDictionaryValueStrict("originY").GetInt()

getWidth():
    if source.GetTypeCategory() != Dictionary(7): return 32
    return source.GetDictionaryValueStrict("width").GetInt()

getHeight():
    if source.GetTypeCategory() != Dictionary(7): return 32
    return source.GetDictionaryValueStrict("height").GetInt()
```

`originX/originY` 没有 category gate；空 facade 或非 Dictionary 会进入 strict raw lookup 的
原版失败边。`width/height` 只在 category 不是 7 时返回 32；Dictionary 缺成员时仍 strict
失败，不能把 32 当成“缺字段默认值”。每次 strict getter 产生的临时 PSBRawNode owner 都
在正常路径和该平台的异常清理路径中释放。

## 4. `clip` 的共同伪代码

```text
getClip():
    clip = empty PSBRawNode
    if source.category != Dictionary(7):
        return Void
    if !source.tryGet("clip", clip):
        return Void

    dictionary = new Dictionary
    dictionary["left"]   = clip.strict("left").GetDouble()
    dictionary["top"]    = clip.strict("top").GetDouble()
    dictionary["right"]  = clip.strict("right").GetDouble()
    dictionary["bottom"] = clip.strict("bottom").GetDouble()
    return Variant(dictionary, dictionary)
```

只有外层 `clip` 使用 try-get；一旦存在，四个子字段全部 strict，值按 Double 发布。四次
SetValue 均使用 `TJS_MEMBERENSURE` (`0x200`) 和四个彼此独立的 process-wide hint slot。
返回 Variant 的 Object 与 ObjThis 都是同一个新 Dictionary dispatch。

如果任一 strict read、Dictionary 分配或 setter 抛出，已取得的 raw-node owner 和 Dictionary
closure 按对应 ABI landing pad 释放；返回对象只在四个 setter 都执行后发布。普通 setter
返回状态不另设回滚分支。

## 5. `right/bottom` 不是 `r/b`

Hex-Rays 在四端都曾把两个 Dictionary 成员指针显示成 `"r"` / `"b"`。本轮使用
`ida-search-string` 的 UTF-16LE raw-byte 流程搜索完整 NUL 结尾字符串，并把结果回连到
`clip` callback：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `left` | `0x14D6F9A` | `0xD75FD2` | `0x10195B61C` | `0x174D980` |
| `top` | `0x14CD548` | `0xD7F6C6` | `0x10195B626` | `0x174D98A` |
| `right` | `0x14C175E` | `0xD84EEA` | `0x10195B62E` | `0x174D992` |
| `bottom` | `0x14C176A` | `0xD780EC` | `0x10195B63A` | `0x174D99E` |

四端、四种 pattern 的分页 cursor 全部 `done=true`。相关 xref 分别落在四端 callback 的
四次 strict-read/SetValue 序列中，例如 Android arm64 的 `0x69A894/0x69A8FC`、iOS arm64
的 `0x1000F9180/0x1000F91CC`、iOS armv7 的 `0xF61E0/0xF6236`。因此本地完整成员名是
正确的，短字符串只是 IDA 对相邻/错误类型化宽字面量的显示伪影。

## 6. `drawLayer` 共同伪代码

```text
drawLayer(target):
    if source.category != Dictionary(7):
        return
    ensureTexture()
    layer = Layer::FromVariant(target)     // strict native-instance conversion
    layer.AssignTexture(texture)
    layer.SetSize(texture.width, texture.height)
```

category gate 在目标 Variant 转换之前；因此默认空 ObjSource 对任意目标都是无操作。
Dictionary 路径先完成/复用 lazy texture，再转换目标。Layer conversion 的共同 helper 只在
dispatch 非空且 NativeInstanceSupport 失败时抛 `TVPSpecifyLayer`；typed-null/非 Object 可返回
null，随后 AssignTexture 的未检查解引用保留 sharp boundary。AssignTexture 完成后才读
texture width/height 并设置 Layer 尺寸；没有 rollback。

## 7. lazy texture 主函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ensureTexture` | `0x6D7834` / 378 | internal `0x599A34` / 277 | `0x10012686C` / 224 | `0x125D4C` / 360 |
| local EH cleanup | 函数尾部 `0x6D7DD8..0x6D7E3C` | 无 local landing | `0x100126C14` / 28 | `0x126116` / 57 |
| RL32 decoder | `0x6D7CF0` 内联 | `0x571DA4` / 33 | `0x1000F5474` / 39 | `0xF1F10` / 33 |
| RL8 decoder | `0x6D7B70` 内联 | `0x599BEA` 内联 | `0x1000F5510` / 34 | `0xF1F6A` / 32 |

Android armv7 的 IDA 函数 `0x5999F4` 总计 301 条指令：前 24 条是无关的 vector growth
helper，registrar/caller 使用的 `ensureTexture` 真实内部入口是 `0x599A34`，其后 277 条。
本轮保留该真实 disposition，只加内部入口注释/书签，不创建重叠函数。

## 8. `ensureTexture` 共同数据流

```text
ensureTexture():
    if texture != null: return

    width  = uint32(source.strict("width").GetInt())
    height = int32(source.strict("height").GetInt())
    pixelCount = low32(width * uint32(height))

    compressed = source.contains("compress")
                 && strcmp(source.strict("compress").GetString(), "RL") == 0

    if compressed:
        compressedHasPalette = source.contains("pal")
        pixelData = source.strict("pixel").GetResource(resourceSize)
        decoded = AlignedAlloc(
            low32((compressedHasPalette ? 1 : 4) * pixelCount), 4)
        if compressedHasPalette:
            decodeRL8(decoded, pixelData, resourceSize)
        else:
            decodeRL32(decoded, pixelData, resourceSize)
            ReverseRGB(decoded, decoded, pixelCount)
        sourcePixels = decoded
    else:
        pixelData = source.strict("pixel").GetResource(resourceSize)
        sourcePixels = pixelData
        decoded = null

    if source.contains("pal"):
        paletteData = source.strict("pal").GetResource(resourceSize)
        paletteCount = signed32(resourceSize) / 4
        palette = vector<uint32>(size_t(paletteCount))
        ReverseRGB(palette.data, paletteData, paletteCount)
        bgra = AlignedAlloc(low32(4 * pixelCount), 4)
        Expand8BitTo32BitPal(bgra, sourcePixels, pixelCount, palette.data)
        if decoded != null: AlignedDealloc(decoded)
    else if decoded != null:
        bgra = decoded
    else:
        bgra = AlignedAlloc(low32(4 * pixelCount), 4)
        ReverseRGB(bgra, pixelData, pixelCount)

    bitmap = new Bitmap(width, uint32(height), 32)
    pitch = bitmap.GetPitch()
    dst = bitmap.GetScanLine(0)
    rowBytes = low32(4 * width) as int32
    if rowBytes == pitch:
        memcpy(dst, bgra, size_t(low32(height * pitch) as int32))
    else if height >= 1:
        repeat height times:
            memcpy(dst, bgra, size_t(rowBytes))
            dst += pitch
            bgra += rowBytes

    texture = RenderManager().CreateTexture2D(bitmap)  // 直接发布成员
    bitmap.Release()
    AlignedDealloc(originalBgra)
```

固定 raw keys 全部 strict。`compress` 只有精确 C 字符串 `RL` 才开启解码；其他值与缺字段
都走未压缩分支。`pal` 会检查两次：第一次只决定 RL element width，第二次决定 palette
展开；中途 raw data 不一致仍按这个次序执行。

同一个未初始化的 32 位 stack slot 传给每次 GetResource；成功路径由 getter 覆盖它。所有
尺寸乘法都保留低 32 位。负 height、过大 width、负/畸形 resource size 不做合法性检查，
会继续影响分配长度、vector size、指针和 memcpy 边界。

## 9. RL8 / RL32 的精确边界

共同编码为 marker stream：

- marker bit7 为 0：literal count=`marker+1`；RL8 复制 count bytes，RL32 复制
  `4*count` bytes；source 与 destination 各前进一次对应长度；
- marker bit7 为 1：repeat count=`(marker&0x7F)+3`；RL8 读取一个 byte 并 memset，
  RL32 读取一个 little-endian uint32 并重复写入；source 分别前进 2 / 5 bytes；
- 循环唯一结束条件是 source 到达或越过 `source + signed32(resourceSize)`；无 output
  capacity、payload 完整性或 marker 对齐检查，越界输入保持原版未定义/非法访问边界；
- signed size `<1` 时不读 marker。Android arm64 优化体会先形成 end pointer，再检查；
  其余三端先做有符号 gate 再执行实际地址加法。这是同一源结构的优化差异，也意味着
  负 size 的机器级无副作用边界不应从单一平台外推。

四端明确证明 RL8 literal run 只前进一次 destination。本地当前第 258--262 行也是一次
`destination += count` 和一次 `source += count`；先前审阅摘要中的“双前进”只是转录错误，
源码无需修改。

## 10. Bitmap、Texture 与异常 owner

关键 helper family：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| aligned alloc/free | `0xA0C748/0xA0C790` | `0x75F5E8/0x75F60A` | `0x10025836C/0x1002583B4` | `0x259720/0x259742` |
| Bitmap ctor | `0xA747AC` | `0x797F7E` | `0x10004FAE0` | `0x4EDCC` |
| Bitmap Release path | inline + `0xA749E4` | `0x599D5C` | `0x1000512E4` | `0x505F0` |
| render-manager getter | `0x848834` | `0x6571B4` | `0x100323D78` | `0x32915C` |

三个平台（Android arm64、iOS arm64、iOS armv7）的 landing pad 在 Bitmap constructor
抛出时删除仍处于 new-expression 构造阶段的分配，并清理当时存活的临时 raw node/vector；
Android armv7 的合并函数没有 local EH landing。构造成功以后，四端都没有 bitmap、decoded
或 BGRA 的通用 unwind owner。

因此：

- aligned allocation 后任一后续 strict read、vector 操作、颜色转换或 Bitmap/renderer
  操作抛出，都可能泄漏 decoded/BGRA；
- Bitmap 构造成功后，后续 memcpy、RenderManager lookup 或 CreateTexture2D 抛出时 Bitmap
  也没有 RAII guard；
- `CreateTexture2D` 返回值直接写入 `_texture`，然后才 Release Bitmap 和 free BGRA。
  成员发布后的清理若失败，会留下已提交 texture；调用抛出前则仍为 null；
- `ensureTexture` 命中非空 `_texture` 时完全跳过 raw PSB 读取和再分配。

本地应保留 raw pointer/source structure，不能用 `unique_ptr` 或 scope guard 抹平这些泄漏和
partial-publication 边界。

## 11. ObjSource / adaptor 销毁链

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| adaptor shell factory | `0x6FBCA8` | `0x5B6E84` | `0x10014E4D8` | `0x1502D8` |
| adaptor owner-clear core | `0x6FBCDC` / `0x6FBD70` | internal `0x5B6EF8` | `0x10014E588` | `0x150350` |
| ObjSource source destructor | `0x6E145C` / 30 relevant insns | `0x5A1EE8` / 13 | `0x100132A60` / 16 | `0x131AF8` / 50 |

adaptor shell 是 64 位 `0x18`、32 位 `0x0C` 的 native-instance wrapper（vptr、native pointer、
sticky/ownership byte），不要误当成 ObjSource 本体。非 sticky 且 native pointer 非空时，
adaptor 调用 ObjSource destructor 后 delete 本体；sticky/shell-only 路径只清 wrapper 状态。

ObjSource 本体的源级字段只有 `PSBRawNode _source` 和 `iTVPTexture2D *_texture`，大小同样是
64 位 `0x18`、32 位 `0x0C`，但没有自有 vptr。四端析构共同次序为：

```text
if texture != null:
    texture.Release()
destroy source PSBRawNode owner
// adaptor 随后 delete ObjSource allocation
```

Texture Release 抛出时，PSB owner 尚未释放；正常路径从不把成员清零。该次序精确对应
显式 `~ObjSource` body 先运行、随后 `_source` 隐式成员析构的 C++ 结构。

## 12. 平台差异

- Android armv7 把 `ensureTexture` 误并到一个无关 vector growth 函数之后；其他三端有独立
  函数。不能人为定义重叠函数。
- Android 两端主要内联 RL8，Android arm64 同时内联 RL32；iOS 两端保留两个独立 decoder；
  源级算法一致。
- AArch64 可向量化 RL32 repeat fill；32 位端主要标量循环。输出字节序和 run 语义一致。
- Android armv7 没有 ensureTexture local EH landing；另外三端保存 temporary raw owner、
  vector 与 pending new-expression 的 ABI cleanup。正常成功路径 owner 次序一致。
- libc++ / libstdc++ 的 vector allocate/resize、Bitmap refcount helper 和 adaptor vtable thunk
  形态不同；这些是 ABI 实现差异，不应写成源级 padding、平台分支或自制容器。
- 四端 callback、raw keys、尺寸/解码/转换次序、Texture publication 和 destructor 次序没有
  可见源级分歧。

## 13. 与本地源码逐行对照

对应实现：

- `cpp/plugins/motionplayer/SourceCache.h:101`：ObjSource 两字段结构、getter category/strict
  边界和 lazy API；
- `cpp/plugins/motionplayer/SourceCache.cpp:241`：RL8；
- `cpp/plugins/motionplayer/SourceCache.cpp:266`：RL32；
- `cpp/plugins/motionplayer/SourceCache.cpp:307`：显式 Texture-first destructor；
- `cpp/plugins/motionplayer/SourceCache.cpp:315`：fresh clip Dictionary；
- `cpp/plugins/motionplayer/SourceCache.cpp:340`：lazy texture 数据流、raw buffers、Bitmap 和
  direct Texture publication；
- `cpp/plugins/motionplayer/SourceCache.cpp:467`：drawLayer gate、conversion、assign、resize。

六个 callback 的编译语义已经与四端共同证据一致，无需修改生产逻辑。唯一发现的本地
问题是 `ensureTexture` 注释把有 Bitmap-constructor local cleanup 的平台写成“两端”，实际
为三端；这是证据说明错误，不是运行语义差异，应改成“三端，Android armv7 除外”。

现有测试 `tests/unit-tests/plugins/motionplayer-dll.cpp:15780` 已覆盖默认空 facade 的
width/height=32、clip=Void、drawLayer category gate、read-only descriptor 和 adaptor shell
构造边界。实际 raw image/palette/RL 路径目前没有可在缺失正式构建工具的环境中执行的独立
测试；本报告不虚构 fixture 或声称运行验证。

## 14. 状态结论

`ObjSource` 的 6 个非构造 callback 可从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。连同上一 SourceCache slice，`SourceCache/ObjSource/ResourceManager` 的公开
非构造 NCB callback body 已全部闭合；构造器仍保持独立的 constructor-evidence 状态。
完整 motionplayer root-reachable helper、其余 Player/EmotePlayer callback、对象/容器总账和
正式构建审计仍未完成，因此本报告不代表总目标完成。
