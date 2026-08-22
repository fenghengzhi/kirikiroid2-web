# MotionPlayer OGL split 单纹理迁移、constructor commit 与 EH 四文件联合反编译报告（2026-08-22）

## 1. 取证范围与结论

本报告以 `reference/binaries/` 的四个原始目标及各自 `.i64` 为联合权威：

| 平台 | 原始目标 | IDB | image base |
|---|---|---|---:|
| Android arm64-v8a | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` | `0` |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` | `0` |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_arm64.i64` | `0x100000000` |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7` | `Kirikiroid2_1.3.9_iOS_armv7.i64` | `0x4000`（恢复出的 thin armv7 输入） |

本轮闭合两个相邻生命周期专题：

1. `tTVPOGLTexture2D_split::AsSingleTexture` 如何把按区域缓存的 map/Bitmap 状态不可逆地迁移为
   一张降采样 GL texture，以及 normal/EH/final-dtor 的边界；
2. OGL base/static/mutable constructor 如何围绕 `InternalInit` 提交尺寸与 `_totalVMemSize`，constructor
   unwind 如何回收已提交的 GL/CPU 状态。

最重要结论：

- `AsSingleTexture` 先清空全部 cached GL names，再以固定 `CV_8UC4` 创建源/目标 Mat；源 texture
  `Format` 为 Gray/RGB 也不改变四通道缩放；
- resize 成功后立即 `Bitmap->Release(); Bitmap=nullptr`，此时 replacement GL name 尚未生成；
- 生成的单 texture **不增加** OGL `_totalVMemSize`；split destructor 又先把 dimensions 清零，故该
  单 texture 从创建到销毁完全不进入 metric；
- `AsSingleTexture` 的四端异常 cleanup 只析构两个 `cv::Mat` wrapper，均没有 `delete[] tmp`；
  `cv::resize` 抛出时 raw resize buffer 泄漏，cache 已丢失，但 Bitmap 仍保留；
- normal conversion 把 Bitmap 清零，split destructor 却仍无 guard 地进入 Bitmap Release helper；
  四端 helper 第一条操作都解引用 refcount，因此 converted object 的最终析构确定会 null dereference；
- OGL base ctor 确实不写 `internalW/internalH`，但上一份报告把 `TVPCheckMemory()` 解释成提交前
  可抛边界是错误的：四个 release target 的 callee 都是空 return；
- static/mutable ctor 中所有可见 `operator new[]` 都发生在 `InternalInit` 已上传、写尺寸并增加 metric
  之后，constructor unwind 调 base dtor 时能按已提交状态逆向回收；
- 未初始化尺寸仍是准确源结构：manual-init static constructor 本身不写尺寸，要等后续
  `InitPixel`/`InitCompressedPixel`。纠错的是异常因果，不是字段布局。

## 2. `AsSingleTexture` 与两个调用点的四文件映射

### 2.1 主函数

| 目标 | `AsSingleTexture` | fresh 状态 |
|---|---|---|
| Android arm64 | `tTVPOGLTexture2D_split_AsSingleTexture_guess@0xA63CA8` | fresh decompile + EH disasm |
| Android armv7 | `tTVPOGLTexture2D_split_AsSingleTexture_guess@0x78DB20` | fresh decompile + out-of-line landing disasm |
| iOS arm64 | `tTVPOGLTexture2D_split_AsSingleTexture_guess@0x1002F05EC` | fresh decompile；cleanup `0x1002F0958` |
| iOS armv7 | `tTVPOGLTexture2D_split_AsSingleTexture_guess@0x2F0CD4` | fresh decompile；SJLJ cleanup `0x2F101A` |

二进制没有保留精确源码符号；`_guess` 名由两个 vtable caller、对象布局和本地标识符共同交叉参照，
不把行为猜测冒充二进制名字证据。

### 2.2 唯一两个 caller

| 目标 | rect overload | point-array overload |
|---|---|---|
| Android arm64 | `tTVPOGLTexture2D_split_ApplyVertex_rect_guess@0xA633C0`，call `0xA634FC` | `..._points_guess@0xA6377C`，call `0xA638E0` |
| Android armv7 | `..._rect_guess@0x78D574`，call `0x78D5B6` | `..._points_guess@0x78D7A0`，call `0x78D940` |
| iOS arm64 | `..._rect_guess@0x1002EFFEC`，call `0x1002F0044` | `..._points_guess@0x1002F0234`，call `0x1002F0350` |
| iOS armv7 | `..._rect_guess@0x2F06A8`，call `0x2F07E6` | `..._points_guess@0x2F08BA`，call `0x2F0A66` |

