# Follow-up：完整 Hex-Rays cexpr / cot_* 表达式树闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 114 个 MANIFEST FDE 的完整反编译树共恢复 **9,629 个 `cexpr/cot_*` 节点**，
  覆盖 108 个 owner、42 种表达式 op、17 种 child relation、271 种恢复类型与
  360 种 operator detail。
- 其余 6 个 owner 是四只 dispatch nullsub 与两只 media normalize nullsub：
  `0x596F38/0x596F3C/0x597A28/0x597A2C/0x5998BC/0x5998C0`；它们的空表达式树与
  既有 statement 面一致，不是漏反编译。
- 7,077 个 concrete 节点与 2,552 个 optimizer-synthetic 节点全部落到 3,076 个
  normal-entry exact-word anchor；landing-only 为 0。synthetic 节点逐个继承直接
  parent 或 owning statement 的真实锚点。
- 1,935 个 statement-root expression 与既有 2,922-row STS1 逐 statement 交叉为零
  差异；1,181 个 `cot_num` 与 NMC1 逐行零差异；3,073 个 `cot_var` 与纠正后的
  LVS1 逐行零差异。
- fresh Android ARM64 代表函数反编译和本地逐项对照没有发现新的生产 `cpp/` GAP；
  本轮不修改 `cpp/`、测试或 fixture，也不构建生产目标。

## 为什么这是独立证据面

既有表面都只是完整表达式树的投影：

1. STS1 固定 `cit_*` statement/control tree，但只记录每个 statement 的根表达式 op，
   不记录根以下的运算、调用参数与类型；
2. NMC1 只记录 `cot_num`；
3. LVS1 只记录 lvar 声明和 `cot_var`；
4. raw-memory surface 只记录 `cot_ptr/cot_idx`；
5. typed-member surface 只记录带 stroff 的 member expression。

ECT1 直接以 preorder 固定每个表达式节点的 owner-local ordinal、直接父表达式、
owning statement、child relation、类型、detail、`exflags`、raw EA、realization anchor
和 exact ARM64 word，因此能够检测运算树重排、call arity/参数位置漂移、cast/member/
pointer 层丢失以及 statement 归属变化。

## 全量 census

| 项 | 数量 |
| --- | ---: |
| MANIFEST 函数 | 114 |
| 有表达式 owner / 零表达式 owner | 108 / 6 |
| statement / expression root | 2,922 / 1,935 |
| expression row | 9,629 |
| op / relation | 42 / 17 |
| type / detail | 271 / 360 |
| concrete / synthetic | 7,077 / 2,552 |
| concrete raw site | 3,058 |
| realization anchor | 3,076 |
| single / shared anchor | 674 / 2,402 |
| 单 anchor 最大 row | 20 |
| 最大表达式深度 | 11 |

42 种 op 的完整数量为：

```text
add=356 asg=1123 asgadd=5 band=81 bnot=2 bor=14 call=405
cast=689 comma=2 empty=3 eq=32 fnum=4 helper=111 idx=206
land=25 lnot=79 lor=11 memptr=427 memref=198 mul=57 ne=35
num=1181 obj=457 postdec=6 postinc=3 predec=1 preinc=21
ptr=461 ref=252 sge=17 sgt=5 shl=13 sle=4 slt=6 sshr=6
sub=181 uge=7 ugt=30 ult=6 ushr=32 var=3073 xor=2
```

direct-child 门禁按 Hex-Rays 实际形状固定：

- unary op 必须恰有 `x`；
- binary op 必须依序恰有 `x,y`；
- `call` 必须依序为 `callee,arg:0..arg:N-1`，并与 `args=N` detail 一致；
- `empty/fnum/helper/num/obj/var` 必须为 leaf；
- `memptr/memref/ptr/ref/num/fnum/helper/obj/var` 的结构化 detail 必须满足各自格式。

root relation 精确分为：

```text
cond=348 expr=1379 init=1 return=164 selector=42 step=1
```

其中唯一 `for` 同时拥有 init/step/cond 三个根；verifier 按 relation 与 statement
detail 对齐，不把 visitor 的 preorder 顺序误当成源码字段顺序。

## 机器锚点

每个 ECT1 row 都保存 raw decompiler EA 和 nearest concrete realization anchor：

- concrete row 必须满足 `raw_ea == anchor`；
- synthetic child 必须继承 direct expression parent 的 anchor；
- synthetic root 必须继承 owning statement 的 anchor；
- anchor 必须 4-byte 对齐、位于 owner FDE、逐字匹配权威 ELF，并从 normal entry 可达。

3,076/3,076 个唯一 anchor 全部通过；没有 landing-only、越界、错字或不可达锚点。
唯一非 `BADADDR` synthetic raw marker 位于 `GetListAt@0x5999F4`，它仍沿 ancestor
落到真实 normal-flow `BR`，没有被伪装成 ELF VMA。

## 与既有表面的独立交叉及 LVS1 纠正

ECT1 不是孤立的新摘要，verifier 会重新解码 STS1、NMC1 与 LVS1：

- statement：每个 `expr/if/while/do/for/switch/return` 的 detail 字段必须与同
  statement 的 ECT1 root relation/op 集合完全相同，1,935/1,935 通过；
