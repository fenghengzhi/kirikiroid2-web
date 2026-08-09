# 全局 `.bss` 状态机闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

本轮把先前仅在文字中解释的 psbfile 全局/静态状态推进为完整机器门禁：从 114 个
MANIFEST FDE 独立枚举所有指向 ELF `.bss` 的 DataRef，得到 94 行、93 个唯一站点、
16 个目标和 22 个 owner。94 行严格分成 `29 address + 31 read + 34 write`，再由现有
normal/landing CFG 独立分成 `84 normal + 10 landing`。

这些引用闭合为 9 个语义对象、6 个生命周期家族：lazy native-class ID、两只
AutoRegister 静态实例、PSBMedia singleton pointer/guard、class-info/初始化 guard、
三路 AutoRegister 链头数组，以及 old-libstdc++ COW empty representation。fresh 反编译
确认 lazy publication、静态构造顺序、guarded singleton、class-info 的 set/clear 和异常
清理顺序均与当前源码一致。

后续完整 `cot_obj` census 又恢复出 COW storage 的 `base+0x18` 字符子对象，并由
`EnumMembers@0x596F50` 的 `data-24` sentinel 比较及同 ELF 同类动态符号的
`st_size=0x20` 交叉确认：该语义对象的完整范围是 32 bytes，不是本报告初版只计入的
首 qword 8 bytes。下文范围与合计已就地纠正；94 行机器 DataRef 和 digest 不变。

最终判定：**ALIGNED / 无新增生产 GAP**。本轮不修改 `cpp/`、fixture 或测试物料；因为
没有生产代码变化，也不触发构建。

## fresh 反编译证据

本轮 fresh decompile 与完整反汇编覆盖：

- `0x42CEF8`：class-info guarded dynamic zero-initialization；
- `0x42CF28`：psbfile class/callback 两只 AutoRegister 静态对象构造；
- `0x596D90`：`PSBValueDispatch::NativeInstanceSupport` lazy class ID；
- `0x59849C`：function-local guarded `PSBMedia *` singleton；
- `0x597ED0` / `0x597F08`：class-info `Set` / `Clear`；
- `0x59A968`：unregister 正常路径与异常 landing 的重复 Clear；
- `0x59AA84`：`RegistBegin` class-info publication。

关键行为压缩为十行：

```text
if class-info guard is clear: zero initialized/name/id/object; publish guard=1
static init: read old top[0]/top[1]; construct class/callback objects; commit new heads
NativeInstanceSupport: wrong flag -> -1002; lazy-register ID; mismatch -> -1; publish view -> 0
Set: if initialized return false; write name/id/object; publish initialized=true last
Clear: write name/id/object as zero; publish initialized=false last
RegistBegin: create class; get ID; reject initialized info; publish info; install class ID/finalize
UnregistEnd: delete global member/release; Clear on both normal and exception paths
singleton: acquire guard; allocate/init/publish pointer; release guard; abort guard on exception
every callback invocation reloads the singleton pointer and registers it
COW empty representation is read-only standard-library runtime state
```

## 完整 `.bss` 引用 census

ELF `.bss` 元数据也纳入门禁：`SHT_NOBITS`、`WA`、VMA `0x1AB23F0`、长度
`0x263088`、alignment `0x10`。下表的“行数”是目标地址对应的完整 DataRef 行数；相邻字段
仍分别计数，因此 `STP@0x42CF90` 合法贡献两行，而唯一机器站点总数少一。