四端 xref count 都严格为 2。两个 caller 的共同控制流是：

```text
if Bitmap == null:
    使用 downscaled-single 的 scale/internal-size 坐标路径
    return

计算请求区域 w/h
if w > maxTextureWidth || h > maxTextureHeight:
    AsSingleTexture()
    通过同一个 virtual overload 递归重派发
    return

继续按区域 map cache 路径
```

递归调用时 `Bitmap` 已为 null，所以立即进入 single-texture 分支，不会第二次转换。

## 3. cache、Fetch 与平台 STL 形态

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| clear method | 调用点内联遍历；`std_Rb_tree_GLTextureInfo_M_erase_guess@0xA63C5C` 只递归删树节点 | `split_ClearTextureCache_guess@0x78DACA` | `...@0x1002F0544` | `...@0x2F0C70` |
| FetchGLTexture | 内联 `0xA63F48..0xA63F90` | `..._FetchGLTexture_guess@0x78E078` | `...@0x1002F0D28` | `...@0x2F1368` |
| map field | `+0x40` | `+0x34` | `+0x40` | `+0x34` |
| Bitmap field | `+0x70` | `+0x4C` | `+0x58` | `+0x40` |

`ClearTextureCache` 的共享源码语义：

```text
for each node in CachedTexture:
    cocos2d::GL::deleteTexture(node.second.Name)
CachedTexture.clear()
texture = 0
```

Android arm64 的旧 libstdc++ 把遍历、header reset 和 `_M_erase` 分开内联；其余三个目标保留可识别的
完整 helper。不能因为 A64 helper 只删除树节点就推断共享源码缺少 method。

`FetchGLTexture` 共同执行：

```text
glGenTextures(1, &name)
bindTexture2D(name)
MIN_FILTER = LINEAR
MAG_FILTER = LINEAR
WRAP_S = CLAMP_TO_EDGE
WRAP_T = CLAMP_TO_EDGE
return name
```

没有 allocator、map insertion 或 metric 操作。

## 4. `AsSingleTexture` 的共同状态机

### 4.1 共同伪代码

```text
ClearTextureCache()

internalW = Bitmap->GetWidth()
internalH = Bitmap->GetHeight()

if internalW > maxW:
    scaleW = float(maxW) / internalW
    internalW = maxW

if internalH > maxH:
    scaleH = float(maxH) / internalH
    internalH = maxH

tmp = new unsigned char[internalW * internalH * 4]

src = cv::Mat(Bitmap.height, Bitmap.width,
              CV_8UC4, Bitmap.bits, Bitmap.pitch)
dst = cv::Mat(internalH, internalW,
              CV_8UC4, tmp, internalW * 4)
cv::resize(src, dst, Size(internalW, internalH), 0, 0, INTER_LINEAR)

Bitmap->Release()
Bitmap = null

switch Format:
    Gray -> GL_LUMINANCE
    RGB  -> GL_RGB
    RGBA/default -> GL_RGBA

pitch = internalW * 4
glPixelStorei(GL_UNPACK_ALIGNMENT, internalW odd ? 4 : 8)

TVPCheckMemory()              // empty in all four release targets
texture = FetchGLTexture()
glTexImage2D(..., internalW, internalH, ..., format, UNSIGNED_BYTE, tmp)

delete[] tmp
// normal cv::Mat destruction
// no _totalVMemSize increment
```

`internalW * internalH * 4` 先按 32-bit unsigned arithmetic 计算，再在 64-bit 目标扩为 `size_t`；这与
源码中 unsigned dimensions 的普通乘法一致，不应人为换成 checked 64-bit size。

### 4.2 每个平台的提交里程碑

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| clear cache | `0xA63CD4..0xA63D1C` | call `0x78DB38` | call `0x1002F0610` | call `0x2F0CF2` |
| `new[] tmp` | `0xA63D8C` | `0x78DB9E` | `0x1002F0680` | `0x2F0DA0` |
| `cv::resize` | `0xA63ED0` | `0x78DCA0` | `0x1002F07C4` | `0x2F0EAE` |
| Bitmap Release | `0xA63EE8` | `0x78DCA6` | `0x1002F07CC` | `0x2F0EB6` |
| Bitmap null | `0xA63F04` | `0x78DCB4` | `0x1002F07D0` | `0x2F0EBC` |
| empty memory check | `0xA63F3C` | `0x78DCE4` | `0x1002F081C` | `0x2F0F04` |
| publish single name | `0xA63F9C` | `0x78DCF2` | `0x1002F0828` | `0x2F0F14` |
| upload | `0xA63FBC` | `0x78DD0A` | `0x1002F084C` | `0x2F0F38` |
| normal `delete[] tmp` | `0xA63FC4` | `0x78DD10` | `0x1002F0854` | `0x2F0F3E` |

