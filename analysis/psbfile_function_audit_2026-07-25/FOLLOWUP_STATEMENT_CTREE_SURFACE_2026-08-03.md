# Follow-up：完整 Hex-Rays statement / control-tree 表面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 114 个 MANIFEST FDE 的完整 Hex-Rays `cinsn_t/cit_*` 树现已机械枚举：共
  **2,922 个 statement 节点**，每个 owner 都有且仅有一个根 `block`。
- 12 种 statement op 精确为 `759 block / 1,379 expr / 321 if / 42 switch /
  23 do / 3 while / 1 for / 177 break / 1 continue / 46 goto / 164 return /
  6 empty`；最大嵌套深度为 15。
- 父子关系精确为 `114 root / 2,163 stmt / 321 then / 75 else / 27 body /
  222 case`。46 个 `goto` 全部解析到同一 owner 内 36 个唯一 label，没有悬空或跨函数
  target。
- 2,882 个具体节点落在 1,942 个唯一 EA；40 个 optimizer-synthetic 节点保留原始
  Hex-Rays sentinel，并只继承最近 concrete parent 的机器锚点。全部 1,942 个 anchor
  都由 exact word 和 entry-rooted normal CFG 验证，landing-only 为 0。
- fresh Android ARM64 反编译与本地实现对照后，唯一 `for`、唯一 `continue`、嵌套
  `while`、最深 switch/loop/goto 树及 RAII cleanup loop 都能由现有源码加 O3/STL/析构
  展开解释；没有形成新的确定生产 `cpp/` GAP。本轮不修改 `cpp/`、测试或 fixture。

## 为什么这是独立证据面

既有门禁已经分别固定：机器 normal CFG、switch dispatch、branch predicate、LSDA/
landing、ctree 数值表达式和 `lvar/cot_var` 使用。但是这些表面都不完整描述 Hex-Rays
恢复出的高层 statement 归属：

- normal CFG 能证明机器 successor，却不记录某个节点属于 `then`、`else`、loop body、
  switch case 还是 block 中第几个 statement；
- LSDA 能证明异常 cleanup，却不覆盖普通控制块；
- `cot_num/cot_var` 能固定表达式叶和局部变量使用，却不固定包围它们的完整 `cit_*` 树；
- 仅统计反编译文本中的关键词会丢失 synthetic 节点、label identity 与父子次序。

本轮因此直接从 114 个 `cfunc.body` 根做 preorder traversal，记录每个 `cinsn_t` 的 owner、
owner-local ordinal、parent ordinal、op、child relation、depth、detail、label、raw EA 和
nearest concrete ancestor。机器 CFG 与 ctree 树在 verifier 中独立生成，再在 exact-word
anchor 上汇合。

## 完整 statement census

| `cit_*` | 节点数 | 有该 op 的 owner 数 |
| --- | ---: | ---: |
| `block` | 759 | 114 |
| `expr` | 1,379 | 77 |
| `if` | 321 | 61 |
| `switch` | 42 | 20 |
| `do` | 23 | 9 |
| `while` | 3 | 2 |
| `for` | 1 | 1 |
| `break` | 177 | 19 |
| `continue` | 1 | 1 |
| `goto` | 46 | 18 |
| `return` | 164 | 74 |
| `empty` | 6 | 6 |

父子关系不是只做总数快照，而是逐节点验证：

- `block children=N` 必须恰好拥有 `stmt:0..N-1`；
- `if cond=...;else=0/1` 必须恰好拥有一个 `then`，并按 flag 决定是否有 `else`；
- `for/while/do` 必须恰好拥有一个 `body`；
- `switch selector=...;cases=N` 必须恰好拥有 `case:0..N-1`，每个 case relation 同时固定
  value 集或 `default`；
- leaf op 不允许拥有 statement child；label 在 owner 内唯一，`goto target=N` 必须命中
  同 owner label。

relation 完整 catalog 有 119 种，detail 完整 catalog 有 122 种；可聚合的表达式根字段有
47 种。后者覆盖 condition、expression、initializer、step、selector 和 assignment/call
root，而 `children/else/cases/target` 由上述结构断言单独验证。

## concrete 与 synthetic 的边界

40 个 synthetic 节点精确分为：

| synthetic op | 数量 |
| --- | ---: |
| `block` | 18 |
| `break` | 6 |
| `expr` | 1 |
| `goto` | 7 |
| `return` | 8 |

