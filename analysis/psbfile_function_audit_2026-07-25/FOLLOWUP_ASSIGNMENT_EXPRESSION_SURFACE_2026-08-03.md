# Follow-up：完整 Hex-Rays `cot_asg` 赋值表达式面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 完整 ECT1 中共有 **1,123 个 `cot_asg`**，覆盖 69 个 owner、61 种结果类型与
  1,052 个 normal-entry exact-word anchor。
- 1,122 个 assignment 是 concrete，唯一 synthetic assignment 位于
  `GetListAt@0x5999F4`；全部 assignment 都有严格有序的 `x=lvalue / y=rvalue` 两个
  child，没有 landing-only、缺 child 或倒置边。
- 直接左值形成五类互斥分区：`804 local lvar + 158 typed member + 109 raw memory +
  32 global object + 20 helper pseudo-lvalue = 1123`。
- 五类分别与 LVS1、typed-member write/synthetic、RMC2、global-BSS DataRef 和 residual
  helper leaf 独立交叉。交叉同时保留 2 个嵌套 raw write、6 个 ECT-only synthetic
  member realization、2 个 STP 第二 lane source subobject 与 4 个 landing-only global
  cleanup write，不把不同层面的数量差异误报为 GAP。
- fresh Android ARM64 反编译确认静态注册初始化、packed name vector、GetInt tag switch、
  media container/resource 与 NCB unregister 的赋值/清理结构都能由当前源码解释；没有
  生产 `cpp/` GAP。本轮不改 `cpp/`、测试或 fixture，也不构建生产目标。

## 全量 census

| 项 | 数量 |
| --- | ---: |
| owner | 69 |
| assignment row | 1,123 |
| root / nested | 1,119 / 4 |
| concrete / synthetic | 1,122 / 1 |
| result / lhs / rhs type | 61 / 61 / 69 |
| lhs / rhs op | 7 / 25 |
| lhs-rhs op pair | 64 |
| raw assignment EA | 1,051 |
| normal anchor | 1,052 |
| single / shared anchor | 984 / 68 |
| 每 anchor 最大 assignment | 3 |
| 最大 assignment 深度 | 3 |

anchor cardinality 精确为：

```text
1 row=984 anchors
2 rows=65 anchors
3 rows=3 anchors
```

assignment 自身的 parent/relation 拓扑为：

```text
parent: expr=1118 for=1 comma=2 eq=1 ne=1
relation: expr=1118 init=1 x=4
```

其中 `for/init` 是 `FindNameIndex@0x59641C` 唯一 for 的初始化赋值。四只 nested
assignment 是：

```text
DecodeName@0x597B1C: comma.x 中的 width-mask 赋值；eq.x 中的 packed value 赋值
GetResourceData@0x59A0B4: comma.x 中的 owner->header 赋值；ne.x 中的 header->chunkData 赋值
```

这四只 assignment 的结果直接被 enclosing comparison/comma 消费，不能扁平化成四条
独立 statement 而仍声称完整复刻目标恢复出的表达式数据流。

## 五类左值分区

| destination | row | 直接 lhs op |
| --- | ---: | --- |
| local lvar | 804 | `var` |
| typed member | 158 | `79 memptr + 79 memref` |
| raw memory | 109 | `78 ptr + 31 idx` |
| global object | 32 | `obj` |
| helper pseudo-lvalue | 20 | `call` |
| **合计** | **1,123** | **七种 op** |

lhs realization 精确分为：

```text
var synthetic=804
memptr concrete=39 synthetic=40
memref concrete=66 synthetic=13
ptr concrete=34 synthetic=44
idx concrete=16 synthetic=15
obj synthetic=32
call synthetic=20
```

assignment root 通常有真实 EA，而 lvar/global/helper 左值自身没有独立 citem EA；这正是
source operator 与机器 realization 的不同层次，不能给 synthetic lhs 发明新地址。

## local lvar：804/804

ECT1 中 804 个直接 `asg.x=var` 按 owner 内 visitor 次序逐项映回 LVS1；全部且仅有：

```text
mode=write parent=asg relation=x
```

它们恰等于 LVS1 的全部 804 个 write use。LVS1 中另有
`2166 read / 20 read-write / 83 address`，均不会被本面提升成普通赋值左值。

这项交叉固定的是局部对象/标量的 assignment edge 和 nearest definition anchor；ARM64
寄存器号、栈槽与 Hex-Rays lvar 名仍不是 portable C++ layout/token。

## typed member：105 concrete + 53 synthetic

158 个直接 member 左值分成：

