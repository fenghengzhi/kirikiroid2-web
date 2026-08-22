# Motionplayer concrete software/OGL texture 与 adapter 生命周期（四参考二进制）

## 1. 结论

V282 从 V281 已恢复的 `iTVPTexture2D::RecycleProcess -> virtual deleting destructor` 槽继续向内，
枚举该 `Release` 函数在四个 IDB 中的全部 vtable data xref，恢复了内置 concrete texture hierarchy：

```text
iTVPTexture2D
├─ iTVPSoftwareTexture2D
│  └─ tTVPSoftwareTexture2D_static          // borrowed pixels, empty dtor
│     ├─ tTVPSoftwareTexture2D               // owns tTVPBitmap ref
│     └─ tTVPSoftwareTexture2D_compress      // second callback base + lazy pixels
│        ├─ tTVPSoftwareTexture2D_half       // two vectors of shared/unique lines
│        └─ tTVPSoftwareTexture2D_lz4        // vector<Block>
│           └─ tTVPSoftwareTexture2D_lz4_tlg5
└─ tTVPOGLTexture2D                          // abstract GL owner
   ├─ tTVPOGLTexture2D_static
   ├─ tTVPOGLTexture2D_mutatble
   └─ tTVPOGLTexture2D_split                 // map<index, GLTextureInfo> + Bitmap

tTVPOGLTexture2D::AdapterTexture2D
└─ cocos2d::Texture2D, owns one reverse iTVPTexture2D ref
```

四端共同析构链与当前源码一致；没有发现需要改变 concrete texture 运行语义的偏差。最重要的
生命周期结论是：

- software adapter 是普通 autoreleased `cocos2d::Texture2D`，上传/复制像素后不持有 software wrapper；
- OGL adapter 直接借用 owner 的 GL name，因此构造时直接 `owner->AddRef()`，析构时先把自身
  `_name` 写 0，再虚调用 `owner->Release()`；
- OGL same-size adapter reuse 只覆写 GL name，**不重绑 `_owner`**，所以 adapter 继续持有旧 owner；
- built-in concrete texture destructor 本身没有直接调用另一个 `iTVPTexture2D::Release`。V281 的
  recycle 重入 append 在内置 destructor 图中没有直接边，但 adapter 析构可以在其它时点把 owner
  压入 deferred vector；
- software compressed texture 的 continuous hook 是裸 secondary-base pointer vector；remove 只把所有
  相等 entry 改成 null，不 erase；
- OGL base constructor 有意不初始化 `internalW/internalH`，而 base destructor 无条件用二者扣减
  `_totalVMemSize`；本轮后续取证已纠正“`TVPCheckMemory` 可在提交前抛异常”的旧解释：四个发布目标
  的 callee 都是空函数，所有可见 constructor `new[]` 又位于 `InternalInit` 提交之后；
- OGL split destructor 先把尺寸清 0；split cache 和 `AsSingleTexture` 生成的单一 GL name 从未增加
  `_totalVMemSize`，因此这里不是防止重复计账，而是保持整条 split 路径不计账。

当前源码中唯一明确失真的内容是六条 software adapter 和两条 window update diagnostic 把函数标成
旧 `libkrkr2.so` 地址 `0xAA6268`。四个新参考目标使用不同地址，iOS 还裁剪了相同符号；本轮已删除
这个地址字段，保留稳定的 stage/kind 语义标签。

## 2. vtable 穷举依据

V281 已确认 `iTVPTexture2D::Release` 位于 texture vtable slot 2。对四端 Release 地址做全量 data-xref：

| 目标 | Release | vtable data xref 数 | 解释 |
|---|---:|---:|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0x846C48` | 11 | interface/base 1 + software 6 + OGL abstract/concrete 4 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | Thumb `0x6563C5` | 11 | 同上 |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x100323480` | 10 | interface/base 1 + software 6 + OGL concrete 3；abstract vtable 被裁剪 |
| `Kirikiroid2_1.3.9_iOS_armv7` | Thumb `0x3288F5` | 10 | 同上 |

因此本轮不是据一次源码搜索宣称“只有这些类”，而是先从四个二进制的共同 virtual slot 反向枚举了
所有内置 vtable，再用构造点、allocation size、字段和 override slot 逐一归类。

## 3. software hierarchy 函数映射

