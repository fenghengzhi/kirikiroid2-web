# Motion 根 ClassInfo、全局发布与 dormant Unregist 四参考闭环（2026-08-17）

## 1. 范围与过时结论更正

本轮不重复 `motionplayer_motion_root_ncb_surface_lifecycle_four_binary_2026-08-14.md` 已恢复的
23 个常量、11 个 subclass、两个静态 method 和 dummy constructor wrapper，而是继续向外闭合
Motion 根 class info、auto-register vtable、集成式 module loader 与实际 teardown 可达性。

早期根报告有一个重要表述错误：它把 auto-register 模板生成的 `Unregist(isRegist=false)`
虚函数体写成当前 loader 实际会执行的卸载链。四份当前 `reference/binaries/` 的 fresh xref、
vtable、wrapper 和 `LoadModule` 共同证明：

1. `Motion` 有一份独立 `ncbClassInfo<Motion>::InfoT`，LP64 32 B、ILP32 16 B；
2. 正常 `LoadModule("motionplayer.dll")` 只经每个 auto-register object 的 Regist slot 遍历
   Pre/Class/Post 三个 list，全部成功后向 registered set 插入小写 module name；
3. Motion auto-register object 的 vtable 确实有 Unregist slot，且函数体能以 `isRegist=false`
   重跑同一 registrar、删除全局成员并 Clear ClassInfo；
4. 但 Unregist 入口四端都只有 auto-register vtable data xref。集成式 loader 没有
   Unregist traversal、registered-set erase、module unload API 或可达 `AllUnregist` 实体；
5. 因此“反注册按正向顺序删除”只能描述 dormant template capability，不能描述当前正常运行时；
6. 成功加载后 `global.Motion`、Motion ClassInfo 和由根 registrar 建立的 11 个 subclass
   ClassInfo 均保持到进程退出；
7. 注册失败仍不是事务：RegistBegin 先发布 Motion ClassInfo，root registrar/member/subclass
   前缀和 EH RegistEnd 的 global publication 都不会由 registered-set commit 失败自动回滚。

这也落实了用户关于旧注释的提醒：函数“存在”不等于当前四参考调用链“可达”。

## 2. 独立 Motion InfoT

| 目标 | InfoT | guard | static init |
|---|---:|---:|---:|
| Android ARM64 | `0x1AB5860` | `0x1AB5880` | `0x42F1A4` |
| Android ARMv7 | `0x1111B5C` | `0x1111B6C` | `0x30168C` |
| iOS ARM64 | `0x101ADF788` | `0x101ADF7A8` | `0x10014FC20` |
| iOS ARMv7 | `0x1831868` | `0x1831878` | `0x151C44` |

LP64 自然布局：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[7]
+0x08  const tjs_char *name       // borrowed
+0x10  int32 classID
+0x14  uint8 pad[4]
+0x18  iTJSDispatch2 *classObject // borrowed
sizeof = 0x20
```

ILP32：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[3]
+0x04  const tjs_char *name       // borrowed
+0x08  int32 classID
+0x0C  iTJSDispatch2 *classObject // borrowed
sizeof = 0x10
```

name 和 classObject 都不 AddRef；Set 没有锁、原子发布或失败 rollback。四库 typed readback
显示 InfoT 和 guard 初始全零。static init 只清一次 InfoT 并设置 guard；dormant Clear 会清
InfoT 四个逻辑字段，但不会清 guard。

Android 两端的独立 trivial leaf 映射为：

| 角色 | Android ARM64 | Android ARMv7 |
|---|---:|---:|
| GetName | `0x6D6E48` | `0x599160` |
| GetID | `0x6D6E58` | `0x59916C` |
| GetClassObject | `0x6D6E68` | `0x599178` |
| IsSubClass | `0x6D6E78` | `0x599184` |
| Set | `0x6D6E80` | `0x599188` |
| Clear | `0x6D6EB8` | `0x5991B0` |
| InfoT ctor | `0x6D6ED4` | `0x5991C4` |

iOS 两端将这些 trivial access 折叠进 static init、RegistBegin 和 UnregistEnd，不应为了表格对齐
虚构独立函数。

## 3. live Regist 与 dormant Unregist pair

