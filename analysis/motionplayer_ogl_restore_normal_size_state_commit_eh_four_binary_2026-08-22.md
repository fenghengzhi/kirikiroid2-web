# Motionplayer OGL `RestoreNormalSize` GL-name 状态迁移与失败边界（四参考二进制）

## 1. 结论

V286 沿 V282/V283/V284 留下的 OGL texture 边界，对
`tTVPOGLTexture2D::RestoreNormalSize`、它仅有的两个 direct caller，以及 virtual
`ApplyVertex(rect)` handoff 做四参考 fresh 取证。共同源码级结构与当前 portable implementation 一致，
本轮不需要修改运行语义；但新证据纠正了旧报告中“Restore 自己执行 PixelData/readback”的过时描述，
并闭合了此前未记录的 raw GL name、FBO cache、guarded static 与异常部分提交边界。

最重要的结论是：

```text
RestoreNormalSize(this):
    logicalW = uint(internalW / scaleW)
    logicalH = uint(internalH / scaleH)
    if logicalW >= maxTexture || logicalH >= maxTexture:
        ShowSimpleMessageBox(...)
        return false

    newName = glGenTextures()
    allocate RGBA POT backing, each side at least 32
    configure guarded static vertices and raw cached "Copy" method
    render old texture into newName through virtual ApplyVertex(rect)

    // commit begins only after draw
    totalVMem -= uint32(internalW * internalH * pixelSize(Format))
    GL::deleteTexture(oldName)
    texture = newName
    internalW/H = new POT dimensions
    totalVMem += uint32(newW * newH * pixelSize(Format))
    scaleW = scaleH = 1
    return true
```

该函数始终不读、不分配、不释放、不刷新也不清空 `PixelData`；也不修改 logical `Width/Height`、
`Format`、`IsCompressed`、`PixelDataCounter` 或已经存在的 Cocos `AdapterTexture2D`。新 GL name 在 commit
前没有 RAII owner，异常路径会泄漏它，并可能让全局 FBO/current-target cache继续指向该泄漏 name。

## 2. 四端定位方法与宽字符串证据

四库此前都没有该函数的可用语义名。普通 `mcp__idalib__find(type=string)` 对两条消息在四端均为
0 命中；按 `ida-search-string` 工作流搜索 UTF-8、UTF-16LE、UTF-32LE 原始模式后，只有 UTF-16LE
命中。读取原始字节确认完整正文、终止符和边界，再由唯一正文 xref定位函数：

| 目标 | 正文 UTF-16LE | title UTF-16LE | 正文 xref / function |
|---|---:|---:|---|
| Android arm64 | `0x154B734` | `0x15087FC` | `0xA4DDD4` / `0xA4DBA4` |
| Android armv7 | `0xDEAD2A` | `0xDB122A` | `0x7853DC` / `0x785160` |
| iOS arm64 | `0x101975B7A` | `0x101975BD2` | `0x1002E37F0` / `0x1002E37A4` |
| iOS armv7 | `0x1767F26` | `0x1767F7E` | `0x2E3186` / `0x2E2E0C` |

正文是 43 个 UTF-16 code units 加终止符，即 `unsigned short[44]`、88 bytes；title 是 28+1，
即 `unsigned short[29]`、58 bytes。Android armv7 与 iOS 两端原 IDA item size只有 2 bytes，反编译
显示为 `"T"` / `"P"`。单纯 `set_type` 虽报告成功，readback仍为 size 2；本轮按已核对边界先
undefine完整 88/58 bytes，再用 typed data item重建，最终 readback size与数组类型一致，反编译也改为
完整 data symbol，不再固化截断字符串。

## 3. 函数与 caller 映射

名字均带 `_guess`：二进制提供消息和 vtable/callgraph身份，但没有保存这些 private C++ 方法的完整
源码符号；本地标识符只作为交叉参照。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `RestoreNormalSize` | `0xA4DBA4`, size `0x488` | `0x785160`, size `0x2C2` | `0x1002E37A4`, size `0x380` | `0x2E2E0C`, size `0x3DE` |
| `GetScanLineForRead` | `0xA4E02C`, `0x18C` | `0x7854A8`, `0xC0` | `0x1002E3B98`, `0x100` | `0x2E32B0`, `0xC4` |
| `InternalUpdate` | `0xA5E8F4`, `0x1F0` | `0x78B448`, `0x152` | `0x1002EC4AC`, `0x1EC` | `0x2ECE34`, `0x16C` |
| base `ApplyVertex(rect)` | `0xA4E494`, `0xC8` | `0x785718`, `0x9E` | `0x1002E3F24`, `0xB4` | `0x2E35CE`, `0xD4` |
| Restore cleanup | integrated `0xA4DFBC..0xA4E028` | recovered `0x785422..0x785454` | no local landing found | SjLj `0x2E31EA`, `0x8C` |