### 3.1 创建与选择器

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| default `Create` | `tTVPSoftwareTexture2D_Create_guess@0x8497F4` | `...@0x657BB8` | `...@0x100325518` | `...@0x32A694` |
| half `Create` | `0x8498D4` | `0x657C10` | `0x1003255C4` | `0x32A788` |
| lz4 `Create` | `0x84995C` | `0x657C58` | `0x10032564C` | `0x32A844` |
| lz4+tlg5 `Create` | `0x849A00` | `0x657CBC` | `0x1003256EC` | `0x32A8AE` |
| manager compression selection | ctor `0x848CEC` | ctor `0x657368` | ctor `0x100324DDC` | ctor `0x329E14` |

四端 selector 都先把 factory 设为 default，再读取 `software_compress_tex`，按精确字面值选择：

```text
default                         -> tTVPSoftwareTexture2D::Create
"halfline"                     -> tTVPSoftwareTexture2D_half::Create
"lz4"                          -> tTVPSoftwareTexture2D_lz4::Create
"lz4+tlg5"                     -> tTVPSoftwareTexture2D_lz4_tlg5::Create
anything else / "none"         -> keep default
```

default factory 的 `bmp` 分支也四端一致：

```text
if bmp != null:
    allocate owned texture (0x30 on 64-bit, 0x20 on 32-bit)
    borrow bmp bits into BmpData
    store Bitmap = bmp
    ++bmp.RefCount
    softwareVMem += Pitch * Height
else:
    allocate static wrapper (0x28 / 0x1C)
    store caller pixel as borrowed BmpData
```

### 3.2 destructor 映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| owned complete | `0x84D5E4` | `0x65B400` | common `0x100329690`, thunk `0x100329230` | common `0x32EA72`, thunk `0x32E6DC` |
| owned deleting | `0x84D654` | `0x65B43C` | `0x100329234` | `0x32E6E0` |
| static complete/deleting | `0x84DA0C/0x84DA10` | `0x65B642/0x65B644` | `0x1003295AC/0x1003295B0` | `0x32E9B6/0x32E9B8` |
| half complete | `0x84E528` | `0x65BAB0` | common `0x10032AB58`, thunk `0x100329F98` | common `0x32FF80`, thunk `0x32F484` |
| half deleting | `0x84E674` | `0x65BB5C` | `0x100329F9C` | `0x32F488` |
| compress-base complete | `0x84EA50` | `0x65BD50` | `0x100329F40` | `0x32F3C4` |
| common lz4 complete | `0x84F4BC` | `0x65C6B0` | `0x10032AF4C` | `0x330392` |
| lz4 deleting | `0x84F0FC` | `0x65C474` | `0x10032AE9C` | `0x330308` |
| tlg5 deleting | `0x84F544` | `0x65C718` | `0x10032B3CC` | `0x3307D0` |

iOS 为 lz4/tlg5 各保留 complete thunk；Android vtable 直接共享 common complete body。tlg5 没有新增
owning member，也没有显式 destructor body，但编译器仍保留不同 dynamic-class deleting wrapper。

## 4. software 对象布局与析构顺序

| 成员 | 64-bit offset | 32-bit offset |
|---|---:|---:|
| base vptr/refcount/Width/Height | `+0/+8/+12/+16` | `+0/+4/+8/+12` |
| `Pitch` / `Format` | `+20/+24` | `+16/+20` |
| `BmpData` | `+32` | `+24` |
| owned `Bitmap` | `+40` | `+28` |
| compress secondary callback vptr | `+40` | `+28` |
| `PixelFrameLife` | `+48` | `+32` |
| half `_scanline` vector | `+56` | `+36` |
| half `_scanlineData` vector | `+80` | `+48` |
| lz4 `CompressedBlock` vector | `+56` | `+36` |
| lz4 `ShiftH/DataSize` | `+80/+84` | `+48/+52` |

### 4.1 owned/static

四端共同源码级析构为：

```text
owned complete dtor:
    softwareVMem -= Pitch * Height
    if Bitmap != null:
        Bitmap->Release()       // synchronous tTVPBitmap refcount

static complete dtor:
    // empty; BmpData is borrowed
```

owned dtor 不清 `Bitmap` 或 alias `BmpData`。static deleting dtor 只 raw-delete wrapper，不释放 caller pixel。

`GetBitmapSize()` 也保留一个看似重复的四端行为：

```text
return Pitch * Height * (Format == RGBA ? 4 : 1)
```

`Pitch` 已经是 byte pitch，但参考机器码仍额外按 RGBA 乘 4；当前源码不能按直觉“修正”。

### 4.2 compress base 与 continuous hook