| 目标 | root registrar | auto-register Regist | auto-register Unregist | Unregist vtable slot |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x6D6EE8` | `0x6F944C` | `0x6F95B0` | `0x1A1EBA0` |
| Android ARMv7 | `0x5991D0` | `0x5B55E4` | `0x5B5668` | `0x10BCFDC` |
| iOS ARM64 | `0x100125974` | `0x10014C4A4` | `0x10014C50C` | `0x101AE6CA0` |
| iOS ARMv7 | `0x124B7C` | `0x14DDD8` | `0x14DE8C` | `0x18352DC` |

32 位 vtable 存的是 Thumb-tagged code pointer；表内 wrapper 地址是实际偶地址函数入口。
Regist 与 Unregist 是同一个 `ncbNativeClassAutoRegister<Motion>` 对象的两个 virtual slot：

```text
Regist:
  construct delegate("Motion")
  construct registration guard(isRegist=true)
  RegistBegin()
  rootRegistrar(23 constants, 11 subclasses, 2 methods)
  RegistEnd()

Unregist:                         // emitted but dormant
  construct delegate("Motion")
  construct registration guard(isRegist=false)
  rootRegistrar(same rows, deletion mode)
  UnregistEnd()
```

四端 wrapper 都把 bool 编译成常量；不存在运行时从参数选择两种模式的公共 public API。

## 4. RegistBegin、global publication 与前缀可见性

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| RegistBegin | `0x6F9708` | `0x5B56E4` | `0x10014C568` | `0x14DF3C` |
| RegistEnd | `0x6F9960` | `0x5B5854` | `0x10014C798` | `0x14E1C4` |
| dormant UnregistEnd | inline/shared wrapper | `0x5B5914` | `0x10014C894` | `0x14E2F8` |
| dummy callback | `0x6F9AC0` | `0x5B5990` | `0x10014C968` | `0x14E36E` |

RegistBegin 的稳定阶段是：

```text
classObject = TJSCreateNativeClassForPlugin("Motion", MotionCreateEmpty)
classID = TJSRegisterNativeClass("Motion")
if MotionClassInfo.initialized:
    throw "Already registerd class."

MotionClassInfo = {true, borrowedName, classID, borrowedClassObject}
classObject.classID = classID
classObject.register("finalize", noOp)
```

之后 root registrar 才添加 23 个常量、11 个 subclass 和两个 static method。根没有显式
constructor row，故 RegistEnd 再安装名为 `Motion`、返回 `-1002/TJS_E_NOTIMPL` 的 dummy，
然后获取 global 并尝试 PropSet `global.Motion`。PropSet status 被忽略；global 为空只记日志。
两种失败都不会清 Motion ClassInfo。

EH cleanup 也会调用 RegistEnd 后继续 unwind，所以第 N 行失败可留下：Motion ClassInfo、前
N-1 个 root member、若干已 Setup 的 subclass ClassInfo、dummy constructor，以及一个已发布的
partial `global.Motion`。module loader 只有所有 callback 正常结束后才插入 registered-set marker；
异常不会撤销这些前缀。

## 5. dormant Unregist 的精确能力

如果外部直接选择 Unregist vtable slot，四端函数体会：

```text
rerun Motion registrar with isRegist=false
  delete 23 constants in original forward order
  for 11 subclasses in original forward order:
    Setup(false) / unregister subclass rows / Clear subclass ClassInfo
    delete subclass member from Motion class
  delete doAlphaMaskOperation
  delete getD3DAvailable

global = TVPGetScriptDispatch()
if global != null:
  global.DeleteMember("Motion")
  global.Release()

MotionClassInfo.Clear()           // even if global is null
```

这段实现不是逆序 destructor，也没有 reference-counted module dependency teardown。但当前正常
loader 根本不选择它，所以生产生命周期中不能期待这些 DeleteMember/Clear 副作用发生。

## 6. 集成式 LoadModule 只注册、不卸载

| 目标 | public `LoadModule` | inner loader |
|---|---:|---:|
| Android ARM64 | `0x548E24` | `0x701DE8` |
| Android ARMv7 | `0x4A9648` | `0x5BA8E8` |
| iOS ARM64 | `0x100287B38` | `0x10029FDE4` |
| iOS ARMv7 | `0x28A8A4` | `0x2A48FC` |

inner loader 的唯一 module lifecycle 为：

```text
if lowercaseName in registeredSet:
  return false