四端对 Restore 的 direct code xref count都严格为 2：

```text
GetScanLineForRead -> RestoreNormalSize
InternalUpdate     -> RestoreNormalSize
```

没有第三个 direct caller。`ApplyVertex(rect)` 是 virtual call：64-bit vtable slot byte offset `+160`，
32-bit `+80`。static/mutable vtable都指向上述 base helper；split vtable指向 V283 已恢复的 override：

| 目标 | static/mutable slot 20 | split slot 20 |
|---|---:|---:|
| Android arm64 | `0xA4E494` | `0xA633C0` |
| Android armv7 | `0x785718` | `0x78D574` |
| iOS arm64 | `0x1002E3F24` | `0x1002EFFEC` |
| iOS armv7 | `0x2E35CE` | `0x2F06A8` |

所以 `RestoreNormalSize` 不应把该调用降成固定 base helper；converted split texture 可通过
`Bitmap == null` 的 split override生成坐标。

## 4. 本轮使用的对象布局

V282 已四端证明 hierarchy与布局；V286 用每条 field load/store重新核对 Restore consumer：

| 字段 | 64-bit offset | 32-bit offset | Restore 行为 |
|---|---:|---:|---|
| vptr / `RefCount` / logical `Width/Height` | `0/+8/+12/+16` | `0/+4/+8/+12` | logical size不修改 |
| `texture` | `+20` | `+16` | old source/delete；commit为 new name |
| `IsCompressed` | `+24` | `+20` | 不读取/不修改 |
| `Format` | `+28` | `+24` | metric multiplier；字段不修改 |
| `internalW/internalH` | `+32/+36` | `+28/+32` | 输入与 new POT commit |
| `PixelData` | `+40` | `+36` | 完全不访问 |
| `PixelDataCounter` | `+48` | `+40` | Restore不访问；caller先写 5 |
| `_scaleW/_scaleH` | `+52/+56` | `+44/+48` | 输入；成功后均写 1 |

`GLVertexInfo` 保持普通源码结构：

```cpp
struct GLVertexInfo {
    tTVPOGLTexture2D *tex;       // raw
    std::vector<GLfloat> vtx;    // owns only vector storage
};
```

base `ApplyVertex(rect)` 先把 `tex=this`，resize到 12 个 float，再按
`scale/internal-size` 写两组三角形 UV。异常 cleanup只需要释放 vector storage，raw `tex` 不参与 owner。

## 5. 尺寸门与数值边界

四端都把两个值截断为 unsigned：

```text
w = uint(float(internalW) / scaleW)
h = uint(float(internalH) / scaleH)
```

正常正数表现为 toward-zero/floor。NaN、负值、零 scale或超出 unsigned范围在共享 C++ 层不是可移植
安全转换；四端机器指令分别使用 ARM float-to-unsigned conversion，不应在 portable source前加 clamp、
finite check或 scale==0 guard。

门是严格小于：

```text
success candidate iff w < TVPMaxTextureSize && h < TVPMaxTextureSize
```

任一 logical side**恰好等于** max也走失败消息，不分配新 name。short-circuit的指令调度不同：
Android arm64/armv7与 iOS armv7可以在 width失败时推迟 height conversion；iOS arm64用 SIMD先同时计算，
但共享源码的两个 initializer都在 if 前完成，差异属于优化。

POT helper从 32 开始不断左移，直到 `value >= input`；所以小于等于 32（包括 0）都得到 32。
当前 gate通常把循环限制在 GPU max内；代码本身没有 overflow guard。

## 6. 新 GL name 的构造与 guarded statics

成功候选路径首先：

1. `glGenTextures(1,&newtex)`；
2. bind new name；
3. 分配 `GL_RGBA` internal storage，physical dimensions为两个 POT；
4. min/mag=`GL_LINEAR`，wrap S/T=`GL_CLAMP_TO_EDGE`。

无论原 `Format` 是 Gray/RGB/RGBA，新 backing的 internal/external format都固定 RGBA。`Format` 字段仍
保持原值；后续 metric却继续用 `getPixelSize(Format)`，因此 Gray/RGB restore得到 RGBA storage但分别
按 1/3 bytes-per-pixel计账。这是四端共同数据流，不应“修正”为 4。