lazy materialization 的四端共同顺序：

```text
GetPixelData:
    if BmpData == null:
        BmpData = TVPAllocBitmapBits(Pitch * Height, Width, Height)
        decode lines until destination reaches end
    if PixelFrameLife == 0:
        TVPAddContinuousEventHook(adjusted secondary-base this)
    PixelFrameLife = 3
    return BmpData

OnContinuousCallback(adjusted this):
    --PixelFrameLife
    if PixelFrameLife != 0: return
    if BmpData != null:
        free BmpData
        BmpData = null
    PixelFrameLife = 0
    TVPRemoveContinuousEventHook(adjusted this)

compress complete dtor:
    install compress primary/secondary vptrs
    if BmpData != null:
        free BmpData
        BmpData = null
        TVPRemoveContinuousEventHook(adjusted secondary-base this)
```

`TVPAddContinuousEventHook` 四端都先启动 continuous event，再 `push_back(cb)`；没有 duplicate guard，
扩容异常发生时 event 已开始但 pointer 未注册。`TVPRemoveContinuousEventHook` 扫描 live raw-pointer
vector，把所有等于 `cb` 的 entry 写 null，不 erase、不缩容、也不停止 continuous event。

析构只有在 `BmpData != null` 时 remove hook。正常 callback 把 buffer 释放后已经自行 remove，因此
`BmpData == null` 路径不再次扫描 vector。`PixelFrameLife == 0` 被异常 callback 调用时会减为 `-1` 并
立即返回；实现没有下溢 guard。

### 4.3 half

```text
byteWidth = Width
if Format == RGB:  byteWidth *= 3
if Format == RGBA: byteWidth *= 4

softwareVMem -= _scanlineData.size() * byteWidth
for p in _scanlineData:
    TVPFreeBitmapBits(p)
destroy _scanlineData vector storage
destroy _scanline vector storage
run compress-base dtor
```

`_scanline` 可以多次引用同一 line；只有 `_scanlineData` 保存唯一 allocation owner。Android vector
destruction内联为 begin/end walk + operator delete，iOS 调 libc++ vector helpers；raw line pointer 的
free 顺序一致。

### 4.4 lz4/tlg5

`Block` 是平台自然布局：64-bit 16 bytes，32-bit 12 bytes，字段为 `Data/Length/Height`。

```text
for Block in CompressedBlock:
    if Block.Data: delete[] Block.Data
softwareVMem -= DataSize
destroy CompressedBlock vector storage
run compress-base dtor
```

tlg5 complete dtor 共享该链；它只改变 block 编码/解码，不新增 destructor ownership。

## 5. software adapter 与 Cocos 生命周期

函数映射：

| adapter | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| static/owned | `0x84D908` | `0x65B5A0` | `0x1003294A8` | `0x32E874` |
| half | `0x84E7D0` | `0x65BC2C` | `0x10032A12C` | `0x32F5A4` |
| compress/lz4 | `0x84EBB4` | `0x65BDE8` | `0x10032A3B0` | `0x32F7C8` |

static/owned 和 compress 路径都在 orig 尺寸匹配时 `updateWithData`，否则：

```text
ret = new cocos2d::Texture2D
ret->autorelease()
ret->initWithData(... RGBA8888 ...)
return ret
```

它们不会把 `ret` 存入 software wrapper，也不会给 wrapper AddRef。Cocos texture 拥有上传后的独立
GPU data；wrapper 后续可由 deferred queue 删除。

compress adapter 先调用 virtual `GetPixelData()`，因而可能创建 lazy BmpData、注册 hook 并把 life
刷新到 3。half adapter 使用内部 half-height scanline vector；新 Cocos texture 的 physical height 是
`_scanline.size() == ceil(logicalHeight/2)`，随后逐行 `updateWithData`。

## 6. OGL hierarchy 函数映射

### 6.1 vtable 与 destructor

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| abstract/base vtable | `0x1A2F660` | `0x10C5568` | linker 裁剪 | linker 裁剪 |
| common base complete dtor | `0xA5F264` | `0x78B90C` | `0x1002EC698` | `0x2ECFA0` |
| abstract deleting slot | `BRK@0xA4E1B8` | `UDF@0x785568` | 不存在 | 不存在 |
| static vtable | `0x1A2F830` | `0x10C5650` | `0x101AFD7D8` | `0x1840844` |
| static deleting dtor | `0xA5EAE4` | `0x78B5A4` | `0x1002EC708` | `0x2ED078` |
| mutable vtable | `0x1A2F8F0` | `0x10C56B0` | `0x101AFD898` | `0x18408A4` |
| mutable deleting dtor | `0xA5F2CC` | `0x78B960` | `0x1002ECBEC` | `0x2ED580` |
| split vtable | `0x1A2FEC0` | `0x10C5998` | `0x101AFDF60` | `0x1840C08` |
| split complete dtor | `0xA631B0` | `0x78D49C` | `0x1002F04DC` | `0x2F0B94` |
| split deleting dtor | `0xA632D8` | `0x78D4E8` | `0x1002EFF14` | `0x2F0618` |