- 105 个 concrete member assignment，与 typed-member machine write 面中
  `mode=W` 的 105 个 `(owner,site,base type,field type,offset)` 精确相等；另三只
  `mode=RW` 属于 vector/media refcount read-modify-write，不是 `cot_asg`；
- 53 个 synthetic member assignment 去重为 42 个 signature，与 typed-member
  synthetic 面的 42 个 `mode=W` semantic row 精确相等。

旧 synthetic typed projection 为这 42 个 signature 保留了 47 个 coalesced machine
occurrence；完整 ECT1 assignment 面有 53 个逐表达式 occurrence。多出的六个并非新字段
语义，而是已知 signature 的额外 realization：

| owner | anchor | member |
| --- | --- | --- |
| `PSBFile::Adopt@0x598708` | `0x598810` | owner refcount `+0` |
| `PSBFile::LoadStorage@0x598A64` | `0x598A98` | file owner `+0` |
| `PSBMedia::Resolve@0x59A4B0` | `0x59A6D8` | owner refcount `+0` |
| `PSBMedia::Resolve@0x59A4B0` | `0x59A740` | owner refcount `+0` |
| `PSBMedia::Resolve@0x59A4B0` | `0x59A798` | owner refcount `+0` |
| NCB tail cleanup `@0x59B7E8` | `0x59B8B4` | COW string data `+0` |

新门禁要求“旧 47 occurrence 是新 53 occurrence 的真子集，差集恰为上述六项”；既不把
旧 projection 误称 row-complete，也不重复制造六个新 semantic field。

## raw memory：109 直接 + 2 嵌套

109 个直接 `asg.x=ptr/idx` 与 RMC2 的直接 write 逐项相等：

```text
ptr=78 idx=31 mode=W parent=asg mode_parent=asg
```

RMC2 另有两只 `mode=W` 的 nested `idx`：

```text
PSBMedia registration @0x59A330: idx@0x59A404 under ptr/cast/add lhs
NCB registerMembers     @0x59B14C: idx@0x59B1F0 under ptr/cast/add lhs
```

两者是 109 个直接 `ptr` 左值内部的 address calculation，不是第 110/111 条 assignment。
因此 `cot_asg=109 direct raw destination` 与 `RMC2=111 raw W node` 同时正确。

## global object：32 source assignment 与 34 machine write

32 个 `asg.x=obj` 全部位于既有九只 `.bss` 语义对象范围。与 34 条 global-BSS machine
write 交叉后形成：

- 30 个 `(target,owner,site)` 直接相等；
- source-only 两项是 `psbfile_static_init@0x42CF28` 的两只 STP 第二 lane：
  `0x1AB50A8@0x42CF74` 与 `0x1AB50C8@0x42CF78`。DataRefsFrom 只把 STP base lane
  记为 target，ECT1 则正确恢复两只相邻源码字段；
- machine-only 四项是 `Unregist@0x59A968` 的 landing cleanup
  `0x59AA68/6C/70/74`，异常路径再次 Clear class-info。normal ECT 不应制造第二组正常
  source statement。

fresh `0x42CF28` 明确显示两只 AutoRegister 记录、callback/member pointer 与三路链头的
有序赋值；fresh `0x59A968` 的 normal tree 只显示一组 class-info Clear。既有 landing
contract/global state-machine 面独立固定异常 Clear，两个表面的边界相互吻合。

## 20 个 helper pseudo-lvalue

20 个 `asg.x=call` 精确使用 residual helper leaf 的全部：

```text
LOBYTE=5
LODWORD=15
```

它们的 call 都是 `args=1`，不是机器 `BL/BLR`。代表例：

- `DecodeName@0x597B1C` 的 `LOBYTE(v32)=0` 是 W-register 低 byte 的 Hex-Rays 表达；
- `GetInt@0x599438` 的 13 个 `LODWORD(self)=...` 把 W0 result 与进入函数时的 X0 lvar
  合并显示；源语义仍是按 tag 返回 `tjs_int`，绝不是修改 `const PSBRawNode *self`；
- `EnsureContainer@0x599E04` 的两只 `LODWORD(v21.manager)` 是复用临时聚合低 word 的
  rendering，当前普通 `tTJSVariant nextFile` 生命周期才是 source-level 对照。

因此本面固定 helper 名、参数树、赋值 RHS 与 exact anchor，但明确禁止把
`LOBYTE/LODWORD` 抄成生产 C++ API 或据此修改真实参数对象。

## RHS 根节点

1,123 个 RHS immediate root 精确分为：

```text
num=230 ptr=167 var=147 memptr=95 ref=93 cast=92 sub=67 call=52 band=50
idx=36 obj=25 memref=18 add=16 ushr=12 bor=5 postdec=5 fnum=3 eq=2 xor=2
land=1 ne=1 sgt=1 sshr=1 uge=1 ugt=1
```

