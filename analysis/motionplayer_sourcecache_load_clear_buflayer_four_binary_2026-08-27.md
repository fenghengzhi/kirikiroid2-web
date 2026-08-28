# SourceCache `loadSource` / `clearCache` / `bufLayer` 四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `Motion.SourceCache` 的三个非构造 NCB callback，并同时闭合
`Motion.ResourceManager` 注册表中对这三个 callback 的原样重复发布：

- `loadSource`：descriptor 读取、复合缓存键、命中/变色/未命中分流、预插入淘汰、
  Layer 创建、bake、list 插入和返回 owner；
- `clearCache`：逐 Layer `Invalidate`、异常 partial commit、list 节点析构与字节计数复位；
- `bufLayer`：持久 scratch Layer 的 getter CopyRef 和 SourceCache/ResourceManager 两张类表复用；
- `bakeSource`、corner tint、private GLL 查询、trim 和 `std::list<Entry>` 的关键 helper、
  对象生命周期及四端 STL ABI 差异。

四个参考二进制共同构成权威。Android arm64 的两个 callback 是同一 IDA 函数范围中的
内部入口，因此该端以完整分页反汇编为主，另外三端的完整反编译与反汇编用于恢复共同
源码结构。没有为了形成整齐地址表而人为创建重叠函数。

## 2. callback 与主 helper 地址表

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `loadSource` callback | `0x6A4F88`，位于 `0x6A4CD4` 函数内 | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| `clearCache` callback | `0x6A5818`，位于 `0x6A4CD4` 函数内 | `0x57B018` | `0x100100F10` | `0xFE0D4` |
| `bufLayer` getter | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |
| entry identity helper | callback 内联 | `0x57A0AC` | `0x1000FF9D4` | `0xFCC9C` |
| pre-insert trim | `0x6A3EE0` | `0x57A106` | `0x1000FFA1C` | `0xFCCD2` |
| bake | `0x6A3FC0` | `0x57A168` | `0x1000FFB24` | `0xFCD68` |
| packed corner tint | `0x6A48F8` | `0x57A754` | `0x10010032C` | `0xFD4B4` |
| private GLL native query | tint 尾部内联 | `0x57AA80` | `0x1001006C0` | `0xFD7E8` |

fresh 指令覆盖如下：

- Android arm64 的合并函数共 763 条指令，已按 offset `0..700` 分八页读至
  `cursor.done=true`；其中 `loadSource` 内部范围 541 条、`clearCache` 内部范围 49 条；
- 其余三个 `loadSource` callback 分别为 242、202、302 条，`clearCache` 为
  30、29、29 条，getter 为 5、3、5 条，均完成 fresh 完整反编译和反汇编；
- trim 为 55/37/39/37 条，bake 为 585/414/379/514 条，corner tint 为
  244/306/225/321 条，全部完成 fresh 完整读取；
- Android armv7、iOS arm64、iOS armv7 的 private query 分别为 37、23、24 条；
  Android arm64 的同构序列直接内联在 tint 尾部。

## 3. `Entry` 源码结构与初始化边界

四端共同的源级字段顺序为：

```text
Entry:
    key        : tTJSVariant
    layer      : tTJSVariant
    src        : ttstr
    blendMode  : tjs_int
    colors[4]  : tjs_int[4]
    byteWeight : tjs_int
```

list 节点总大小在 64 位端为 `0x58`，在 32 位端为 `0x3C`。该差异来自指针宽度、
Variant/ttstr 表示和 STL 节点布局，不应以 padding 字段写回源码。

`Entry entry;` 的默认构造边界非常尖锐：`key`、`layer`、`src` 和 `byteWeight` 被构造或
置零；`blendMode` 之后总会由 descriptor 默认值 0 覆盖；`colors` 不整体初始化。若
descriptor 的 `color` 是 Void，只写 `colors[0]`：高半字节非零时写 `0xFF808080`，否则
写 `0xFFFFFFFF`。`colors[1..3]` 保持未初始化，后续仍会被比较、复制、bake 和 tint 读取。
本地必须保留这个原版缺陷，不能为了“安全”把数组清零。

