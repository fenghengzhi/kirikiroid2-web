# Android `libkrkr2.so` 内嵌 psbfile 完整 emitted-function 关系树（2026-07-25）

## 证据边界与计数口径

唯一权威来源是 Android kirikiroid2 `libkrkr2.so` 内嵌的 psbfile 实现；不查看、
引用或从任何外部 `psbfile.dll` 推导。树中出现的 `"PSBFile.dll"` 仅是同一
`libkrkr2.so` 内的 NCB 模块注册字面量。

本树以 `analysis/psbfile_function_coverage_2026-07-19.md` 的 emitted-function
manifest 为 canonical 集合：连续主实现簇 111 个函数、该源码触发的 vector 扩容慢路径
1 个、两只静态初始化入口 2 个，共 **114 个**。每个 `0x...` 地址在 canonical 树中只出现
一次；后续关系树使用 `@地址` 交叉引用，不重复计数。

**本文的“完整/所有函数”严格指 Android stripped/O3 二进制中可枚举、可归属于 psbfile
的全部独立 emitted 入口**；不把已经内联而没有独立入口的源码 helper、外部 TJS/引擎
callee 或整个 libstdc++ 调用闭包冒充成 PSB 专属函数。每个节点的完整分支、默认值和异常
边界仍以 coverage manifest 及其链接的 fresh 反编译记录为准；本文负责关系索引。

`_guess` 表示名字来自 Android 行为、vtable、调用链和本地交叉参照，而不是二进制保留的
原始 C++ 符号。节点分类标记如下：

- `[vslot]`：vtable 槽；
- `[secondary]`：secondary-base 重复入口或 adjustor thunk；
- `[ncb]`：ncbind/NCB 模板生成边界；`callback`、`vslot` 只是该分类的限定词；
- `[stl]`：该源码触发的标准库模板实例；
- `↪ @地址`：指向 canonical 树中另一节点的关系引用。

canonical 树的缩进只表示**归属/分类**，不是隐含调用。关系树使用以下显式边；逗号后的
`first`、`second`、`filter non-empty` 等文字只是路径限定词，不改变边类型：

- `[construct]`：构造对象或把对象登记进生命周期链；
- `[register]`：安装 wrapper/member 元数据，不表示当场调用所绑定的 callback；
- `[index only]`：只把对象归入模块索引，不执行其 callback；
- `[direct]`：Android 中保留下来的直接调用；`callback` 仅说明直接目标的角色；
- `[inline]`：源码级调用关系可证，但目标函数体在该调用点被内联；
- `[inline/helper shape]`：调用点只证明存在与独立 helper 相同的 emitted 形状；不是 direct
  call，也不能唯一证明 Android 原源码是否调用该 helper；
- `[vdispatch]`：经 vtable 或其他运行时槽进行的间接调用；
- `[binds runtime callback]` / `[binds runtime chain]`：注册元数据把 wrapper 绑定到之后才
  执行的 callback 或 callback 链，不表示注册阶段发生调用；
- `[registered member-pointer BLR]`：wrapper 随后经注册的 C++ member pointer 以 `BLR`
  间接进入目标，不是静态 direct-call 边；
- `[PLT]`：经 PLT/重定位边界进入目标实现；
- `[lifetime]`：只表示对象最终析构/释放关系，不表示当前语句直接调用该入口；
- `[owns]`、`[retains]`、`[contains]`、`[borrows]`：分别表示独占所有权、保留引用、内嵌
  子对象和非 owning 借用关系。

## 总览

```text
Android libkrkr2.so::embedded psbfile（114）
├─ A. PSBValueDispatch / packed dispatch ABI（42）
├─ B. packed name 反向解码（1）
├─ C. PSBFile typed NCB 前段 / factory / root（10）
├─ D. raw PSBFile / PSBRawOwner / PSBRawNode（19）
├─ E. PSBMedia singleton / psb: storage gateway（17）
├─ F. typed NCB 自动注册与 wrapper 尾链（22）
├─ G. vector<string> 容量增长慢路径（1）
└─ H. 静态初始化入口（2）
```

本地源码主映射：

| 分支 | 本地实现 |
| --- | --- |
| A、C、F | `cpp/plugins/psbfile/main.cpp`、`PSBDispatch.h`、`cpp/core/plugin/ncbind.hpp` |
| B、D | `cpp/plugins/psbfile/PSBRawFile.cpp/.h`、`PSBPackedInternal.h` |
| E | `cpp/plugins/psbfile/PSBMedia.cpp/.h`、`PSBMediaRegistry.cpp` |
| G | `PSBRawNode::GetDictionaryKeys()` 中的 `std::vector<std::string>` |
| H | `NCB_REGISTER_CLASS(PSBFile)`、`NCB_PRE_REGIST_CALLBACK(initPsbFile)` 与 class-info 静态对象 |

