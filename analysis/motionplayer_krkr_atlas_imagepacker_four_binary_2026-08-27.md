# KRKR atlas loader、record/vector/cache 与 ImagePacker 四参考二进制联合闭包

日期：2026-08-27

## 1. 范围与结论

本 slice 闭合 `Player::loadKrkrAtlasSource` 的完整两调用者共享 helper，以及它唯一引入的
bundled `ImagePacker::pack/_rect2D` 依赖：path/module/cache gate、全 source/icon 枚举、
ABI-specific record owner 顺序、PSB RL8/RL32 解码、透明图片替换、pointer/bin vectors、
texture page 创建、persistent KRKR map publication、逆序销毁、异常泄漏和 malformed 边界。

此前 `Player_findSourceForNode` 报告只闭合 spec-1 调用条件、成功/失败 handoff；D3D source
getter 报告只闭合 render-time caller 与 cache 消费。它们不能替代本 helper 内部总账。
本轮 fresh 完整证据闭合了主体算法。2026-08-30 的运行时故障复核进一步确认，旧报告在
`CreateTexture2D` 处遗漏了 receiver：四端都先取得 Motion 私有 OpenGL manager；本地当时
错误地使用了默认 manager。修正证据见
`motionplayer_krkr_atlas_private_gl_manager_four_binary_2026-08-30.md`。

## 2. 四端函数映射与完整指令

| 端 | KRKR loader | ImagePacker `pack` | ImagePacker `_rect2D` |
|---|---:|---:|---:|
| Android arm64 | `0x6931C8`，1997 | `0xA6DA58`，149 | `0xA6CFFC`，477 |
| Android armv7 | `0x570F54`，1014 | `0x79436C`，112 | `0x793F5C`，293 |
| iOS arm64 | `0x1000F4098`，1136 | `0x100054E20`，147 | `0x100054944`，237 |
| iOS armv7 | `0xF0BE4`，1667 | `0x53EB8`，219 | `0x53A04`，379 |

12 个主函数均 fresh decompile，并从 offset 0 分页读取完整 disassembly；总计 7827 条指令，
所有 cursor 均 `done=true`。四库已统一命名 `Player_loadKrkrAtlasSource_guess`、
`ImagePacker_pack_guess`、`ImagePacker_rect2D_guess`。

共享 decoder disposition 为：

| 端 | RL32 | RL8 |
|---|---:|---:|
| Android arm64 | loader/ObjSource 各自 inline | loader/ObjSource 各自 inline |
| Android armv7 | `0x571DA4`，33 | loader inline |
| iOS arm64 | `0x1000F5474`，39 | `0x1000F5510`，34 |
| iOS armv7 | `0xF1F10`，33 | `0xF1F6A`，32 |

5 个 retained decoder 也都 fresh decompile 并完整读取，共 171 条指令。每个 retained helper
恰有两个 caller：本 KRKR loader 与 `ObjSource::ensureTexture`；Android inline disposition由
两个完整 caller body 联合确认，不能因没有独立函数地址解释成算法缺失。

## 3. caller 分母

四端对 loader 的 xref 都精确为 2：

| 端 | timeline source resolver caller | D3D source getter caller |
|---|---:|---:|
| Android arm64 | `0x691FD0` | `0x6EE528` |
| Android armv7 | `0x570668` | `0x5AC598` |
| iOS arm64 | `0x1000F335C` | `0x100140244` |
| iOS armv7 | `0xEFB9C` | `0x141594` |

`ImagePacker::pack` 每端都只有 loader 中一个 code xref。`_rect2D` 只由 `pack` 调用；其
node/tree/sort/vector helpers 是 bundled packer 的编译展开，不被其它 motionplayer 路径直接发布。
因此本报告与既有 resolver、D3D source getter 和 ObjSource 报告交叉后，caller 分母封闭。

## 4. loader 共同伪代码

