# Follow-up：Android 目标内 RTTI、MDF 与 PSBMedia 源码面复扫

日期：`2026-08-03`。本轮继续只使用当前 Android ARM64 `libkrkr2.so` 与 IDA 数据库，
不使用 Android/iOS ARMv7，不访问外部私库、Git LFS 对象或同版本源码。目标不是重复证明
正常样本结果，而是继续寻找能区分私有类型名、helper factorization、allocator 生命周期和
复杂对象图的目标内正证据。

## 结论

- Android 目标内没有保留 `PSBRawOwner`、`PSBRawNode`、`PSBValueDispatch`、
  `PsbArray`、`DecodeName`、`GetDictionaryValue/Keys`、`GetResourceData`、
  `LoadStorage` 或 `EnsureContainer` 等私有类型/helper 名字符串；双继承 dispatch vtable
  前缀的 RTTI qword 也为 null。`"PSBFile"`、`"PSBFile.dll"` 与
  `"PSBValueClass"` 仍只证明 NCB/runtime 字面量，不证明私有 C++ 类型同名。
- fresh `decompile` 重新覆盖 `Load@0x598268`、`LoadStorage@0x598538`、
  `EnsureContainer@0x599E04` 与 `Resolve@0x59A4B0`。allocator/deallocator 不配对边界、
  read/Adopt 失败泄漏、adaptor-null 所有权、ttstr 临时量、raw-node assignment 与延迟写
  caller output 均和当前实现一致，没有新生产 GAP。
- 依据这些目标数据流，IDA 中为 `0x599E04/0x59A4B0` 共持久化 40 个局部变量名并保存
  IDB；后续反编译可直接区分 cache/adaptor/path segment 与 old/current/out owner 生命周期。
- 本轮不修改 `cpp/`，总判定保持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。目标内名字阴性结果只限制精确 token
  唯一化，不阻塞继续采用当前二进制支持最强的 `_guess` 候选。

## 目标内名字与 RTTI

对 IDA strings/globals 进行 broad regex 与下列精确字符串查询：

```text
PSBRawOwner, PSBRawNode, PSBValueDispatch, PsbArray,
CreateVariant, DecodeName, GetDictionaryValue, GetDictionaryKeys,
GetResource, GetResourceData, LoadStorage, EnsureContainer
```

精确查询全部为 0。broad `psb` 命中项只是无关 mangled symbol/脚本常量；已有 PSB 模块
字面量仍位于 `0x14C8D70..0x14C9086`。`PSBValueDispatch` primary/secondary vtable 之间的
ABI 前缀已由原始 qword 固定为 `offset-to-top=-8, RTTI=null`，primary 前缀同样没有
typeinfo。因而当前没有 Android 正证据支持把 guessed 私有类型/helper 名升级为确定名，
也不能仅凭 runtime 字面量把 `PSBValueDispatch` 强改名为 `PSBValueClass`。

## MDF 两只完整克隆

### `PSBFile_Load@0x598268`

```text
if value.type==String: fresh ttstr(value chars); LoadStorage; false→throw; destroy ttstr; return true
if value.type!=Octet: throw invalid-argument; helper 若返回则直接 return true
size/source = octet length/data
if size>=11 && magic==mdf: decoded=aligned_alloc(expected,4); uncompress
    failure: decoded 非空则 operator delete[]；随后回到原 octet copy
    success: size=actual；decoded 非空则作为 data，否则仍回到原 octet copy
fallback: data=aligned_alloc(size,4); memcpy(data,source,size)
if !Adopt(data,size,empty-filter): data 非空则 operator delete[]；throw；helper-return false
return true
```

这里不能照抄 Hex-Rays 把 invalid-type 分支排进外层 Octet block 的外观。完整 137 条指令
明确是 `0x5982A0 B.NE 0x5983A4 → 0x5983AC BL throw → 0x5983B0 MOV W0,#1`；Octet 读取
只从 `type==3` 的 fallthrough `0x5982A4` 开始。当前源码在 throw helper 后显式
`return true` 正确，伪代码重排不构成 GAP。

### `PSBFile_LoadStorage@0x598538`