## Canonical 函数树（114 个地址，无重复）

```text
Android libkrkr2.so::embedded psbfile
│
├─ A. PSBValueDispatch / packed dispatch ABI（42）
│  │
│  ├─ packed Dictionary 公共 helper（2）
│  │  ├─ 0x59641C  PSB_FindNameIndex_guess
│  │  │              local: detail::FindNameIndex_guess
│  │  └─ 0x59659C  PSB_FindDictionaryValueOffset_guess
│  │                 local: detail::FindDictionaryValueOffset_guess
│  │
│  └─ PSBValueDispatch
│     ├─ 构造与私有 helper（5）
│     │  ├─ 0x597AD4  PSBValueDispatch_ctor_guess
│     │  │              local: PSBValueDispatch(fileHolder, node)
│     │  ├─ 0x59673C  PSBValueDispatch_CreateVariant_guess
│     │  │              local: CreateVariant_guess(result, node)
│     │  │              [inline] ↪ @596BC4, @596C70
│     │  ├─ 0x596BC4  PSBValueDispatch_getString_guess
│     │  ├─ 0x596C70  PSBValueDispatch_getResource_guess
│     │  └─ 0x5975C0  PSBValueDispatch_decodeName_guess
│     │                 [direct] ↪ @597B1C
│     │
│     ├─ primary vtable @1A0B3D8（32 slots）
│     │  ├─ 0x597AC0  [vslot 00] AddRef
│     │  ├─ 0x597A40  [vslot 01] Release
│     │  ├─ 0x597A20  [vslot 02] FuncCall
│     │  ├─ 0x597A18  [vslot 03] FuncCallByNum
│     │  ├─ 0x597854  [vslot 04] PropGet
│     │  │              [direct] ↪ @59641C, @59659C, @59673C
│     │  ├─ 0x5976C4  [vslot 05] PropGetByNum
│     │  │              [direct] ↪ @59673C
│     │  ├─ 0x5976BC  [vslot 06] PropSet
│     │  ├─ 0x5976B4  [vslot 07] PropSetByNum
│     │  ├─ 0x5975E0  [vslot 08] GetCount
│     │  ├─ 0x5975D8  [vslot 09] GetCountByNum
│     │  ├─ 0x5975D0  [vslot 10] PropSetByVS
│     │  ├─ 0x596F50  [vslot 11] EnumMembers
│     │  │              [direct] ↪ @59673C, @597B1C
│     │  ├─ 0x596F48  [vslot 12] DeleteMember
│     │  ├─ 0x596F40  [vslot 13] DeleteMemberByNum
│     │  ├─ 0x596F0C  [vslot 14] Invalidate(dispatch overload)
│     │  ├─ 0x596F04  [vslot 15] InvalidateByNum
│     │  ├─ 0x596EF0  [vslot 16] IsValid
│     │  ├─ 0x596EE8  [vslot 17] IsValidByNum
│     │  ├─ 0x596EE0  [vslot 18] CreateNew
│     │  ├─ 0x596ED8  [vslot 19] CreateNewByNum
│     │  ├─ 0x596ED0  [vslot 20] Reserved1
│     │  ├─ 0x596E24  [vslot 21] IsInstanceOf
│     │  ├─ 0x596E1C  [vslot 22] IsInstanceOfByNum
│     │  ├─ 0x596E14  [vslot 23] Operation
│     │  ├─ 0x596E0C  [vslot 24] OperationByNum
│     │  ├─ 0x596D90  [vslot 25] NativeInstanceSupport
│     │  │              lazy native class id = `PSBValueClass`
│     │  ├─ 0x596D88  [vslot 26] ClassInstanceInfo
│     │  ├─ 0x596D80  [vslot 27] Reserved2
│     │  ├─ 0x596D78  [vslot 28] Reserved3
│     │  ├─ 0x597A30  [vslot 29] Construct（main-vtable duplicate）
│     │  ├─ 0x596F38  [vslot 30] native Invalidate no-op（nullsub_258）
│     │  └─ 0x597A28  [vslot 31] native Destruct no-op（nullsub_260）
│     │
│     └─ secondary iTJSNativeInstance vtable @1A0B4E8（3）
│        ├─ 0x597A38  [secondary 0] Construct duplicate/thunk
│        ├─ 0x596F3C  [secondary 1] native Invalidate duplicate（nullsub_259）
│        └─ 0x597A2C  [secondary 2] native Destruct duplicate（nullsub_261）
│
├─ B. packed name 反向解码（1）
│  └─ 0x597B1C  PSB_DecodeName_guess
│                 local: detail::DecodeName_guess
│                 callers: @596F50, @5975C0, @598E64, @5999F4
│
├─ C. PSBFile typed NCB 前段 / factory / root（10）
│  ├─ ncbClassInfo<PSBFile> emitted surface（7）
│  │  ├─ 0x597E98  [ncb] GetName_guess
│  │  ├─ 0x597EA8  [ncb] GetID_guess
│  │  ├─ 0x597EB8  [ncb] GetClassObject_guess
│  │  ├─ 0x597EC8  [ncb] IsSubClass_guess
│  │  ├─ 0x597ED0  [ncb] Set_guess
│  │  ├─ 0x597F08  [ncb] Clear_guess
│  │  └─ 0x597F24  [ncb] InfoCtor_guess
│  │
│  └─ NCB_REGISTER_CLASS(PSBFile) 前段（3）
│     ├─ 0x597F38  [ncb] PSBFile_ncb_registerMembers_guess
│     │              Factory → root Property → load Method
│     │              [direct] ↪ @59AEEC
│     ├─ 0x5980F4  [ncb callback] PSBFile_Factory_guess
│     │              local: ::PSBFileFactory; [direct] ↪ @598268
│     └─ 0x5981F8  [ncb callback] PSBFile_GetRootDispatch_guess
│                    local: PSBFile::GetRootDispatch
│                    [inline] constructs dispatch shape ↪ @597AD4
│
├─ D. raw PSBFile / PSBRawOwner / PSBRawNode（19）
│  ├─ PSBFile load / holder API（5）
│  │  ├─ 0x598268  PSBFile::Load
│  │  │              String → @598538; Octet → @598708
│  │  ├─ 0x598538  PSBFile::LoadStorage_guess
│  │  │              [direct] ↪ @598708
│  │  ├─ 0x598708  PSBFile::Adopt_guess
│  │  │              [inline] owner ctor ↪ @598AAC；仅 filter 非空时 strict refresh ↪ @598960
│  │  ├─ 0x598A3C  PSBFile::GetRoot_guess
│  │  └─ 0x598A64  PSBFile::Transfer_guess
│  │
│  ├─ PSBRawOwner 生命周期 / header view（3）
│  │  ├─ 0x598AAC  PSBRawOwner_ctor_guess
│  │  ├─ 0x598960  PSBRawOwner_Refresh_guess
│  │  └─ 0x598B3C  PSBRawOwner_dtor_guess
│  │
│  └─ PSBRawNode API 与 dictionary vector（11）
│     ├─ 0x598C58  GetDictionaryValueStrict_guess
│     │              [direct] ↪ @59641C, @59659C
│     ├─ 0x598D58  GetDictionaryValue_guess
│     │              [direct] ↪ @59641C, @59659C
│     ├─ 0x598E44  IsValid_guess
│     ├─ 0x598E64  GetDictionaryKeys_guess
│     │              [direct] ↪ @597B1C; [stl] ↪ @599174, @59B7E8
│     ├─ 0x599174  [stl] std::vector<std::string>::reserve
│     ├─ 0x5995D8  ContainsDictionaryKey_guess
│     │              [direct] ↪ @598D58
│     ├─ 0x598B58  GetString_guess
│     ├─ 0x5992E8  GetDouble_guess
│     ├─ 0x599438  GetInt_guess
│     ├─ 0x599554  GetTypeCategory_guess
│     └─ 0x5996E4  GetResource_guess
│
├─ E. PSBMedia singleton / psb: storage gateway（17）
│  ├─ pre-register / process lifetime（1）
│  │  └─ 0x59849C  PSBFile_preRegister_guess / local initPsbFile
│  │                 guarded process-lifetime PSBMedia* → TVPRegisterStorageMedia
│  │
│  ├─ PSBMedia vtable @1A0B510（11）
│  │  ├─ 0x5997F0  [vslot 00] PSBMedia_completeDestructor_guess
│  │  ├─ 0x599830  [vslot 01] PSBMedia_deletingDestructor_guess
│  │  ├─ 0x599878  [vslot 02] PSBMedia_AddRef_guess（non-atomic）
│  │  ├─ 0x599888  [vslot 03] PSBMedia_Release_guess
│  │  ├─ 0x5998A8  [vslot 04] PSBMedia_GetName_guess → `psb`
│  │  ├─ 0x5998BC  [vslot 05] NormalizeDomainName no-op（nullsub_262）
│  │  ├─ 0x5998C0  [vslot 06] NormalizePathName no-op（nullsub_263）
│  │  ├─ 0x5998C4  [vslot 07] CheckExistentStorage_guess
│  │  │              [direct] ↪ @599E04, @59A0B4
│  │  ├─ 0x59993C  [vslot 08] Open_guess
│  │  │              [direct] ↪ @599E04, @59A0B4
│  │  ├─ 0x5999F4  [vslot 09] GetListAt_guess
│  │  │              [direct] ↪ @599E04, @59A4B0, @597B1C
│  │  └─ 0x599DD8  [vslot 10] GetLocallyAccessibleName_guess
│  │
│  └─ private / shared data flow（5）
│     ├─ 0x599E04  EnsureContainer_guess
│     │              [direct] ↪ @59A284, @598538, @59A330
│     ├─ 0x59A0B4  GetResourceData_guess
│     │              [direct] ↪ @59A4B0；[complete inline clone] ↪ @5996E4；本地已恢复
│     │              source call，但 Android emitted body 没有对 @5996E4 的 BL
│     ├─ 0x59A284  ttstr_IndexOfChar_guess
│     │              shared char-index helper; used by @599E04 and @59A4B0
│     ├─ 0x59A330  [ncb] ncbInstanceAdaptor<PSBFile>::CreateAdaptor_guess
│     │              null: native PSBFile 未被认领而泄漏，`_file=Void`，但 `_container`
│     │              仍更新且 EnsureContainer 返回 true；同 container 下次会重新加载
│     └─ 0x59A4B0  Resolve_guess
│                    [direct] ↪ @59A284, @5995D8, @598C58
│
├─ F. typed NCB 自动注册与 wrapper 尾链（22）
│  ├─ class autoreg 生命周期（3）
│  │  ├─ 0x59A8D8  [ncb] AutoRegister::Regist_guess
│  │  │              Begin @59AA84 → body @597F38 → End @59AD84
│  │  ├─ 0x59A968  [ncb] AutoRegister::Unregist_guess
│  │  │              unregister body @597F38 → class-info Clear @597F08
│  │  └─ 0x59AA84  [ncb] RegistBegin_guess
│  │                 create class object / class id / register empty finalize method
│  │
│  ├─ native-instance adaptor / registrar（8）
│  │  ├─ 0x59ABD8  [ncb] instance-adaptor CreateEmpty/ctor_guess
│  │  ├─ 0x59AC04  [ncb] PSBFile_ncbFinalizeEmptyCallback_guess
│  │  ├─ 0x59AC0C  [ncb] PSBFile_ncbInstanceAdaptor_Invalidate_guess（cleanup/reset）
│  │  ├─ 0x59AC7C  [ncb] PSBFile_ncbInstanceAdaptor_completeDestructor_guess
│  │  ├─ 0x59AD08  [ncb] PSBFile_ncbInstanceAdaptor_deletingDestructor_guess
│  │  ├─ 0x59AD84  [ncb] RegistEnd_guess
│  │  ├─ 0x59AEE4  [ncb] PSBFile_ncbDummyConstructorNotImpl_guess
│  │  └─ 0x59AEEC  [ncb] RegistItem_guess
│  │                 registers factory / root property / load method metadata
│  │
│  ├─ Factory wrapper（2）
│  │  ├─ 0x59B14C  [ncb vslot] factory FuncCall_guess
│  │  │              void-shell path used by @59A330; normal path ↪ @5980F4
│  │  └─ 0x59B268  [ncb vslot] PSBFile_ncbFactory_deletingDestructor_guess
│  │
│  ├─ `root` read-only Property wrapper（5）
│  │  ├─ 0x59B28C  [ncb vslot] root PropGet_guess → @59B48C
│  │  ├─ 0x59B378  [ncb vslot] root PropSet_guess（access denied）
│  │  ├─ 0x59B460  [ncb vslot] PSBFile_rootProperty_deletingDestructor_guess
│  │  ├─ 0x59B484  [ncb vslot] root GetFlags_guess → 0
│  │  └─ 0x59B48C  [ncb] root typed Invoke_guess → @5981F8
│  │
│  └─ `load` Method wrapper（4）
│     ├─ 0x59B570  [ncb vslot] load FuncCall_guess → @59B708
│     ├─ 0x59B6DC  [ncb vslot] PSBFile_loadMethod_deletingDestructor_guess
│     ├─ 0x59B700  [ncb vslot] load GetFlags_guess → 0
│     └─ 0x59B708  [ncb] load CopyFirstArgument_guess
│                    @59B570 随后经已注册 member pointer BLR → @598268；
│                    @59B708 本体不调用 @598268
│
├─ G. vector<string> 容量增长慢路径（1）
│  └─ 0x59B7E8  [stl] std::vector<std::string>::_M_emplace_back_aux<std::string &>
│                 caller: @598E64 when finish == end_of_storage
│
└─ H. 静态初始化入口（2）
   ├─ 0x42CEF8  PSBFile_ncbClassInfo_static_init
   │              guarded default construction of class-info state
   └─ 0x42CF28  psbfile_static_init
                  constructs class autoreg + pre-register callback autoreg
                  class Regist/Unregist ↪ @59A8D8/@59A968
                  pre-register callback ↪ @59849C
```

