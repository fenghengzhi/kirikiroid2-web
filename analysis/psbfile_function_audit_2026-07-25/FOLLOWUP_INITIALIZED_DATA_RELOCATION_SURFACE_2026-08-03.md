# initialized-data / relocation / vptr 发布闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

本轮把 114 个 MANIFEST FDE 的剩余 initialized-data 引用完整闭合。排除已经覆盖的
`.rodata` 与 `.bss` 后，IDA DataRef 原始 census 为 146 行：`.data.rel.ro=39`、
`.got=48`、`.data=2`、`extern=9`、`.text=27`、无 segment 20、`.plt=1`。逐条检查指令
语义后的正确分类是：`.text` 含 10 行真实 callable 取址、15 行 packed mask 与 2 行 MDF
magic 碰撞；无 segment 的 20 行是栈 operand；`.plt` 的唯一一行是 PSB magic 碰撞。本报告
只闭合 initialized-data 子集，callable/碰撞已在后续
[address-taken callable 报告](FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md)
单独固定。initialized-data 表面闭合为：

- 39 条 `.data.rel.ro` 地址物化，覆盖 18 个目标、13 个 owner；
- 48 条 GOT 指令恰好组成 24 组 `ADRP → LDR`，覆盖 4 个 slot、7 个 owner；
- 9 条 extern 引用与 9 组 `pthread_create` GOT LDR 是同一批站点，不重复计数；
- 2 条 `.data` 引用中一条与 GOT 读取重合，另一条是 canonical 空串的第二级指针；
- 两类 owner 的并集为 19 个，未发现未分类 initialized-data consumer。

fresh 反编译把这些静态数据恢复为 class/callback AutoRegister、双基类 dispatch、三只
typed NCB wrapper、PSBMedia、native class object 与 instance adaptor 的 vptr 发布/回退链，
以及 COW empty representation、`pthread_create` 和 canonical 空串的动态重定位链。当前
本地类继承、嵌入 interface、构造/析构、字段顺序和注册模板逐项一致。

最终判定：**ALIGNED / 无新增生产 GAP**。本轮不修改 `cpp/`、fixture 或测试物料；没有
生产代码变化，因此不触发构建。

## fresh 反编译证据

本轮 fresh decompile 覆盖：

- `0x42CF28`：class AutoRegister 与 pre-register callback 两只静态对象；
- `0x59673C`、`0x597A40`、`0x597AD4`、`0x5981F8`：dispatch inline construction、
  terminal Release、constructor 与 root getter；
- `0x597F38`：factory/root/load 三只 typed wrapper 注册对象；
- `0x59849C`、`0x5997F0`、`0x599830`：PSBMedia singleton 构造和两种析构入口；
- `0x59AA84`：native class object 的 `RegistBegin`；
- `0x59ABD8`、`0x59AC7C`、`0x59AD08`：adaptor 创建、complete destructor 与 deleting
  destructor。

关键行为压缩为十行：

```text
static init derives class/callback address points, constructs objects, then commits heads
dispatch ctor/inline/root getter publish primary+secondary vptrs before ref/owner/node
dispatch terminal Release restores both vptrs before field teardown/delete
factory/root/load wrappers each publish main dispatch vptr + embedded method-interface vptr
media construction/destruction publishes the same storage-media address point
RegistBegin base-inits class object, installs CreateEmptyAdaptor, then publishes plugin vptr
adaptor creation publishes derived vptr; complete destruction restores derived then base vptr
GOT relative relocations resolve AutoRegister head, COW empty rep, canonical empty-string chain
pthread_create uses GLOB_DAT and gates old-libstdc++ atomic vs plain refcount path
all initialized-data references are address materialization/read-only; mutable writes remain in .bss/heap
```

## 三张补充基类表与 12 个 address point

既有 static-object 门禁固定的是 10 个 module-facing 表、177 个 qword。本轮没有篡改该历史
口径，而是把构造/析构实际消费、但此前未单独固定的三张 base/interface vtable 另建表面：

| 表 | header | qword |
| --- | ---: | ---: |
| native-class plugin base | `0x19FD6B8` | 37 |
| native-instance base | `0x19FD818` | 7 |
| method-object interface | `0x19FE1E8` | 6 |
| **合计** | — | **50** |

三张表的 400 bytes canonical SHA-256 为
`39d4c227329ae3757ce823bff1a160f6cb0ae4db937f1380c8b3fba18fc836d6`。它们与既有表共同
提供 12 个实际 address point：

