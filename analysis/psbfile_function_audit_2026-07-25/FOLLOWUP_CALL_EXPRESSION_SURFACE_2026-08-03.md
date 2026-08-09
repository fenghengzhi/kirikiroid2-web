# Follow-up：完整 Hex-Rays cot_call 源码调用表达式面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 完整 ECT1 中共有 **405 个 `cot_call`**，覆盖 61 个 owner、638 个实参和
  405 个互不重复的 normal-entry exact-word anchor。
- callee 结构精确分为：
  - 285 个 `cot_obj` direct transfer：274 `BL` + 11 out-of-owner tail `B`；
  - 40 个 computed indirect transfer：39 `BLR` + 1 tail `BR`；
  - 80 个 Hex-Rays helper/intrinsic：60 个 concrete + 20 个 synthetic，
    全部由非 transfer 指令实现。
- 325 个真实 transfer call-expression 全部逐站点匹配既有机器 callsite census；
  40 个 computed call 又精确等于 46 个非 switch 间接 transfer 中的 40 个
  normal-entry 站点，余下 6 个严格为 landing-only。
- 机器侧排除 42 个 switch `BR` 后共有 525 个 transfer，比 ECT1 多 200 个：
  194 direct + 6 indirect。它们保持为 EH、canary、隐式析构/释放和 compiler lowering，
  没有被伪装成原始 C++ 显式调用。
- fresh Android ARM64 的 direct/indirect/helper/landing 代表对照没有发现新的生产
  `cpp/` GAP；本轮不修改 `cpp/`、测试或 fixture，也不构建生产目标。

## 源码调用树与机器 callsite 不是同一集合

既有机器 callsite surface 从 5,525 条 FDE 指令中机械枚举所有
`BL/B/BLR/BR`。它必须包含：

- C++ 显式调用；
- implicit destructor/release；
- 编译器降级出的 helper；
- stack canary failure；
- landing cleanup、catch/rethrow/terminate；
- switch dispatch。

`cot_call` 则是当前权威 IDB 的 Hex-Rays source tree 恢复出的 call-expression。
本轮把两者逐站点联结，而不把“机器上有一条 BL”直接反推成“原始源码写了一次函数调用”。

| 集合 | direct | indirect | 合计 |
| --- | ---: | ---: | ---: |
| ECT1 真实 transfer call-expression | 285 | 40 | 325 |
| 机器 transfer（排除 switch） | 479 | 46 | 525 |
| machine-only residual | 194 | 6 | 200 |

所有 325 个 ECT1 transfer 都是机器集合的严格子集，没有 source-only 站点。direct
callee 共 79 个目标；机器 direct 共 86 个目标，唯一 7 个 machine-only 目标全部是
runtime/EH 边界：

```text
__cxa_guard_abort@0x4013C0
__stack_chk_fail@0x406D70
__cxa_begin_catch@0x408BD0
__cxa_rethrow@0x4139B0
_Unwind_Resume@0x41A950
__cxa_end_catch@0x422530
clang_call_terminate_guess@0x520FAC
```

这 7 个目标承接 138 个 machine-only direct site；另 56 个 residual site 指向也被
正常 source-call 使用的释放/析构/runtime target，属于 implicit lifetime/lowering
复用，不产生新的源码 call target。

## direct、indirect 与 helper 全量结构

### Direct

285 个 direct call-expression 精确覆盖 79 个目标：

- 43 个 MANIFEST 内站点、21 个内部 target、39 条去重 edge；
- 242 个 MANIFEST 外站点、58 个外部 target；
- 39 条内部 edge 与既有完整机器内部 edge 集合相同。

机器内部站点为 44 而不是 43 的唯一原因是
`AutoRegister::Regist@0x59A8D8`：

- normal body 在 `0x59A924` 调一次 `RegistEnd@0x59AD84`；
- constructor/registration 抛出时，landing 在 `0x59A958` 再调用同一
  `RegistEnd` 完成 RAII `ncbRegistClass` 析构，然后 `_Unwind_Resume`；
- 两站点属于同一 `0x59A8D8→0x59AD84` edge，故 edge 仍是 39。

这与 [`ncbind.hpp:1665-1675`](../../cpp/core/plugin/ncbind.hpp) 的构造 `Begin()`、
析构 `End()`，以及
[`ncbind.hpp:2148-2157`](../../cpp/core/plugin/ncbind.hpp) 的局部
`RegistT r(d,true)` scope 一致。

### Computed indirect

40 个 normal-flow computed call 的 callee op 为：

```text
ptr=26 cast=5 memptr=5 var=3 idx=1
```

它们精确等于独立 indirect ABI surface 的 40 个 normal-entry 站点；6 个未进入
`cot_call` 的机器间接站点全部只在 LSDA landing 中可达。三处最大 8 实参 call 为：

- `EnumMembers@0x596F50` Array callback；
- 同函数 Dictionary callback；
- `CreateAdaptor@0x59A330` 的 TJS dispatch。

唯一 5 实参和四个 6 实参 call 也由 occurrence-aware child relation 固定，包含本轮
纠正 LVS1 的 `0x59A9CC arg:4` 与 `0x59AE6C arg:5`。

### Helper / intrinsic

80 个 helper callee 不对应 `BL/B/BLR/BR`：

