# Follow-up：非 switch 间接调用 ABI surface

日期：`2026-08-03`。本轮继续只读权威 Android ARM64 `libkrkr2.so`，不读取或恢复
Android/iOS ARMv7 物料，也不访问已停用私库或 Git LFS 对象。目标是把先前 callsite
surface 中的 `45 BLR + 1 BR` 从“已分类”推进到“目标寄存器生产者可机械复核”。没有修改
`cpp/` 或测试物料。

## 结论

- 46 个非 switch 间接 transfer 的目标寄存器全部有闭合生产者；没有来源不明的寄存器。
- 其中 44 个由固定偏移 `LDR Xd,[Xn,#imm]` 产生，2 个由 Itanium C++
  pointer-to-member 的 register-offset `LDR` 产生。
- 18 类语义角色完整覆盖 callback、`std::function` manager/invoker、stream、storage
  lister、`iTJSDispatch2`、NCB item interface、adaptor、global registration、析构和 typed
  member wrapper。
- 逐类对照当前 `PSBRawFile.cpp`、`PSBMedia.cpp`、`StorageIntf.h`、`tjs.h`、
  `tjsInterface.h`、`ncbind.hpp` 与 `ncb_invoke.hpp` 后未发现生产 GAP，统计保持
  `ALIGNED=99 / EVIDENCE_LIMITED=15 / HAS_GAP=0`。

## 新鲜 IDA 证据

先从 114 个 MANIFEST FDE 逐指令枚举 `BLR`，并只把 `PSBMedia::Release@0x59989C` 的
非 switch `BR` 纳入集合。对每个站点在 owner FDE 内反向寻找最后一次目标寄存器写入，
再读取 producer word。重新反编译了：

```text
PSBFile::Load@598268                 PSBFile::LoadStorage@598538
PSBFile::Adopt@598708                PSBMedia::EnsureContainer@599E04
ncbInstanceAdaptor::CreateAdaptor@59A330
ncbRegistNativeClass::RegistEnd@59AD84
ncbRegistNativeClass::RegistItem@59AEEC
ncb factory wrapper@59B14C          root typed Invoke@59B48C
load typed FuncCall@59B570
```

反编译与原始指令共同固定以下关键形状：

```cpp
if (filter.manager) filter.manager(&filter, &filter, 3);
if (filter.manager) filter.invoker(&filter, owner);
size = stream->GetSize();
stream->deleting_destructor();
target = isVirtual ? *(vtable + functionOrVtableOffset)
                   : functionOrVtableOffset;
result = target(adjustedThis, ...);
```

## 46 个 producer 的闭合分类