Android 把 static/mutable complete slot 直接指向 base complete body，给各 dynamic class 保留不同 deleting
wrapper。iOS 为 static/mutable 各保留 complete thunk，再进入 common base dtor。Android abstract vtable
的 deleting slot 是显式 trap；不能把它误认成缺失 dtor 或 callable fallback。

### 6.2 base layout 与未初始化字段

| 字段 | 64-bit offset | 32-bit offset | base ctor |
|---|---:|---:|---|
| vptr / RefCount / Width / Height | `0/+8/+12/+16` | `0/+4/+8/+12` | 安装/1/参数 |
| `texture` | `+20` | `+16` | 0；mode 非零时 glGenTextures |
| `IsCompressed` | `+24` | `+20` | false |
| `Format` | `+28` | `+24` | 参数 |
| `internalW/internalH` | `+32/+36` | `+28/+32` | **不写** |
| `PixelData` | `+40` | `+36` | null |
| `PixelDataCounter` | `+48` | `+40` | 0 |
| `_scaleW/_scaleH` | `+52/+56` | `+44/+48` | 1/1 |

四端 base ctor 都跳过 `internalW/internalH`，derived `InternalInit` 在完成 GL upload 后才赋值并增加
OGL translation-unit `_totalVMemSize`。base complete dtor却无条件执行：

```text
oglVMem -= internalW * internalH * getPixelSize()
if PixelData: delete[] PixelData
if texture: GL::deleteTexture(texture)
```

后续四端 fresh 取证证伪了本节最初对异常来源的解释：`TVPCheckMemory()` 在 Android arm64/armv7、
iOS arm64/armv7 分别落到一个空 `RET`/Thumb return 函数，不能形成提交前异常。static/mutable
constructor 的 unwind landing 确实都会调用 base complete dtor，但所有可见 `operator new[]` 都发生在
`InternalInit` 已完成 `internalW/internalH` 与 metric 提交之后。真正仍需保留的未初始化边界是
manual-init static constructor 在后续 `InitPixel`/`InitCompressedPixel` 前不写这两个字段；四端 base
ctor 的源码结构也直接证明不能补默认 `internalW = internalH = 0`。完整纠错与 EH 映射见同日
`motionplayer_ogl_split_as_single_ctor_commit_eh_four_binary_2026-08-22.md`。

## 7. split texture 的 map、Bitmap 与计账

split layout 因标准库不同：

| 目标 | CachedTexture map | Bitmap | 说明 |
|---|---:|---:|---|
| Android arm64 | `+0x40` | `+0x70` | old-libstdc++ RB tree |
| Android armv7 | `+0x34` | `+0x4C` | old-libstdc++ RB tree |
| iOS arm64 | `+0x40` | `+0x58` | libc++ tree |
| iOS armv7 | `+0x34` | `+0x40` | libc++ tree |

构造时 base mode 为 0，不生成单一 texture；`Bitmap=bmp; Bitmap->AddRef()`。完整析构共同顺序：

```text
internalW = 0
internalH = 0

for each node in CachedTexture:
    GL::deleteTexture(node.GLTextureInfo.Name)
CachedTexture.clear()
texture = 0

Bitmap->Release()              // no null check; AsSingleTexture may have set null
destroy CachedTexture map      // normally already empty
run OGL base complete dtor     // subtracts 0 because dimensions were cleared
```

Android map node中的 GL name 分别落在 arm64 node `+36`、armv7 `+20`；iOS 为 arm64 node `+32`、
armv7 node `+20`。这些是标准库 node ABI 差异，不进入 portable struct。

map 的第一次 `clear()` 属于 `ClearTextureCache()` body；随后自动 map destructor 仍运行一次，但树已经
为空。普通 split 析构不会清空 `Bitmap`；然而 `AsSingleTexture()` 会先 Release 再把该字段写成 null，
而析构仍无条件进入同一个 Release helper。四端 helper 第一条操作都是读取 `*Bitmap` refcount，所以
converted split texture 的最终析构是确定的 null-dereference 边界，不存在此前注释暗示的 constructor
invariant 保护。