- numeric：每个 `cot_num` 的 owner-local 次序、值、类型、直接 parent、
  concrete/synthetic、EA、anchor 与 word 必须与 NMC1 相同，1,181/1,181 通过；
- lvar：每个 `cot_var` 的 lvar index、直接 parent、child relation、
  concrete/synthetic、EA、anchor 与 word 必须与 LVS1 相同，3,073/3,073 通过。

这次交叉检查发现旧 LVS1 的两个 relation 标签确实错误：

1. `AutoRegister::Unregist@0x59A968` 的 use ordinal 17 位于
   `0x59A9CC` 五参 indirect call 的 `arg:4`；同一 lvar 也作为 synthetic `arg:0`
   出现，旧生成器按 `equal_effect` 找到第一只等价节点，误记为 `a0`。
2. `RegistEnd@0x59AD84` 的 use ordinal 30 位于 `0x59AE6C` 六参
   `PropSet` call 的 `arg:5`；同一 global dispatch lvar 同时是 `arg:0`，旧逻辑
   同样误记为 `a0`。

fresh 反编译分别显示五参与六参调用的首尾实参确实复用同一对象。LVS1 已改为按
**具体 child occurrence** 记录 relation，故计数从
`a0=178/a4=4/a5=3` 纠正为 `a0=176/a4=5/a5=4`；其余 3,071 行不变。
这是对已被新证据证伪的审计资料的就地纠正，不是生产源码差异。

## fresh 反编译与本地逐项对照

本轮重新反编译 `0x599888`、`0x59A0B4`、`0x596F50`、`0x59673C`、
`0x59A968`、`0x59AD84`。代表性行为可压缩为：

```text
Release: ref==1 时经 deleting-destructor vslot 删除，否则只递减。
GetResourceData: Resolve；成功后按 raw resource helper 解码并返回 borrowed data。
EnumMembers: 构造四只 Variant；按 Array/Dictionary 分支枚举；2/3 参数 callback；逆序清理。
CreateVariant: classifier 后按 scalar/string/resource/container 分支写 result；保留默认/诊断边。
Unregist: 取 class/global dispatch；五参 DeleteMember，首参和 arg4 复用 global dispatch。
RegistEnd: 可选注册 dummy ctor；构造 Variant；六参 PropSet，首参和 arg5 复用 global dispatch。
```

| Android ARM64 结构 | 当前源码对照 |
| --- | --- |
| `PSBMedia::Release@0x599888` 的唯一分支与 refcount 边界 | [`PSBMedia.h:18-22`](../../cpp/plugins/psbfile/PSBMedia.h) 精确保留 delete/decrement 分支 |
| `GetResourceData@0x59A0B4` 的 Resolve 后完整 inline resource helper | [`PSBMedia.cpp:112-124`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留 source-level `GetResource` 调用边界 |
| `EnumMembers@0x596F50` 的四 Variant、两类容器、2/3 参数与 callback | [`main.cpp:371-442`](../../cpp/plugins/psbfile/main.cpp) 保留同一对象顺序、分支和回调 |
| `CreateVariant@0x59673C` 的 classifier/scalar/string/resource/container 树 | [`main.cpp:557-687`](../../cpp/plugins/psbfile/main.cpp) 保留类别、默认值和 owner-sharing dispatch 生命周期 |

未发现缺失 op、不同 call arity、参数位置漂移、少一层 cast/member/pointer、错误
statement 归属或不同对象生命周期。大型树
`EnumMembers@0x596F50`（802 row）、`CreateVariant@0x59673C`（580 row）、
`GetListAt@0x5999F4`（372 row）及最深
`GetResourceData@0x59A0B4` 表达式层次均能由当前普通 C++ 加 Android ARM64 O3/inlining
解释；没有依据把编译器展开形状反写成手工 ARM64 layout。

## ECT1 与门禁

`verify_elf_surface.py` 新增 ECT1 compact manifest：

- raw payload 451,263 bytes，SHA-256
  `b0defc8a30753b8dd1596a64ef01e0da420c7f416e4127ac16f4ee007f674459`；
- canonical semantic sequence 1,025,218 bytes，SHA-256
  `3a9735ce4aa33d6854ec1945347ef9a4054ff96228753a10a9a384bea9b51f4d`；
- 两次独立 IDA 全量导出得到相同 raw/semantic digest；
- reader 会从每一行重建 semantic sequence 后再比较 stored/recomputed digest；
- verifier 会检查完整拓扑、detail、计数、三份既有 manifest 的逐行交叉和 ELF CFG。

固定输出：

```text
expression_ctree_surface=true functions=114 owners=108 zero_owners=6 statements=2922 rows=9629 roots=1935 ops=42 relations=17 types=271 details=360 concrete=7077 synthetic=2552 raw_sites=3058 anchors=3076 normal=3076 landing=0 single=674 shared=2402 max=20 max_depth=11 root_relations=6 synthetic_ops=18 statement_root_cross=1935 numeric_cross=1181 lvar_cross=3073 semantic_bytes=1025218 semantic_sha256=true instruction_words=true paths_complete=true cross_surfaces=true
```

完整 ELF verifier 通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。ECT1 固定的是当前 IDB 从权威
Android ARM64 恢复出的表达式结构、类型传播与机器实现映射；它不声称 stripped/O3
能够唯一恢复原始局部名、精确 source factorization 或被优化消除的源码 token。