| 角色 | 数量 | transfer → producer | 源码/ABI 对照 |
| --- | ---: | --- | --- |
| dispatch release | 2 | `596958←596954 [vptr+08]`; `59B524←59B520 [vptr+08]` | `iTJSDispatch2::Release`；临时 Object/ObjThis 与 root 返回 dispatch 的尾释放 |
| Enum callback | 2 | `597200←5971E0 [vptr+10]`; `5973F0←5973D0 [vptr+10]` | Array/Dictionary 两循环的 `callback->FuncCall` |
| `std::function` manager | 6 | `598318/37C/458/48C←manager`; `599F04/A088←manager` | `OwnerFilter` 正常/EH 析构，三参 `{self,self,3}` |
| stream `GetSize` | 2 | `5985A0/5B8←[vptr+20]` | `tTJSBinaryStream::GetSize` 两次独立虚调用 |
| stream delete | 2 | `59862C/6E0←[vptr+30]` | 正常与 EH 路径的 deleting destructor；本地 `unique_ptr` 保留相同虚析构边 |
| `OwnerFilter` invoke | 1 | `598858←[filter+18]` | `std::function` invoker；manager 非空门控后传 `*owner_` |
| media delete tail | 1 | `59989C←599898 [vptr+08]` | ref==1 时经 deleting destructor `BR X1`，不是普通 `Release` 递归 |
| storage lister | 2 | `599BA4←599B8C [vptr+00]`; `599C50←599C3C [vptr+00]` | `iTVPStorageLister::Add`，跨 `ttstr` 构造保存到 X23/X20 |
| adaptor ref ops | 3 | `599F30/F40←[vptr+00]`; `599F5C←[vptr+08]` | Variant Object/ObjThis 的两次 AddRef 与 construction ref Release |
| adaptor creation | 3 | `59A3B4←[vptr+90]`; `59A3CC←+08`; `59A3F4←+C8` | `CreateNew`、global `Release`、`NativeInstanceSupport` |
| Resolve native lookup | 1 | `59A514←[vptr+C8]` | `_file` dispatch 的 `NativeInstanceSupport(GETINSTANCE)` |
| Unregist dispatch | 4 | `59A9CC/AA48←[vptr+60]`; `59A9DC/AA58←+08` | 两组 `DeleteMember` + global `Release` |
| RegistEnd dispatch | 4 | `59ADFC←+00`; `59AE48←+08`; `59AE6C←+30`; `59AE7C←+08` | class AddRef/Release、global `PropSet`/Release |
| RegistItem interface | 4 | `59B050←+00`; `59B068←+10`; `59B07C←+08`; `59B0A8←+18` | `GetDispatch/GetType/GetFlags/Release`；顺序与 `ncbIMethodObject` 一致 |
| factory wrapper | 2 | `59B1A8←[self+30]`; `59B1DC←[vptr+C8]` | 四参 factory callback，再把实例写入 adaptor |
| root native lookup | 2 | `59B30C/B3E8←[vptr+C8]` | root getter/setter wrapper 的 `NativeInstanceSupport` |
| root typed Invoke | 3 | `59B4C8←59B4C4 [X9,X8]`; `59B4E4/F4←+00` | 成员指针调用 `GetRootDispatch`，随后为 Object/ObjThis 两次 AddRef |
| load wrapper | 2 | `59B5F0←[vptr+C8]`; `59B640←59B620 [X8,X20]` | native lookup 后按成员指针调用 `PSBFile::Load` |

数量合计为 `46`。表中偏移是目标 ARM64 vtable/对象 ABI 证据，只写入分析，不用于在
wasm32 C++ 中硬凑对象字节布局。

## 两处成员函数指针

`root@0x59B4C8` 与 `load@0x59B640` 都读取两个 qword：函数/虚表偏移，以及带低位
virtual 标志的 this-adjustment。两处均执行：

1. `adjustedThis = native + (adjustmentAndFlag >> 1)`；
2. 低位为 1 时从 `*adjustedThis + functionOrVtableOffset` 二次取目标；
3. 否则直接使用函数地址；
4. 以调整后的 this 调用。

这正是 `MethodCaller::Invoke` 对 `&PSBFile::GetRootDispatch` 与 `&PSBFile::Load` 的
Itanium ABI 展开。不能把它改写成裸全局 callback，也不能因 Android 最终是 `BLR` 就删除
本地 typed wrapper/member-pointer 分层。

## 机械门禁

`verify_elf_surface.py` 新增 46 行显式 producer manifest。门禁现在同时要求：

1. 从 114 个 FDE 实际解码出的全部非 switch `BLR/BR` 集合与 46 行完全相等；
2. producer 与 transfer 位于同一 owner FDE，且 producer 严格早于 transfer；
3. producer 的 32-bit AArch64 word 精确一致；
4. producer `LDR` 的目标寄存器与 `BLR/BR` 使用的寄存器一致；
5. 44 个固定偏移 load、2 个 register-offset member-pointer load 与 18 类角色计数不漂移。

通过输出：

```text
indirect_abi_surface=true sites=46 fixed_loads=44 member_pointer_loads=2 roles=18 producer_words=true target_registers=true
```

原 callsite SHA 只固定 transfer 指令；本门禁额外固定“间接目标怎样被取出”，因此不是把
同一计数换一种写法重复记录。

后续输入侧复核已继续为这 46 个 transfer 恢复 117 个 `X0..X7` 语义参数与 120 条完整
producer 关系，并把 40 个 normal-entry 与 6 个 LSDA landing-only 站点分开验证。该复核
还纠正 factory callback 的 stale `X4` 假第五参，固定两处八参 `FuncCall`，且没有发现
生产 GAP；详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