| 语义 | header | address point | offset-to-top |
| --- | ---: | ---: | ---: |
| class AutoRegister | `0x1A0B568` | `0x1A0B578` | 0 |
| pre-register callback | `0x19FD8D8` | `0x19FD8E8` | 0 |
| dispatch primary | `0x1A0B3C8` | `0x1A0B3D8` | 0 |
| dispatch secondary | `0x1A0B4D8` | `0x1A0B4E8` | -8 |
| embedded method interface | `0x19FE1E8` | `0x19FE1F8` | 0 |
| factory wrapper | `0x1A0B5C0` | `0x1A0B5D0` | 0 |
| root-property wrapper | `0x1A0B6E0` | `0x1A0B6F0` | 0 |
| load-method wrapper | `0x1A0B800` | `0x1A0B810` | 0 |
| PSBMedia | `0x1A0B500` | `0x1A0B510` | 0 |
| native-class plugin | `0x19FD6B8` | `0x19FD6C8` | 0 |
| instance-adaptor derived | `0x1A0B588` | `0x1A0B598` | 0 |
| native-instance base | `0x19FD818` | `0x19FD828` | 0 |

门禁逐项要求 `address point = header + 16`、首 qword 等于 signed offset-to-top、第二 qword
RTTI 为零，并要求 header 必须属于上述 13 张已固定表之一。dispatch secondary 的 `-8`
独立证明 `iTJSNativeInstance` 是第二基类，不允许把双继承简化成单表对象。

## `.data.rel.ro` 地址物化

39 行精确分为：

```text
ADRP=7 ADRL=11 ADD=21
```

每行固定 `{target, owner, site, decoded-size, word0, word1, mnemonic}`。`ADRL` 同时固定
ADRP 与后续 ADD；普通 ADD 则沿同寄存器回溯到最近的 `ADRP` 页基址或 `ADRL` 完整
header，再验证绝对目标。后者很重要：例如 `0x5968E8` 的 `ADD #0x10` 是从已经物化的
dispatch header 生成 primary address point，不是从页基址直接加目标低 12 位。

39 行 canonical manifest 为 1,326 bytes，SHA-256 为
`8707b3a9f6d3002f83dcddda5be562b5e032e9ca1477e7e07d9dbef46e8c655a`。

## GOT、`.data` 与动态重定位链

24 组 GOT pair 精确分为：

| 角色 | slot | pair | relocation / 目标 |
| --- | ---: | ---: | --- |
| AutoRegister top array | `0x1A9FE48` | 1 | `R_AARCH64_RELATIVE → 0x1AB8920` |
| old-libstdc++ COW empty rep | `0x1A9F580` | 13 | `R_AARCH64_RELATIVE → 0x1C95280` |
| pthread gate | `0x1A9F750` | 9 | `R_AARCH64_GLOB_DAT → pthread_create` |
| canonical empty pointer | `0x1AA35A8` | 1 | `R_AARCH64_RELATIVE → 0x1AA7EF8` |

`0x1AA7EF8` 再由 `R_AARCH64_RELATIVE` 指向 UTF-16 空串 `0x1522752`，因此 canonical empty
路径是 `GOT slot → shared pointer object → empty tjs_char buffer`，不是 nullable fallback
或本地新建字面量。五条 relocation 中 4 条是 `R_AARCH64_RELATIVE`、1 条是
`R_AARCH64_GLOB_DAT`。

24 组 pair 的 exact ADRP/LDR word、base register、scaled immediate 和 slot 均进入 984-byte
canonical manifest，SHA-256 为
`3861776cabc1ccf66e549ba010ccefa3e136fc85ef6a983dd2156d99881fc8ed`。现有 CFG 独立把它们
分成 `16 normal + 8 landing`；每组 ADRP 与 LDR 必须属于同一 flow。8 组 landing 全部是
COW/pthread 异常清理路径，不是额外业务 global。

校验器直接解析 ELF64 `.rela.dyn` 及其链接的 `.dynsym/.dynstr`，不依赖外部命令输出；
relocation type、addend、symbol name、目标槽原始 qword 都被逐项固定。

## 24 次语义 vptr 发布

fresh 反编译恢复 24 个 lifecycle event、20 个唯一 store site：

