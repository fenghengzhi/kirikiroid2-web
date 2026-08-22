# MotionPlayer Player HM3/HM4/variable-deque 连续布局（四参考，2026-08-18）

## 结论

四个 `reference/binaries/` 参考共同证明，`Player` 的 tag 后标量区并不在
`meshDivisionRatio` 结束。它后面没有其他标量、padding owner 或 Web 端辅助状态，而是
立刻连续声明三份 nontrivial STL 成员：

```cpp
double meshDivisionRatio;
unordered_map<ttstr, PerNodeLayerState, ttstr_hash, ttstr_equal> perNodeLayerStateMap;
unordered_map<ttstr, double,            ttstr_hash, ttstr_equal> variableSnapshotMap;
deque<VariableLabelScope> variableLabelScopes;
// next Player member
```

即本项目调查编号中的 HM3、HM4 和 variable-track deque。三者构造顺序与上述声明顺序
相同，正常析构严格相反：deque -> HM4 -> HM3 -> 更早的 tag Variant。旧源码把 HM3、
HM4 和 deque 分散在类尾部，虽然业务访问能工作，却改变了对象布局、构造异常回滚和
正常析构的 owner 顺序；本轮已把它们迁回 native 连续区。

本报告只把四参考共同支持的源级声明写入编译源码。绝对地址、调查编号和各 STL ABI
的物理 header/node 布局继续只保留在分析与 recovery IDB 中。

## 1. 四 ABI 的闭合边界

从上一轮已确认的 `meshDivisionRatio` 开始，三个容器及下一成员边界如下：

| 成员/边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `meshDivisionRatio` | `+0x498` | `+0x340` | `+0x428` | `+0x2FC` |
| HM3 `PerNodeLayerStateMap` | `+0x4A0` | `+0x348` | `+0x430` | `+0x304` |
| HM3 header size | `0x38` (56) | `0x1C` (28) | `0x28` (40) | `0x14` (20) |
| HM4 `VariableSnapshotMap` | `+0x4D8` | `+0x364` | `+0x458` | `+0x318` |
| HM4 header size | `0x38` (56) | `0x1C` (28) | `0x28` (40) | `0x14` (20) |
| variable deque | `+0x510` | `+0x380` | `+0x480` | `+0x32C` |
| deque header size | `0x50` (80) | `0x28` (40) | `0x30` (48) | `0x18` (24) |
| 下一成员边界 | `+0x560` | `+0x3A8` | `+0x4B0` | `+0x344` |

每一行都由前一对象的 ABI 大小精确触及下一行，不存在可容纳另一字段的空洞。Android
采用旧 libstdc++ unordered header 和 deque header，iOS 采用 libc++；因此四端绝对
大小不同，但源级声明次序唯一。

## 2. constructor：默认构造与 STL ABI 差异

### 2.1 四端构造位置

| 目标 | `Player` constructor | HM3/HM4/deque 构造区 |
|---|---:|---:|
| Android arm64 | `0x6CC110` | `0x6CC30C..0x6CC3C8` |
| Android armv7 | `0x5935C4` | HM3 `0x593724`；HM4 `0x593740`；deque `0x593744` |
| iOS arm64 | `0x10011EC04` | HM3 `0x10011ED5C`；HM4 `0x10011ED74`；deque `0x10011ED90` |
| iOS armv7 | `0x11D488` | HM3 `0x11D6FE`；HM4 `0x11D716`；deque `0x11D72C` |

constructor 先完成 HM3，再完成 HM4，最后构造 deque；异常清理只逆序销毁已经完成的
nontrivial prefix。这个顺序与 normal destructor 的三段反向链互相验证，并排除了把
deque 放到 maps 之前、把 HM4 放到 HM3 之前或把三者留在本地类尾部的可能。

### 2.2 Android 的 `10 -> 11 buckets` 不是源代码 `reserve(10)`

Android 两份旧 libstdc++ 实现的默认 unordered-map 构造路径会把内部 bucket-count
hint `10` 交给 prime rehash policy；策略选择素数 `11`，立即分配 11 个指针并清零
bucket array。Android armv7 中 HM3 经 `0x5AF5B4`、HM4 经共享的 `0x564AC2` 进入这条
helper，调用点明确传入 10。arm64 的内联区呈现相同的 11-bucket 分配和清零结果。

iOS libc++ 的默认构造完全不同：bucket pointer/count、first-node/size 初始化为零，
`max_load_factor` 初始化为 `1.0f`，此时不分配 bucket。第一次实际插入才进入 lazy
allocation/rehash。

这两种轨迹来自**相同的源级默认构造**在不同标准库 ABI/版本中的实现差异。因此正确
的可移植恢复是普通默认构造：

```cpp
PerNodeLayerStateMap perNodeLayerStateMap;
VariableSnapshotMap variableSnapshotMap;
```

不能为了模仿 Android 的结果而增加 constructor 参数、`reserve(10)` 或 `rehash(10)`。
那会在 libc++/Web STL 上制造参考源码没有要求的 eager allocation，并改变 bucket
count、异常点、allocator 调用和后续迭代/rehash 边界。