传入的 `source` 与 `descriptor` dispatch 都是 borrowed；property accessor 在读取期提供
临时 AddRef/Release。缓存 entry 从不持有 `source`。

## 4. `loadSource` 共同伪代码

```text
loadSource(source, descriptor):
    candidate = Entry()                 // colors 不整体初始化
    candidate.key = descriptor.key      // Variant，缺省 Void
    candidate.src = descriptor.src      // String，缺省空串
    candidate.blendMode = descriptor.blendMode as Integer, default 0
    color = descriptor.color            // Variant，缺省 Void

    if color is not Void:
        require color be Object
        for i in 0..3:
            candidate.colors[i] = color[i] as Integer, default 0
    else:
        candidate.colors[0] =
            (candidate.blendMode & 0xF0) != 0 ? 0xFF808080 : 0xFFFFFFFF

    for node from list.begin to list.end:
        if node.key DiscernCompareStrictReal candidate.key
           and node.src == candidate.src
           and node.blendMode == candidate.blendMode:
            result = CopyRef(node.layer)
            if node.colors[0..3] exactly equal candidate.colors[0..3]:
                return result            // 不提升 LRU，不重新 bake

            copy candidate.colors into node.colors
            bakeSource(source, node)
            list.push_front(copy(node))   // 先 AddRef key/layer/src
            list.erase(node)              // 再释放旧节点 owner
            return result                 // 仍为同一个 Layer closure

    trimCacheBeforeInsert()
    candidate.layer = Layer(owner, primaryLayer)
    result = CopyRef(candidate.layer)
    bakeSource(source, candidate)
    currentBytes += uint32(candidate.byteWeight)
    list.push_front(copy(candidate))
    return result
```

复合 identity 只含 `key/src/blendMode`。`key` 使用 Variant 的
`DiscernCompareStrictReal`，`src` 使用 ttstr 精确相等，`blendMode` 使用整数相等；颜色不
属于 identity。Android arm64 内联这一比较，其余三端调用小 helper，语义一致。

命中但颜色变化时是 `push_front(copy) + erase(old)`，不是 `splice`。因此新节点复制会先
增加 key、Layer 和 src 的引用，之后旧节点才按 `src -> layer -> key` 的成员逆序释放。
若 bake 抛出，颜色已在原节点中提交，但节点位置尚未变化；若新节点复制/分配抛出，旧
节点仍存在且已经被 bake。

未命中分支在创建新 Layer 前先 trim。bake 完成后才增加 byteWeight 并插入，因此 Layer
创建或 bake 异常不会把 entry 发布到 list；局部 Variant owner 负责释放已创建的 Layer。

## 5. 预插入淘汰算法

```text
trimCacheBeforeInsert():
    if signed32(currentBytes) <= signed32(cacheLimitBytes):
        return

    thresholdBits = uint32(cacheLimitBytes * 99) / 100
    keptBits = 0
    for node from front to back:
        sumBits = uint32(keptBits + node.byteWeight)
        if signed32(sumBits) > signed32(thresholdBits):
            currentBytes -= uint32(node.byteWeight)
            erase node
        else:
            keptBits = sumBits
```

乘法和加法均保留低 32 位，比较按有符号 32 位解释。算法只在 miss 前运行，并且只有
`currentBytes > limit` 才启动；新 entry 插入后可以再次超过 limit，直到下一次 miss 才
尝试淘汰。遍历从 front 到 back，保留仍能落入 99% threshold 的较新节点；不调用
`Invalidate`，被淘汰 Layer 只经 Variant owner 释放。

## 6. bake 数据流与 scratch Layer 生命周期

