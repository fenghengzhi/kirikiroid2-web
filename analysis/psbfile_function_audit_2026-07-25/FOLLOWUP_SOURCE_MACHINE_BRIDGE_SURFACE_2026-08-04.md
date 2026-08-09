# psbfile normal-flow source/machine bridge（2026-08-04）

## 结论

本轮只使用权威 Android ARM64 `libkrkr2.so`、当前 IDB 与既有 ELF/ECT1 审计物料，
没有访问其他源码仓库，也没有引入其他架构二进制。此前的 `HAS_GAP=0` 只表示已审计
表面没有差异，不代表“全部正常流机器指令都已有来源归属”。本轮补上这个完备性缺口：

```text
normal-entry instructions = 4956
ECT1 unique expression anchors = 3076
machine-only residual = 1880
3076 + 1880 = 4956
```

1,880 条 residual 逐站点固定 exact word、IDA printed mnemonic、五类机器族与内存角色；
没有任何站点落在分区之外。该补集全部能归入控制流、调用/返回、计算 lowering、栈帧、
switch table、全局/GOT、引用计数/COW、vtable、Variant 载荷、closure/output 或已确认
reload，未发现新的生产源码 GAP，因此没有修改 `cpp/`，也不需要构建。

## fresh IDA 证据

本轮重新调用 IDA decompile：

- `PSBValueDispatch_CreateVariant_guess@0x59673C`：Variant tag 分派、dispatch vtable
  取得与结果构造；
- `PSBValueDispatch_EnumMembers@0x596F50`：closure pair、COW string、GOT 与
  intrusive reference 路径；
- `PSBRawNode_GetDictionaryValueStrict_guess@0x598C58`：strict miss continuation
  的 hidden-sret owner/node 双清零；
- `PSBRawNode_GetDictionaryKeys_guess@0x598E64`：旧 libstdc++ 三指针 vector、
  reusable COW string 与 `end` 增量；
- `PSBRawNode_GetInt_guess@0x599438`：tag 与 signed byte/short payload 载荷；
- `PSBFile_ncbRegistNativeClass_RegistBegin_guess@0x59AA84`：class object 构造、
  class-info publication 与 finalize 注册；
- `PSBFile_ncbRegistNativeClass_RegistItem_guess@0x59AEEC`：constructor gate、
  `GetDispatch/GetType/GetFlags/Release` 接口链。

另用现代 IDAPython `ida_ua.decode_insn` / `ida_bytes.get_wide_dword` 逐条扫描全部
residual，并单独检查最后三个未被既有独立表面命名的站点：

| 站点 | 指令 | fresh 反编译归属 |
|---|---|---|
| `0x598FE4` | `LDR X8,[X19,#8]` | `std::vector<std::string>::end` 在 copy ctor 后回读，随后 `+8` 并写回 |
| `0x59AB20` | `LDR X20,[X19,#8]` | “Already registerd class.” helper 的 continuation 上回读 `_classobj`，并汇入公共尾部 |
| `0x59B040` | `LDR X8,[X19]` | 读取 `item->vftable`，下一条 slot load 调用 `GetDispatch` |

三者分别是 vector-end reload、class-object reload 与 interface-vptr load；都已由当前
普通 C++ 容器/NCB 模板表达，不是缺失字段或缺失调用。

## 完备分区

### 五类 residual 指令

| family | 数量 |
|---|---:|
| computation/address lowering | 781 |
| memory | 554 |
| branch | 370 |
| return | 143 |
| call | 32 |
| **合计** | **1880** |

28 种 printed mnemonic 的完整计数被 SMB1 canonical payload 固定；高频项包括
`ADD=307`、`B=232`、`LDP=157`、`LDR=148`、`STP=143`、`RET=143`。
42 条 `LDRSW` 与既有 42 张 switch table/dispatch 一一对应，距离严格为
`8/12/16` 字节。