## 3. HM3：node-path -> `PerNodeLayerState`

HM3 的 key 是 slash 拼接的 node ancestry path，不是更早的 raw-label node-index map。
它在 join reset 时只为符合类型和 join-target 门的非根节点建立快照，在 full reseek 时
按相同 path 和 node type 恢复；成功条目即时 erase，未消费条目在终端 invalidation
pre-pass 后统一 clear。

四端 `operator[]`/upsert 入口为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6EFA54` | `0x5AD532` | `0x10010BF74` | `0x109928` |

miss 会分配 native hash node，并把整个 mapped `PerNodeLayerState` 零初始化；hit 直接
返回旧值而不重置。四端 mapped-value/node 关系如下：

| 目标 | hash-node allocation | mapped base | mapped size |
|---|---:|---:|---:|
| Android arm64 | 720 | node `+16` | 696 |
| Android armv7 | 584 | node `+16` | 560 |
| iOS arm64 | 720 | node `+24` | 696 |
| iOS armv7 | 552 | node `+12` | 540 |

mapped value 的源级嵌套为：

```cpp
struct PerNodeLayerState {
    int nodeType;
    MotionNode::ClipSlot clipSlot;       // map miss 时完整全零，不取 live-slot defaults
    tTJSVariant childPlayerSnapshot;
    std::vector<MeshPoint> meshControlPoints;
    double particleInterp[9];
    tTJSVariant particleArraySnapshot;
};
```

node destruction 的共同逆序是 particle Variant -> outer mesh vector -> child Variant ->
embedded `ClipSlot` -> ttstr key -> hash node。对应 destroy helpers：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| HM3 node destroy | `0x6DA3F8` | `0x59BBCE` | `0x10012A038` | `0x128E08` |
| embedded slot destroy | `0x6DA44C` | `0x59BC0A` | `0x10012A0A4` | `0x128E4A` |

完整 producer/restore/invalidation、malformed parent-chain 以及 slot 内部 owner 顺序见
`analysis/motionplayer_join_snapshot_four_binary_2026-08-11.md`；本轮新增的是 HM3 在完整
`Player` 声明中的精确前后邻接、默认构造 ABI 和外层析构位置。

## 4. HM4：cascade-key -> raw `double`

HM4 与 HM3 使用相同的 `ttstr_hash` / `ttstr_equal`，但 mapped value 只是原始 8-byte
`double`，没有 dispatch、Variant 或引用计数所有权。join reset 把 live variable-track
value 写入 HM4；full reseek 命中时覆盖重建后的 active slot value，随后 clear HM4。

hash node 销毁只需释放 ttstr key 和 node；mapped double 是 trivial。Android armv7
`0x564B18`、iOS arm64 `0x100129F8C`、iOS armv7 `0x128DB0` 是该 ttstr->double
unordered-map destructor family。它与 Player 早期 label-value map/Engine 的同型 map
共享 template 实例是正常的代码折叠，不能据此合并物理容器或生命周期。

旧 IDB 名 `EmoteLabelValueMap_dtor_guess` 对共享 helper 的 receiver 限定过窄；本轮改成
`TtstrDoubleUnorderedMap_dtor_guess`。这只是 stripped template helper 的保守语义名，
不声称恢复了原始 mangled spelling。

## 5. variable-track deque 的连续位置与生命周期

HM4 后立即是 `deque<VariableLabelScope>`。每个 element 保存 cascade key、cursor、raw
value、frame-source Variant 和两个 timeline slots；64-bit element 为 160 bytes，32-bit
element为 128 bytes。element 析构按 slot[1].easing -> slot[0].easing -> frameSource ->
cascadeKey 释放 owner。

deque 的 native header size 随 STL ABI 为 80/40/48/24 bytes，但四端都精确终止于本轮
记录的下一成员边界。`clear()` 销毁所有 element，但保留该实现允许保留的 map/block
capacity；不能用 swap-empty 等价替换。初始化的 append-first 异常前缀、两个独立
`label` 读取、slot type-zero seed、forward/backward cursor 与 merge 的 stale-field 边界
见 `analysis/motionplayer_variable_track_four_binary_2026-08-12.md`。

## 6. destructor 与对象生命周期闭环

| 目标 | `Player` destructor | deque destroy | HM4 destroy | HM3 destroy | 更早 tag-owner 边界 |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6CCEBC` | `0x6CCF10` | `0x6CCF6C` | `0x6CCFC4` | `0x6CD018` |
| Android armv7 | `0x593C24` | `0x593C50` | `0x593C84` | `0x593C8C` | `0x593C94` |
| iOS arm64 | `0x10011F2A0` | `0x10011F2D8` | `0x10011F318` | `0x10011F320` | `0x10011F328` |
| iOS armv7 | `0x11DCC4` | `0x11DD3E` | `0x11DD7E` | `0x11DD88` | `0x11DD92` |

共同 normal teardown：

```text
later Player members
  -> variableLabelScopes
  -> variableSnapshotMap (HM4)
  -> perNodeLayerStateMap (HM3)
  -> intervening POD scalar block (no destructor work)
  -> tagFrameSource Variant
  -> earlier Player owners
```

