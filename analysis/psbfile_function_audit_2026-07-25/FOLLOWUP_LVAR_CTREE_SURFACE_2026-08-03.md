# Follow-up：完整 Hex-Rays lvar / cot_var 局部变量面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 114 个 MANIFEST FDE 共恢复 **1,056 个 Hex-Rays lvar 声明**；其中 112 个 owner
  有声明，`ncbClassInfo<PSBFile>::Clear@0x597F08` 与动态零初始化器 `0x42CEF8`
  没有 lvar。
- 完整 ctree 中共有 **3,073 个 `cot_var` 使用**，覆盖 77 个 owner 和 770 个声明；
  另外 286 个声明没有最终 `cot_var`，主要是未消费 ABI 参数、result pseudo-variable
  或被 O3 消除的中间量，不能据此在 C++ 中制造具名局部对象。
- 声明层精确分为 `311 arg / 111 stack / 945 register / 72 result / 58 byref`；另有
  1 个 fake lvar、3 个 floating lvar、97 种类型和 8 种宽度。
- 使用层精确分为 `R=2,166 / W=804 / RW=20 / address=83`；1,686 条有具体 EA，
  1,387 条是 optimizer-synthetic。全部使用最终绑定到 2,214 个 normal-entry
  exact-word anchor，landing-only 为 0。
- 后续完整 ECT1 表达式树交叉检查纠正了旧生成器的两个重复实参 relation：
  `0x59A968:17 a0→a4`、`0x59AD84:30 a0→a5`；其余 3,071 个使用行不变。
- fresh Android ARM64 反编译与本地声明顺序、对象作用域、参数/结果槽和 RAII 层次逐项
  对照后，没有发现新的生产 `cpp/` GAP；本轮不修改 `cpp/`、测试或 fixture。

## 为什么这是独立证据面

既有三个表面各自只覆盖局部变量问题的一部分：

1. stack-frame surface 固定 frame allocation、callee-save、canary 和 shrink-wrap，不能
   说明 frame 内是什么 C++ 对象；
2. stack-local DataRef surface 只覆盖 IDA 产生 stroff identity 的 7 个 typed local、
   20 行引用；
3. LSDA/landing surface 固定发生异常时实际执行的 cleanup，却不包含所有普通标量、
   ABI 参数、hidden result 与未进入 landing 的局部声明。

本轮直接枚举每个 `cfunc.lvars`，再用完整 `cot_var` visitor 记录每次使用的直接 parent、
child relation、读写/取址模式与 nearest emitted ancestor。它补的是“反编译器恢复出的声明
集合 + 使用拓扑”，不是重复记录 frame byte 数。

## 声明层 census

| 项 | 数量 |
| --- | ---: |
| lvar 声明 | 1,056 |
| 有声明 owner | 112 |
| argument | 311 |
| stack | 111 |
| register | 945 |
| result pseudo-variable | 72 |
| by-reference | 58 |
| fake / floating | 1 / 3 |
| 类型 / 宽度种类 | 97 / 8 |
| 空名 / `vN` generic / 语义名 | 137 / 451 / 468 |

宽度分布为：

| bytes | 声明数 |
| ---: | ---: |
| 1 | 42 |
| 2 | 2 |
| 4 | 285 |
| 8 | 686 |
| 16 | 28 |
| 20 | 3 |
| 24 | 8 |
| 32 | 2 |

111 个 stack lvar 中，58 个被取址。可直接识别的聚合/局部对象包括：

- `PSBRawNode` 5 个；
- `ttstr_psb_arm64` 3 个、`std_string_cow_arm64` 2 个；
- `tTJSVariant_psb_arm64` 2 个，以及 Hex-Rays 尚以 `_BYTE/_DWORD/_QWORD` 数组呈现的
  Variant/参数槽聚合；
- `PSBOwnerFilter_arm64` 2 个、`PSB_paramsFunctor_arm64` 3 个；
- NCB class/native registration state 3 个；
- old-libstdc++ vector/string scratch 与 hidden-sret 指针/三指针状态。