### 554 条 residual memory

```text
stack-frame = 418
switch-table = 42
semantic non-stack/non-switch = 94
418 + 42 + 94 = 554
```

94 条语义内存补集进一步互斥闭合：

| role | 数量 | 独立约束 |
|---|---:|---|
| vtable base load | 33 | 全部为 `LDR X8,[Xn,#0x28]` |
| global BSS | 18 | 与完整 global-BSS xref surface 交叉 |
| Variant tag | 15 | 排除 global guard 后全部为 raw `LDRB` |
| refcount/COW | 14 | 与 reference-count machine-site surface 交叉 |
| GOT relocation load | 6 | 与 initialized GOT pair surface 交叉 |
| closure pair | 2 | 两条 `LDP` |
| Variant narrow payload | 2 | `LDRSB + LDURSH` |
| output pair zero | 1 | `0x598D08` |
| vector-end reload | 1 | `0x598FE4` |
| class-object reload | 1 | `0x59AB20` |
| interface-vptr load | 1 | `0x59B040` |
| **合计** | **94** | 无未分类站点 |

global/GOT/refcount 有物理站点重叠时按更直接的 global → GOT → refcount 证据优先级归类；
verifier 从三套既有独立 manifest 重新计算集合，而不是只相信 SMB1 自报标签。

## 门禁伪代码

```text
normal = union(walk_normal_cfg(entry) for all 114 FDEs)
anchors = unique((owner, row.anchor) for every ECT1 expression)
assert anchors <= normal
residual = normal - anchors
decode SMB1 rows; assert every exact word and sorted residual site
partition residual into computation/memory/branch/call/return
partition memory into stack/switch/11 semantic roles
cross global/GOT/refcount roles with independent manifests
assert 42 LDRSW map bijectively to 42 switch dispatches
assert anchors | residual == normal and anchors & residual == empty
```

## 本地源码对照

| Android ARM64 结构 | 当前实现 |
|---|---|
| Variant tag/category 与 raw scalar payload | `cpp/plugins/psbfile/PSBPackedInternal.h` 与 `PSBRawFile.cpp` 保留相同分类和 typed 读取 |
| Dictionary key 的 reusable string、reserve、emplace、end 增量 | `PSBRawFile.cpp:280-306` 保留 `std::vector<std::string>` 和同一循环/生命周期 |
| strict miss 的 owner/node 零结果 | `PSBRawFile.cpp:252-267` 保留 helper continuation 的 `return {}` |
| EnumMembers closure/COW/reference 生命周期 | `PSBDispatch.cpp` 保留 TJS Array/closure 参数和逐项回收 |
| class-info publication 与 RegistItem 四 vslot | `cpp/core/plugin/ncbind.hpp:1843-1935` 保留 `RegistBegin/Item/End` 模板层次 |
| switch `LDRSW/ADD/BR` | 源码层各 tag/category `switch` 已由 selector/dispatch 独立表面逐项对齐 |

这里的 stack spill、register reload、GOT 读取与旧 libstdc++ COW 内联属于目标 ABI/编译器
realization；源码复原要求保留对应普通 C++ 对象、容器与生命周期，不在 wasm32 代码中
硬写 ARM64 栈槽或对象偏移。

## 验证

`verify_elf_surface.py` 新增 `verify_source_machine_bridge_surface()` 与 SMB1 payload，
首次完整运行输出：

```text
source_machine_bridge_surface=true functions=114 normal=4956
expression_anchors=3076 residual=1880 families=5 mnemonics=28
memory=554 stack=418 switch=42 semantic=94 semantic_roles=11
semantic_bytes=67886 semantic_sha256=true normal_partition=true
ect_anchor_cross=true global_cross=true got_cross=true
refcount_cross=true switch_bijection=true paths_complete=true
```

本轮判定：`HAS_GAP=0`。这是新增完备表面的结论，不把整个长期复原目标误标为完成。
