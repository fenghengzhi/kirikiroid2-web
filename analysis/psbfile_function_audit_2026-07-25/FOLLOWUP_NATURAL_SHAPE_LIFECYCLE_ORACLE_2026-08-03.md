# 天然 Null / Array / Dictionary 枚举、dispatch-owner 与 native vtable 生命周期 oracle

## 范围与结论

本轮只使用 Android arm64 `libkrkr2.so` 和仓库已有、未修改的天然
`reference/xp3/logo_test/m2logo.mtn`。只读资产扫描新增 Null 与集合形状盘点，Android
runner 新增 `--shape-boundary`，固定一个 Null、一个 Array 和一个 Dictionary，并观察公共
TJS Variant、raw type category、dispatch 双引用和 owner 保活。没有生成、修改或复制出新
fixture。随后同一 oracle 增加完整 32-slot primary vtable、secondary native lifecycle
vtable、19 个 unsupported primary slot、NativeInstanceSupport、IsInstanceOf、EnumMembers、
GetCount、负索引、strict/non-strict miss 与逻辑失效边界；仍没有修改 `cpp/` 生产实现。

fresh Android 证据与当前生产实现逐项一致，未发现新的确定 GAP；总统计仍为
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## fresh Android 证据

本轮重新反编译：

- `PSBValueDispatch_CreateVariant_guess @ 0x59673C`
- `PSBValueDispatch_Release @ 0x597A40`
- `PSBValueDispatch_AddRef @ 0x597AC0`
- `PSBValueDispatch_ctor_guess @ 0x597AD4`
- `PSBFile_GetRootDispatch_guess @ 0x5981F8`
- `PSBRawNode_GetTypeCategory_guess @ 0x599554`
- `PSBValueDispatch_PropGet @ 0x597854`
- `PSBValueDispatch_NativeInstanceSupport @ 0x596D90`
- `PSBValueDispatch_Construct_primary @ 0x597A30`
- `PSBValueDispatch_Construct_secondary @ 0x597A38`
- `PSBValueDispatch_native_Invalidate_primary_nullsub @ 0x596F38`
- `PSBValueDispatch_native_Invalidate_secondary_nullsub @ 0x596F3C`
- `PSBValueDispatch_native_Destruct_primary_nullsub @ 0x597A28`
- `PSBValueDispatch_native_Destruct_secondary_nullsub @ 0x597A2C`
- `PSBValueDispatch_FuncCall @ 0x597A20`
- `PSBValueDispatch_FuncCallByNum @ 0x597A18`
- `PSBValueDispatch_PropSet @ 0x5976BC`
- `PSBValueDispatch_PropSetByNum @ 0x5976B4`
- `PSBValueDispatch_GetCountByNum @ 0x5975D8`
- `PSBValueDispatch_PropSetByVS @ 0x5975D0`
- `PSBValueDispatch_DeleteMember @ 0x596F48`
- `PSBValueDispatch_DeleteMemberByNum @ 0x596F40`
- `PSBValueDispatch_InvalidateByNum @ 0x596F04`
- `PSBValueDispatch_IsValidByNum @ 0x596EE8`
- `PSBValueDispatch_CreateNew @ 0x596EE0`
- `PSBValueDispatch_CreateNewByNum @ 0x596ED8`
- `PSBValueDispatch_Reserved1 @ 0x596ED0`
- `PSBValueDispatch_IsInstanceOfByNum @ 0x596E1C`
- `PSBValueDispatch_Operation @ 0x596E14`
- `PSBValueDispatch_OperationByNum @ 0x596E0C`
- `PSBValueDispatch_ClassInstanceInfo @ 0x596D88`
- `PSBValueDispatch_Reserved2 @ 0x596D80`
- `PSBValueDispatch_Reserved3 @ 0x596D78`
- `PSBValueDispatch_IsInstanceOf @ 0x596E24`
- `PSBValueDispatch_EnumMembers @ 0x596F50`
- `PSBValueDispatch_GetCount @ 0x5975E0`
- `PSBValueDispatch_PropGetByNum @ 0x5976C4`
- `PSBValueDispatch_Invalidate @ 0x596F0C`
- `PSBValueDispatch_IsValid @ 0x596EF0`

