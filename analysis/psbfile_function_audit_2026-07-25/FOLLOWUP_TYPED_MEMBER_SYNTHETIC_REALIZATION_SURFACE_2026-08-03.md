# typed-member synthetic expression 机器实现闭环（2026-08-03）

## 结论

本轮继续只以 Android ARM64 `reference/libkrkr2/libkrkr2.so` 为权威，把
typed-member ctree 审计中没有独立表达式地址的全部 **67 条** optimizer-synthetic
语义行闭合到真实机器实现。

> 后续纠正：本报告精确闭合的仍是 67 条 synthetic 行；但这里使用的
> `416 EA-backed + 67 = 483` 只是旧 typed-member 基线。后续 raw surface 复扫提升了
> 5 条新的 EA-backed read，当前完整 typed-member 表面为
> `421 + 67 = 488`。详见
> [FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

结果为 **ALIGNED / 无生产代码 GAP**：

- 67 条语义行覆盖 33 个 owner FDE，精确分为
  `W=42 / R=15 / RW=7 / address=3`；
- 五条语义行因分支或合并初始化产生多个实现位置，展开为 **73 个 occurrence / 73 个唯一
  机器锚点**；
- 73 个锚点全部从各自 owner 的正常入口可达，landing-only 为 0；
- 62 个锚点是 coalesced/assignment/RMW store，另有
  `LDR=6 / BLR=3 / BR=2`；
- 既有 416 条 EA-backed 语义行与本轮 67 条 synthetic 行重新合计为当时的
  **483 条旧基线**，没有遗漏或重复发明 member EA；后续 5 条 promotion 独立追加，
  不改写本报告的 synthetic payload。

本轮没有修改 `cpp/`、fixture 或测试物料，因此不触发构建。

## 为什么不能给这些行伪造 EA

Hex-Rays 把合并初始化、成对 store、栈临时 scalarization、cast 左值和 switch selector
折叠成 ctree expression 时，最外层 `cot_memptr/cot_memref` 的 EA 为 `BADADDR`。这表示
“该源码语义没有一条独占的机器指令”，不是“目标里缺少该字段”。

因此本轮固定两层互补证据：

1. semantic row：owner、读写模式、完整 expression text、base/member type、ARM64 field
   offset 与 `memptr/memref`；
2. occurrence：直接父 ctree op、机器实现类别、真实 anchor EA、mnemonic 与 exact word。

其中 11 个 anchor 同时也是既有 385-site typed-member instruction surface 的站点，因为
一条 `STP`/共享指令还承载另一条有 EA 的外层 member expression。其余 anchor 只证明
synthetic 语义的机器归属，不被冒充为新的 member site。

## 语义与 occurrence 分布

| 模式 | semantic rows | emitted occurrences |
| --- | ---: | ---: |
| `W` | 42 | 47 |
| `R` | 15 | 16 |
| `RW` | 7 | 7 |
| `address` | 3 | 3 |
| **合计** | **67** | **73** |

每条语义行的 occurrence 基数为：

```text
62 × 1
 4 × 2
 1 × 3
= 73
```

五条多 occurrence 语义是：

- `Load@0x598268` 的 `filter.manager`：`BLR@0x598318/0x59837C`；
- `Adopt@0x598708` 的 `v9->refCount`：`STR@0x59876C/0x5987FC`；
- `Adopt@0x598708` 的 `self->owner`：`STR@0x59882C/0x598944`；
- `Resolve@0x59A4B0` 的 `current.owner`：
  `STR@0x59A4F0/0x59A548/0x59A6C4`；
- adaptor complete destructor `@0x59AC7C` 的 `self->vftable`：
  `STR@0x59ACA0` 与 `STP@0x59ACEC`。

## 七类机器实现

| realization class | occurrences | 机器形状 | 语义约束 |
| --- | ---: | --- | --- |
| assignment-store | 47 | `STR/STP/STRB` | `W` + `cot_asg` |
| read-modify-write-store | 7 | `STR` | `RW` + `cot_preinc` |
| address-coalesced-store | 3 | SIMD `STR` | `address` + `cot_ref` |
| direct-load | 6 | `LDR` | `R` + `cot_cast` |
| indirect-call-target | 3 | `BLR` | `R` + `cot_cast`，并与 indirect ABI 的 `function-manager` 站点交叉证明 |
| switch-dispatch | 2 | `BR` | `R` + `cot_ptr`，并与既有 switch table 站点交叉证明 |
| cast-coalesced-store | 5 | `STR/STP` | ctree 仍是 `R`，机器上作为外层 assignment 左值/存储聚合的一部分 |

机器 mnemonic 总数为：

```text
STR=53 / STP=8 / STRB=1 / LDR=6 / BLR=3 / BR=2
```

这里最重要的边界是：ctree 的 `R/W/RW/address` 是最外层 member expression 的语法
分类；machine realization 描述优化后指令的作用。二者不能强行改成同一种分类。例如
`v11.converterStorage`、`io.converterStorage`、`v20.converterStorage` 都位于 cast
下面，语义分类为 `R`，但实际分别由 `STR@0x59B2E8/0x59B3C4/0x59B5CC` 清零其外层
functor storage；把它们改报成普通 load 会制造不存在的数据流。

## 高风险锚点

### dispatch 双地址点

`PSBValueDispatch_Release@0x597A40` 的
`&self->dispatch_vftable` 没有独立 EA。`STR Q0,[X19]@0x597A7C` 一次写回两个 base
address point，随后才执行 owner refcount 终结释放。构造路径 `0x597AF0/0x598238`
具有同一合并写形状。因此源码层仍应保留双继承对象，不应从一条 128-bit store 推导
`__int128` 生产字段。

### `std::function` manager

`filter.manager` / `v21.manager` 的三次 synthetic read 直接闭合到
`BLR@0x598318/0x59837C/0x599F04`。验证器不靠邻近关系猜测：三个调用点必须同时存在于
独立 `EXPECTED_INDIRECT_ABI_SITES`，且角色必须为 `function-manager`。

`filter.targetStorage` 与 `v21.targetStorage` 则分别由 `STR@0x5983E4`、
`STP@0x599F4C` 聚合搬运，保持 `std::function` target/manager bundle，不把它简化为
普通函数指针。

### raw node switch

`GetListAt@0x5999F4` 的 `out.node` 与
`GetResourceData@0x59A0B4` 的 `out.node` synthetic read 分别闭合到
`BR@0x599A70/0x59A138`。两个站点必须同时属于既有 65-entry 与 4-entry packed-tag
switch surface；这固定的是 raw node → tag classifier → jump table 链，而不是只固定最终
case 结果。

### adaptor 与 vector commit

- `ncbInstanceAdaptor<PSBFile>` complete destructor `@0x59AC7C`：先将 vptr 写成完整
  adaptor vtable，按 `_instance && !_sticky` 删除 native holder，再由
  `STP@0x59ACEC` 同时恢复 base vptr 与 `_instance=null`，最后清 `_sticky`；
- `std::vector<std::string>::_M_emplace_back_aux@0x59B7E8`：
  `STP@0x59B8F8` 同时提交 begin/end，`STR@0x59B8FC` 单独提交 capacity end；没有把
  三指针容器拓扑压成一个裸 buffer。

## fresh 反编译证据

本轮 fresh decompile 复核了覆盖不同 synthetic 类别的函数：

- `PSBValueDispatch_Release@0x597A40`；
- `PSBMedia_GetListAt_guess@0x5999F4`；
- `PSBMedia_GetResourceData_guess@0x59A0B4`；
- `PSBFile_ncbInstanceAdaptor_completeDestructor_guess@0x59AC7C`；
- `PSBFile_loadMethod_FuncCall_guess@0x59B570`；
- `std::vector<std::string>::_M_emplace_back_aux<std::string &>@0x59B7E8`。

目标逻辑摘要不超过十行：

```text
enumerate all 67 typed-member semantic rows whose ctree EA is BADADDR
find every nearest ancestor with a concrete emitted address; keep all branch occurrences
classify parent syntax as asg/cast/preinc/ref/ptr without rewriting the original access mode
bind each occurrence to its owner FDE, exact instruction word and normal-entry CFG node
require stores for assignment/RMW/address/cast-store realizations
require direct LDR for cast-read realizations
require function-manager BLR sites to exist in the independent indirect ABI manifest
require raw-node BR sites to exist in the independent switch-table manifest
reject missing/extra/shared anchors, landing-only anchors, class drift and word drift
require 416 EA-backed rows + 67 synthetic rows == the legacy 483-row baseline
```

## 本地逐段对照

| Android ARM64 synthetic 机器形状 | 当前源码对照 |
| --- | --- |
| dispatch 双 vptr 合并写 + `refCount`/owner 终结释放 | `cpp/plugins/psbfile/PSBDispatch.h:17-20,118-133` 保留双继承、独立 refcount 与嵌套 raw node；`cpp/plugins/psbfile/main.cpp:96-107` 的 `delete this` 自然触发 holder 释放 |
| `Load@0x598268` 的 `std::function` target/manager 两分支 | `cpp/plugins/psbfile/PSBRawFile.h:79` 保留 `OwnerFilter = std::function<...>`；`cpp/plugins/psbfile/PSBRawFile.cpp:442-479` 保留 String/Octet/Adopt 结构 |
| `GetListAt@0x5999F4` 的 local raw node 与 packed switch | `cpp/plugins/psbfile/PSBMedia.cpp:149-219` 保留 `PSBRawNode`、Array/Dictionary 分支、packed count 与 lister 调用 |
| `GetResourceData@0x59A0B4` 的 local raw node + resource switch | `cpp/plugins/psbfile/PSBMedia.cpp:112-124` 保留 `Resolve → value.GetResource(size)` 源码层调用；内联不反写成扁平字段 |
| adaptor vptr/native/sticky 合并提交 | `cpp/core/plugin/ncbind.hpp:120-148` 保留 `ncbInstanceAdaptor`、`_instance`、`_sticky` 与 `_deleteInstance()` 生命周期 |
| paramsFunctor converter storage 清零 | `cpp/core/plugin/ncbind.hpp:924-963` 保留 `ArgsConvT`/result convertor、参数字段与转换调用，不手写功能等价 wrapper |
| vector begin/end/capacity 合并提交 | `cpp/plugins/psbfile/PSBRawFile.cpp:280-306` 保留 `std::vector<std::string>`、reserve/emplace 与 COW string 元素生命周期 |

逐段对照没有发现需要修改 `cpp/` 的差异。这里的 `STR/STP` 合并方式和 ARM64 byte
offset 是优化/ABI 证据，不应变成 wasm32 `_pad`、`__int128` 字段或手写三指针容器。

## IDB 与机械门禁

`0x597A7C`、`0x598318`、`0x59837C`、`0x599A70`、`0x599F4C`、
`0x59A138`、`0x59ACEC`、`0x59B2E8` 与 `0x59B8F8` 已加入
`TYPED-MEMBER-SYNTHETIC` 证据注释并保存。

[verify_elf_surface.py](verify_elf_surface.py) 新增一份 canonical payload：

```text
raw bytes = 4,714
SHA-256  = 17fd084c92ee4fb40a9c2b73c951fc8790b603ebfc638e096113dd60b0a063e6
```

门禁会：

1. 校验 payload 大小、SHA、UTF-8 语义串、canonical 行/occurrence 顺序与唯一性；
2. 从 ELF 读取 73 个 exact word，并要求全部属于各自 owner 的 normal CFG；
3. 逐项核对 access mode、ctree parent op、realization class 与 mnemonic 组合；
4. 将三条 `BLR` 和两条 `BR` 分别交叉连接到独立 indirect ABI / switch surface；
5. 核对 17 类 base type、12 类 member type、41 种 expression text、offset 与多 occurrence
   基数；
6. 重新组合 `311 read + 108 write - 3 shared RW = 416 EA-backed`，再加 67 得到
   483 条旧基线语义行；后续 5 条 read promotion 另行验证，当前总数为 488。

本轮旧基线的通过输出：

```text
typed_member_synthetic_surface=true owners=33 semantic_rows=67 occurrences=73 sites=73 normal=73 landing=0 w=42 r=15 rw=7 address=3 single=62 multi=5 max=3 ea_backed=416 total=483 paths_complete=true sha256=true
typed_member_synthetic_realization=true assignment_store=47 read_modify_write_store=7 address_store=3 direct_load=6 indirect_call_target=3 switch_dispatch=2 cast_store=5 machine_stores=62 str=53 stp=8 strb=1 ldr=6 blr=3 br=2 instruction_intersection=11 expressions=41 base_types=17 member_types=12
```

后续 verifier 会在这两行之后继续输出 `typed_member_promotion_surface=true ... total=488`
与 `raw_memory_surface=true ... rows=667`；本报告的 67-row payload 仍保持字节级不变。