## 静态构造与 NCB 模块执行关系树

### 静态构造顺序

```text
.init_array
├─ [construct] @42CEF8  class-info state ctor
└─ [construct] @42CF28  two autoreg objects
   ├─ (1) class autoreg object
   └─ (2) pre-register callback autoreg object

两只对象都只在这里构造并插入全局 autoreg 链；静态初始化阶段不调用注册 callback。
```

### 后续模块执行顺序

```text
AllRegist
└─ [index only] 将两只 autoreg 对象归入字面模块名 `PSBFile.dll`
   └─ LoadModule
      ├─ [vdispatch, first] PreRegist callback @59849C initPsbFile
      │  └─ [direct] guarded PSBMedia* → TVPRegisterStorageMedia
      └─ [vdispatch, second] class Regist @59A8D8
         ├─ [direct] @59AA84 RegistBegin
         ├─ [direct] @597F38 registerMembers
         │  ├─ [register] outer Factory wrapper @59B14C
         │  │  └─ [binds runtime callback] @5980F4
         │  ├─ [register] outer root Property wrapper @59B28C
         │  │  └─ [binds runtime chain] @59B48C → @5981F8
         │  └─ [register] outer load Method wrapper @59B570
         │     ├─ [direct] @59B708 copy first argument
         │     └─ [registered member-pointer BLR] @598268
         └─ [direct] @59AD84 RegistEnd

global Unregist lifecycle（若通用反注册流程调用；未证明 PSB-specific unload caller）
└─ [vdispatch] class Unregist @59A968
   └─ [inline/helper shape] 删除同三 outer members 后内联 class-info Clear 同形体
      ↪ @597F08（非 direct call）
```