顺序在四端完全一致；差异来自 ABI、OpenCV wrapper 和指令选择，不是版本逻辑差异。

## 5. 固定四通道缩放与 Gray/RGB 边界

四端 Mat type 常量都对应 `CV_8UC4`，同时 raw buffer 与 dst step 固定为 `4 * internalW`。这与
`Format` 无关：

- Gray split 仍按四通道解释源 bitmap bits 并生成四通道 tmp；
- RGB split 同样如此；
- resize 后 GL upload 又按原始 `Format` 选择 `GL_LUMINANCE`/`GL_RGB`/`GL_RGBA`；
- `glTexImage2D` 没有 row-pitch 参数，因此 Gray/RGB 会按 GL format 从 tmp 开头连续消费 1/3 byte-per-pixel
  所要求的总字节数，而不是按四通道 row step 跨行。

这是一条原生边界，不得依据 texture format 把 Mat 类型或 tmp element size“优化”为 1/3 channel。

W/H 分别独立 clamp；只更新超限轴的 scale，不进行保持宽高比的统一缩放。split ctor 的 base scales
初始均为 1，转换只执行一次。

## 6. `AsSingleTexture` EH 与 partial state

### 6.1 cleanup 映射

| 目标 | cleanup | 机制 |
|---|---|---|
| Android arm64 | `0xA640DC..0xA641C4`，仍属于主函数 | Itanium/DWARF cleanup |
| Android armv7 | `0x78DDE0..0x78DE8C`，IDA 未建独立函数 | ARM EH landing pad |
| iOS arm64 | `tTVPOGLTexture2D_split_AsSingleTexture_cv_cleanup_guess@0x1002F0958` | 独立 `_Unwind_Resume` cleanup |
| iOS armv7 | `..._cv_cleanup_guess@0x2F101A` | SJLJ switch cleanup |

四个 cleanup 都执行两个 Mat header/reference/step-buffer 的析构工作，然后 resume。逐条搜索均没有：

```text
operator delete[](tmp)
```

唯一的 raw `delete[]` 是 normal path 上表所列地址。

### 6.2 按失败点划分的对象状态

| 失败点 | cache | dimensions/scales | Bitmap | tmp | texture |
|---|---|---|---|---|---|
| cache clear 前 | 原状态 | 原状态 | owned | 无 | 可能是 cached node name |
| `new[]` 抛出 | 已清空 | 已 clamp/更新 | retained | 未返回 | 0 |
| `cv::resize` 抛出 | 已清空 | 已 clamp/更新 | retained | **泄漏** | 0 |
| resize 成功、Bitmap Release 后 | 已清空 | 已 clamp/更新 | null | live | 尚未生成 |
| normal upload 后 | 已清空 | 已 clamp/更新 | null | 已释放 | single GL name |

`new[]` failure 本身不会泄漏未返回的 allocation，但方法已经清 cache 和改 dimensions/scales，没有 rollback。
`cv::resize` failure 的 raw tmp 确定不在本函数 cleanup 中回收；Bitmap Release 位于 resize 后，因此此时
Bitmap 仍有效，后续对象析构不会走 null Bitmap 边界。

Bitmap Release 之后的实际 call graph只包含：空 `TVPCheckMemory`、无分配的 GL cache/bind helper、C GL
入口和 normal delete。四参考目标中没有发现产生 C++ exception 的内部边；如果平台外部调用以非本地方式
退出，该范围不在四文件可证明不存在的集合中，且没有 transaction rollback。

## 7. normal conversion 后的确定析构边界

### 7.1 split complete dtor

| 目标 | complete dtor |
|---|---|
| Android arm64 | `tTVPOGLTexture2D_split_complete_dtor_guess@0xA631B0` |
| Android armv7 | `...@0x78D49C` |
| iOS arm64 | `...@0x1002F04DC` |
| iOS armv7 | `...@0x2F0B94` |

共同顺序：

```text
internalW = 0
internalH = 0
ClearTextureCache()
Bitmap->Release()          // unconditional
destroy map
base complete dtor
```

Bitmap Release helper：