接着出现两个独立 function-local static：

```cpp
GLfloat minx=-1, maxx=1, miny=-1, maxy=1;
static const GLfloat vertices[12] = { minx, miny, maxx, ... };
static tTVPOGLRenderMethod *method =
    (tTVPOGLRenderMethod*)TVPGetRenderManager()->GetRenderMethod("Copy");
```

`vertices` 虽然数值是常量，但 initializer引用普通 automatic locals，因此四端都生成独立 guard和 BSS
publication；不能改写成 `static constexpr` 而仍声称复刻源码结构。`method` 是无 owner的 raw pointer：

- 首次成功 lookup 后永久缓存；
- 不注册 pointer destructor；
- render manager以后切换也不 refresh；
- null result会在 `method->Apply()` 自然解引用；
- Android arm64/armv7与 iOS armv7有 guard-abort landing；iOS arm64函数体没有发现对应 local landing。

## 7. Render handoff 与 GL global state

共同调用顺序：

```text
method->Apply()
TVPSetRenderTarget(newtex)
glViewport(0,0,w,h)
enable position + first texture-coordinate attributes
position attribute -> static vertices

GLVertexInfo vtx
virtual this->ApplyVertex(vtx, Rect(0,0,w,h))
bind old this->texture as texture unit 0 source
texcoord attribute -> vtx.vtx.front()
glDrawArrays(TRIANGLES,0,6)
```

`TVPSetRenderTarget(newtex)` 会修改 translation-unit全局 FBO cache和 current render target。随后 bind
old texture只改变采样 texture binding，不恢复 FBO；success return时 current render target仍是 new name，
正好与新 `this->texture` 一致。

attribute index缺失时 method helper返回 `-1`；表达式仍执行 `1 << -1`/对应机器 shift，没有验证或
fallback。`vtx.vtx.front()` 也依赖 virtual ApplyVertex正常生成非空 vector，没有 guard。

## 8. Commit顺序、metric与 adapter边界

对象字段 commit直到 draw完成后才开始：

```text
totalVMem -= uint32(oldInternalW * oldInternalH * pixelSize)
GL::deleteTexture(oldName)              // oldName==0 也照常调用
texture = newName
internalW = newPOTW
internalH = newPOTH
totalVMem += uint32(newPOTW * newPOTH * pixelSize)
scaleW = 1
scaleH = 1
```

乘法在四端都以 32-bit完成，再 zero-extend到 64-bit `_totalVMemSize` add/sub；没有 checked overflow或
underflow。Android arm64把部分独立 stores重排成先 dimensions后 texture，其它端先 texture后
dimensions；中间没有可观察的 C++ callback，属于指令调度，不代表源码字段顺序差异。

现有 `AdapterTexture2D` 没有登记在 owner中，Restore也不遍历/更新它：

- commit后旧 GL name已删除；旧 adapter仍保存旧 name；
- owner引用仍指向同一 `iTVPTexture2D`，不会因为 Restore转移；
- 只有以后 `GetAdapterTexture(orig)` 再运行，才可能按 physical-size gate更新旧 adapter name或创建新
  adapter；
- 在此之前，Cocos Sprite可暂时持有 stale deleted GL handle。

## 9. `PixelData` 与 `GetScanLineForRead` 的真实关系

旧报告把 `PixelData/readback` 写进 Restore 的后续项，四端 fresh caller证明归属相反：

```text
GetScanLineForRead(line):
    PixelDataCounter = 5
    if scaleW == 1 && scaleH == 1:
        if PixelData == null:
            PixelData = new byte[internalW*internalH*4]
            render-target old/current name
            glReadPixels(..., PixelData)
        return PixelData + line*internalW*4

    if PixelData != null:
        logicalW = uint(internalW/scaleW)
        return PixelData + line*logicalW*4

    if RestoreNormalSize():
        return virtual GetScanLineForRead(line)
    return null
```

因此：

- counter在任何返回/失败前都写 5；
- existing PixelData明确**阻止** Restore；
- scaled+null才调用 Restore；
- success后通过 virtual slot递归，而非固定 base call；递归再次把 counter写 5；
- Restore failure返回 null；
- Restore success后的真正 readback发生在递归 caller中，而非 Restore内。

### 9.1 Android armv7 独有差异

Android armv7 `GetScanLineForRead@0x7854A8` 的 normal-size gate只读取 `_scaleW@+44`：

```text
if scaleW == 1: direct readback path
```

