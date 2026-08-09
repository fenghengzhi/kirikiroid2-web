# 反向函数指针 / 重定位 / vtable 归属闭环（2026-08-04）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

上一轮已从完整 `.text` 反向闭合所有外部 `B/BL -> MANIFEST` 直接 consumer。本轮补它的
非直接对偶：不从 114 个 owner 向外看，而是从 114 个函数地址反向枚举整个 ELF 中所有
地址物化、绝对指针、动态重定位、init-array、vtable 槽和 PLT 弱符号别名。

最终 target-address-bearing 表面只有三类：

- 7 个由 MANIFEST 内 4 个注册 owner 生成的 callback/member-pointer；
- 62 个 `R_AARCH64_RELATIVE` 指针槽：2 个 `.init_array` + 60 个 psbfile vtable 槽；
- 2 个弱 STL 定义的 `R_AARCH64_JUMP_SLOT`/PLT 别名。

完整 `.text` 只有 8 条 `ADRP` 会落到某个 MANIFEST 所在页。七条解析到上述注册目标；
第八条位于 `sub_42CFA0`，其 `ADD` 精确解析到独立 `PackinOne.dll` callback
`0x59B9C8`，只是与 `0x59Bxxx` MANIFEST 入口同页，不能提升成 psbfile 边。全段没有任何
精确指向 MANIFEST 的 `ADR`、general-register literal load、MOVZ/MOVN+MOVK 或 logical-
immediate `MOV`。

因此没有来自 MANIFEST 外业务 FDE 的额外函数取址 consumer；也没有隐藏的外部静态
callback table。psbfile 专属 vtable/header 的 IDA 反向引用同样全部来自 MANIFEST owner。
共享 `ncbind` 基类/interface address point 的大量跨模块引用属于所有 NCB 类共同使用的
基类表，不能误归属为 psbfile consumer。

本地 `ncbind.hpp` 的静态对象、类信息、wrapper、adaptor 与注册调用链逐项一致。最终判定：
**ALIGNED / 无新增生产 GAP**。本轮不修改 `cpp/`、fixture 或测试物料，因此不触发构建。

## fresh 反编译证据

本轮 fresh decompile：

- `PSBFile_ncbClassInfo_static_init@0x42CEF8`；
- `psbfile_static_init@0x42CF28`；
- `PSBFile_ncb_registerMembers_guess@0x597F38`；
- `PSBFile_ncbRegistNativeClass_RegistBegin_guess@0x59AA84`；
- `PSBFile_ncbRegistNativeClass_RegistEnd_guess@0x59AD84`；
- 相邻页碰撞目标 `sub_59B9C8@0x59B9C8`。

关键逻辑压缩为十行以内：

```text
.init_array invokes class-info initialization before the psbfile static registrar
class-info init guard-zeroes initialized/name/id/classObject, then commits the guard
psbfile static init constructs class/callback AutoRegister records and stores &preRegister
registerMembers allocates Factory/root/load wrappers and stores their three callable addresses
RegistBegin creates the class with &CreateEmptyAdaptor, publishes class info, then adds finalize
RegistEnd installs &NotImplCallback only when no constructor exists, then publishes the class
all other absolute MANIFEST pointers are loader-relocated init-array or vtable qwords
the two weak STL symbol values feed JUMP_SLOT/PLT aliases, not source callback tables
the sole same-page outsider resolves exactly to PackinOne callback 0x59B9C8
no non-MANIFEST business FDE materializes a MANIFEST function address
```

## 完整反向 census

### IDA data xref 交叉检查

114 个目标共得到 78 条 `Data_Offset`：

| 来源 | 行数 | 正确归属 |
| --- | ---: | --- |
| `.init_array` | 2 | 两个静态初始化入口 |
| `.data.rel.ro` | 60 | psbfile 专属 vtable 函数槽 |
| `.text` | 10 | 7 个 callable 的 `ADRP/ADD/ADRL` 行 |
| dynsym/PLT/GOT 分析视图 | 6 | 两个弱 STL 符号各一组三层别名 |

这 78 行没有来自其他业务函数的新增 target-address xref。IDA 结果只作交叉分类；机械门禁
独立从原始 ELF 指令与 relocation 表重新建立集合。

### 原始 `.text` 指令

全 `.text` 的 MANIFEST-page `ADRP` 候选恰好 8 个：

| owner | target | 归属 |
| --- | ---: | --- |
| `0x42CF28` | `0x59849C` | pre-register callback |
| `0x42CFA0` | `0x59B9C8` | PackinOne 同页碰撞；非 MANIFEST |
| `0x597F38` | `0x5980F4` | Factory callback |
| `0x597F38` | `0x5981F8` | `root` member pointer |
| `0x597F38` | `0x598268` | `load` member pointer |
| `0x59AA84` | `0x59ABD8` | CreateEmptyAdaptor |
| `0x59AA84` | `0x59AC04` | `finalize` callback |
| `0x59AD84` | `0x59AEE4` | dummy constructor callback |