## 8. OGL AdapterTexture2D 的反向 owner

函数映射：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| owner `GetAdapterTexture` | `0xA4E394` | `0x785682` | `0x1002E3E00` | `0x2E3484` |
| nested ctor | `0xA64F04` | `0x78E96C` | `0x1002F16C0` | `0x2F1C7C` |
| nested complete dtor | `0xA650E0` | `0x78EA58` | common `0x1002F185C` | common `0x2F1E80` |
| nested deleting dtor | `0xA65138` | `0x78EA90` | `0x1002F1848` | `0x2F1E70` |

nested allocation/layout：

| 目标 | allocation size | `_owner` offset |
|---|---:|---:|
| Android arm64 | `0x80` | `+0x78` |
| Android armv7 | `0x5C` | `+0x58` |
| iOS arm64 | `0x98` | `+0x90` |
| iOS armv7 | `0x68` | `+0x64` |

构造共同顺序：

```text
cocos2d::Texture2D base ctor
install AdapterTexture2D vptr
_name = owner.texture
_owner = owner
++owner.RefCount                 // direct plain increment, not virtual call
initialize content size, dimensions, pixel format, flags and GL program
caller invokes autorelease()
```

析构共同顺序：

```text
install AdapterTexture2D vptr
_name = 0                        // base Texture2D must not delete borrowed name
_owner->Release()                // virtual slot 2; can enqueue owner at count 1
cocos2d::Texture2D complete dtor
deleting wrapper raw-deletes adapter
```

`_owner` 不清零。owner texture 也不保存 adapter pointer，因此 owner dtor不会主动销毁 adapter。
正常引用计数保证 adapter 存活时 owner 不能先被最终删除；adapter dtor 的 Release 才可能把 owner
送入 deferred vector。

### 8.1 same-size reuse 的旧 owner 保留

```text
if orig != null and orig.width == internalW and orig.height == internalH:
    static_cast<AdapterTexture2D *>(orig)->_name = this->texture
    return orig
```

该路径没有检查 dynamic type，也不改变 `orig->_owner`：

- 普通 `cocos2d::Texture2D` 只要尺寸相同，也会被当 adapter 写入共享 `_name` 字段；
- 来自另一个 OGL texture 的 adapter 会显示新 texture name，但仍 AddRef 旧 owner；
- 新 owner 不获得 adapter 引用，旧 owner 则一直保留到这个 adapter 析构；
- 这是四端共同机器语义，不能添加 dynamic_cast、owner rebinding 或 ref transfer。

## 9. 与 deferred recycle 的可达性

对全部内置 vtable 和 complete dtor逐个核对后：

- software owned dtor 调的是 `tTVPBitmap::Release`，不是 `iTVPTexture2D::Release`；
- software half/lz4/compress 只释放 raw bitmap bits/vector/block/hook；
- OGL base/static/mutable 只处理 CPU buffer、计账和 GL name；
- OGL split 释放的是 `tTVPBitmap` 和 GL-name map；
- 没有一个 built-in concrete `iTVPTexture2D` destructor 直接 Release 另一个 texture。

所以 V281 记录的 `RecycleProcess` 析构期 append/iterator invalidation 边界在这些内置 dtor中没有直接
调用边。不能据此删除该边界：

- public virtual hierarchy 仍允许外部 subclass；
- duplicate final Release 本身即可制造 double-delete queue；
- AdapterTexture2D destructor 是已确认的反向 owner Release 点，只是它不由 owner dtor直接拥有；
- allocator/GL 平台回调的任意外部重入不属于本轮能证明不存在的范围。

因此准确表述是“内置 concrete dtor没有直接 reentrant append”，而不是“reentry 不可能”。

## 10. 本地源码逐行对照与纠错

### `cpp/core/visual/RenderManager.cpp`

当前源码已经精确保留：

- static 的空 destructor和 borrowed `BmpData`；
- owned `Pitch*Height` 计账先于 Bitmap Release；
- compress 仅在 `BmpData` 非空时 free/null/remove hook；
- callback `--life -> zero free/null/remove` 顺序；
- half unique-owner vector与 reverse member destruction；
- lz4 block pointer delete、DataSize 计账和 vector destruction；
- tlg5 无额外 destructor；
- ordinary/autoreleased software Cocos adapter，不持有 wrapper。

