# MotionPlayer Engine HM4 instant-variable set 四参考二进制复原（2026-08-14）

## 范围与结论

本纵切面重新以 `reference/binaries/` 中 Android arm64、Android armv7、
iOS arm64、iOS armv7 四份当前参考产物为准，闭合 Engine HM4
`unordered_set<ttstr>` 的：

- Engine 内偏移、表头大小与构造默认值；
- 节点字段顺序、大小、key 引用计数和缓存 hash；
- insert、duplicate、find、rehash、clear 和 dtor；
- `instantVariableList` 到 Timeline Track 标志的完整数据流；
- 空 key、重复 key、重复 builder、异常中途状态和平台 STL 差异。

此前源码把 Android arm64 的 24 字节节点写成所有平台的统一布局，并引用了旧
`libkrkr2.so` 的 `sub_68BF40` / `sub_689760` / `sub_6696B8`。这三项都不能描述
当前四参考 ABI，已经从编译源码注释移除。共同的源类型仍然是：

```cpp
std::unordered_set<ttstr, ttstr_hash, ttstr_equal>
```

但 Android 使用旧 libstdc++ 实现，iOS 使用 libc++ 实现；不能把任一端的私有表头
或节点结构硬编码成跨平台类型。

## 四端函数映射

### Engine 调用链

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine ctor | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |
| variable-container reset | `0x666B78` | `0x555A04` | `0x1001A66AC` | `0x1A5D88` |
| metadata reset caller | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |
| instant list builder | `0x66CA2C` | `0x558DBC` | `0x1001AB6E4` | `0x1AAE18` |
| initialize timeline state | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |
| Engine dtor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |

### 容器原语

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| set ctor | Engine ctor 内联 | `0x565842` | Engine ctor 内联 | Engine ctor 内联 |
| set insert | `0x686B40` | `0x5680F4` | builder 内联 | builder 内联 |
| find predecessor | `0x686DD0` | `0x568270` | 不适用 | 不适用 |
| find node | predecessor 后取 next | `0x56850A` | `0x1001C50C8` | `0x1C2524` |
| insert/link node | `0x686CB0` | `0x5681C8` | builder 内联 | builder 内联 |
| rehash | `0x686EC0` | `0x5682CA` -> `0x56833E` | `0x1001C421C` | `0x1C1968` |
| clear | reset helper 内联 | `0x563C02` | `0x1001BDB3C` | `0x1BC6A4` |
| destroy node chain | clear/dtor 内联 | `0x563C24` | `0x1001B7F74` | `0x1B7768` |
| full set dtor | Engine dtor 内联 | `0x563BE4` | `0x1001B7F3C` | `0x1B774C` |

Android 的 find 原语返回 bucket 中记录的“前驱”；实际命中节点是
`predecessor->next`。libc++ 的 find 直接返回命中节点。四端名字因此只表达恢复的
源语义，不声称它们有相同私有 ABI。

## Engine 成员与表头布局

| 参考 | STL | HM4 偏移 | 表头大小 | 构造后的 bucket 状态 |
|---|---|---:|---:|---|
| Android arm64 | old libstdc++ | `+1272` | 56 B | 请求 10，素数策略选 11，立即分配 88 B |
| Android armv7 | old libstdc++ | `+676` | 28 B | 请求 10，素数策略选 11，立即分配 44 B |
| iOS arm64 | libc++ | `+904` | 40 B | bucket pointer/count 均为 0，惰性分配 |
| iOS armv7 | libc++ | `+488` | 20 B | bucket pointer/count 均为 0，惰性分配 |

旧 libstdc++ 表头按指针宽度缩放：

```text
+0P  bucket predecessor array pointer
+1P  bucket count
+2P  before_begin.next / global first node
+3P  element count
+4P  float max_load_factor (= 1.0), 后随 32 位对齐槽（64 位）
+5P  next_resize threshold
+6P  inline one-bucket storage
```

因此 arm64 的实际字节偏移是 `0,8,16,24,32,40,48`，armv7 是
`0,4,8,12,16,20,24`。默认 11 桶时 bucket pointer 指向独立 allocation；inline
bucket 只服务 bucket-count 为 1 的路径。构造把 size、first node、next-resize
初始槽清零，再由 prime rehash policy 产生 bucket count 和阈值。

libc++ 表头为：

```text
+0P  bucket predecessor array pointer
+1P  bucket count
+2P  global first node
+3P  element count
+4P  float max_load_factor (= 1.0)
```

arm64 对应 `0,8,16,24,32`，自然大小 40；armv7 对应 `0,4,8,12,16`，自然大小
20。构造不分配 bucket。第一次 miss 插入请求 rehash(1)，libc++ 把 1 规范化为
2，所以首次成功插入后是两个桶。

## 节点布局与所有权

| 参考 | 节点大小 | 字段布局 |
|---|---:|---|
| Android arm64 | 24 B | `next@0, ttstr key@8, cached node hash@16` |
| Android armv7 | 12 B | `next@0, ttstr key@4, cached node hash@8` |
| iOS arm64 | 24 B | `next@0, cached node hash@8, ttstr key@16` |
| iOS armv7 | 12 B | `next@0, cached node hash@4, ttstr key@8` |