不超过 10 行的关键伪代码：

```text
category = GetTypeCategory(node)                  // tag 01/20/21 -> 0/6/7
if category == 0: result = Void
if category in {6, 7}: dispatch = new(0x30) PSBValueDispatch(file, node)
dispatch.ref = 1; dispatch.owner = file.owner; owner.ref++; valid = 1
temporary = Object(dispatch, dispatch)             // ref 1 -> 3
CopyRef(result, temporary)                         // ref 3 -> 5
destroy temporary; dispatch.Release()              // ref 5 -> 3 -> 2
AddRef(): return ++ref
Release(): if (--ref != 0) return ref; owner.Release(); delete dispatch
```

`GetTypeCategory@0x599554` 先读取 `tag - 1`；tag `0x01` 返回 0，tag `0x20`
返回 6，tag `0x21` 返回 7，未知 tag 才输出内部错误。构造器写入双 vptr、ref=1、owner、
node 与 valid=1；`Release` 只有在最后一个 dispatch 引用消失时才释放一次 owner。

集合边界伪代码：

```text
PropGet: null name -> -1002; Array "count" -> packed count
PropGet miss: strict -> -1001/preserve; non-strict -> Clear(result)/0
GetCount: invalid/owner-null -> -1006; membername != null -> -1002
GetCount: Array -> packed count + success; Dictionary -> -1002
PropGetByNum: only Array; negative index += count in W32
out-of-range + flag 0x400 -> -1001 and preserve result
out-of-range without 0x400 -> Clear(result), return 0
Invalidate: membername != null -> -1002; invalid -> -1006
Invalidate: valid=0, return 0; IsValid maps valid/nonvalid to 1/2
```

枚举边界伪代码：

```text
EnumMembers: invalid/owner-null -> -1006; non-collection -> -1002
construct name/flags/value/callback-result; flags=0; noValue=flag&0x100000
Array: signed i<count; name=ttstr(i); value=CreateVariant(tableEnd+UXTW(offset[i]))
Dictionary: encoded key order; DecodeName; fresh node + UXTW(W32(tableEnd+offset[i])) + 1
callback argc=noValue?2:3; callback this=closure.ObjThis ?: self
ignore callback status/result for loop control; empty container succeeds
destroy scratch/Variants in reverse order; return 0
```

类型判断边界伪代码：

```text
IsInstanceOf: membername != null -> -1002 before reading self
read node tag directly; do not inspect valid, owner, flag, hint, or objthis
category 4/5/6/7 maps to exact UTF-16 String/Octet/Array/Dictionary
exact case-sensitive match -> 1; known mismatch/default category -> 2
```

native-instance 边界伪代码：

```text
NativeInstanceSupport: flag != GETINSTANCE(2) -> -1002; preserve cache/output
if cachedClassId == 0: cachedClassId = RegisterNativeClass("PSBValueClass")
if cachedClassId != classid: return -1; preserve output
*output = secondary iTJSNativeInstance base; return 0
do not AddRef/Release or inspect valid, owner, or node
```

native vtable 生命周期伪代码：

```text
primary vptr=base+0x1A0B3D8; secondary vptr=base+0x1A0B4E8
secondary offset-to-top=-8; both RTTI slots=0
Construct primary/secondary: ignore all args; return 0
native Invalidate primary/secondary: ignore this; return
native Destruct primary/secondary: ignore this; return
all six paths do not write fields, retain/release, allocate, or inspect valid
```

unsupported dispatch 边界伪代码：

```text
for each of the 19 unsupported primary slots:
    ignore this and every explicit argument
    do not read/write output, hint, param, dispatch, owner, node, or valid
    do not allocate, retain/release, call, or throw
    return TJS_E_NOTIMPL (-1002), including after logical invalidation
```

## 本地生产实现对照

- `cpp/plugins/psbfile/main.cpp:20-26`：构造 `PSBValueDispatch` 时复制同一 `PSBFile`
  holder 与 node，复制 holder 即保留 owner。