本轮只增加证据注释，并从六条 adapter trace 删除失真的 `func=0xAA6268`。

### `cpp/core/visual/ogl/RenderManager_ogl.cpp`

当前源码已经精确保留：

- base未初始化 internal dimensions；
- dtor计账 -> PixelData delete[] -> GL delete顺序；
- split dimensions=0 -> cache clear -> Bitmap Release -> map/base teardown；
- Adapter direct AddRef、name=0、virtual Release；
- same-size reuse只 update name、不 rebind owner。

本轮只补充这些容易被现代 owner/RAII 直觉改坏的注释。

### `cpp/core/environ/cocos2d/MainScene.cpp`

`TVPWindowLayer::UpdateDrawBuffer` 的两条 trace 也删除旧 `0xAA6268`。当前四参考目标中 Android
符号地址已分别是 `Kirikiroid2_1.3.9_Android_arm64-v8a.so!TVPWindowLayer::UpdateDrawBuffer@0xAA5954`
和 `Kirikiroid2_1.3.9_Android_armabi-v7a.so!...@0x7BADD4`；iOS 地址独立且符号被裁剪。portable log
不能选其中任一地址冒充共享来源。

## 11. IDB 修正与验证

- 四个 IDB 共新增/修正 149 个 concrete texture、adapter、hook、ctor/dtor `_guess` 函数名；
- 命名了 software factory selector、software/OGL total-vmem globals（Android armv7 packed globals仅加注释）；
- 给 software 6 个 class vtable、OGL abstract/concrete vtable、nested adapter vtable、layout、hook、map、
  metric 和 same-size owner边界补了逐目标注释；
- Android arm64 原先把 `0xA5F264..0xA5F458` 三个独立函数合并；本轮按实际 ARM64 序言/入口拆为
  base complete dtor、mutable deleting dtor和 mutable SetSize，恢复 `0xA5F2CC` deleting body；
- Android armv7 `GetBitmapSize@0x65B630..0x65B642` 原先没有函数边界，本轮已恢复；
- 四个 canonical IDB 均成功保存；完全关闭后重新冷启动，software/OGL base/nested adapter 三组代表性
  函数共 12 个名字全部保留，12/12 均可重新反编译，Android arm64 手工拆分的函数边界也未回并；
- 冷读关闭后删除了 IDA 可再生的 `.id0/.id1/.id2/.nam/.til` 工作文件，`reference/binaries/`
  恢复为四个原始目标加四个 `.i64`，共且仅有 8 个文件；
- 使用缓存中实测为 GNU Bison 3.8.2 的 `win_bison.exe`，执行 `cmake --build out/web/debug`
  成功完成 6/6（退出码 0）；固定产物 `index.html`、`index.js`、`index.wasm`、`vlfs.js`、
  `assets.zip` 全部存在，`index.data` 不存在。构建仅报告既有的 literal-operator、枚举 case、
  `LZ4_decompress_fast`、pthreads memory-growth、JSPI 和 JS-library 警告，没有新增错误。

## 12. 后续方向

本轮闭合了所有 built-in texture dynamic classes 的最终删除与 Cocos adapter反向 owner。下一阶段可继续：

1. `TVPWindowLayer::UpdateDrawBuffer -> Sprite::setTexture -> AdapterTexture2D autorelease` 已由 V284
   闭合；同尺寸不同 owner 可由 screen-size target invalidation、primary尺寸不变的正常路径触发，
   详见 `motionplayer_window_ogl_adapter_sprite_autorelease_lifetime_four_binary_2026-08-22.md`；
2. V287 已四端闭合 continuous-event delivery 的 null tombstone、callback自移除、同轮 live vector
   mutation、exclusive abort 与完整 EH；同时把默认路径上的 WCHAIN 诊断隔离到显式编译期开关，详见
   `motionplayer_continuous_event_hook_handler_delivery_lifecycle_four_binary_2026-08-22.md`；
3. V286 已四端闭合 `RestoreNormalSize` 的旧/new GL name、尺寸/scale/metric 迁移及失败边界，并纠正
   本条的过时措辞：该函数本身**不做 PixelData/readback**；只有 `GetScanLineForRead` 在 scaled 且
   `PixelData == null` 时调用它，existing PixelData 反而绕过 restoration；
4. 继续核对 manual-init static texture 的 `InitPixel`/PVR loader 调用者，确认未初始化 dimensions 在
   所有公开 factory 正常/早退路径上的实际可达范围。