这些数字是 Android NDK/O3 + 当前 IDB 类型传播的证据快照。特别是 register/stack
location、stack offset、数组宽度不能反向成为 wasm32 的 `offsetof/sizeof` 契约；实际源码
只复刻对象种类、声明顺序、作用域和析构层次。

1,055 个普通声明行折叠为 781 个唯一 definition anchor，全部 normal-entry reachable。
唯一特殊项是 `PSBRawNode::GetInt@0x599438` 的 unnamed result lvar：Hex-Rays 使用
`defea=-4` 表示返回 pseudo-location，它没有可对应的机器定义地址，因此在 LVS1 中显式
保留为唯一 `special_definition`，而不是伪造 entry anchor。

## 使用层 census

| 项 | 数量 |
| --- | ---: |
| `cot_var` 行 | 3,073 |
| 有具体 EA / synthetic | 1,686 / 1,387 |
| 具体 EA site | 1,419 |
| realization anchor | 2,214 |
| single / shared anchor | 1,574 / 640 |
| 单 anchor 最大行数 | 6 |
| direct parent 种类 | 35 |
| child relation 种类 | 11 |
| realization mnemonic 种类 | 65 |

synthetic 行的 nearest ancestor 选择只接受“同 owner FDE 内、4-byte 对齐”的真实 EA。
例如 `GetListAt@0x5999F4` 有一条 synthetic `data` assignment 的最内层 citem 携带
Hex-Rays 非地址标记 `0xF1C0000000000004`；本轮跳过该标记，沿 ancestor 回到真实
switch `BR@0x599B38`。这避免把 decompiler sentinel 当作 ELF VMA。

机械读写分类只看直接 ctree 关系，不从变量名猜语义：

- `asg.x` 为 write；
- compound assignment 与 pre/post inc/dec 的 `.x` 为 read-write；
- `ref.x` 为 address；
- 其余为 read。

由此得到 `R=2166 / W=804 / RW=20 / address=83`。35 种 parent 覆盖 assignment、
member/index/pointer、call/cast、predicate、loop、return 和算术；call child 现按
**具体 child occurrence** 精确区分 `a0..a7`，不能按 `equal_effect` 找第一只等价
表达式。这能检测 lvar merge/split、参数槽变化与对象使用顺序漂移，但不会把 IDA 的
`vN` 名字宣称为二进制原始标识符。

## ECT1 交叉发现并纠正的旧 relation

完整表达式树对 3,073 个 `cot_var` 逐行反查时发现两个真实旧标注错误：

1. `AutoRegister::Unregist@0x59A968` 的 use ordinal 17 是
   `0x59A9CC` 五参 indirect call 的 `arg:4`；同一 lvar 还作为 synthetic
   `arg:0` 出现，旧 relation detector 误取了第一只等价节点。
2. `RegistEnd@0x59AD84` 的 use ordinal 30 是 `0x59AE6C` 六参
   `PropSet` 的 `arg:5`；同一 global dispatch lvar 同时也是 `arg:0`。

fresh 反编译确认两处调用都复用了首尾实参。LVS1 relation 计数因此从
`a0=178/a4=4/a5=3` 纠正为 `a0=176/a4=5/a5=4`；这只是审计资料纠错，
不是生产源码 GAP。reader 也已从“只信 payload 尾部 digest”加强为逐声明/使用行重建
canonical semantic sequence，再同时核对 stored 与 recomputed SHA-256。

## fresh 反编译与本地逐项对照

本轮重新反编译 `0x596F50`、`0x598268`、`0x598E64`、`0x599E04`、`0x59A4B0`、
`0x59B570`。代表性局部对象结构可压缩为：

```text
EnumMembers: construct 4 Variant + params[3] -> branch Array/Dictionary -> reverse cleanup
Load: by-value Variant -> String(ttstr,filter) or Octet(raw data,filter) -> return
GetDictionaryKeys: construct hidden-sret vector -> Dictionary gate -> one reusable COW key
EnsureContainer: requested ttstr -> raw PSBFile -> adaptor -> temporary Variant -> publish cache
Resolve: current raw node + remaining ttstr -> per-segment holder + next raw node -> delayed out commit
load wrapper: paramsFunctor -> argument Variant -> native call -> result Variant -> reverse cleanup
```