## 主要运行数据流树

```text
TJS: new PSBFile(value)
└─ [vdispatch] @59B14C factory FuncCall
   └─ [direct] callback @5980F4 PSBFileFactory
      └─ [direct] @598268 PSBFile::Load
         ├─ String [direct] → @598538 LoadStorage → @598708 Adopt
         └─ Octet  [direct] → @598708 Adopt
            ├─ [inline] owner/header ctor shape @598AAC
            ├─ [inline, filter non-empty] Refresh shape @598960
            └─ [lifetime] final owner destruction @598B3C

TJS: file.root
└─ [vdispatch] @59B28C root PropGet
   └─ [direct] @59B48C typed invoke
      └─ [direct] callback @5981F8 GetRootDispatch
         └─ [inline] PSBValueDispatch(fileHolder,node) shape ↪ @597AD4

TJS: dispatch.member
├─ named access @597854 PropGet
│  ├─ [direct] @59641C FindNameIndex
│  ├─ [direct] @59659C FindDictionaryValueOffset
│  └─ [direct] @59673C CreateVariant_guess
│     ├─ [inline] @596BC4 getString
│     └─ [inline] @596C70 getResource
├─ numeric access @5976C4 PropGetByNum [direct] → @59673C
└─ enumeration @596F50 EnumMembers
   ├─ [direct] @597B1C DecodeName
   └─ [direct] @59673C CreateVariant_guess

raw-node Dictionary
├─ strict @598C58 ─┬─[direct]→ @59641C
│                  └─[direct]→ @59659C
├─ try @598D58 ───┬─[direct]→ @59641C
│                  └─[direct]→ @59659C
├─ contains @5995D8 ─[direct]→ @598D58
└─ keys @598E64
   ├─ [direct] @597B1C DecodeName
   ├─ [PLT] @599174 vector::reserve
   └─ capacity full [PLT] → @59B7E8 emplace slow path

psb: storage media
├─ exists @5998C4 ─┬─[direct]→ @599E04 EnsureContainer
│                  └─[direct]→ @59A0B4 GetResourceData
├─ open @59993C ───┬─[direct]→ @599E04
│                  └─[direct]→ @59A0B4
│                     └─ new caller-owned tTVPMemoryStream over borrowed Block
└─ list @5999F4 ───┬─[direct]→ @599E04
                   ├─[direct]→ @59A4B0 Resolve
                   └─[direct]→ @597B1C DecodeName

@599E04 EnsureContainer
├─ [direct] @59A284 IndexOf('/')
├─ [direct] @598538 LoadStorage
└─ [direct] @59A330 CreateAdaptor
   └─ [vdispatch] class-object CreateNew → @59B14C void-shell factory path

@59A4B0 Resolve
├─ [direct] @59A284 split path
├─ [direct] @5995D8 ContainsDictionaryKey
└─ [direct] @598C58 GetDictionaryValueStrict
```