节点没有 mapped value。插入 miss 对 key 执行一次 ttstr CopyRef；clear、异常清理或
dtor 对同一槽 Release 一次，然后删除节点。缓存 hash 同时存在两个层次：

1. ttstr backing object 的 Hint 字段，64 位参考在 backing `+68`，32 位参考在
   backing `+60`；
2. 当前 unordered-set 节点自己的 cached hash 字段。

节点级 hash 避免在 bucket 链遍历和 rehash 时重新读取/计算字符串。所有 bucket
entry 都指向该 bucket 第一节点的前驱，所有节点另由一个全局 forward chain 串联；
clear/dtor 沿全局链释放，不需要逐桶查找。

## hash 与 key equality

四端复用同一 ttstr hash 语义：

- null backing 的 hash 是 `0`；
- backing Hint 非零时直接复用；
- Hint 为零时遍历 UTF-16 code unit，使用
  `1025 / xor>>6 / *9 / xor>>11 / *32769` 混合；
- 非 null backing 的计算结果若为零，以 `0xffffffff` 作为缓存 sentinel；
- 计算结果同时写回 backing Hint，并复制进节点 hash。

相等比较顺序也是四端共同的：

1. backing pointer 相同立即相等；
2. 只有两边都非 null 才继续；
3. 先比较 UTF-16 长度，再比较内容。

bucket 链先比较节点 cached hash，再执行上述 key equality。Android 的 bucket index
始终按 prime bucket count 取模。libc++ 在 bucket count 为 2 的幂时用 mask，否则
取模；遍历时若下一节点的重新归桶结果已离开当前 bucket，会立即结束查找。

## 插入、重复键和 rehash

### Android old libstdc++

Android 两端的 builder 都调用独立 set-insert helper，顺序为：

```text
allocate 24/12-byte candidate
  -> candidate.next = null
  -> CopyRef candidate.key
  -> compute/reuse backing Hint and node hash
  -> locate bucket predecessor and compare existing nodes
     hit : Release candidate.key, delete candidate, size unchanged
     miss: test rehash policy, cache node hash, link candidate, size++
```

所以即使 key 已存在，重复插入仍会先进行一次节点 allocation 和临时 key retain。
allocation 失败可以使“逻辑上什么也不会改变”的 duplicate insert 抛异常。命中时
既有节点的 key、链位置和 size 完全不变。

默认表已经有 11 桶；`max_load_factor == 1.0`。miss 插入由 old-libstdc++ prime
rehash policy 的 `next_resize` 决定是否扩桶。rehash 只重排 next 链和 bucket 前驱，
不重新分配节点，也不改变 key 引用计数。

### iOS libc++

iOS 两端把 unique insert 展开在 builder 中：

```text
compute/reuse key hash
  -> find existing node
     hit : return immediately, no node allocation/retain
     miss: allocate 24/12-byte node, CopyRef key, write hash
           -> grow buckets if size+1 exceeds bucket_count*max_load
           -> link node and size++
```

因此 duplicate insert 在 iOS 不会经过候选节点 allocation。第一次 miss 把零桶表扩为
2 桶；后续 rehash request 对 2 的幂保留 power-of-two 数量，否则调用
`std::__next_prime`。bucket selection 与 find 使用完全相同的 mask/modulo 规则。

### 共同边界

- duplicate 不替换 key，不移动既有节点，不增加 size；
- 没有 HM4 单键 erase 调用链；字段级操作只有 insert、find、clear 和 dtor；
- builder 不排序、不迭代集合，也不依赖 unordered iteration order；
- 每个已经成功插入的先前 key 立即提交，后续转换/allocation/rehash 异常没有
  builder 级 rollback。

## clear、析构与 Engine 生命周期

正常 metadata replacement 的相关顺序是：

```text
resetMetadataState
  -> clear HM6
  -> clear metadata controller deques / mirror sets / HM3
  -> recreate variable label Array/Dictionary objects
  -> clear HM4
  -> clear HM5
...
optional buildInstantVariableList
required buildTimelineControl
```

HM4 `clear()` 的共同效果：

1. 沿 global node chain 逐项 Release key、delete node；
2. 将每个 bucket predecessor entry 写零；
3. 将 first-node 和 size 写零；
4. 保留 bucket allocation、bucket count 与 `max_load_factor`。

Android 还保留 old-libstdc++ 的 rehash-policy/next-resize 状态。iOS 在 size 已为零时
直接返回，不重复扫描或重写 bucket table。因而 clear 不是 shrink，也不是用一个新
空 set 替换旧 set。

Engine 正常析构按字段逆序执行：HM7 -> HM6 -> HM5 -> HM4 -> 三个变量 Variant。
HM4 full dtor 先完成上述节点清理，再释放 bucket allocation：

- Android 在 bucket pointer 指向内联 one-bucket 槽时不 delete；
- iOS 将 bucket pointer 置零后，对旧非 null allocation 调用 delete。

