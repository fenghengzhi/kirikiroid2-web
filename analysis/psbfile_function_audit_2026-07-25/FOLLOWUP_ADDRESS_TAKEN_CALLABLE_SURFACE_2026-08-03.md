# address-taken callable / 数值碰撞闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

上一轮 initialized-data census 留下 27 条“目标位于 `.text`”的 IDA DataRef。本轮逐条读取
owner、site、目标、完整指令和 consumer 后，证明它们并不等于 27 个取函数地址：

- 10 行是真实 `ADRP/ADD/ADRL`，组成 7 个 address-taken callable；
- 15 行是 `AND Wn, Wm, #0xFFFFFF`，属于 packed 24-bit mask；
- 2 行是 MOVZ/MOVK 生成数值 `0x0066646D`，即内存字节 `MDF\0`；
- 另有 1 条目标落在 `.plt` 的假 DataRef，实际是数值 `0x00425350`，即 `PSB\0`。

因此完整 code-address-like 表面是 `10 real + 18 numeric collision`。7 个真实 callable
覆盖 pre-register callback、Factory、`root` getter、`load` method、CreateEmptyAdaptor、
`finalize` 空回调和缺省构造器回调。fresh 反编译确认它们分别进入 AutoRegister、typed
NCB wrapper、native-class 创建和 TJS method wrapper；现有 direct-call 门禁不会看到这些
间接入口，本轮补上了这块调用链证据。

当前本地宏、普通函数指针、ARM64 member-pointer `{code, adjustment}`、callback prototype
与注册顺序逐项一致。最终判定：**ALIGNED / 无新增生产 GAP**。本轮不修改 `cpp/`、fixture
或测试物料；没有生产代码变化，因此不触发构建。

## fresh 反编译证据

本轮 fresh decompile 覆盖四个取址生产者：

- `0x42CF28`：PSBFile class/callback 静态对象初始化；
- `0x597F38`：Factory、`root` Property、`load` Method 三只 typed wrapper；
- `0x59AA84`：native class `RegistBegin`、CreateEmptyAdaptor 与 `finalize`；
- `0x59AD84`：无 constructor 时安装 dummy callback。

并 fresh 反编译全部七个目标：`0x59849C`、`0x5980F4`、`0x5981F8`、`0x598268`、
`0x59ABD8`、`0x59AC04`、`0x59AEE4`。为消除 magic 假阳性，另 fresh 复核
`LoadStorage@0x598538` 与 `Adopt@0x598708`。

关键行为压缩为十行：

```text
static init stores &initPsbFile in the pre-register callback before committing list heads
typed registration stores &PSBFileFactory in the factory wrapper
root property stores member pointer {&PSBFile::GetRootDispatch, adjustment=0}
load method stores member pointer {&PSBFile::Load, adjustment=0}
RegistBegin passes &CreateEmptyAdaptor into native-class creation
RegistBegin wraps EmptyCallback under the literal finalize member
RegistEnd wraps NotImplCallback under the class name only when hasConstructor is false
all seven targets are consumed indirectly by callback/member wrappers, not producer-side BL edges
fifteen apparent .text refs decode exactly to numeric packed mask 0xFFFFFF
two MDF and one PSB MOV-wide pairs decode to numeric magic, not code addresses
```

## 七个 callable

| role | target | 取址行 | ABI / consumer |
| --- | ---: | ---: | --- |
| pre-register callback | `0x59849C` | 2 | `void (*)()`，AutoRegister callback object |
| Factory callback | `0x5980F4` | 2 | `tjs_error (*)(PSBFile **, int, Variant **, Dispatch *)` |
| `root` getter member | `0x5981F8` | 1 | member pointer `{code, 0}`，Property getter |
| `load` method member | `0x598268` | 2 | member pointer `{code, 0}`，Method wrapper |
| CreateEmptyAdaptor | `0x59ABD8` | 1 | `iTJSNativeInstance *(*)()`，native class field |
| `finalize` empty callback | `0x59AC04` | 1 | four-argument TJS callback，返回 `TJS_S_OK` |
| dummy constructor callback | `0x59AEE4` | 1 | four-argument TJS callback，返回 `TJS_E_NOTIMPL` |

10 行精确分为 `3 ADRP + 3 ADD + 4 ADRL`，覆盖 4 个 owner、7 个目标和 10 个唯一 site；
全部 site 都在 normal CFG。每行固定
`{target, owner, site, decoded-size, word0, word1, role, form}`：

- `ADRP` 解码并固定目标页；
- 对应 `ADD` 固定寄存器链和 page offset；
- `ADRL` 同时固定两条机器指令、寄存器和完整绝对目标；
- 每个 target 必须是 114-address MANIFEST 中的独立 FDE 起点。

canonical manifest 为 350 bytes，SHA-256 为
`222d3c50d3e6549685416cb22bb03187a560e29eaacdbbfe969a3c8605a57ec5`。

本 114-FDE 取址 census 中没有额外的 `std::function` manager/invoker 代码地址。默认
OwnerFilter 的 manager 由零值表示；非空 filter 的 manager/invoker 从外部调用者传入，
本轮不把运行时字段 load 伪造成当前模块内的 address materialization。

## 18 个数值碰撞