## 对象生命周期与持有关系树

```text
PSBFile holder
├─ [retains] one PSBRawOwner* (intrusive, non-atomic; holders may share it)
├─ copy: AddRef incoming owner
└─ Transfer_guess
   ├─ hidden-result copy 成功后，才 clear/release source holder
   └─ hidden-result copy 抛异常时，source 不清空，未完成构造的 hidden-sret 不析构

PSBRawOwner
├─ [owns] raw allocation
├─ [contains] inline PSBRawHeader view
└─ final Release → aligned data dealloc → owner delete

PSBRawNode
├─ [contains/reuses] PSBFile-compatible holder first subobject → [retains] PSBRawOwner*
├─ [contains] independent raw node pointer
├─ GetRoot/Dictionary child returns create retained node copies
└─ implicit copy assignment: PSBFile assignment（Release old → copy owner → AddRef）→ copy node

PSBValueDispatch : iTJSDispatch2, iTJSNativeInstance
├─ primary vptr + secondary vptr
├─ refCount = 1
├─ [contains] PSBRawNode value_ → [retains] owner
└─ Release-to-zero
   ├─ restore both derived vptrs
   ├─ release owner through value_.file_ / PSBFile destructor
   └─ delete this

PSBMedia (process-lifetime pointer)
├─ non-atomic refcount
├─ [owns] `_file` Variant → current NCB adaptor dispatch
│  └─ adaptor [owns] raw PSBFile* unless sticky
└─ [owns] `_container` ttstr
   destruction order: container → file Variant → base

PSBMedia::Open result
└─ caller [owns] tTVPMemoryStream object
   └─ stream [borrows] resource Block; does not retain PSBFile / PSBRawOwner

ncbInstanceAdaptor<PSBFile>
├─ `{vptr, PSBFile*, sticky}`
├─ invalidate/destruct: instance && !sticky → delete PSBFile
└─ reset pointer + sticky flag
```

## 完整性校验

- A=42，B=1，C=10，D=19，E=17，F=22，G=1，H=2；合计 **114**。
- canonical 树内共有 114 个 `0x...` 地址、114 个唯一地址，跨组重复为 0。
- A–F 与 IDA 中 `@59641C..@59B708` 的 111-function 集合双向相等。
- A–G 等于上述 111 个函数加 `@59B7E8`；H 的两只入口分别由相邻
  `.init_array` 项直接引用。
- 该结论证明 emitted-function 拓扑完整；被 O3 内联掉的 helper 原名、精确抽取边界和
  等价源码拼写仍不能由 stripped 二进制唯一恢复。
- 逐函数**总判定**为 `ALIGNED=99`、`EVIDENCE_LIMITED=15`；这是每个入口综合六维后的
  verdict，不能与任一单独维度的计数互换。
- 单看**调用链维度**则为 `MATCH=113`、`EVIDENCE_LIMITED=1`；唯一受限入口是
  `GetResourceData_guess@0x59A0B4`。Android emitted body 是
  `GetResource_guess@0x5996E4` 的完整 inline clone，同 release iOS 又保留 source call，
  因而本地已恢复该调用；但目标 O3 单独仍不能唯一恢复精确源码 token，所以该维继续受限。