| 家族 | event | 关键顺序 |
| --- | ---: | --- |
| static class/callback | 2 | 构造对象，再提交 `.bss` 链头 |
| dispatch inline/ctor/root getter | 6 | primary+secondary 同时发布，再写 ref/owner/node |
| dispatch terminal Release | 2 | ref 到零后恢复双 vptr，再析构字段并 delete |
| factory/root/load wrapper | 6 | 每只对象各发布 embedded interface + main dispatch |
| PSBMedia ctor/complete/deleting dtor | 3 | 三处使用同一 media address point |
| native class object | 1 | base init/CreateEmpty callback 后发布 plugin vptr |
| adaptor create/complete/deleting dtor | 4 | derived 发布；complete dtor 再恢复 base vptr |

`0x596900`、`0x597A7C`、`0x597AF0`、`0x598238` 四个 Q-register store 各同时发布 primary 与
secondary address point，所以 24 个语义 event 折叠为 20 个机器 site。全部 site 都必须从
normal entry 可达；event 的 address point、owner、exact store word 与独立 role ID 组成
696-byte manifest，SHA-256 为
`50ae3f375bb25d63b015b8443656380fd55956418e75b873261cb155535be25c`。

## 本地逐行对照

| Android 结构/生命周期 | 本地实现 | 对照 |
| --- | --- | --- |
| dispatch primary + secondary 双基类 | `cpp/plugins/psbfile/PSBDispatch.h:17-18` | 继承顺序与 `offset-to-top=-8` 一致 |
| dispatch ref/value/valid 字段与私有析构 | `PSBDispatch.h:118-133` | 构造与 terminal Release 生命周期一致 |
| dispatch ctor 与 delete-on-zero | `cpp/plugins/psbfile/main.cpp:20-26,96-107` | owner retain、ref 初值、析构触发一致 |
| inline collection 与 root getter 构造 | `main.cpp:668-700` | 两条对象发布路径一致 |
| PSBMedia interface、字段与逆序析构 | `cpp/plugins/psbfile/PSBMedia.h:10-48` | `_ref/_file/_container` 和 container→file 清理一致 |
| function-local pointer singleton | `cpp/plugins/psbfile/PSBMediaRegistry.cpp:6-11` | 每次调用重新 register，一致 |
| embedded `ncbIMethodObject` interface | `cpp/core/plugin/ncbind.hpp:835-908` | interface + main 双 vptr 拓扑一致 |
| method/property/factory wrapper templates | `ncbind.hpp:1325-1496,1664-1775` | 三种 wrapper 构造和注册一致 |
| adaptor native/sticky 生命周期 | `ncbind.hpp:119-148,228-231` | create、derived dtor、base dtor 一致 |
| native class object / `RegistBegin` | `ncbind.hpp:1819-1873` | base init、CreateEmpty callback、class publication 一致 |
| AutoRegister class/callback 静态对象 | `ncbind.hpp:2093-2172,2366-2380` | 两只静态对象和链头提交一致 |

因此没有满足“Android 反编译正证据证明本地存在差异”的 `cpp/` 修改项。

## 机械门禁

`verify_elf_surface.py` 新增两行输出：

```text
initialized_data_surface=true relro_tables=3 table_qwords=50 address_points=12 relro_xrefs=39 relro_targets=18 relro_owners=13 vptr_events=24 vptr_sites=20 sha256=true
initialized_relocation_surface=true got_pairs=24 got_slots=4 got_owners=7 got_normal=16 got_landing=8 relative=4 glob_dat=1 cow=13 pthread=9 autoreg=1 canonical_empty=1 relocation_symbols=true
```

门禁同时验证：

1. 三张补充基类表的 50 个 qword 与 canonical digest；
2. 12 个 Itanium address point 的 header/prefix/RTTI/offset-to-top；
3. 39 条 `.data.rel.ro` exact word、寄存器链、绝对目标与 digest；
4. 24 组 GOT pair 的 exact word、slot 解码、角色、normal/landing 分区与 digest；
5. 五条 `.rela.dyn` 的 type/addend/dynamic symbol/raw target value；
6. canonical 空串的两级 `.data` pointer chain；
7. 24 次 vptr 发布的 address point、owner、exact store、packed-store 拓扑与 normal CFG。

完整 ELF 门禁通过。结论只来自 Android ARM64 二进制本体、fresh IDA 反编译、全量机器
census 与本地逐行对照，不依赖同版本源码、安装包旁证或运行时 fixture。