| Android ARM64 局部结构 | 当前源码对照 |
| --- | --- |
| `EnumMembers@0x596F50`：四只 Variant、`params[3]`、Dictionary COW key 跨循环，Array key 临时量逐项释放 | [`main.cpp:371-442`](../../cpp/plugins/psbfile/main.cpp) 保留同一声明顺序、两条分支与 callback 2/3 参数 |
| `PSBFile::Load@0x598268`：String 路径的 `ttstr + OwnerFilter`；Octet 路径 raw buffer + 空 filter 临时 | [`PSBRawFile.cpp:442-479`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留两条路径、raw delete 边界与 filter full-expression |
| `GetDictionaryKeys@0x598E64`：hidden-sret vector、Dictionary gate 后构造单一 COW key，reserve/emplace | [`PSBRawFile.cpp:280-306`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留 `result/key/keys/offsets` 和同一循环层次 |
| `EnsureContainer@0x599E04`：requested-container ttstr、OwnerFilter storage 后复用为 Variant、adaptor 发布后替换 cache | [`PSBMedia.cpp:19-49`](../../cpp/plugins/psbfile/PSBMedia.cpp) 用普通 C++ scope 保留 `container/file/object/nextFile` 发布次序 |
| `Resolve@0x59A4B0`：`current/next` 两只 raw node、remaining/segment ttstr、单一 narrow holder、成功尾部才写 out | [`PSBMedia.cpp:52-109`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留嵌套 segment scope、临时 copy/no-op refcount 与 delayed commit |
| load Method wrapper `0x59B570`：params functor、by-value argument Variant、可选 bool result Variant | [`ncbind.hpp:928-1010`](../../cpp/core/plugin/ncbind.hpp)、[`ncbind.hpp:1087-1194`](../../cpp/core/plugin/ncbind.hpp)、[`ncbind.hpp:1325-1350`](../../cpp/core/plugin/ncbind.hpp) 保留同一模板分层 |

没有发现额外 owning local、不同的声明/析构次序、缺失 hidden result、被错误合并的
params functor，或应从现有普通 C++ scope 改成手工 ARM64 layout 的证据。

## LVS1 门禁

`verify_elf_surface.py` 新增 LVS1 compact manifest：

- raw payload 153,440 bytes，SHA-256
  `b2849a0d760424d02d7a1df659cc9f14819f421426824fdfbed29d58e70abc79`；
- canonical semantic sequence 362,197 bytes，SHA-256
  `89a96b3a6c38f7e7d34ac058c34302c48383a1f578bf7a1cf39a2e59e89f51b2`；
- declaration row 固定 owner-local ordinal、类型、宽度、argument/result/byref/location
  flags、location identity、raw defea、definition anchor 与 exact word；
- use row 固定 owner-local occurrence、lvar index、parent、child relation、syntactic
  mode、concrete/synthetic、EA、nearest ancestor、mnemonic 与 exact word；
- verifier 检查声明/使用 ordinal、use→declaration 完整引用、全部 catalog/count、owner
  FDE、anchor 范围、exact word 与 entry-rooted normal CFG reachability。

新增固定输出：

```text
lvar_ctree_surface=true declaration_owners=112 use_owners=77 declarations=1056 uses=3073 used=770 unused=286 args=311 stack=111 reg=945 result=72 byref=58 types=97 widths=8 definition_anchors=781 special_definitions=1 ea_backed=1686 synthetic=1387 ea_sites=1419 use_anchors=2214 single=1574 shared=640 max=6 r=2166 w=804 rw=20 address=83 parents=35 relations=11 mnemonics=65 semantic_bytes=362197 semantic_sha256=true instruction_words=true paths_complete=true
```

完整 ELF 门禁继续通过，114-entry 判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本轮补强的是源码结构、局部对象和
参数/结果生命周期的正证据，不改变 stripped/O3 无法唯一恢复原始 identifier 与精确
factorization 的证据上限。