```text
pieces = split(source.path, '/')
if pieces.empty or pieces[0] != "src": return false

module = resourceManager.loadedModules.find(moduleKey)
if miss:
    source.valid = false
    return false

source.object.Clear()
root = retained RawNode(module.file)
liveSourceKey = reference(source.path)
cached = module.krkrSourceEntries.find(liveSourceKey)

if cached miss:
    sourceRoot = root["source"] strict
    source.valid = false
    requestedGroup = pieces[1] narrow       // unchecked index
    requestedIcon  = pieces[2] narrow       // unchecked index
    if group miss or requested icon miss: return false

    records = []
    groupKeys = sourceRoot.keys()
    for group in groupKeys:
        groupNode = sourceRoot[group] strict
        iconRoot = groupNode["icon"] strict
        for iconName in iconRoot.keys():
            iconNode = iconRoot[iconName] strict
            width  = iconNode["width"] strict int
            height = iconNode["height"] strict int
            records.emplace(iconNode, width, height,
                            "src/" + group + "/" + iconName)

    rectPointers = []
    for record in records:
        publish record back-pointer/content dimensions
        rectPointers.push(&record.rect)
        decode record into record.rect.bgra

    bins = []
    ignore ImagePacker.pack(rectPointers, TVPMaxTextureSize, bins) result
    for bin in bins:
        texture = getPrivateOpenGLRenderManager().CreateTexture2D(
            null, bin.width*4, bin.width, bin.height)
        for rect in bin.rects:
            iconNode = rect.record.iconNode
            entry = module.krkrSourceEntries[wide(rect.record.sourceKey)]
            entry.setTexture(texture)
            commit originX, originY
            commit packed x/y/(x+w-1)/(y+h-1)
            if in-place clip lookup succeeds:
                commit left, top, right, bottom one by one
            else:
                commit {0,0,1,1}
            if rect.bgra != null:
                texture.Update(rect.bgra, contentWidth*4, entry.textureRect)
                alignedFree(rect.bgra)       // pointer deliberately not cleared
        texture.Release()                    // normal page tail only

    cached = module.krkrSourceEntries.find(liveSourceKey)  // live retry

entry = *cached                              // no end guard
source.valid = true
commit origin, blank=false, width/height, clip, textureRect, texture
return true
```

唯一 prefix gate 后直接读取 `pieces[1]/pieces[2]`；短 path 是未防御边界。cache probe 与
post-build retry读取 live `source.path`，而 `pieces` 是入口快照；texture/cache callback若重入
替换 path，retry会观察新 key。module miss在 `object.Clear` 之前，cache miss路径则先 Clear
object再取得 root owner。所有顺序均是可观察的 partial-publication 边界。

## 5. record 布局、vector growth 与 palette 状态

四端 record stride 与 owner 顺序：

| 端 | stride | source owner order |
|---|---:|---|
| Android arm64 | `0x40` | `PSBRawNode iconNode -> rect/tail -> std::string sourceKey` |
| Android armv7 | `0x2C` | `PSBRawNode iconNode -> rect/tail -> std::string sourceKey` |
| iOS arm64 | `0x50` | `std::string sourceKey -> rect/tail -> PSBRawNode iconNode` |
| iOS armv7 | `0x34` | `std::string sourceKey -> rect/tail -> PSBRawNode iconNode` |

这是 libstdc++/libc++ 的源构建差异，不是 padding 猜测。本地以 `_LIBCPP_VERSION` 选择相同
member order；vector growth按该顺序 copy 两个 owners，再逆序销毁旧 range。rect 中 back-pointer、
content width/height 和 BGRA pointer 在 record append 时不初始化，只有 records vector 完全稳定后
才逐项发布，避免 growth 后悬空 rect/back-pointer。

函数级 `iconNode` scratch在 requested-icon probe、全量枚举和 decode pass 之间持续存活。decode
先对“上一值”执行 `Contains("pal")`，随后才把当前 record.iconNode 赋给 scratch。因此非空记录
`[r0...rn-1]` 的 palette-mode 顺序是 `[pal(rn-1), pal(r0), ..., pal(rn-2)]`；实际 pixel/palette
payload仍从赋值后的当前 record读取。这个看似 bug 的一位旋转在四端一致，本地必须保留。

## 6. PSB 解码与透明图片边界

共同 decoder 先把 `uint32 sourceSize` 重解释为 signed int32；小于 1 直接返回。RL packet没有
source packet 完整性或 destination capacity检查：