它们的 raw EA 是 `BADADDR` 或 Hex-Rays 内部 marker，例如
`GetListAt@0x5999F4` 的 `0xF1C0000000000004`。STS1 保留这些原值，verifier 明确禁止把
它们解释为 owner FDE 内的对齐指令地址；其 realization anchor 必须等于直接 parent 的
anchor。2,882 个 concrete 节点则反向要求 `raw_ea == anchor` 且位于 owner FDE 内。

全部节点最终折叠为 1,942 个机器 anchor：1,124 个只承载一个 statement，818 个承载
多个 statement，单 anchor 最大 5 行。共享 anchor 是 O3 控制折叠或 synthetic parent
继承的结果，不被伪装成额外指令。

## fresh 反编译与本地逐项对照

本轮重新反编译 `0x59641C`、`0x597B1C`、`0x5999F4`、`0x59A4B0`。代表性控制流可压缩为：

```text
FindNameIndex: decode two packed arrays -> initialize parent/state -> for(;;) validate/advance/return
DecodeName: follow parent links while nonzero -> grow byte vector -> reverse -> assign string
GetListAt: EnsureContainer/Resolve gates -> category switch -> packed-count or dictionary iteration -> cleanup
Resolve: obtain root -> require slash -> segment loop -> dictionary contains/strict lookup -> delayed out commit
```

| Android ARM64 高层树 | 当前源码对照 |
| --- | --- |
| `FindNameIndex@0x59641C`：全审计唯一 `cit_for`，循环内按 parent/state 失败或 terminal 成功返回 | [`PSBRawFile.cpp:52`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的两个 packed view、state 初始化与 `for(;;)` 分支逐项对应 |
| `DecodeName@0x597B1C`：全审计唯一 `cit_continue`，并出现两层 `while`；其中内层控制节点来自 vector growth/reverse 的内联实现 | [`PSBRawFile.cpp:112`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留源码层 parent walk、`push_back`、`reverse`、`assign` 数据流；不能把内联 STL 控制块反向制造成手写源码 |
| `GetListAt@0x5999F4`：深度 15，三层 `switch`、三只 `do` 与八个 `goto` 包含 classifier、packed 解码、vector/string 与 cleanup 展开 | [`PSBMedia.cpp:149`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留 category/packed 两层源码 switch、array/dictionary 两条循环和相同调用/清理次序；O3 后的 `do/goto` 拼写不唯一 |
| `Resolve@0x59A4B0`：四只 `do`、29 个 `if`，局部字符串与 raw-node 析构驱动 loop-state | [`PSBMedia.cpp:52`](../../cpp/plugins/psbfile/PSBMedia.cpp) 的无限 segment loop、嵌套 scope、early return 与成功尾部 delayed commit 产生同一数据流和逆序生命周期 |

因此，本轮 statement 树证明的是当前源码与 Android 机器产物之间的控制结构、数据流与
对象 cleanup 相容性；它不会把优化后二进制中内联 STL/RAII 产生的 `do/while/goto`
字面数量错误宣称为唯一原始 C++ spelling。

## STS1 门禁

`verify_elf_surface.py` 新增 STS1 compact manifest：

- raw payload 132,667 bytes，SHA-256
  `cac88aa5586595835739a22e07dd714c0c9bca373dde44c9e9cb3280f4af2e06`；
- canonical semantic sequence 273,424 bytes，SHA-256
  `336869e9ac1baaaea6b084b946348107e59b0949fc951beedf2b9b4b2bbe6f13`；
- row 固定 owner、ordinal、parent ordinal、op/relation/detail catalog id、depth、
  concrete flag、raw EA、anchor、label 与 exact instruction word；
- verifier 检查 preorder parent、depth、完整 block/if/loop/switch child shape、同 owner
  label/goto、synthetic parent anchor、concrete FDE 范围、exact word 与 entry-rooted
  normal CFG reachability。

新增固定输出：

```text
statement_ctree_surface=true owners=114 rows=2922 ops=12 blocks=759 expr=1379 if=321 switches=42 loops=27 breaks=177 continue=1 gotos=46 returns=164 concrete=2882 synthetic=40 ea_sites=1942 anchors=1942 normal=1942 landing=0 single=1124 shared=818 max=5 relation_catalog=119 relations=6 detail_catalog=122 detail_fields=47 labels=36 max_depth=15 semantic_bytes=273424 semantic_sha256=true instruction_words=true paths_complete=true
```

完整 ELF 门禁继续通过，114-entry 判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本轮新增的是 statement/control-tree 的
全量正证据，不改变 stripped/O3 无法唯一恢复原始 block spelling 与 helper factorization
的证据上限。