```text
stream=TVPCreateStream(TVPGetPlacedPath(name),READ); null→false
if stream.GetSize()<9: destroy stream; return false
size=u32(stream.GetSize); data=aligned_alloc(size,4); stream.ReadBuffer(data,size)
// ReadBuffer 抛异常时只析构 stream，raw data 泄漏
若 data 是 mdf：decoded=aligned_alloc(expected,4); uncompress
failure: decoded 非空则 operator delete[]；保留原 data/size
success: size=actual；decoded 非空时 aligned_dealloc(data) 后替换 data
result=Adopt(data,size,filter); Adopt false 不释放 data
destroy stream; return result
```

本地 `PSBRawFile.cpp:21-48,442-513` 逐项保持这些不寻常边界：MDF decode 和 Octet
Adopt 失败走 `delete[]`，Storage 成功替换旧输入才走 `TJSAlignedDealloc`，ReadBuffer 抛出与
Storage Adopt false 均不回收 raw allocation。两个 target body 保留完整相同算法 clone；
这支持当前共享 `tryDecodeMdf_guess` 候选，但 Android O3 本身仍不能唯一证明 helper 名与
header/member/free token。

## PSBMedia 缓存与路径解析

### `PSBMedia_EnsureContainer_guess@0x599E04`

```text
slash=name.IndexOf('/'); slash<0→false; container=name.SubString(0,slash)
if file.type==Object && cachedContainer==container: destroy container; return true
file=new PSBFile(owner=null); ok=file.LoadStorage(container, empty-filter)
if !ok: destroy file/owner; destroy container; return false
adaptor=CreateAdaptor(file); temp Variant 初始 Void
if adaptor: SetObject(adaptor,adaptor) 建立引用，再 Release construction ref
self.file=temp；销毁 temp
self.container=container；销毁 container；return true
// adaptor==null 仍提交 Void/cache name，且 raw file 不再由本函数回收
```

本地 `PSBMedia.cpp:19-49` 的 slash gate、cache 命中、new/load/delete、adaptor temporary、
Void fallback 与 cache 更新顺序一致；尤其没有把 adaptor-null 改成失败或补回收。

### `PSBMedia_Resolve_guess@0x59A4B0`

```text
dispatch=self.file.AsObjectNoAddRef; file=GetNativeInstance(dispatch)
current=(file holder, file.owner.header.entries)；无 null/type/adaptor 成功保护
firstSlash=name.IndexOf('/'); -1→false 且 out 不变
rest=name.SubString(firstSlash+1,-1)
loop: slash=rest.IndexOf('/'); segment=last ? rest : rest.SubString(0,slash)
      非 last 时以新的 SubString 替换 rest；保留 segment temporary 的 AddRef/Release
      同一 narrow key 先 ContainsDictionaryKey，再 strict getter
      miss→false 且 out 不变；hit→current = strict-result，保留临时 owner ref no-op
      销毁 key/segment；last→out=current（Release-old→copy→AddRef→node）并 true
```

本地 `PSBMedia.cpp:52-109` 按相同局部量次序与作用域复刻。fresh target 仍明确显示：
root `current` 在 slash gate 前建立；non-last substring 有一次额外 ref no-op；同一个 narrow
buffer贯穿 contains/strict lookup；严格 getter临时量赋给 current 后出现 incoming-zero
删除边界；caller output 只在最后一段成功且 key/segment 已销毁后更新。

## IDB 质量推进

`0x599E04` 的 17 个局部现在包括 `slashIndex`、`requestedContainer`、
`cachedContainerStorage`、`newFile`、`loadSucceeded`、`createdAdaptor/adaptor`、
`failedFileOwner` 与 container retain/release 状态。`0x59A4B0` 的 23 个局部现在包括
`fileVariant/dispatchObject/nativeFileHolder`、`currentOwner/entries/current/next`、
`firstSlash/remainingPath/slash/segmentStorage/segmentKey`、`oldCurrentOwner`、
`oldOutOwner/currentOwnerForOut` 和各 release 后 refcount。所有 rename 均以 fresh ctree
重新验证，随后保存当前 IDB；没有把 runtime 字面量或本地类名反向写成确定函数名。

## 验证

- Mac Debug `psbfile-dll`：`598 assertions in 11 test cases`，通过。
- Mac Debug `motionplayer-dll`：`1386 assertions in 21 test cases`，通过。
- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`，通过。
- Web Release `psbfile`：目标最新，无待构建项。
- `verify_audit.py`：114/114，`ALIGNED:99,EVIDENCE_LIMITED:15`。
- `verify_elf_surface.py`：114 FDE；39 LSDA/232 call-sites=`77/80/75`；raw 子集
  51=`18/16/17`。
- `git diff --check`：通过。