if lowercaseName not in internalModuleMap:
  return false

for line in [PreRegist, ClassRegist, PostRegist]:
  for item in moduleMap[name].lists[line]:
    item.vtable.Regist(item)

registeredSet.insert(lowercaseName)
return true
```

四端没有对称的 `UnloadModule`，没有 `registeredSet.erase`，也没有遍历 Unregist slot 的第二个
loader。源码模板里声明的 inline `AllUnregist` 因无 caller 未形成可达当前二进制实体。每个 class
Unregist wrapper 只有其 vtable data xref，不能把该 data edge 误读成执行 call edge。

因此成功 `motionplayer.dll` 加载的真实 owner 图为：

```text
registeredSet["motionplayer.dll"]
  -> prevents every later LoadModule retry

global.Motion Variant
  -> owns published Motion class dispatch

Motion ClassInfo borrowed tuple
  -> process-lived lookup identity (no independent AddRef)

11 subclass ClassInfo tuples
  -> process-lived because root Unregist is unreachable
```

## 7. 本地注释与旧报告同步

本轮不改变 executable behavior：

- `cpp/plugins/motionplayer/main.cpp` 的 delayed-subclass 总注释现在明确：生成的 Unregist wrapper
  **会** Clear，但四端集成式 loader 不调用它；
- Motion 根 registrar 注释补充 dormant `Unregist(false)` 与 live registered-set-only pipeline 的
  区分；
- `analysis/motionplayer_motion_root_ncb_surface_lifecycle_four_binary_2026-08-14.md` 将“实际反注册”
  改为“如果直接调用 wrapper 的精确能力”，并在 5.4 节记录 no-unload xref 证据。

没有为本地端发明 unload API，也没有在 fixture 中手工调用 `AllUnregist` 来制造参考二进制不存在的
对象重建周期。

## 8. Recovery IDB 回写

四份 recovery IDB 已原位保存。本轮新增：

- 8 个 typed data item：4 个 Motion InfoT + 4 个 static guard；
- 4 个显式 padding ABI type：2 个 LP64 32 B、2 个 ILP32 16 B；
- 18 个 semantic rename 和 18 个函数签名：Android 两端各 8 个 ClassInfo/init leaf，iOS 两端
  各 1 个 static init；
- 60 条 function/line comment：Android 两端各 16，iOS 两端各 14；
- 4 个 V200 bookmark；
- 47 次成功 force-recompile request：A64 14、A32 15、iOS64 9、iOS32 9；
- 四个 InfoT 与 guard 均 read back 为零；四个 Unregist wrapper 强制刷新后仍直接显示
  isRegist=false 与 Clear/dormant dispatcher 路径；
- A32/iOS32 的 Unregist vtable slot 保留真实 Thumb-tagged pointer，没有把 data xref 伪装成 caller。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 9. 验证

本轮验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均通过；唯一输出是既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug` 重新编译 `main.cpp` 并完成 3/3，
  `cmake --build out/wasmtime/debug` 重新编译 ordinary/headless 两份 `main.cpp` 并完成 4/4；
  链接输出只含既有 `_tss`、pthread/JSPI 和 JS-library warning；
- Node `WebAssembly.Module` 解析成功，Web imports/exports 为 `539/69`，headless 为 `538/69`；
- 精确产物审计如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

大小、SHA、import/export 和 FUNCTION/GLOBAL/CODE/DATA/name section 均与 V199 精确一致，
证明 V200 的 compiled comments、报告和 IDB metadata 没有改变 executable bytes。两套
`ctest --output-on-failure` 均成功退出但继续报告 `No tests were found!!!`。scoped tracked
`git diff --check` 通过，仅显示既有 LF/CRLF warning；本文 trailing-whitespace 扫描通过。

## 10. 与既有报告的分工

- `motionplayer_motion_root_ncb_surface_lifecycle_four_binary_2026-08-14.md`：23/11/2 root 表面、
  dummy、empty adaptor 和两个 static method wrapper；
- `motionplayer_module_dependency_registration_lifecycle_four_binary_2026-08-14.md`：三模块归属、
  Pre/Class/Post list、registered set 与依赖链；
- 本文：独立 Motion InfoT、Regist/Unregist vtable pair、dormant Clear 能力与实际 no-unload/
  process-lived owner topology。