门禁直接解码每个 ADRP 页、ADD immediate、源/目标寄存器和最终地址；不依赖反汇编文本。
四类替代物化形式计数均为零：`ADR=0`、`LDR-literal=0`、`MOV-wide=0`、
`logical-immediate=0`。

### `.rela.dyn` / `.rela.plt`

完整解析 75,504 条 `.rela.dyn` 后，addend 位于 MANIFEST 的恰好 62 条，全部满足：

```text
type=R_AARCH64_RELATIVE(1027), symbol="", symbol_value=0, symbol_size=0
```

62 个 relocation target 与既有十张静态表中 MANIFEST-valued qword 的位置和值逐项相等：
2 个在 `.init_array`，60 个在 `.data.rel.ro`。没有 `.rela.dyn` dynamic-symbol value 指向
MANIFEST。

完整解析 10,394 条 `.rela.plt` 后，只有两个 dynamic-symbol value 位于 MANIFEST：

- `std::vector<std::string>::reserve@0x599174`；
- `std::vector<std::string>::_M_emplace_back_aux@0x59B7E8`。

二者均为 `R_AARCH64_JUMP_SLOT(1026)`。文件中的两个 GOT qword 初值都是 PLT0
`0x3FEDC0`；加载后的 IDA 内存可显示已经解析的本地弱定义地址。门禁明确以文件初值、
`.rela.plt` 符号值和四指令 PLT stub 为准，避免把运行期 GOT mutation 误报成 initialized
function pointer。

### vtable address-point 归属

对 8 个 psbfile 专属 address point 的 IDA 反向查询得到 12 个直接 AP xref、9 个 owner，
外部 site/owner 均为 0；对其 8 个 header 的查询得到 19 行，也全部位于 MANIFEST。

另外四个 address point 是共享 `ncbind` 基类/interface 表，而非 PSBFile 专属表。它们共有
178 个 xref、161 个 owner，其中 176 个 site/159 个 owner 在 MANIFEST 外；这些引用构造
其他 NCB 类的同一基类，正是共享模板拓扑证据，不能据地址相同建立到 psbfile 的对象边。

## 本地逐行对照

| Android 数据流 | 本地实现 | 对照 |
| --- | --- | --- |
| class-info guard + 四字段初始化 | `cpp/core/plugin/ncbind.hpp:73-114` | `InfoT` 构造、字段与 Set/Clear 状态一致 |
| class AutoRegister 静态对象 | `ncbind.hpp:2147-2172`；`main.cpp:751-755` | module/class 名、注册对象与 body 一致 |
| pre-register callback 静态对象 | `ncbind.hpp:2366-2379`；`main.cpp:757` | `PreRegist`、`&initPsbFile`、空 term 一致 |
| Factory/root/load callable | `main.cpp:732-755`；`ncbind.hpp:1325-1496` | plain callback、两只 member pointer 与 wrapper 种类一致 |
| CreateEmpty/finalize/class info | `ncbind.hpp:1843-1873` | 创建顺序、Set、class ID 与 finalize 一致 |
| dummy ctor/global publication | `ncbind.hpp:1890-1924,1944-1954` | `!_hasCtor` gate、NotImpl、Variant/Release/PropSet 顺序一致 |
| dispatch/media/adaptor vtables | `PSBDispatch.h`、`PSBMedia.h`、`ncbind.hpp:119-148` | 专属表只由本插件构造/析构路径发布；共享基表保持共享 |

没有一项出现 Android 正证据与本地实现不一致，因此没有满足前置条件的 `cpp/` 修改。

## 机械门禁

`verify_elf_surface.py` 新增：

```text
inverse_pointer_reference_surface=true page_candidates=8 manifest_materializations=7 materialization_owners=4 external_materialization_owners=0 page_collisions=1 adr=0 ldr_literal=0 mov_wide=0 logical_immediate=0 relative_slots=62 init_array=2 vtable_slots=60 plt_aliases=2 targets=71 sha256=true
```

门禁固定 1,704-byte canonical surface，SHA-256 为
`caad7bc0263d2b48a7d1a2d34395dfcf87282bc7eb3d3d29440028ea71ad120d`，并同时验证：

1. 完整 `.text` 中所有 MANIFEST-page ADRP 候选与最终 ADD target；
2. 四类替代绝对地址物化不存在；
3. 62 个 RELATIVE relocation 与静态表槽逐项双向相等；
4. `.init_array=2`、`.data.rel.ro=60` 的 section 分区；
5. 两个 JUMP_SLOT 的 symbol/value/size、PLT stub 和文件 GOT 初值；
6. 71 个唯一 MANIFEST target 的三类互斥分区与 canonical digest。

完整 ELF 门禁通过。结论只来自 Android ARM64 二进制本体、fresh IDA 反编译、原始指令、
ELF relocation/symbol 表和本地逐行对照。