| 目标 | 行数 | 语义 |
| --- | ---: | --- |
| `0x1AB5098` | 3 | lazy native-class ID |
| `0x1AB50A0` | 3 | class AutoRegister 起点/字段 |
| `0x1AB50B0` | 1 | class AutoRegister 相邻字段 |
| `0x1AB50B8` | 1 | class AutoRegister 相邻字段 |
| `0x1AB50C0` | 3 | pre-register callback 起点/字段 |
| `0x1AB50D0` | 1 | callback 相邻字段 |
| `0x1AB50D8` | 1 | callback 相邻字段 |
| `0x1AB50E8` | 4 | PSBMedia singleton pointer |
| `0x1AB50F0` | 5 | singleton initialization guard |
| `0x1AB50F8` | 25 | class-info initialized flag |
| `0x1AB5100` | 7 | class-info name |
| `0x1AB5108` | 13 | class-info ID |
| `0x1AB5110` | 8 | class-info class object |
| `0x1AB5118` | 3 | class-info dynamic-init guard |
| `0x1AB8920` | 3 | AutoRegister 三路链头数组 |
| `0x1C95280` | 13 | old-libstdc++ COW empty representation |
| **合计** | **94** | 16 个目标、22 个 owner、93 个唯一站点 |

表中仍只有 `0x1C95280`，因为 13 条机器 DataRef/GOT load 都以 storage base 为目标；
Hex-Rays source tree 另恢复 8 个 `cot_obj@0x1C95298`，即同一 32-byte 对象的
`base+0x18` 字符子对象。两者由
[完整 `cot_obj` 面](FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md)共同固定。

每一行固定 `{target, owner, site, decoded-size, word0, word1, role, mnemonic}`。指令分布为：

```text
ADRP=8 ADRL=19 ADD=2
LDR=26 LDRB=3 LDARB=1 LDP=1
STR=23 STRB=6 STP=4 STUR=1
```

IDA 的 `ADRL` 是跨两条机器指令的伪指令，因此门禁同时解码 ADRP page 和后续 ADD
offset/register chain，并固定两只 exact word。94 行 canonical manifest 为 3,290 bytes，
SHA-256 为 `d7a0c1ad9469b0e25a158220160738e24dbb3f02a66564cfa6a23bd57609d9f7`。

## 状态机证据

### lazy native-class ID

`0x596DB8` 读取 `0x1AB5098`。值为零才在 `0x596DE8` 调用 `0x9F4F18` 注册字面名称
`PSBValueClass`，并在 `0x596DEC` 直接写回 W0。flag 非 GETINSTANCE 返回 `-1002`；class ID
不匹配返回 `-1`；成功才把 embedded native view 写入 caller pointer 并返回 0。这里没有
线程同步，也没有预先注册或 eager static constructor。

### AutoRegister 静态构造

`0x42CF28` 先读取 `0x1AB8920` 的旧 PreRegist/ClassRegist 链头，随后初始化 class
AutoRegister 和 pre-register callback 的 vptr、module name、next/callback 字段，最后才把
两只新对象提交为对应链头。`.init_array` 次序固定为 `0x42CEF8 → 0x42CF28`。

源码层 `_top[LINE_COUNT]` 有 PreRegist、ClassRegist、PostRegist 三个槽；当前 psbfile 静态
初始化只需要前两槽，PostRegist 保持 `.bss` 零值。第三槽并非凭空推断：同一二进制其他
模块对 `0x1AB8920/+8/+0x10` 的引用共同证明三路数组存在。这里只恢复源码容器拓扑，不把
ARM64 对象偏移写入生产 C++。

### guarded PSBMedia singleton

`0x59849C` 先以 `LDARB` 检查 guard；未初始化时在 `0x5984C0` acquire guard、`0x5984CC`
分配 0x28 bytes、写入 ref=1/vptr/初始字段，在 `0x5984FC` 发布 pointer，再于 `0x598508`
release guard。构造异常路径只在 landing graph 内执行 `0x59852C` guard abort 与
`0x598534` resume，因此失败时不发布 pointer。

无论本次是否执行初始化，每次调用都会在 `0x598510` 重新读取 pointer，并于 `0x59851C`
tail-register media。静态量本身是 pointer，不是带析构的 PSBMedia 对象，所以目标没有
注册退出析构。

### class-info publication 与清理