### packed 24-bit mask

15 个站点的机器指令都是 W-register logical-immediate AND。门禁不依赖 IDA 文本，而是从
`N/immr/imms` 重新解码 AArch64 bitmask immediate；15/15 均得到 `0x00FFFFFF`。这些站点
位于 packed offset/count/name/resource 路径，数值恰好落在目标 ELF `.text` 范围内，不能
因此建立到 `0xFFFFFF` 的函数或数据边。

### MDF / PSB magic

`0x5982BC` 与 `0x5985EC` 的两条 MOVZ/MOVK pair 均生成 W-register 值 `0x0066646D`；
小端字节为 `6D 64 66 00`，对应 `MDF\0`。`0x598738` 的 pair 生成 `0x00425350`；小端字节
为 `50 53 42 00`，对应 `PSB\0`。

`0x00425350` 同时恰好是一个无关 cocos2d import 的 `.plt` 地址，因此 Hex-Rays 即使没有
data xref 也可能在常量传播后显示那个符号。fresh `Adopt@0x598708` 的 size gate、随后的
PSB header 字段读取和本地 magic 定义共同证明这里是数值比较；IDB 已在 materialization 与
compare 处留下明确的 numeric-collision 注释。

18 行精确分成 `15 AND-imm + 3 MOV-wide`，覆盖 12 个 owner、3 个碰撞值、18 个唯一
normal site。canonical manifest 为 630 bytes，SHA-256 为
`63563bca3fe680cbde8c72cb495ce28500cd74d33353d153a4317fdd11058ced`。

## IDB 纠正

本轮对 15 个 mask、2 个 MDF magic 和 1 个 PSB magic operand 执行 numeric format 修正并
删除 18 条伪 data xref；复扫后 `.text/.plt` code-address-like DataRef 从 28 行收敛为
10/10 真实 callable 行。MDF/PSB materialization 和 compare site 均补充数值碰撞注释，
随后保存分析数据库。

这项纠正只改善 IDA 事实层，不更改目标二进制。它也纠正了上一轮报告中过于粗略的
“`.text` 均为代码地址或立即数伪引用”表述：正确分类是 10 行真实取址、17 行 `.text`
数值碰撞，另有 1 行 `.plt` 数值碰撞。

## 本地逐行对照

| Android 取址/consumer | 本地实现 | 对照 |
| --- | --- | --- |
| pre-register callback 写入 AutoRegister | `cpp/plugins/psbfile/main.cpp:757`；`cpp/core/plugin/ncbind.hpp:2366-2379` | `&initPsbFile`、PreRegist、空 term 一致 |
| Factory plain callback | `main.cpp:732-752`；`ncbind.hpp:1397-1435` | prototype、存储和 wrapper 调用一致 |
| `root` getter member pointer | `main.cpp:690-700,753` | code pointer + zero adjustment 一致 |
| `load` method member pointer | `cpp/plugins/psbfile/PSBRawFile.cpp:442-465`；`main.cpp:754` | by-value Variant method 与注册字面一致 |
| CreateEmptyAdaptor | `ncbind.hpp:228-231,1855-1858` | static callback、空 adaptor 字段一致 |
| `finalize` EmptyCallback | `ncbind.hpp:1869-1872,1944-1945` | literal、四参数 ABI、`TJS_S_OK` 一致 |
| dummy NotImplCallback | `ncbind.hpp:1890-1893,1947-1954` | `!_hasCtor` gate、class-name key、`TJS_E_NOTIMPL` 一致 |
| packed mask / MDF / PSB magic | `cpp/plugins/psbfile/PSBRawFile.cpp:17-30,387-418,521`；`PSBPackedInternal.h:199`；`PSBMedia.cpp:178`；`main.cpp:53-316` | 数值语义一致，无函数指针 |

因此没有满足“Android 反编译正证据证明本地存在差异”的 `cpp/` 修改项。

## 机械门禁

`verify_elf_surface.py` 新增：

```text
address_taken_callable_surface=true owners=4 targets=7 rows=10 sites=10 free_callbacks=5 member_pointers=2 normal=10 landing=0 sha256=true
code_reference_artifact_surface=true owners=12 targets=3 rows=18 sites=18 packed_24_mask=15 mdf_magic=2 psb_magic=1 normal=18 landing=0 numeric_collisions=true sha256=true
```

门禁同时验证：

1. 10 行 callable exact word、ADRP/ADD/ADRL 目标解码与 canonical digest；
2. 7 个 target 均为 MANIFEST FDE 起点，7 个 role 各唯一映射一个 target；
3. 5 个 free/static callback 与 2 个 member-pointer role 的完整分区；
4. 15 条 logical-immediate 指令重新解码为 `0xFFFFFF`；
5. 3 组 MOV-wide 指令重新构造两种 magic；
6. `0xFFFFFF/0x66646D` 位于 `.text`、`0x425350` 位于 `.plt` 的碰撞事实；
7. 全部 28 个 site 的 normal/landing 唯一归属与两份 canonical SHA。

完整 ELF 门禁通过。结论只来自 Android ARM64 二进制本体、fresh IDA 反编译、指令解码、
CFG 与本地逐行对照，不依赖同版本源码、安装包旁证或运行时 fixture。