- RL8 run：`(marker&0x7f)+3` 次复制一个 byte；literal为 `marker+1` bytes；
- RL32 run：从可能未对齐的 source读取一个 32-bit pixel并重复；literal按 4-byte pixel复制；
- packet可以越过 sourceEnd，output也可以越过调用者分配；循环只在 packet完成后比较cursor。

palette分支按低32位 pixelCount分配 index/BGRA；uncompressed index复制使用resource长度而不是
pixelCount。palette长度除4后分配vector，ReverseRGB，再按signed pixel count展开。non-palette
compressed path先RL32到BGRA再in-place ReverseRGB；raw path直接ReverseRGB。

alpha scan只有signed低32位pixelCount为正才进入，但循环上界使用完整signed 64-bit宽×高乘积。
全透明时立即free BGRA、指针置null，并只把 padded rect宽高改成2×2；content width/height不改。
异常时 record析构不free BGRA，未到正常透明/上传释放点的buffer按原版泄漏。

## 7. bundled ImagePacker 的精确边界

`pack` 与仓库 bundled `imagepacker.cpp` 共同结构一致：

1. 任一 rect不能装入 `maxSide×maxSide` 时立即返回false，不修改bins；
2. 构造两份 `vector<rect*>`，第一份resize到n后按原顺序memcpy；
3. 即使n==0也先append一个bin，所以成功结果含一个0×0空bin；
4. 每轮 `_rect2D` 把成功项写入bin.rects、失败项写入另一vector，随后`shrink_to_fit`；
5. 失败vector非空就交换两vector继续，直到空；返回true。

`_rect2D` 固定做5种降序sort：area、perimeter、max side、width、height。每种sort用一个独立
`new rect*[n]` owner；二叉切分tree在搜索尝试间reset/reuse，`discard_step`是process-global 128。
候选bin按signed int宽高/area/perimeter计算，溢出不净化；不启用源码中注释掉的旋转分支。
若maxSide仍装不下全部rect，记录面积最大的partial排序。最后按所选排序再插入一次，写x/y、
clip width/height与success/unsuccess vectors，逆序delete五个排序数组和tree children。

loader故意忽略`pack`返回值。oversized rect导致bins保持空，随后loader返回true但没有cache entry；
caller的unchecked retry/dereference承担崩溃边界。所有decode BGRA也因record析构不拥有它们而泄漏。

## 8. texture/cache owner 与异常前沿

每页texture创建引用是raw local。`entry.setTexture(texture)`先Release旧值，再保存新值并AddRef；
每个entry因此独立持有一份引用。页构造引用只在整页内循环正常结束时Release。operator[]分配、
旧texture Release、metadata getter、clip conversion或Update任一抛出时：

- 已插入entry与已写字段保留；
- 已成功setTexture的entry继续持有引用；
- page构造引用没有cleanup，泄漏；
- 当前及后续未free BGRA没有record析构cleanup，泄漏；
- 已free BGRA指针不清零，但record析构也不读取它，避免double free。

KRKR map由`LoadedResourceRecord`持有，普通销毁顺序为KRKR map、Win map、PSB file。每个
`PackedSourceAtlasEntry`析构Release自己的texture。`unload/unloadAll`报告已闭合map node与
bucket生命周期；本报告补齐entry产生、替换与page共享owner关系。

## 9. 本地逐行对照与验证

本地已经匹配：

- `KrkrAtlasRecord_guess` 的libstdc++/libc++ member order和无BGRA析构；
- 全量枚举、record稳定后发布rect pointers、旋转palette-mode scratch；
- signed-W32乘加/size边界、RL无检查行为、透明2×2替换；
- 忽略pack结果、空bin、inclusive packed rect、live-key retry；
- raw page texture与BGRA异常泄漏、cache逐字段partial commit；
- `PackedSourceAtlasEntry`逐entry AddRef/Release和LoadedResourceRecord逆序owner。

四库已为loader/packer/_rect2D统一命名，添加函数注释、bookmark并保存。执行coverage 12列、
duplicate ID、`git diff --check`和受影响生产翻译单元syntax-only检查。当前环境仍没有正式
CMake/Ninja/Emscripten/Catch2工具链；现存真实atlas unit case不能在本机宣称运行通过。