| 转换 | flow | initialized 检查 | 字段提交顺序 |
| --- | --- | --- | --- |
| dynamic zero-init `@42CEF8` | normal | guard | initialized/name/id/object，最后 guard |
| `Set@597ED0` | normal | `597ED4` | name/id/object，最后 initialized=true |
| `Clear@597F08` | normal | — | name/id/object，最后 initialized=false |
| `RegistBegin@59AA84` | normal | `59AB08` | emitted id/name/object，最后 initialized=true |
| `UnregistEnd@59A968` | normal | — | name/id/object，最后 initialized=false |
| `UnregistEnd@59A968` | landing | — | name/id/object，最后 initialized=false |

`RegistBegin` 的 emitted id/name 次序是优化调度结果；源码语义仍由完整内联 `Set` 证明为
name/id/object/flag。关键约束是 initialized flag 在成功提交和清理中均最后写入，异常
landing 也不得漏掉同一 Clear。

### COW runtime global

`0x1C95280` 的 13 行 machine DataRef 全部是 read，分布在旧 libstdc++ string/vector
路径；没有 write。fresh `0x596F50` 明确以 `&byte_1C95298` 初始化 data，并在清理时
比较 `data-24` 与 `&qword_1C95280`，所以完整 storage 为 24-byte header 加尾随字符区，
语义范围 `0x1C95280..0x1C952A0`。它是目标标准库 COW empty representation，不是
psbfile 作者自定义的业务 singleton。源码层继续使用普通
`std::string`/`std::vector<std::string>`，不在 wasm32 生产代码中伪造 Android
标准库私有 ABI。

## 本地逐行对照

| Android 状态机 | 本地实现 | 对照 |
| --- | --- | --- |
| `NativeInstanceSupport@596D90` lazy ID、三种返回边界 | `cpp/plugins/psbfile/main.cpp:455-471` | 一致，包括函数内 `static tjs_int32{}` |
| class/callback 两只静态注册对象 | `main.cpp:751-757`；`ncbind.hpp:2148-2172,2366-2380` | 宏展开拓扑一致 |
| PSBMedia pointer singleton，每次调用均 register | `cpp/plugins/psbfile/PSBMediaRegistry.cpp:6-11` | 一致，无静态对象析构 |
| class-info ctor、Set、Clear | `cpp/core/plugin/ncbind.hpp:74-114` | 字段、默认值、flag-last 一致 |
| `RegistBegin` / `UnregistEnd` | `ncbind.hpp:1843-1935` | publication 与异常析构语义一致 |
| `_top[3]`、read-old/commit-new 链头 | `ncbind.hpp:2093-2144` | 容器拓扑和构造顺序一致 |
| COW empty representation | 普通 `std::string`/`std::vector<std::string>` 使用点 | 源码选型一致；不复制目标 ABI 内联产物 |

因此没有满足“反编译证据已证明本地存在差异”的 `cpp/` 修改项。

## 机械门禁

`verify_elf_surface.py` 新增两行输出：

```text
global_bss_surface=true objects=9 families=6 targets=16 owners=22 rows=94 sites=93 address=29 reads=31 writes=34 normal=84 landing=10 zero_init_bytes=188 sha256=true
global_state_machine_surface=true calls=7 call_roles=7 classinfo_transitions=6 cow_runtime_reads=13 static_init_order=true singleton_guard=true
```

门禁同时验证：

1. `.bss` section type/flags/VMA/size/alignment；
2. 9 个语义对象的范围、互不重叠与 188 bytes 语义零初始化区；
3. 94 行 exact word、角色、mnemonic、ADRP/ADRL 目标解码与 canonical digest；
4. 22 个 owner 的 normal/landing CFG 唯一归属；
5. lazy ID、singleton acquire/allocate/release/abort/resume/register 的 7 个 direct call；
6. AutoRegister read-old/construct/commit 与 `.init_array` 次序；
7. 六组 class-info 转换和 initialized-last 约束；
8. COW global 的 13/13 machine DataRef read-only，并固定完整 0x20-byte storage 范围。

完整 ELF 门禁通过。结论来自 Android ARM64 二进制本体、fresh IDA 反编译、全量机器
census 与本地逐行对照，不依赖同版本源码、安装包旁证或运行时 fixture。