| 目标 | helper | 首个语义操作 |
|---|---|---|
| Android arm64 | dtor内联 | `if (*Bitmap == 1)` |
| Android armv7 | `0x599D5C` | `if (*a1 == 1)` |
| iOS arm64 | `0x1000512E4` | `if (*a1 == 1)` |
| iOS armv7 | `0x505F0` | `if (*a1 == 1)` |

没有一个 helper 先测试 pointer。`AsSingleTexture` normal path 已把 Bitmap 写成 0，因此转换后的对象最终
进入 complete dtor 时会在 refcount load 处解引用 null。此前报告中的“non-null-by-construction”说明已
就地删除；constructor invariant 会被该 private state transition 主动破坏。

### 7.2 metric 不是 double-account，而是完全缺失

split cache 的创建/更新函数不增加 `_totalVMemSize`；`AsSingleTexture` 也不增加；complete dtor 又在 base
前清零 dimensions。结果：

```text
split cached GL names       metric +0 / -0
converted single GL name    metric +0 / -0
```

所以“清零尺寸用于避免对 split cache 重复减计账”不是准确因果。机器事实只是清零，然后 base subtraction
为 0；整条 split 路径从未有对应 add。

## 8. OGL constructor / `InternalInit` 四文件映射

### 8.1 base、static pixel 与 mutable ctor

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| base ctor | `tTVPOGLTexture2D_ctor_guess@0xA5E730`；mutable 中另有内联副本 | `...@0x78B2F4` | `...@0x1002EC2BC` | `...@0x2ECCE4` |
| static pixel ctor | `..._static_pixel_ctor_guess@0xA5E464` | `...@0x78B1BC` | `...@0x1002EC0F0` | `...@0x2ECB04` |
| mutable ctor | `..._mutable_ctor_guess@0xA5EC64` | `...@0x78B6D0` | `...@0x1002EC8E4` | `...@0x2ED240` |
| `InternalInit` | `...@0xA5E7E8`，多分支内联副本 | `...@0x78B388` | `...@0x1002EC37C` | `...@0x2ECD6C` |

### 8.2 compressed static route

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| compressed ctor | `..._static_compressed_ctor_guess@0xA5F764` | `...@0x78BC28` | `...@0x1002ECFF0` | `...@0x2ED884` |
| `InitCompressedPixel` | ctor内联 | `...@0x78BC94` | `...@0x1002ED09C` | `...@0x2ED970` |

### 8.3 base ctor field commit

| 字段 | 64-bit | 32-bit | base ctor |
|---|---:|---:|---|
| `RefCount/Width/Height` | `+8/+12/+16` | `+4/+8/+12` | `1/参数/参数` |
| `texture` | `+20` | `+16` | 0；mode 非零则 glGen/bind/params |
| `IsCompressed` | `+24` | `+20` | false |
| `Format` | `+28` | `+24` | 参数 |
| `internalW/internalH` | `+32/+36` | `+28/+32` | **不写** |
| `PixelData` | `+40` | `+36` | null |
| `PixelDataCounter` | `+48` | `+40` | 0 |
| `_scaleW/_scaleH` | `+52/+56` | `+44/+48` | 1/1 |

base mode 非零的 glGen/bind/filter/wrap 不写 internal dimensions。

## 9. `TVPCheckMemory` 纠错与 `InternalInit` commit

### 9.1 四个 release callee

| 目标 | IDB 名/地址 | body | xref 数 |
|---|---|---|---:|
| Android arm64 | `TVPCheckMemory_noop_guess@0x9130EC` | one ARM64 `RET` | 15 |
| Android armv7 | `...@0x6CDA48` | one Thumb return | 6 |
| iOS arm64 | `...@0x10020E2F8` | one ARM64 `RET` | 6 |
| iOS armv7 | `...@0x20C192` | one Thumb return | 6 |

xref 分布同时覆盖 `InternalInit`、compressed init、split upload、split cache update 和 software bitmap
allocation 对应点，与本地 `TVPCheckMemory` call sites 多向吻合。名字仍保留 `_guess`，因为 stripped binary
没有直接符号字符串。

旧报告仅看到 call 位于 dimension assignment 前，就把它串成可抛异常来源；fresh callee 取证直接否定
该结论。Web 的 `Platform.cpp` 也实现为空函数，但这只用于本地对照，不是证明来源。

### 9.2 `InternalInit` 的共同顺序