- `cpp/plugins/psbfile/main.cpp:96-107`：非原子 `++refCount_` / `--refCount_`；零时
  `delete this`，成员析构再释放 owner。
- `cpp/plugins/psbfile/main.cpp:668-679`：Array/Dictionary 共用同一 allocation、
  `tTJSVariant(dispatch, dispatch)` 与 construction-reference Release，最终 closure 精确
  持有两份引用。
- `cpp/plugins/psbfile/main.cpp:120-205,208-273,286-326`：named count、named miss、
  负索引、strict/non-strict miss 与 Array-only GetCount 保留相同 packed/W32 数据流；
  Dictionary 数字访问与 count 失败均不写 result。
- `cpp/plugins/psbfile/main.cpp:371-443`：`EnumMembers` 保留四只 Variant、Array
  cached table-end、Dictionary fresh node、编码键顺序、2/3 参数以及 callback-this fallback；
  callback 返回不参与循环控制。
- `cpp/plugins/psbfile/main.cpp:339-369`：`IsInstanceOf` 先执行 membername gate，随后
  直接分类 node tag；不增加 valid/owner guard，四类 UTF-16 名称与 1/2 返回完全一致。
- `cpp/plugins/psbfile/main.cpp:455-471`：`NativeInstanceSupport` 精确保留 GETINSTANCE gate、
  `PSBValueClass` 进程期 class-id cache、mismatch `-1` 与
  `static_cast<iTJSNativeInstance *>(this)` secondary-base 调整；不读 valid/owner/node，
  不增减引用，也不在失败路径写 output。
- `cpp/plugins/psbfile/PSBDispatch.h:17-18,74-78` 与
  `cpp/core/tjs2/tjsInterface.h:326-335`：单一 `Construct/Invalidate/Destruct` override 由
  `iTJSDispatch2 + iTJSNativeInstance` 双继承生成 primary/secondary 两组 vslot。
- `cpp/plugins/psbfile/main.cpp:474-487`：`Construct` 唯一返回 `TJS_S_OK`；native
  `Invalidate/Destruct` 是空函数，不读写对象，也不改变 owner/dispatch 引用。
- `cpp/plugins/psbfile/PSBDispatch.h:26-29` 与
  `cpp/plugins/psbfile/main.cpp:110-118,277-284,330-336,445-452,509-555`：19 个 unsupported
  override 各自只有 `return TJS_E_NOTIMPL`，没有继承 `tTJSDispatch` 的 member-sensitive
  默认实现，也没有输出清空、安全 guard 或 valid 分支。
- `cpp/plugins/psbfile/main.cpp:486-503`：`IsValid/Invalidate` 只读写独立 valid 状态，
  不释放 owner 或 dispatch。
- `cpp/plugins/psbfile/PSBRawFile.cpp:137-165` 及 holder special members：raw owner
  继续采用 intrusive 生命周期；未发现 `shared_ptr`、额外容器或旁路 owner。

因此本轮不改生产代码；新增 oracle 用于把已对齐但此前缺少设备级可观察面的对象生命周期
固定下来。

## 天然样本 pin

物理文件 SHA-256：
`4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`。

| 类型 | offset / 路径 | 固定形状 | 公共/探针预期 |
|---|---|---|---|
| Null `0x01` | `0x5365`, `$/source/logo/metadata` | `01` | `tvtVoid(0)`；raw category 0 |
| Array `0x20` | `0x448B`, `$/object/m2cheeseware_logo/motion/back_white/priority[0]/content` | 30 项；table 34 B；前 32 B `200d1e0d00020406080a0c0e10121416181a1c1e20222426282a2c2e30323436`；末节点 `0x44E7=04` | `tvtObject(1)`；raw category 6；`[0] == 29`、`[-1] == 0` |
| Dictionary `0x21` | `0x4972`, `$/source/logo/icon` | 36 项；table 115 B；前 32 B `210d240d1c1d1e1f202122232425262728292a2b2c2d2f303132333435363738` | `tvtObject(1)`；raw category 7；`icon42/clip/left` 为全零 Real bits |

