# Motionplayer EmoteEngine HM6/HM7 容器四参考恢复

日期：2026-08-14

本记录只使用 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS
armv7 四个当前目标的新反编译、反汇编、内存数据和交叉引用。它替换 `EmoteEngine.h/.cpp`
中仍以旧单一 Android arm64 地址、统一 32 字节节点和“insertion order”描述全部目标的注释。

## 结论

源码级类型已经正确：

```cpp
struct EmoteVarRef {
    int32_t type;
    int32_t index;
};

using EmoteVarRefMap =
    std::unordered_map<ttstr, EmoteVarRef, ttstr_hash, ttstr_equal>;
using LabelValueMap =
    std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;
```

HM6 是 metadata label 到 `{controller category, deque index}` 的路由表；HM7 是长期存活的
label 到 scalar double 表。二者都是 `EmoteEngine` 的末尾内联成员，按 HM7、HM6 的顺序析构。
HM6 在 metadata reset 开头 `clear()`，HM7 故意不清理。两种 mapped value 都是无 owner 的
8 字节 POD；每个节点唯一的非平凡成员是 owning `ttstr` key。

真正需要修正的是物理 ABI 和注释：四端 map header、成员偏移和 node layout 不相同；旧注释
所列 `0x686944` 也不是 HM7 upsert，而是落在 Android arm64 的 deque 扩容函数内部。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_ctor_guess` | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |
| `EmoteEngine_dtor_guess` | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |
| `EmoteEngine_resetMetadataState_guess` | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |
| `EmoteVarRefMap_getOrInsertMapped_guess` | `0x6859AC` | `0x56719C` | `0x1001A8574` | `0x1A7B48` |
| HM6 find primitive | `0x685BD4` | `0x569A10` | `0x1001C5AB0` | `0x1C2CB0` |
| `LabelValueMap_getOrInsertMapped_guess` | `0x683D24` | `0x56559C` | `0x10010BD28` | `0x1096A4` |
| HM6 clear | reset 内联 | `0x564B96` | `0x1001BDBEC` | `0x1BC6FC` |
| HM6 node-chain destroy | reset/dtor 内联 | `0x564BB8` | `0x1001B87C8` | `0x1B7F64` |
| HM7 node-chain destroy | dtor 内联 | `0x564B58` | `0x100129FC4` | `0x128DCC` |
| HM6 full destructor | dtor 内联 | `0x564B78` | `0x1001B8790` | `0x1B7F48` |
| HM7 full destructor | dtor 内联 | `0x564B18` | `0x100129F8C` | `0x128DB0` |

Android arm64 的 find primitive 返回“匹配节点的 predecessor/link”，调用者再解引用得到 node；
另外三端表中的 wrapper 返回 node 本身。这只是旧 libstdc++ 模板展开差异，源码共同语义都是
`find(key)`。

## Engine 成员偏移和 map header

| 目标 | STL | HM6 offset | HM7 offset | 单个 header | Engine 尾端 |
|---|---|---:|---:|---:|---:|
| Android arm64 | old libstdc++ | `+1384` | `+1440` | 56B | `1496` |
| Android armv7 | old libstdc++ | `+732` | `+760` | 28B | `788` |
| iOS arm64 | libc++ | `+984` | `+1024` | 40B | `1064` |
| iOS armv7 | libc++ | `+528` | `+548` | 20B | `568` |

旧 libstdc++ header 按 pointer width 缩放：

| 逻辑字段 | arm64 | armv7 |
|---|---:|---:|
| bucket pointer | `+0` | `+0` |
| bucket count | `+8` | `+4` |
| `_M_before_begin._M_nxt` | `+16` | `+8` |
| size | `+24` | `+12` |
| max load factor (`float`) | `+32` | `+16` |
| rehash-policy next resize | `+40` | `+20` |
| inline single-bucket slot | `+48` | `+24` |

libc++ `__hash_table` header 同样按 pointer width 缩放：bucket pointer、bucket count、first-node
link、size 和 max-load-factor 分别位于 `0/1/2/3/4 * pointer_width`；最后一个字段是 float，
因此得到 40B/20B。

### 初始 bucket 状态

Android 两端旧 libstdc++ 的 default constructor 内部请求 bucket count 10。rehash prime table 的
前 12 字节在两端完全相同：

```text
02 02 02 03 05 05 07 07 0B 0B 0B 0B
```

索引 10 因而选择 11 buckets。arm64 分配并清零 88B，armv7 分配并清零 44B；max load
factor 为 1.0，node chain 和 size 为零。Android arm64 的 HM6/HM7 初始化块分别位于
`0x67BBF4..0x67BC4C`、`0x67BC50..0x67BCA8`；Android armv7 由
`0x564A6C`、`0x564AC2` 两个同形 helper 完成。

iOS 两端 libc++ default constructor 把 bucket pointer/count、first-node、size 清零，写入
max load factor 1.0，不分配 bucket；HM6/HM7 在第一次插入时才分配。这是同一源码级
default construction 在不同标准库中的实现差异，不能在 portable 源码中用显式
`reserve(10)` 强行统一，否则会破坏 iOS 的原始 lazy-allocation 边界。

## 节点布局

### HM6：`ttstr -> EmoteVarRef`

| 目标 | node size | next | cached hash | owning key | `{type,index}` | 返回 mapped |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | 32B | `+0` | `+24` | `+8` | `+16` | node `+16` |
| Android armv7 | 20B | `+0` | `+16` | `+4` | `+8` | node `+8` |
| iOS arm64 | 32B | `+0` | `+8` | `+16` | `+24` | node `+24` |
| iOS armv7 | 20B | `+0` | `+4` | `+8` | `+12` | node `+12` |

每个 miss 先分配 node、原子 CopyRef key、把两个 int32 清零，再插入/必要时 rehash，最后
返回 mapped pointer。hit 直接返回既有 mapped pointer，不 AddRef key，也不移动/重链 node。
因此 builder 的 `map[label]` 完全等价于现有源码：新 label 初始 `{0,0}`，调用点随后顺序写
`type` 和 `index`；重复 label 覆盖原 pair，保留首次节点身份和当前迭代位置。

四端 `EmoteVarRefMap_getOrInsertMapped_guess` 都恰好有 11 个 metadata-builder 直接调用：

- bust/simple spring 两个 key；
- 两组 chain spring 共三个 key；
- eye 一个；
- eyebrow 一个；
- mouth 的 ordinary/talk 两个；
- transition 一个；
- loop 一个。

selector 的 HM6 插入在其较大 builder 中与其他写入合并/展开方式不同，但共同业务集合还包括
type 8；四端最终 reader 都支持 type `0..8` 中当前实现使用的 `0/1/2/3/4/5/6/7/8` 路由。
这里的“11 个直接调用”描述 helper xref，而不是 label 的最大数量或 category 数量。

### HM7：`ttstr -> double`

| 目标 | node size | next | cached hash | owning key | double | 返回 mapped |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | 32B | `+0` | `+24` | `+8` | `+16` | node `+16` |
| Android armv7 | 32B | `+0` | `+24` | `+8` | `+16` | node `+16` |
| iOS arm64 | 32B | `+0` | `+8` | `+16` | `+24` | node `+24` |
| iOS armv7 | 20B | `+0` | `+4` | `+8` | `+12` | node `+12` |

Android armv7 是旧统一注释最明显的反例：HM6 node 只有 20B，但 HM7 的 8 字节 double
对齐使 key 移到 `+8`、mapped 移到 `+16`，node 扩成 32B。iOS armv7 ABI 允许 double
位于 `+12`，所以仍是 20B。

miss 的 double 明确 value-initialize 为 `+0.0`；hit 只返回 mapped pointer，调用者覆盖 double，
不重链节点。该 helper specialization 同时服务 Player 的 label-value map 与 EmoteEngine HM7，
所以本地复用 `detail::LabelValueMap` 不是偶然的类型合并，而是与参考生成代码一致。

## hash、相等和 bucket 边界

四端共同使用 `tTJSVariantString` 内的 Hint cache：arm64 hint 位于 backing object `+68`，armv7
位于 `+60`。已有非零 hint 直接使用；零 hint 才遍历 UTF-16 code units，执行
`1025 -> xor >> 6 -> *9 -> xor >> 11 -> *32769` 混合。最终零 hash 改成 `0xffffffff`；
null ttstr backing 不取 hint，hash 为零。因此 null ttstr 与非-null empty ttstr 不混同。

key equality 先走 backing-pointer identity，然后检查 null/non-null 和长度，最后比较 UTF-16
内容。bucket scan 先比较 cached hash，再比较 key。Android 的 11/prime bucket count 走 modulo；
libc++ 同时支持 power-of-two mask 和非 power-of-two modulo。

两个标准库都用 bucket predecessor + 全局 forward node chain 表示 unordered container。
但“链就是 insertion order”不是源码或 ABI 保证：

- 新 bucket 的第一个节点和 collision 节点的链接位置不同；
- rehash 会重建 bucket predecessor，并可能改变全局遍历次序；
- libstdc++ prime policy 与 libc++ power-of-two/非 power-of-two policy 不同；
- hit overwrite 不会重链。

因此 portable port 正确边界是使用 `std::unordered_map`，不另外模拟顺序。HM7 post-loop 对每个
已合并 label 写不同 Player key，没有跨 label 的顺序依赖；四端不同迭代顺序不会改变业务结果。

## clear、析构和跨 metadata 生命周期

HM6 `clear()` 的四端共同语义：

1. 沿全局 first-node chain 逐 node；
2. 先保存 `next`；
3. Release key 的一个引用；
4. delete node；
5. 清零所有 bucket slot、first-node 和 size；
6. 保留 bucket pointer/count、max load factor 和 policy/capacity。

`resetMetadataState` 在所有 controller deque 之前清 HM6，然后清旧 metadata deques并重建。
HM7 没有出现在 reset clear 链中：旧 scalar label/value 可以跨 metadata replacement 存活，直到
后续 controller step、`setVariable` fallthrough、restore/spring 路径覆盖，或 Engine 最终析构。
本地原先的“HM6 clear、HM7 不 clear”执行代码已经正确；本纵切面只把该非直观边界写回注释。

正常析构在删除 wind owner 后严格按 HM7 -> HM6 -> HM5 -> HM4 的逆声明顺序。HM6/HM7 node
都只析构 key，随后释放 bucket storage；mapped `double`/`EmoteVarRef` 没有析构动作。Android
可能内联完整 chain/bucket teardown，iOS 则更常调用独立 dtor/chain helper，但源码生命周期相同。

## setVariable 数据流和重复 label 边界

`EmoteEngine::setVariable` 是 HM6 mapped pair 的业务 reader：

1. 计算/读取 key hash，查 HM6；空 key 也照常查找；
2. hit 后读取 type/index，置 dirty，并把 double/easing/duration 转 float 后路由到对应 controller；
3. type `0/1/2` 在 sync-wait gate 下可能落入 HM7；
4. HM6 miss 也落入 `HM7[key] = value`；
5. HM7 hit overwrite 不重链，miss CopyRef key 并先产生 `+0.0` mapped 再被 assignment 覆盖。

metadata 中若多个 builder 重复使用同一 label，后执行的 HM6 builder 覆盖 `{type,index}`，不会
生成第二个节点。HM7 同样把多个 producer 的同名输出折叠成一个 mapped double，最后一次写胜出。
这些是直接使用 `operator[]` 必须保留的边界，不能改成 `emplace` 后忽略 duplicate。

## 与本地源码的对照

执行语义已经一致：

- HM6/HM7 都是精确的 `std::unordered_map` specialization；
- `ttstr_hash` 保留 Hint cache、null/empty 区分和非零 sentinel；
- `ttstr_equal` 使用原生 `ttstr` equality；
- builders 和所有 HM7 writers 使用 `operator[]`，保留 zero-init + overwrite 语义；
- reset 只 clear HM6，正常析构按 HM7 -> HM6；
- HM7 与 Player 复用同一 `LabelValueMap` specialization；
- post-loop 不依赖特定 unordered iteration order。

本纵切面没有改变运行时代码。源码修改仅为：

- 删除错误的旧 upsert 地址和单一 A64 node layout；
- 写入四 ABI header/member/node 表和标准库 constructor 差异；
- 明确 node chain 不是 insertion-order contract；
- 明确 HM6 clear 保留 buckets、HM7 跨 metadata reset 存活；
- 给 `EmoteVarRef` 增加 8B compile-time layout assertion；
- 删除 progress mouth/transition 中已过期的旧 A64 地址和 helper 名注释。

四份 recovery IDB 已写入上述 helper 语义名、节点/成员布局、clear/dtor 生命周期和旧地址纠错
注释，并在验证后保存。

## 验证

- 完整 motionplayer 翻译单元语法检查；
- Web Debug 完整构建与最终链接；
- `git diff --check`；
- 四份 recovery IDB 保存。