```text
bakeSource(source, entry):
    source.drawLayer(entry.layer)              // 返回状态忽略
    width  = entry.layer has width  ? Integer(width)  : 0
    height = entry.layer has height ? Integer(height) : 0
    entry.byteWeight = low32(width * height * 4)

    applyPackedCornerTint(entry.layer, entry.colors,
                          rect(0, 0, width, height),
                          (entry.blendMode & 0xF0) == 0x10)

    lowBlend = entry.blendMode & 0xF
    if lowBlend is neither 1 nor 2:
        return
    if renderer is GPU and entry.layer has private GLL native:
        return

    bufLayer.setSize(width, height)
    bufLayer.copyRect(0, 0, entry.layer, 0, 0, width, height)
    entry.layer.fillRect(0, 0, width, height, 0xFF000000)
    entry.layer.operateRect(0, 0, bufLayer, 0, 0, width, height, 15)
    if lowBlend == 2:
        entry.layer.adjustGamma(1,255,0, 1,255,0, 1,255,0)
```

`source.drawLayer`、Layer dimension 读取和后续脚本方法都复用同一个 dispatch result
Variant，并忽略普通 HRESULT。dimension helper 先做 MEMBERMUSTEXIST probe；不存在或
普通读取失败时的有效结果为 0。

`_bufLayer` 在参数构造器中只创建一次，是 `SourceCache` 持有的长期 scratch Layer；每次
低 blend bake 复用同一个对象。默认脚本构造器不会创建它，因此默认构造对象的 getter
返回 Void。`ResourceManager(owner, limit)` 的基类构造路径会创建它。

## 7. packed corner tint 的精确边界

四端共同流程为：

1. 四角全为 `0xFF808080`，或四值 bitwise-AND 等于 `0xFFFFFFFF` 时直接返回；
2. 非 software renderer 只做一次 private GLL native-instance query 并丢弃结果，不执行
   CPU 像素循环；
3. software 分支严格把 Variant 转为 Layer，随后读取 clip、writable pixel buffer 和 pitch；
4. 用 32 位 wrap-add 计算 `rect.x + width` / `rect.y + height` 后与 clip 相交；仅
   `top >= bottom` 是函数级提前返回。`left >= right` 只令内层循环零次，外层仍逐行推进；
5. 角颜色按小端 packed RGBA 解包，横纵 span 都是 wrap-subtract 的 `dimension - 1`；
   插值乘加保持 32 位 wrap，除法遵循 ARM 有符号整除边界；
6. pixel 内存按 BGRA 邻接关系写入。半强度模式只让 RGB 乘积除以 128；Alpha 在四个原版
   中始终除以 255。每通道上限 clamp 到 255。

最后一点已逐端核实：iOS arm64 伪代码对前三个通道使用运行时 divisor，对 Alpha 明确
使用 `0xFF`；Android arm64 和两个 32 位端以不同的 magic multiply/shift 形式实现同一
固定 `/255`。本地 `SourceCache.cpp` 第 220--227 行也正是 RGB 使用 `colorDivisor`、Alpha
固定传 `255u`，无需语义修改。

## 8. list 容器与 owner 证据

| 容器操作 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| copy-node create / push-front | `0x6E8040` | `0x5A67DE` | `0x100100E54` | `0xFDFB0` |
| link-before | `0x145EFDC` | `0xD3B884` | push helper 内联 | push helper 内联 |
| erase/unlink | `0x145EFF8` + caller cleanup | `0x5A67B0` | `0x1000FFABC` | `0xFCD2E` |
| clear | caller/通用 list 析构内联 | `0x59A594` | `0x1001281E8` | `0x1275F8` |

上述 helper 均完成 fresh 完整反汇编。节点复制顺序与 `Entry` 字段顺序一致：key、layer、
src 先取得各自 owner，随后复制标量。节点销毁按 C++ 成员逆序执行：src、layer、key，
最后释放节点内存。iOS libc++ helper 显式维护 list size；Android 的 libstdc++ 形态在
sentinel、size storage 和 unlink helper 上不同，但可恢复的源级容器均为
`std::list<Entry>`，不是 vector/deque 或自制 intrusive list。

## 9. `clearCache` 和 getter 的共同伪代码