```text
_ReadStatusReg=31 LODWORD=15 __ldaxr=12 __stlxr=12
LOBYTE=5 vdupq_n_s64=2 __CFADD__=2 atomic_load=1
```

- `_ReadStatusReg` 是 `MRS TPIDR_EL0` 的 Hex-Rays 展示；
- `__ldaxr/__stlxr/atomic_load` 是 AArch64 原子指令；
- `vdupq_n_s64` 是 SIMD zero construction；
- `__CFADD__` 是 flags/arithmetic 恢复；
- `LOBYTE/LODWORD` 是 synthetic low-lane 表示，尤其不能被写成真实 helper 调用。

`GetInt@0x599438` 的 15 个 `LODWORD` call-expression 全部落到真实
load/extend/convert/MOV 锚点；本地
[`PSBRawFile.cpp:318-356`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 与
[`PSBPackedInternal.h:103-124`](../../cpp/plugins/psbfile/PSBPackedInternal.h)
保留的是普通整数分支和 shared decoder，而不是虚构 `LODWORD()` 函数。

## 参数、返回与父表达式

405 个 call 的 arity 为：

```text
0=6 1=267 2=69 3=40 4=15 5=1 6=4 8=3
```

638 个 argument root op 为：

```text
add=14 cast=143 helper=31 idx=6 memptr=14 memref=6
mul=2 num=61 obj=29 ref=55 sub=9 var=268
```

call result 恢复为 14 种类型，callee 为 107 种函数/函数指针类型，argument 为 64 种
类型，`callee op + detail + type` 共 120 种 shape。每条 canonical semantic row
固定 owning statement、direct parent、call relation、result type/exflags、
callee ordinal/op/detail/type、完整有序 argument 子树、concrete/synthetic、EA、
anchor 与 exact word。

## fresh 反编译与本地逐项对照

本轮 fresh 反编译/反汇编 `0x59A8D8`、`0x59AD84`、`0x599438`、
`0x42CF28`，并复用同轮 fresh 的 `0x596F50`。代表性行为不超过 10 行：

```text
StaticInit: SIMD 构造零值；发布 pre-register callback 与两只 AutoRegister 记录。
GetInt: 按 tag 分支 load/extend/convert；LODWORD 只是反编译 low-lane 表示。
EnumMembers: Array/Dictionary 各执行完整 8 实参 FuncCall callback。
Regist: 构造 registrar 后 Begin -> registerMembers -> 析构 End。
Regist landing: 异常时调用 End，再把原异常交给 _Unwind_Resume。
RegistEnd: 可选 dummy ctor；构造 Variant；六参 PropSet；释放 global/Variant。
```

| Android ARM64 调用结构 | 当前源码对照 |
| --- | --- |
| `0x59A8D8` normal 三个 direct call 与 `0x59A958` landing End | [`ncbind.hpp:1665-1675`](../../cpp/core/plugin/ncbind.hpp)、[`ncbind.hpp:2148-2157`](../../cpp/core/plugin/ncbind.hpp) 的 constructor/destructor RAII |
| `RegistEnd@0x59AD84` 的 optional ctor、global、Variant、六参 indirect PropSet | [`ncbind.hpp:1890-1917`](../../cpp/core/plugin/ncbind.hpp) 保留相同调用和所有权顺序 |
| `EnumMembers@0x596F50` 两只八参 callback | [`main.cpp:371-442`](../../cpp/plugins/psbfile/main.cpp) 保留两分支、2/3 个脚本参数与完整 C++ ABI |
| `psbfile_static_init@0x42CF28` 的 SIMD zero 与 callback/object publication | [`main.cpp:751-757`](../../cpp/plugins/psbfile/main.cpp) 由 NCB 宏生成同一注册结构 |

未发现缺失 direct target、不同间接 arity、callback 参数错位、把 helper intrinsic 当成
真实函数、漏掉 RAII landing cleanup，或本地显式调用多于/少于 Android source tree 的
确定证据。

## 门禁

本面直接由 ECT1 重建，不另复制第二份 row payload。canonical semantic sequence 为
144,542 bytes，SHA-256：

```text
d275605703285f6ef86763bc0e174911e832f92401b551b772ad6aaaf8e75b35
```

`verify_elf_surface.py` 固定输出：

```text
call_expression_surface=true owners=61 rows=405 roots=252 direct=285 indirect=40 helpers=80 concrete=385 synthetic=20 args=638 max_args=8 return_types=14 callee_types=107 argument_types=64 callee_shapes=120 direct_targets=79 internal_sites=43 external_sites=242 internal_targets=21 external_targets=58 internal_edges=39 helper_names=8 raw_sites=385 anchors=405 normal=405 landing=0 source_transfers=325 machine_transfers=525 machine_only=200 source_direct=285 machine_direct=479 machine_only_direct=194 source_indirect=40 machine_indirect=46 machine_only_indirect=6 machine_direct_targets=86 machine_only_direct_targets=7 realizations=5 callee_ops=7 arities=8 semantic_bytes=144542 semantic_sha256=true instruction_words=true normal_indirect_complete=true source_machine_split=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面固定 source-facing call tree 与
machine lowering 的边界，不把 stripped/O3 删除的 helper 名或 implicit C++ 语法冒充为
可唯一恢复的源码 token。