其余三个目标明确读取两字段并要求 `scaleW==1 && scaleH==1`。这不是 Hex-Rays省略：Thumb disassembly
在该 branch前没有 `_scaleH@+48` load。production constructor可以分别接收 `sw/sh`，所以本轮不能把
它证明成无条件等价优化；暂归类为 Android armv7平台/版本差异。

Portable source保留另外三端共同的双轴源码形状，并在源码注释/本报告显式保存该差异，不静默声称
四端完全相同。若 armv7出现 `scaleW==1 && scaleH!=1`，它会把 physical buffer当 normal size并用
`internalW*4` stride返回，可能让 logical line索引越过 physical height范围。

## 10. `InternalUpdate` 对失败返回值的处理

四端 `InternalUpdate` 都是：

```text
if scaleW != 1 || scaleH != 1:
    RestoreNormalSize()      // bool故意不消费

bind this->texture
select source format/alignment
optional row rearrangement
glTexSubImage2D(...)
```

所以 strict-max failure显示消息后，调用者仍继续绑定/更新原有 scaled texture；它不会 return、throw、
改坐标、更新 scale，也不会把 Restore failure向上传播。Portable implementation原本就精确保留这点，
本轮只补注释，不能改成 `if(!RestoreNormalSize()) return;`。

若 `PixelData` 在调用前存在，Restore保持它原样；mutable `Update/SyncPixel` 对该缓存的 delete、dirty和
upload行为属于 caller自己，而非 Restore cleanup。

## 11. EH与部分提交

### 11.1 共同非事务边界

`newtex` 是普通栈 scalar，不被 unique owner/guard管理。以下 C++操作位于 allocation/FBO publication后、
object commit前：

- guarded `GetRenderMethod("Copy")`；
- `method->Apply()`；
- virtual `ApplyVertex` 内的 vector allocation；
- 后续可逃逸的 C++ wrapper边界。

若异常逃逸：

- object的 old `texture/internalW/internalH/scales/metric` 尚未 commit，仍保持旧值；
- fresh GL name没有 delete，发生 GPU resource leak；
- `TVPSetRenderTarget` 已发生时，全局 current FBO target仍可指向 fresh leaked name；
- existing adapter仍指向 old name；old name尚未被 Restore删除；
- cleanup最多释放 branch-local ttstr或 `GLVertexInfo::vtx` storage，不回滚 GL状态。

### 11.2 四端 EH encoding

| 目标 | Restore cleanup形态 | 已证明动作 |
|---|---|---|
| Android arm64 | main function尾部 landing | method guard abort；ttstr cleanup；vector storage delete；resume；throwing cleanup转 terminate |
| Android armv7 | 本轮恢复 `0x785422..0x785454` | branch-specific ttstr、guard abort、vector delete、resume |
| iOS arm64 | function范围与邻近 guard-abort xref均无 local landing | 机器体只含 normal cleanup；不把缺失 landing擅自补成其它端行为 |
| iOS armv7 | SjLj dispatcher `0x2E31EA` | call-site 0/1 guard abort；2..6 vector delete；7/8 ttstr cleanup；9 abort；resume |

iOS arm64是否来自 TU exception配置、link-time proof或其它编译差异，本轮没有足够名字/metadata证据进一步
定性；报告只保留“该机器体没有 local cleanup landing”这一可直接验证的差异。

failure-size分支在任何 GL allocation前构造两份 `ttstr`、调用 message box，再按 reverse order释放。
Android arm64/armv7与 iOS armv7有对应 unwind cleanup；iOS arm64同样只观察到正常析构路径。

## 12. 本地源码对照与过时记录纠正

`cpp/core/visual/ogl/RenderManager_ogl.cpp` 逐项已一致：

- unsigned truncation与 strict `< max`；
- `power_of_two(...,32)`；
- 固定 RGBA allocation和四个 texture parameter；
- 两个 guarded function-local static的原源码形态；
- raw process-lifetime method cache；
- method/FBO/viewport/attribute/virtual ApplyVertex/draw次序；
- draw后 metric subtract、old delete、new publication、metric add、scale reset；
- no PixelData/logical-size/Format/adapter mutation；
- `InternalUpdate`忽略 bool；
- `GetScanLineForRead`的 counter、cached buffer gate、virtual recursion与 null return。

本轮只增加证据注释，没有加入 GL name guard、FBO rollback、adapter refresh、PixelData invalidation、
format normalization、max equality放宽或 Restore失败早退。