POD scalars在物理上位于 tag Variant 与 HM3 之间，但析构器不产生调用，所以 native
listing 从 HM3 直接跨回 tag Variant。这正是为什么只看 destructor call list 容易把
HM3 错放到 tag 后；必须结合 constructor stores、四端 header size 和 accessor displacement
才能恢复完整声明。

## 7. 源码落地

`Player.h` 现在把三份容器直接声明在 `_meshDivisionRatio` 后，并删除原先位于类后段的
重复/散落声明：

```cpp
double _meshDivisionRatio = 1.0;
detail::PerNodeLayerStateMap _perNodeLayerStateMap;
detail::VariableSnapshotMap _variableSnapshotMap;
detail::VariableLabelScopeDeque _variableLabelScopes;
```

容器保持无 constructor 参数、无 `reserve`、无额外 initializer。源码注释只描述容器
角色、所有权和四端共同声明顺序，不保留任何参考绝对地址。

本轮没有更改 HM3/HM4/deque 的业务算法；已有 alias 仍位于
`internal/player_containers.h`，HM3 mapped type 位于 `internal/value_structs.h`。实际语义
变化仅是恢复 `Player` 的 native member order，从而同时纠正 object layout、constructor
异常回滚和 destructor owner order。

## 8. recovery IDB 写回

四库共写回 40 comments、33 bookmarks、24 semantic renames：

- Android arm64：10 comments、8 bookmarks、3 renames；
- Android armv7：11 comments、9 bookmarks、7 renames；
- iOS arm64：10 comments、8 bookmarks、6 renames；
- iOS armv7：9 comments、8 bookmarks、8 renames。

写回覆盖 constructor 三成员边界、destructor 逆序链、HM3 subscript/node destroy、HM4
同型 map destructor/node chain 及 embedded frame-slot destruction。私有 stripped helper
均保留 `_guess` 后缀。

iOS armv7 继续使用 different-path 安全保存：

- pre-V256 backup：
  `out/idb-recovery/v256-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v256.i64`，
  377,493,712 bytes，SHA-256
  `07E8CBF43E988AE0C4C6F57E675F8DDF0F2315504AD7B6CD5BFBE9E02B221592`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v256.i64`，377,592,016 bytes；
- `C:\IDA\idat.exe -A` 独立 probe 退出 0；
- canonical loose files 分别移到 `pre-v256-canonical-loose/` 和
  `verify-readback-loose/`，均未删除；
- candidate 替换 canonical 后由 MCP 重新打开，回读 11 个相关 semantic names 与
  constructor comments，随后关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,671,370 | `788359E4968A0DDB344879A73101DFDA450F2BA08C6193A1894717C3D23D56A4` |
| Android armv7 | 345,837,939 | `89F2D3D38E8462B7F6D7B2A88CEF6D3F6D5A25EC51D43B89299CEC73768316FA` |
| iOS arm64 | 334,876,582 | `97AC9C2AA5BEA67EA835096A6D8078A04F78373A5C77866F1CDC85AECD320957` |
| iOS armv7 | 377,592,016 | `59B13FEC4DD6819B38D80C4E87CC3E4CAC418C4731BDA2E8B3CCB9A20FD9E57A` |

最终 MCP session 数为 0。

## 9. 验证与 Wasm 基线

实际完成：

- 完整 `motionplayer-dll.cpp` 普通 Web `-fsyntax-only`：通过；
- 同一完整 TU 加 `KRKR2_WASMTIME_HEADLESS=1`：通过；
- Web Debug：33-step rebuild/link 通过；
- Wasmtime Headless Debug：62-step rebuild/link 通过；
- `krkr2_wasmtime_guest`：2-step rebuild/link/exnref conversion 通过；
- 三目标 `--parallel 1` 复核均为 `ninja: no work to do`；
- `git diff --check` 无 whitespace error，仅有工作树既有 LF/CRLF warning；
- IDA MCP session 数为 0。

产物：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,346 | `0x1BD31` | `0x1A410B5` | `0x5A3E40` | `0x3185F7B` | `3EFC25165B81DA1C3DA64EC4C14D536F4CBAD839E10DE60F004EA98FC67C3847` |
| Wasmtime `index.wasm` | 85,002,487 | `0x1BA50` | `0x19E9063` | `0x5A1090` | `0x3141E11` | `9BD4E671C434A247C8BE1800D0E49B047150D0EB4460813505FF72B3D836D800` |
| guest | 151,478,415 | `0x1618E` | `0x13D7DE9` | `0x4D1630` | `0x1421EBA` | `D31D0272BB4840D79D08045683F3AD8990352CB5E4440B185EC7F6E934740B4F` |

相对 V255，两份主 wasm 的总大小及列出的 FUNCTION/CODE/DATA/name section size 完全
不变，但哈希变化；这与成员移动后访问 displacement/生成字节变化而整体编码长度不变
一致。guest 的四个列出 section size 也不变，最终文件多 6 bytes；该差值位于未列出的
framing/custom-section 范围，不能据此推断额外业务算法。三条产物链均已由 clean
incremental rebuild 和 no-work 重跑闭合。