```text
clearCache():
    for entry in list:
        if entry.layer Variant type is exactly Object:
            entry.layer.Object->Invalidate(
                flag=0, member=null, hint=null,
                objthis=entry.layer.Object)
            // status 忽略；注意 objthis 不是 Variant.ObjThis
    list.clear()
    currentBytes = 0

getBufLayer():
    return CopyRef(_bufLayer)  // Object 与 ObjThis 都原样保留
```

`clearCache` 只以 Variant type==Object 为 gate，不检查 dispatch 非空。typed-null Object 会在
调用 `Invalidate` 时解引用空指针。若任一 `Invalidate` 抛出，list 不清空、byte counter 不
复位；前面的 Layer 可能已被 invalidated，形成原版的 partial-commit 边界。正常完成后，
list clear 才释放全部节点 owner，并把当前字节数置零。

公共 clear 不触碰 `_owner`、`_primaryLayer` 或 `_bufLayer`。析构也不复用 public clear：它
直接析构 list，所以缓存 Layer 不收到脚本可见 `Invalidate`；随后持久 Variant 按成员逆序
释放。getter 只是普通 Variant CopyRef，因此外部 alias 可以活过 SourceCache 本体。

## 10. 平台差异与共同源码结论

- Android arm64 将 `loadSource`、`clearCache` 和相邻构造清理并入一个 IDA 函数范围，
  registrar 仍分别保存两个内部入口；其余三端为独立函数。这是优化/函数边界差异。
- identity compare 仅 Android arm64 内联；private GLL query 也仅该端内联。
- list helper、sentinel 和 size 维护体现 Android libstdc++ 与 iOS libc++ ABI 差异；节点
  字段 owner 次序和源级 `std::list<Entry>` 一致。
- `/255` 的实现有普通除法和 magic multiply/shift 差异，不能据汇编形态误判为通道语义
  分歧。
- 四端在缓存 identity、命中不提升、变色 copy+erase、pre-insert trim、bake 次序、
  scratch Layer、clear partial commit 和 getter owner 上没有可见源级分歧。

## 11. 与本地源码和测试逐行对照

本地实现对应：

- `cpp/plugins/motionplayer/SourceCache.h`：`Entry` 字段顺序、`std::list<Entry>`、persistent
  Variant 和 32 位 cache counters；
- `cpp/plugins/motionplayer/SourceCache.cpp:485`：参数构造器与 persistent bufLayer；
- `cpp/plugins/motionplayer/SourceCache.cpp:508`：descriptor 读取、identity、三分支和 owner；
- `cpp/plugins/motionplayer/SourceCache.cpp:716`：public clear partial-commit；
- `cpp/plugins/motionplayer/SourceCache.cpp:733`：getter CopyRef；
- `cpp/plugins/motionplayer/SourceCache.cpp:743`：bake/scratch Layer；
- `cpp/plugins/motionplayer/SourceCache.cpp:815`：pre-insert trim；
- `cpp/plugins/motionplayer/SourceCache.cpp:131`：packed tint 与 Alpha 固定 `/255`。

现有单元覆盖对应：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:15957`：SourceCache 与 ResourceManager 两张
  类表的 read-only `bufLayer`、persistent alias 和默认构造 Void；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16039`：析构不调用 public clear；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16175`：复合 identity、颜色命中分支和
  pre-insert trimming；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16375`：bake 的 low-blend scratch Layer 调用序；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16721`：software tint 的严格 Layer conversion 与
  异常不发布边界。

逐行对照没有发现需要修改的本地编译语义；本 slice 只增加证据、台账状态与 IDB 注释/
命名/书签。四个 IDB 均已原位保存。

## 12. 本 slice 的状态结论

`SourceCache` 的三个非构造 callback 已从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。`ResourceManager` 的三条同名重复发布使用完全相同 callback 地址，也一并
提升；ResourceManager 的 12 个非构造 callback body 至此全部闭合。构造器继续保持独立的
`CONSTRUCTOR_EVIDENCED_4_4` 状态，完整 root-reachable helper、对象和容器总账仍未完成，
因此这不代表 motionplayer 总目标完成。