三份旧报告中 `RestoreNormalSize` 的 `PixelData/readback` 归属已就地纠正：

- `motionplayer_concrete_texture_software_ogl_adapter_lifetime_four_binary_2026-08-22.md`；
- `motionplayer_ogl_split_as_single_ctor_commit_eh_four_binary_2026-08-22.md`；
- `motionplayer_window_ogl_adapter_sprite_autorelease_lifetime_four_binary_2026-08-22.md`。

正确表述是：Restore只做 GPU render-copy/name replacement；readback属于它的
`GetScanLineForRead` caller，且 existing PixelData绕过 Restore。

## 13. IDB写回与验证

本轮完成：

- 18 个 function rename（含 Android armv7新定义 cleanup与 iOS armv7既有 SjLj cleanup）；
- 8 个 UTF-16 data item重建/rename；
- 31 条成功的 owner/state/EH/caller注释；
- 四个 Restore、四个 GetScanLine、四个 InternalUpdate、四个 base ApplyVertex与两个 cleanup均可反编译；
- 四端 Restore direct xref count均为 2。

四个 live session先保存到独立 candidate。完全关闭后从 candidate冷开，18/18代表函数重新反编译成功，
8/8 message data item仍为 88/58 bytes。发布前四个旧 canonical均备份到
`out/idb-recovery/v286-ogl-restore/prepublish/`；然后 candidate覆盖 canonical，并逐库验证 candidate与
canonical SHA-256一致：

| canonical IDB | bytes | SHA-256 |
|---|---:|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` | 368,555,677 | `F44FB19B7DC251D4A6296ADA7F0D09F4219C83DFF30EEAF176B289645DA554B5` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` | 347,599,282 | `00F043540850E910BF68CD96BE740BBF9638BF06CE51D3021817D88F079F5F4B` |
| `Kirikiroid2_1.3.9_iOS_arm64.i64` | 337,114,105 | `F679D043CC4D74D8C29F5BC24910206EBC72148CF77E05FC6AE4003C812D1D79` |
| `Kirikiroid2_1.3.9_iOS_armv7.i64` | 378,836,605 | `5718737378A411BDCC067B0F4F3E8C69E21F5E68C91C03A4373EFA4D9D7E1417` |

最终从 `reference/binaries/` 的四个 canonical路径重新 cold-open：Android 4+5、iOS 4+5，共
18/18函数再次反编译成功，8/8 data item边界正确。第一次并行 iOS readback因 C:/G:仅剩约 70 MB、
无法展开 working components而失败；释放本轮可再生 scratch后两个 iOS canonical逐库重试成功，证明
失败原因是磁盘 working-set而非 packed IDB损坏。

为释放验证空间，删除了本轮由 candidate冷读生成的20个 loose scratch（1,432,104,705 bytes）和最初
analysis session生成的12个 loose scratch（1,431,887,872 bytes）。它们都是可由已验证 packed IDB重新
生成的 `.id0/.id1/.id2/.nam/.til`；四个 candidate、四个 prepublish backup和四个 canonical均保留。
其余 final-attempt/retry scratch仍在 `out/idb-recovery/v286-ogl-restore/loose/`。最终
`reference/binaries/` 严格只含四个原始目标和四个 `.i64`（8 files），IDA MCP session count为0。

## 14. 构建验证

- 实测 `win_bison --version` 为 GNU Bison 3.8.2；
- build前确认 `coi-server` process count为0；
- `cmake --build out/web/debug` 重新编译 `RenderManager_ogl.cpp`、重链 `core_visual_module`和最终
  `index.html/index.wasm`，exit 0；
- 固定产物 `index.html`、`index.js`、`index.wasm`、`vlfs.js`、`assets.zip`全部存在；
- `index.data`按预期不存在；
- warning仅为仓库既有 `_tss` literal operator、四个 enum case、pthread memory-growth、JSPI和
  JS-library symbol warning，没有 error。

固定产物大小：

| artifact | bytes |
|---|---:|
| `index.html` | 87,111 |
| `index.js` | 635,860 |
| `index.wasm` | 85,655,005 |
| `vlfs.js` | 42,548 |
| `assets.zip` | 7,858,873 |

该路径依赖真实 OpenGL context、GPU max和 scaled OGL texture；现有 unit/differential fixture没有直接
实例化 private `tTVPOGLTexture2D`的有效环境。本轮不从零捏造 GPU fixture；Web build作为注释改动的
编译/链接非回归，四端 fresh decompile与最终 canonical cold-read是行为结论的权威证据。