scanner 同时记录每份文档的首个 Null，以及 Array/Dictionary 的最小/最大 entry count、
packed table byte size、前缀和可达路径；它只读原文件，不制造边界物料。

## 生命周期断言

`tests/differential/oracle_runner/adapters/psbfile_load.py` 的新增路径执行：

1. TJS 构造 `PSBFile` 并取得固定 Array/Dictionary dispatch；随后清空 file global。
2. script global closure 持有 Object/ObjThis 两份引用；`TJS_GLOBAL` 的独立 PropGet
   结果再持有两份，因此 dispatch ref 至少为 4、owner ref 至少为 1。Full TJS 持久
   Variant 栈可能暂存额外引用，绝对初值只作诊断。
3. 固定 primary/secondary vptr 为 `base+0x1A0B3D8` / `base+0x1A0B4E8`、offset-to-top
   为 0/-8、RTTI 均为空，并逐槽核对全部 32 个 primary 入口及六个 lifecycle 入口。
   分别直调 primary/secondary
   `Construct@0x597A30/0x597A38`、native `Invalidate@0x596F38/0x596F3C` 与 native
   `Destruct@0x597A28/0x597A2C`：Construct 必须返回 0，完整 0x30-byte dispatch 与 owner
   ref 必须逐字节/逐值不变。
4. 逐一调用 19 个 unsupported primary slot，所有显式参数共同指向 64-byte writable
   sentinel。每个入口必须返回 `-1002`，保持 sentinel 与除 refcount 外的全部 dispatch
   结构字段不变；逐调用 refcount transition 保留为诊断，避免把长 trace 期间无关 TJS
   临时 Variant 的回收误记为 vslot 写入。
5. 直调 `NativeInstanceSupport@0x596D90`：非 GET flag 必须返回 `-1002`，且 class-id cache
   与 output sentinel 均不变；mismatch GET 必须按需注册 `PSBValueClass`、返回 `-1` 并保留
   sentinel；matching GET 必须返回借用的 `self+8` secondary base。三条路径均不得改变
   紧邻 matching GET 的 dispatch/owner refcount。
6. 直调 `IsInstanceOf@0x596E24`，要求精确类名返回 1、另一集合类名与大小写错误返回 2、
   非空 membername 在 classname=null 时先返回 `-1002`；非零 flag、hint 与 objthis 不影响
   结果且 hint 不得被写。
7. 从两只真实 TJS function Variant 取得 callback Object，把 closure ObjThis 强制置空后直调
   `EnumMembers@0x596F50`。value 模式必须按 packed 顺序收到全部名称、argc=3、flags=0，
   Array 30 项全为 Integer、Dictionary 36 项全为 Object，且 callback `this` 等于当前
   dispatch；no-value 模式必须保持同序并固定 argc=2。回调每次返回 `-777/-778`，枚举仍
   必须完整结束。
8. valid=1 时直调 PropGet/GetCount/PropGetByNum，分别验证 Array named/count=30、
   Dictionary GetCount `-1002`、Array `[-1]==0`、named/numeric non-strict miss 清
   Variant、strict miss 保留 Variant；Dictionary 数字访问固定 `-1001` 且保留 Variant。
9. named Invalidate 返回 `-1002` 且保持 valid=1；whole-object Invalidate 返回 0 并把
   `IsValid` 从 1 改为 2。重复失效以及失效后的 EnumMembers/GetCount 均返回 `-1006`，
   不得新增 callback 记录；`IsInstanceOf` 仍必须返回 1，matching NativeInstanceSupport
   仍必须返回同一 `self+8`；六个 native lifecycle slot 再调用一次仍须保持整个 dispatch
   与 owner ref 不变，19 个 unsupported slot 也必须再次全部返回 `-1002` 并保持同一
   sentinel/结构字段，因为这些入口都不读 valid/owner。whole-object Invalidate 的紧邻
   前后 dispatch/owner refcount 必须净变化 0。
10. 清空 script/callback globals 后，dispatch ref 必须至少下降 2 且仍不少于 2；owner
    ref 与清理前相等，node 必须等于 `owner.raw + pinned_offset`，valid 保持 0。