```text
format = Format -> LUMINANCE/RGB/RGBA
alignment = pitch&7 -> 8/1/2/1/4/1/2/1
glPixelStorei(UNPACK_ALIGNMENT, alignment)
TVPCheckMemory()                       // empty
bind(texture)
glTexImage2D(... pixel ...)
internalW = intw
internalH = inth
_totalVMemSize += intw*inth*Format
CHECK_GL_ERROR_DEBUG()                 // release 中无可见工作
```

A64 某些 mutable branches 把这段完整内联；其它目标保留 helper。format 的 branch/table 和 ABI 差异不
改变共享源码 switch。

## 10. static/mutable constructor 的 EH 提交边界

### 10.1 static pixel ctor

共同分支：

```text
base ctor
install static vptr
scaleW/H = arguments
pixsize = getPixelSize()

if pitch == iw*pixsize || ((pitch&7)==0 && pitch-iw*pixsize<8):
    InternalInit(pixel, iw, ih, pitch)
else if GL_CHECK_unpack_subimage:
    InternalInit(null, iw, ih, 0)
    InternalUpdate(pixel, pitch, full rect)
else:
    InternalInit(null, iw, ih, 0)
    PixelData = new byte[internalW*internalH*4]
    copy internalH rows, each internalW*pixsize bytes, dst pitch internalW*4
    InternalUpdate(PixelData, internalW*4, full rect)
    delete[] PixelData
    PixelData = null
```

注意所有 `new[]` 前都已 InternalInit；metric/dimensions 已提交。若 allocation 抛出，base dtor减回 metric
并删除 GL name；若 update 在 PixelData 发布后抛出，base dtor还会 delete[] PixelData。

### 10.2 mutable ctor

共同结构与当前源码一致：

- `Format==RGB` 时 base Format 改为 RGBA；
- pixel null 路径设置 scales，按 32 起始逐次乘 2 得到 internal power-of-two dimensions，必要时把
  scale 缩为 `internal/original`，然后 InternalInit(null)；
- pixel 非空路径按 pitch/pixsize 与 height 分支，优先 direct InternalInit；需要时先 InternalInit(null)
  再 virtual Update；
- 无 unpack-subimage 且 row pitch不能直接消费时，先 InternalInit，再 new[] PixelData/copy rows，最后
  `IsTextureDirty=true`；
- 因而所有 throwing allocation 仍位于 commit 之后。

iOS arm64 保留 `assert(sw == 1.f && sh == 1.f)`；iOS armv7 Thumb 指令也先比较 sw，再在 EQ IT block
比较 sh。Hex-Rays 一度只输出 `if (sw != 1)`，已在 `0x2ED2C6` 注释纠正。Android release 两端裁剪 assert。

### 10.3 constructor cleanup 映射

| 目标 | static pixel cleanup | mutable cleanup | compressed cleanup |
|---|---|---|---|
| Android arm64 | main 内 `0xA5E6D8`，内联 base dtor | main 内 `0xA5F20C`，内联 base dtor | ctor 内联 cleanup |
| Android armv7 | landing `0x78B2DE` -> base dtor -> resume | landing `0x78B8EE` -> base dtor -> resume | landing `0x78BC86` |
| iOS arm64 | `..._static_pixel_ctor_cleanup_guess@0x1002EC2A4` | `..._mutable_ctor_cleanup_guess@0x1002ECBC0` | `..._static_compressed_ctor_cleanup_guess@0x1002ED088` |
| iOS armv7 | `..._static_pixel_ctor_cleanup_guess@0x2ECCB2` | main SJLJ `0x2ED55C..0x2ED56E` | `..._static_compressed_ctor_cleanup_guess@0x2ED946` |

base teardown 顺序仍是：metric subtract -> PixelData delete[] -> texture delete。这里没有证据支持把
internalW/H 默认清零；manual-init constructor 与共享源码字段布局都要求保持未初始化。

## 11. 本地源码逐行对照

目标文件：`cpp/core/visual/ogl/RenderManager_ogl.cpp`。

### 已经一比一匹配，无需行为改动

- base ctor不初始化 internal dimensions；
- `InternalInit` 的 alignment/check/bind/upload/dimension/metric 顺序；
- static pixel ctor 的 direct、unpack-subimage 和 rearrange 三路；
- mutable ctor 的 power-of-two、virtual Update 和 dirty PixelData 三路；
- split cache 使用 `std::map<uint32_t, GLTextureInfo>`；
- `AsSingleTexture` 在 cache clear 后才 allocate，Mat固定 `CV_8UC4`；
- Bitmap Release/null 早于 FetchGLTexture；
- 不增加 metric；
- raw tmp而非 RAII container；
- split dtor无 Bitmap guard。