这 25 类与七类 lhs 形成 64 种 pair。assignment semantic hash 固定 lhs/rhs 的直接
metadata；完整 ECT1 已继续固定 RHS 下方的所有后代，所以本面不会只验证根 op 而放过
子树漂移。

## fresh 反编译与本地逐行对照

本轮 fresh decompile 覆盖 `0x42CF28`、`0x597B1C`、`0x599438`、`0x599E04`、
`0x59A0B4`、`0x59A968`。关键行为压缩为六行：

```text
static init: 构造 class/pre-register 两记录，写 vptr/name/callback，并按既有 top 链发布。
DecodeName: 解三只 packed array，沿 parent 收集 byte vector，反转后 assign 到输出 string。
GetInt: 按 tag 返回 bool/narrow/wide/float/double 的 signed 32-bit 结果；未知 tag diagnostic 后为 0。
EnsureContainer: cache miss 时 new/load/adaptor，更新 file/container；adaptor null 仍保持成功边界。
GetResourceData: Resolve 后经 owner.header 的 chunk tables 解 offset/length，最后释放局部 raw owner。
Unregist: 构造 unregister delegate、DeleteMember、Release dispatch，normal/异常尾均 Clear class-info。
```

| Android ARM64 赋值/数据流 | 当前源码对照 |
| --- | --- |
| static AutoRegister 两记录与 `PSBFile.dll/PSBFile` token | [`main.cpp:751-757`](../../cpp/plugins/psbfile/main.cpp)、[`ncbind.hpp:2096-2157`](../../cpp/core/plugin/ncbind.hpp) 保留 class/pre callback 注册与链表构造 |
| DecodeName 的 packed locals、vector push/reverse/string assign | [`PSBRawFile.cpp:112-135`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留相同容器与赋值顺序 |
| GetInt 的 tag 分支、默认值与 signed 32-bit 边界 | [`PSBRawFile.cpp:316-358`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 以正常 return/cast 表达，不复制 `LODWORD(self)` |
| EnsureContainer 的 cached compare、file/adaptor 临时与最终提交 | [`PSBMedia.cpp:19-49`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留相同失败/成功、null adaptor 与对象生命周期 |
| GetResourceData 的 Resolve→raw GetResource source boundary | [`PSBMedia.cpp:112-125`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留成员调用；目标 O3 inline 后的嵌套 assignment 由 callee body解释 |
| class-info Set/Clear 与 initialized-last | [`ncbind.hpp:74-114`](../../cpp/core/plugin/ncbind.hpp) 保留 name/id/object/flag 次序，异常 cleanup 由 RAII/landing 生成 |

六个代表函数的赋值顺序、条件消费、对象提交与 cleanup 边均已有直接 Android 证据；
当前源码逐项可解释，没有依据加入 ARM64 register-part helper、landing-only 正常语句或
目标 ABI padding。

## 机械门禁

assignment canonical semantic sequence 为 293,837 bytes，SHA-256：

```text
b69793a50cc6acbe85410ab2563bcc3e4bea4d7167ee9df4778f4dd016c260dc
```

每一行固定：

```text
assignment owner/ordinal/statement/parent/parent-op/relation/type/detail/depth/
exflags/concrete/raw-EA/anchor/exact-word/destination-family +
lhs ordinal/op/type/detail/exflags/concrete/raw-EA/anchor/exact-word +
rhs ordinal/op/type/detail/exflags/concrete/raw-EA/anchor/exact-word
```

`verify_elf_surface.py` 固定输出：

```text
assignment_expression_surface=true owners=69 rows=1123 roots=1119 nested=4 result_types=61 lhs_types=61 rhs_types=69 lhs_ops=7 rhs_ops=25 pairs=64 relations=3 parents=5 destinations=5 concrete=1122 synthetic=1 raw_sites=1051 anchors=1052 normal=1052 landing=0 single=984 shared=68 max=3 max_depth=3 lvar=804 member_concrete=105 member_synthetic=53 member_signatures=42 member_typed_occurrences=47 member_ect_only=6 raw_direct=109 raw_nested=2 global=32 global_machine_cross=30 global_source_subobjects=2 global_machine_only=4 helper_lvalues=20 helper_names=2 semantic_bytes=293837 semantic_sha256=true lvar_cross=true member_cross=true raw_cross=true global_cross=true helper_cross=true paths_complete=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面固定的是全部 assignment operator、
有序 lhs/rhs edge 与五类独立写入投影，不把 synthetic lvalue、机器 landing store 或
Hex-Rays register-part pseudo-call 反向制造成原始 C++ statement/API。