11. 直调 Android `AddRef@0x597AC0` / `Release@0x597A40`，要求相对当前实测值严格
    `+1/-1` 且 owner 不变；最后由独立输出 Variant 析构走 terminal release。

Null 另用独立 raw owner 调 `GetTypeCategory@0x599554`，避免把 public Void 结果错误地
解释为 owner-retaining 对象。

## 变更位置

- `tests/differential/python/scan_psbfile_natural_boundaries.py:215,1127-1129,1190-1201`
- `tests/differential/oracle_runner/adapters/psbfile_load.py:149,994,1148`
- `tests/differential/python/run_psbfile_load_adb.py:248-302,476,840-939`
- `tests/differential/oracle_runner/trace_targets.py:142-159`
- `tests/differential/oracle_runner/README.md`

trace 集合新增
`NativeInstanceSupport`、六个 primary/secondary native lifecycle 入口、19 个 unsupported
primary dispatch 入口以及
`IsInstanceOf/EnumMembers/GetCount/Invalidate/IsValid/AddRef/Release/GetTypeCategory`，并保留
factory/root/PropGet/PropGetByNum/CreateVariant、Variant CopyRef/析构和 raw-owner
析构链。

## 当前验证

- 四个相关 Python 文件 `py_compile`：通过。
- `verify_audit.py`：114/114、`ALIGNED:99 / EVIDENCE_LIMITED:15`，通过。
- runner `--help`：已出现 `--shape-boundary`。
- 宿主 pin 预检：Array `(tag=0x20,count=30,table=34)`、Dictionary
  `(tag=0x21,count=36,table=115)`、Null byte `01` 全部与常量一致；Array 末项由独立
  packed parser 定位到 `0x44E7`，完整节点为 `04`、值为 0。
- 宿主 ordered-member 预检：Array 名称精确为 `0..29` 且 30 项全为 `Integer`；
  Dictionary 依编码表从 `icon1, icon10, ...` 到 `icon9`，36 项全为 `Object`。
- shape trace 目标：51/51 地址唯一，`ADDR_NAMES/ARG_COUNTS/RETURN_KINDS` 三张映射完整；
  `NativeInstanceSupport@0x596D90` / `IsInstanceOf@0x596E24` /
  `EnumMembers@0x596F50` 分别固定为 4/6/4 个整数参数与 integer return；两只 Construct
  固定为 4 参数/int，两组 Invalidate/Destruct 固定为 1 参数/void；19 个 unsupported
  入口按各自 ABI 固定为 1..8 个整数参数与 integer return。
- 完整只读复扫：222 个物理候选、112 份唯一 decoded PSB、23,415,372 个可达节点、
  0 个解析失败；Null `0x01` 共 274,641 个，Array 共 4,655,138 个（0..16,997 项），
  Dictionary 共 1,916,004 个（0..453 项）；category-1 明确输出
  `boolean_tags_present=none`。
- 聚焦只读扫描：`m2logo.mtn` 共 3375 个可达节点、0 个解析失败；Null `0x01` 118 个，
  Array 337 个（0..30 项），Dictionary 531 个（1..36 项），既有 tag-`0x09` anchor
  `0x36F8` 仍命中。
- `git diff --check`：通过。
- 后续真实 Android ARM64 `--shape-boundary` 已补齐：raw 三组、Null、Array 与 Dictionary
  在无 trace 和单次全量 trace 中全部 `ok`。固定 APK/目标哈希、启动线程隔离、引用计数
  口径与事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

后续同一 shape 模式已增加 raw `GetRoot/Transfer` hidden-sret holder 生命周期，shape trace
扩为 53/53；fresh 证据、所有权断言与 harness `X8` ABI 验证见
[FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md)。
raw Dictionary lookup/alias follow-up 再把 shape trace 扩为 59/59；fresh 证据、天然
两级 miss pin 与完整 raw-node 引用链见
[FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md)。
ordered raw Dictionary keys follow-up 继续把 shape trace 扩为 63/63，并直接固定 Android
gnustl `vector<string>`/COW string 的容器与析构边界，见
[FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md)。
