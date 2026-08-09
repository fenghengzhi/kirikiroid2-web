# 栈局部结构字段 DataRef 闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

在 callable、数值碰撞、`.bss`、`.data.rel.ro`、GOT 和 relocation 全部分类后，114 个
MANIFEST FDE 的 IDA `DataRefsFrom` 还剩 **20 行目标不属于任何 ELF segment**。本轮对
114 个函数重新完整枚举，并以 `ida_typeinf.get_tid_name` 解析目标，确认它们不是外部地址、
损坏指针或待删除假引用，而是 IDA 为 typed stack-local structure offset 生成的特殊
`0xFF...` stroff identity：

- 20 行、19 个机器站点、7 个 owner、7 个 typed-member target；
- `7 read + 13 write`，展开相邻 `STP` 后是 24 个源码字段事件；
- `18 normal + 2 landing`；两条 landing 都是 `PSBFile::Load` 销毁空
  `std::function` 临时的 manager 检查；
- 指令严格分为 `7 LDR + 6 STR + 6 STP-row + 1 STRB`；`0x59B414` 的一条
  `STP` 同时产生 `result`、`param` 两行 DataRef，因此 20 行只对应 19 个唯一 site；
- fresh 反编译与本地逐行对照未发现 `cpp/` GAP。本轮不修改生产代码、fixture 或测试物料，
  因而不触发构建。

这补齐了上一轮 code-address-like DataRef 分类留下的 `<none>=20`。既有
stack-frame 门禁固定 frame allocation、callee-save 和 canary；typed-member 门禁固定
ctree 字段语义；本轮新增门禁则专门固定这 20 行 stroff DataRef 的 SP-relative machine
realization，并首次把其中两条 landing-only manager read 纳入同一闭包。

## 七个 stroff target

| IDA 特殊目标 | `get_tid_name` | 行数 | 字段级含义 |
| ---: | --- | ---: | --- |
| `0xFF0000000003EC0D` | `PSBOwnerFilter_arm64.manager` | 6 | 两次空 manager 初始化、两次 normal 析构读取、两次 landing 析构读取 |
| `0xFF0000000003EBD7` | `PSBRawNode.node` | 9 | 三只 raw-node 的 owner/node 双清零、两次 node 分类读取、Resolve 的三次 current.node 写和一次成功读 |
| `0xFF0000000003EC77` | `PSBFile_ncbRegistNativeClassState_arm64.hasConstructor` | 1 | delegate 默认 `false` |
| `0xFF0000000003EC76` | `PSBFile_ncbRegistNativeClassState_arm64.classObject` | 1 | 同一 `STP` 写 `className` 与 null `classObject` |
| `0xFF0000000003EC57` | `PSB_paramsFunctor_arm64.numparams` | 1 | setter typed invoke 固定参数数 `1` |
| `0xFF0000000003EC58` | `PSB_paramsFunctor_arm64.result` | 1 | setter typed invoke 的 null result |
| `0xFF0000000003EC59` | `PSB_paramsFunctor_arm64.param` | 1 | setter typed invoke 的 `&incomingParam` |

这些值只存在于 IDA 分析命名空间。verifier 不把它们当 ELF VMA，而是读取权威 ELF 的
actual word，重新解码 base register、scaled immediate、lane width、load/store 方向与
normal/landing CFG，再把 stroff identity 作为语义清单键。因此不会把 IDB 内部编号反向
冒充 Android 运行时数据。

## fresh Android ARM64 证据

本轮 fresh decompile 覆盖：

- `PSBFile_Load@0x598268`；
- `PSBRawNode_ContainsDictionaryKey_guess@0x5995D8`；
- `PSBMedia_GetListAt_guess@0x5999F4`；
- `PSBMedia_GetResourceData_guess@0x59A0B4`；
- `PSBMedia_Resolve_guess@0x59A4B0`；
- `PSBFile_ncbAutoRegister_Regist_guess@0x59A8D8`；
- `PSBFile_rootProperty_PropSet_guess@0x59B378`。

关键数据流压缩为十行：

```text
Load(octet): zero empty-filter.manager; Adopt; destroy filter on normal or Adopt landing
Load(string): zero empty-filter.manager; LoadStorage; destroy filter on normal or call landing
Contains: zero {owner,node} before the Dictionary category gate; destroy it on every exit
GetListAt/GetResourceData: zero {owner,node}; Resolve writes it only on success; then read node
Resolve: initialize current.node from root entries
Resolve hit: replace current.node from strict hidden-sret on both owner-null/non-null branches
Resolve success: read current.node only after releasing old out owner, then commit out->node
Regist: initialize delegate {className,classObject=null,hasConstructor=false} before RegistBegin
PropSet invoke: initialize {numparams=1,result=null,param=&incomingParam}
all stores/loads use SP-relative typed locals; only the two filter cleanup reads are landing-only
```

## 五个源码结构族

### 空 `OwnerFilter` 的 normal/landing 析构

`0x5982EC` 与 `0x598350` 都把 `filter.manager` 清零；分别在
`Adopt@0x5982FC` 与 `LoadStorage@0x598360` 返回后由 `0x598304/0x598368` 读取。
若调用抛出，则 LSDA 分别进入 `0x598474/0x598440`，再次读取同一 manager，非空才以
operation `3` 调 manager，随后继续 unwind。manager 已知为空并不授权删除这个源码临时；
它是 `std::function` 析构形状。