### 本轮修正的说明

- 给 internalW/H 标注 manual-init 延迟赋值，删除错误的 `TVPCheckMemory` 可抛因果；
- 给 `InternalInit` 标注 release callee为空、commit在 upload 后；
- 明确 split cache/converted single name完全不计账；
- 明确 split dtor在 conversion 后保留 null dereference；
- 明确 `AsSingleTexture` 是无 rollback 的状态迁移、固定四通道和 raw tmp EH leak；
- 同步就地修正前一份 concrete texture 报告的过时结论。

这些是注释/分析纠错，不改变已匹配的运行语义。

## 12. IDB 质量修正

本轮四个 IDB 共新增/修正 46 个函数名：

- `AsSingleTexture` 4；
- 两个 `ApplyVertex` overload 8；
- static pixel/compressed ctor 8；
- `InternalInit` 4；
- compressed init 3（A64 inline）；
- `ClearTextureCache`/A64 RB-tree erase 4；
- `FetchGLTexture` 3（A64 inline）；
- empty `TVPCheckMemory` 4；
- A64 尚未命名的 base ctor 1；
- constructor/AsSingle cleanup 7。

另覆盖或追加 67 处函数头/指令注释，重点纠正：

- Android armv7 split dtor旧注释中的 `non-null-by-construction`；
- iOS armv7 Hex-Rays漏掉的 sh assert；
- A64/A32/iOS 各自不同的 ctor landing 和 Mat cleanup；
- raw tmp、Bitmap null、single-name publication 与 metric omission 的精确提交点。

## 13. 验证状态

- 四个 canonical IDB 均成功保存；完全关闭 worker、删除 loose work files 后重新冷启动，逐端查询并
  反编译 `AsSingleTexture`、`InternalInit`、empty `TVPCheckMemory`、split complete dtor，共 16 个
  代表函数：16/16 名字保留、16/16 可反编译；
- Android armv7 旧 `non-null-by-construction` function-header comment 属于早先 Hex-Rays user comment，
  当前原生接口不能删除该 comment kind；已在紧邻的 function correction 与 Bitmap Release 指令
  `0x78D4B8` 同时写入明确纠错，cold decompile 会连续显示旧句与 `CORRECTION`，不会继续被当成结论；
- 关闭验证 worker 后删除了 IDA 可再生 `.id0/.id1/.id2/.nam/.til` 工作旁车；
  `reference/binaries/` 恢复为四个原始目标加四个 `.i64`，共且仅有 8 个文件，打开 session 数为 0；
- 实测缓存中的 `win_bison.exe` 为 GNU Bison 3.8.2，构建前确认没有 node/coi-server 进程；
- `cmake --build out/web/debug` 成功完成 3/3，退出码 0；实际重编译
  `RenderManager_ogl.cpp` 并重新链接 `index.html`；
- 固定产物全部存在：`index.html` 87,111 B、`index.js` 635,860 B、`index.wasm` 85,655,005 B、
  `vlfs.js` 42,548 B、`assets.zip` 7,858,873 B；`index.data` 不存在；
- 构建只报告既有 literal-operator、compressed-format enum case、pthreads memory-growth、实验性 JSPI
  和 JS-library symbol 警告，没有编译或链接错误；
- targeted `git diff --check` 退出码 0，报告无 trailing whitespace，旧错误因果只以“已纠正”的历史
  描述保留，不再作为当前结论。

## 14. 后续方向

完成这一专题后，下一组高价值边界是：

1. `TVPWindowLayer::UpdateDrawBuffer -> Sprite::setTexture -> AdapterTexture2D autorelease` 的实际
   Cocos retain/release 时序与 same-size old-owner 可达性；
2. V287 已四端闭合 continuous-event delivery 的 null-slot compaction、callback自移除、同轮 vector
   mutation、exclusive abort、handler ownership 与 EH，详见
   `motionplayer_continuous_event_hook_handler_delivery_lifecycle_four_binary_2026-08-22.md`；
3. V286 已闭合 `tTVPOGLTexture2D::RestoreNormalSize` 的旧 GL name、dimensions/scales/metric 迁移；
   同时证伪这里原有的 `PixelData/readback` 归属：Restore 本身完全不读写 PixelData，readback 属于
   `GetScanLineForRead` 的相邻分支；
4. manual-init static texture 的 `InitPixel`/PVR loader 所有 normal/early-return caller，界定未初始化
   dimensions 是否能越过 factory 边界。