构造异常也使用同一个 set dtor 原语回收已完成构造的 HM1/HM2/HM4。iOS ctor 本身
没有为 HM4 分配内存；Android ctor 的 11-bucket allocation 若失败，HM4 尚未成为
完成构造的成员，外层 unwind 只清理更早完成的成员。

## instantVariableList 到 Track 的数据流

### 2026-08-16 V141 root accessor 补充

本节原来的 `propGetCount` / `propGetByNum` 伪代码只概括 HM4 的输入值流。四端 fresh
复核已闭合更精确的访问层：builder 复制输入 Variant，构造 loop-wide root
`ncbPropAccessor`，一次快照 Count，再以 typed indexed `GetValue<ttstr>` 读取各 key；
root accessor 直到循环结束才释放。portable 已按此恢复，并以 getter 写值后返回失败
HRESULT、首项可重入清除 caller owner 的 probe 验证。详见
`analysis/motionplayer_instant_variable_builder_ncb_accessor_indexed_ttstr_four_binary_2026-08-16.md`。

下列伪代码中的 raw helper 名保留为容器数据流速记，不再代表精确 native 调用层。

四端 builder 的共同源级行为仍是：

```cpp
count = propGetCount(instantVariableList);
for (int i = 0; i < count; ++i) {
    raw = propGetByNum(instantVariableList, i);
    key = ttstr(raw);
    HM4.insert(key);
}
```

但 HM4 的唯一业务消费发生在 TimelineData/Track 初次物化时：

```cpp
track.label = propGetString(rawTrack, "label", engineLabelHint);
track.instantVariable = HM4.find(track.label) != HM4.end();
```

这个 bool 是构建时快照。Track 建成后，清空或追加 HM4 不会追溯修改既有 Track；
只有 HM3 state 被清掉并重新从 raw metadata 物化时才重新执行成员测试。

字段基址级数据流在四端一致：

- builder 是 HM4 的唯一插入者；
- `initializeTimelineState` 是 HM4 的成员测试者；
- variable-container reset 负责 clear；
- Engine/ctor-unwind 负责 final dtor；
- 没有按 key erase、迭代导出或脚本直接暴露 HM4 节点的路径。

共享的 set find/clear/dtor helper 还被 HM1/HM2 镜像缓存调用，不能仅凭 helper xref
数量误判成 HM4 有额外读写者；HM4 的 `self + 1272/676/904/488` 基址只出现在上述
字段级路径。

## 输入与异常边界

- builder 自己不 clear；直接重复调用产生集合并集。正常 `applyMetadata` 之所以从
  空 HM4 开始，是因为更早的 metadata reset 已 clear。
- `count <= 0` 时 builder 完全不改 HM4。
- 不检查 `enabled`、元素 Variant 类型或空字符串；Variant-to-ttstr 的结果原样作为
  key。
- null-backed key 走 hash 0/bucket 0；非 null 但 hash 计算为零的 key 缓存
  `0xffffffff`。重复的同一 key 仍按普通相等比较去重。
- 某个元素的读取/ttstr 转换/插入失败时，之前元素已经留在 HM4；不存在事务、预扫
  或回滚。
- Android duplicate 仍可能在候选节点 allocation 处失败；iOS duplicate hit 不分配
  节点。这是可观察的目标 STL 差异，便携源码应继续使用目标平台
  `std::unordered_set`，而不是手写一种统一候选节点流程。
- find 可能在传入 label 的 backing Hint 尚为零时计算并写回 hash；它不修改 HM4
  size、节点或 bucket 链。

## 源码与 IDB 落地

- `EmoteEngine.h` 已用四端偏移/表头/节点表替换 A64-only 24B 节点注释，并明确
  HM1/HM2/HM4 共用源 specialization、但不共用私有 ABI。
- `EmoteEngine.cpp` 已记录 clear 保留 buckets、Android/iOS duplicate allocation
  差异，以及 Track 的 instant flag 是构建时快照；旧单二进制 helper 地址注释已删。
- 四份 recovery IDB 统一命名 set ctor/clear/dtor/destroy-chain/find/insert-link 原语，
  并在 Engine ctor/reset/builder/timeline-init/dtor 上保存本纵切面注释。

## 验证

本纵切面不改变运行时代码，只修正类型/生命周期注释。验证结果：

- 使用 Web Debug 的真实 Emscripten response file 对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`，通过；只有仓库
  既有 `_tss` literal-operator 弃用 warning。
- `cmake --build --preset "Web Debug Build"` 重新编译 `EmoteEngine.cpp`、链接
  `libmotionplayer.a` 与最终 `index.html`/wasm，8 个步骤全部通过；诊断只有既有
  `_tss`、imagepacker `nodiscard`、pthread memory-growth、JSPI 和 JS library warning。
- 定向 `git diff --check` 通过；只报告工作树既有 LF/CRLF 转换提示。
- 三个旧 HM4/mirror reset helper 文本在受影响编译源码中均为零命中。
- 四份 recovery IDB 均成功保存到 `out/ida-recovery/` 对应目标目录。