本地 `PSBRawFile.h:79,110-113` 保留 `OwnerFilter = std::function<...>` 与两个默认空参数；
`PSBRawFile.cpp:442-479` 的 `LoadStorage(path)` 和 `Adopt(data,size,{})` 分别产生这两只
临时。正常返回、异常展开和 manager-null gate 均与目标一致。

### 三只 default `PSBRawNode`

`0x5995F8`、`0x599A34`、`0x59A0D8` 的 `STP XZR,XZR` 都在业务门控前把 owner/node
同时清零。前者属于 Contains，并在 category gate、lookup hit/miss 与 diagnostic continuation
的每条出口统一 release；后两者先把地址交给 `Resolve`，成功后才在
`0x599A4C/0x59A0FC` 读取 node。

本地 `PSBRawFile.h:81,119,132-183` 让默认 holder owner 和 node 都为 null；
`PSBRawFile.cpp:268-277` 在 Contains 分类前构造 `value`；`PSBMedia.cpp:112-124,
149-162` 在 Resolve 前构造 `value`，顺序和短路边界一致。

### `Resolve` 的 current.node 状态机

`0x59A55C` 在 root owner retain 后写入 entries；strict getter hidden-sret 返回后，
`0x59A6D4` 与 `0x59A700` 分别覆盖 owner 非空/为空的两条 current.node 路径。
只有 loopState 表示末段成功时，`0x59A770` 才读取 current.node，并由下一条
`0x59A774` 写入 caller out；任意 miss 都不触碰 caller out。

本地 `PSBMedia.cpp:52-110` 保留 `current(*file, entries)`、同一 strict-return temporary
的 copy assignment、每轮局部 key/segment 逆序析构，以及只在 `last` 后执行
`value = current`。`PSBRawFile.h:81-106,132-183` 的首 `PSBFile` holder 子对象使
release-old、retain-new、copy-node 与 temporary release 顺序自然对应。

### NCB registrar delegate 默认状态

`0x59A908` 写 `hasConstructor=false`，`0x59A90C` 的 `STP X8,XZR` 同时写 className 与
null classObject；之后才依次调用 RegistBegin、member-registration body 和 RegistEnd。
本地 `ncbind.hpp:1843-1857` 的 delegate 构造器使用完全相同的三字段默认值，
`ncbind.hpp:1664-1682,2148-2157` 保留 delegate + registrar 两层栈对象和 Begin/body/End
作用域。

### read-only PropSet 的 params functor

通用非空 setter 支路在 `0x59B400` 产生 `1`，`0x59B404` 产生 incoming-param 槽地址；
随后 `0x59B410/0x59B414` 形成 `{numparams=1,result=null,param=&v9}`，立即调用
typed invoke `0x59B418 -> 0x59B48C`。本地 `ncbind.hpp:1092-1102,1153-1194` 保留
functor 字段和 instance acquisition，`ncbind.hpp:1468-1482` 精确传
`(_setter, 0, 1, &param, objthis)`。标准 `root` wrapper 的 setter 本身为空，故正常
root 写请求更早返回 access-denied；emitted 通用模板支路仍必须完整保留。

## IDB 处理

本轮没有删除这 20 条 DataRef，也没有把 `0xFF...` target 重命名成全局地址。fresh
`get_tid_name`、stack-frame type 和逐条反汇编一致证明它们是有效的 stroff 引用；删除会
损坏已经恢复的 `PSBOwnerFilter_arm64`、`PSBRawNode`、registrar delegate 和
`PSB_paramsFunctor_arm64` 字段关系。当前类型和反编译表达已经正确，无需额外 IDB 修订。

## 机械门禁

`verify_elf_surface.py` 新增完整 20-row manifest。每行固定
`{owner,site,word,stroff-target,SP-displacement,lane-width,lane-count,access,role,flow,
field-events}`，并独立验证：

1. owner 全部属于 114-address MANIFEST，target 全部位于 IDA stroff namespace；
2. actual word 必须是 SP-based unsigned single `LDR/STR/STRB` 或 64-bit offset `STP`；
3. scaled displacement、lane、读写方向和 ZR/X8/X9/X11 数据寄存器语义逐行一致；
4. 7 个 target、11 个 role、6 个 displacement 与 24 个字段事件计数完整；
5. normal-entry CFG 与所有 LSDA landing CFG 严格互斥，精确得到 `18+2`；
6. 840-byte canonical serialization 的 SHA-256 为
   `0f9d9a2570e0b65ec5333f6799043e477db761a7de0062aed88195f53ffe58d2`。

新增输出：

```text
stack_local_dref_surface=true owners=7 targets=7 rows=20 sites=19 semantic_fields=24 read=7 write=13 normal=18 landing=2 roles=11 sp_relative=true stroff_namespace=true sha256=true
```

完整 ELF verifier 与 114-report audit 均须继续通过。本轮结论为 **ALIGNED**，且不依赖
任何同版本源码、安装包旁证或非目标架构材料。
