# Motion.EmotePlayer 独立 ClassInfo、typed Factory 与无注销生命周期四参考闭环（2026-08-17）

## 1. 范围与本轮纠错

本轮继续向 `motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-14.md` 已恢复的
70 项成员和 2 个常量之外追踪，闭合 `Motion.EmotePlayer` 的类信息、延迟注册、对象创建、
adaptor owner、异常可见性与模块卸载边界。四份当前 `reference/binaries/` 共同证明：

1. `EmotePlayer` 有一份独立 `ncbClassInfo<EmotePlayer>::InfoT`，不复用 `Motion`、
   `Player`、`D3DAdaptor` 或相邻类型的 name/ID/class object；
2. LP64 InfoT 为 32 B，ILP32 为 16 B；name 与 class object 是 borrowed raw pointer，
   `Set`/`Clear` 不做 AddRef、Release、加锁或事务回滚；
3. 该类不是通过一个有生命周期的 `ncbSubClassItem` 发布。`EmotePlayerPreRegist` 调
   `Setup(true)` 后丢弃 bool，直接把 `ClassInfo.classObject` 作为
   `nitClass | TJS_STATICMEMBER` 写到 `Motion.EmotePlayer`；
4. `Setup` 先发布 ClassInfo，再注册 70 项成员；失败时已发布的 class 和成员前缀可见；
5. 本地旧 raw factory 实现错误地允许了普通零参数调用。四端真实注册形态是
   `EmotePlayer *factory(tTJSVariant)` 的 typed Factory；普通零参数返回
   `TJS_E_BADPARAMCOUNT/-1004`，唯一一项 Void 是 empty-shell sentinel，surplus 被接受但忽略；
6. Factory 是唯一的 EmotePlayer native payload producer。它建立一个 ABI-sized Engine，
   再把指针直接写进 non-sticky empty adaptor；没有 existing-native `CreateAdaptor` producer，
   也没有 sticky EmotePlayer producer；
7. attach 前不检查旧 native。对 populated receiver 重复调用 constructor callback 会用新
   Engine 覆盖旧指针，旧 Engine 永久失去 owner；
8. `emoteplayer.dll` 注册链没有 term callback。模板里的 `Setup(false)`/`ClassInfo.Clear()`
   虽然存在，但正常模块卸载没有 caller；它也不会删除 `Motion.EmotePlayer`、两个 decrypt
   setter 或释放 PreRegist 取得的 global dispatch；
9. 第一次装载若在 ClassInfo 发布后抛出，模块不会被标为已注册。重试时 `Setup(true)` 因
   class object 非空返回 false，但 PreRegist 仍直接发布旧/partial class 并继续注入 setter。

这些结论全部来自四个当前参考二进制的 Setup、PreRegist、typed FuncCall、construct/attach、
adaptor vtable 与 caller/xref；旧 `libkrkr2.so` 地址和“可选首参 raw factory”叙事不再作为依据。

## 2. 独立 InfoT 与静态初始化

| 目标 | InfoT | guard | static init |
|---|---:|---:|---:|
| Android ARM64 | `0x1AB5060` | `0x1AB5080` | `0x42EE80` |
| Android ARMv7 | `0x11115E0` | `0x11115F0` | `0x30135C` |
| iOS ARM64 | `0x101AE9270` | `0x101AE9290` | `0x1001CADC0` |
| iOS ARMv7 | `0x18365A0` | `0x18365B0` | `0x1C8E5A` |

LP64 的精确自然布局为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[7]
+0x08  const tjs_char *name       // borrowed
+0x10  int32 classID
+0x14  uint8 pad[4]
+0x18  iTJSDispatch2 *classObject // borrowed
sizeof = 0x20
```

ILP32 为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[3]
+0x04  const tjs_char *name       // borrowed
+0x08  int32 classID
+0x0C  iTJSDispatch2 *classObject // borrowed
sizeof = 0x10
```

四个 recovery IDB 的 typed readback 均显示所有字段与 guard 初始为零。static init 只在 guard
尚未置位时把 InfoT 清零并完成一次性初始化。`Clear()` 会清逻辑字段，但不会重置 guard；若未来
真的调用 unregister 后再注册，InfoT 由普通 RegistBegin 重写，而不是重新运行静态构造。

Android 两端还保留一组可独立识别的 trivial ClassInfo 叶函数：

| 角色 | Android ARM64 | Android ARMv7 |
|---|---:|---:|
| GetName | `0x67CE08` | `0x561278` |
| GetID | `0x67CE18` | `0x561284` |
| GetClassObject | `0x67CE28` | `0x561290` |
| IsSubClass | `0x67CE38` | `0x56129C` |
| Set | `0x67CE40` | `0x5612A0` |
| Clear | `0x67CE78` | `0x5612C8` |
| InfoT ctor | `0x67CE94` | `0x5612DC` |

`Set` 的状态机就是：已 initialized 则返回 false；否则依次保存 supplied name、ID、class object，
最后设置 initialized 并返回 true。字段发布没有引用计数和 rollback。

## 3. Delayed Setup 与直接 Motion 发布

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| member registrar | `0x67CEA8` | `0x5612E8` | `0x1001B5130` | `0x1B4DE0` |
| EmotePlayer PreRegist | `0x67F908` | `0x5623EC` | `0x1001B65DC` | `0x1B645C` |
| Setup | `0x682FA0` | `0x564E2C` | `0x1001B8CD0` | `0x1B82B8` |
| Setup unwind | folded landing blocks | folded | `0x1001B8D54` | `0x1B8382` |
| RegistBegin | `0x683528` | `0x56506C` | `0x1001B8FB8` | `0x1B8660` |
| RegistEnd / Clear | inline in Setup | `0x565040` | `0x1001B91BC` | `0x1B8858` |
| AddDummyConstructor | inline in Setup | `0x5651E8` | `0x1001B920C` | `0x1B8904` |
| dummy callback | `0x6837A4` | `0x565224` | `0x1001B9270` | `0x1B893A` |

Setup 的源码级控制流为：

```text
if isRegist && ClassInfo.classObject != null:
    return false

construct stack delegate(name)
construct registration guard(delegate, isRegist)
if isRegist:
    RegistBegin()              // creates class and publishes ClassInfo here

register/unregister 70 members + 2 constants in fixed order

if isRegist:
    if !constructorSeen:
        install dummy returning -1002
else:
    ClassInfo.Clear()

return !isRegist || ClassInfo.classObject != null
```

typed Factory 条目把 `constructorSeen` 置真，所以 `-1002/TJS_E_NOTIMPL` dummy 只是模板保留体，
正常注册不会安装。RegistBegin 顺序为：创建 native class 并安装 EmotePlayer CreateEmpty；注册
native class ID；调用 ClassInfo.Set；把 ID 写回 native class；安装空 `finalize`；然后才进入完整
成员表。因而成员注册异常发生时 ClassInfo 已可见。

与 `Player`、`LayerGetter` 等普通 Motion subclass 不同，PreRegist 没有分配或 Release 一个
`ncbSubClassItem<EmotePlayer>`。四端的真实发布点是：

| 目标 | `Setup(true)` call | direct `Motion.EmotePlayer` publication |
|---|---:|---:|
| Android ARM64 | `0x67F9A8` | `0x67F9D0` |
| Android ARMv7 | `0x56244A` | `0x562464` |
| iOS ARM64 | `0x1001B6668` | `0x1001B6690` |
| iOS ARMv7 | `0x1B6518` | `0x1B6546` |

四端都丢弃 Setup bool，直接从 InfoT 读取 class object 并 PropSet。这个写入不以 Setup 成功、
PropSet 返回值或一个中间 result 为 gate，也不存在 subclass-item owner 生命周期。

## 4. Typed Factory 的精确 wrapper 边界

| 目标 | outer typed `FuncCall` | construct + attach | make Engine helper | arg0 by-value helper |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x689CA4` | `0x689D7C` | `0x689E94` | `0x689F40` |
| Android ARMv7 | `0x56A280` | `0x56A310` | inline | `0x56A3F4` |
| iOS ARM64 | `0x1001C5F18` | `0x1001C5FBC` | inline | `0x1001C60E0` |
| iOS ARMv7 | `0x1C3158` | `0x1C31C8` | `0x1C3310` | `0x1C33E4` |

四个 outer wrapper 的分支和顺序一致：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND        // -1001

if numparams == 1 && param[0].Type() == tvtVoid:
    return TJS_S_OK                    // before result Clear; no native payload

if result != null:
    result.Clear()

if numparams < 1:
    return TJS_E_BADPARAMCOUNT         // -1004

return typedInvoke(copy-by-value(param[0]), objthis)
// param[1..] are accepted and never read
```

普通零参数因此不可能进入 first-arg helper。normalizer 内部即使保留泛型 `<1 -> Void` 分支，
也已被 outer lower-bound gate 截断。exactly-one Void 是 ncbind 让 `CreateNew` 只建立 empty adaptor
shell 的内部协议，不等价于“正常 factory 参数可选”。

四端创建的 native payload 大小与各 ABI 的完整 EmoteEngine 一致：

| 目标 | Engine payload | empty adaptor |
|---|---:|---:|
| Android ARM64 | `0x5D8`（1496 B） | 24 B |
| Android ARMv7 | `0x318`（792 B） | 12 B |
| iOS ARM64 | `0x428`（1064 B） | 24 B |
| iOS ARMv7 | `0x238`（568 B） | 12 B |

construct + attach 的共同流程是：

```text
rmDispatch = owning by-value copy(param[0])
engine = operator new(ABI-sized payload)
EmoteEngine_ctor(engine, rmDispatch)
destroy rmDispatch

adaptor = objthis.NativeInstanceSupport(GETINSTANCE, EmotePlayerClassID)
if objthis/adaptor query fails:
    engine.~EmoteEngine()
    operator delete(engine)
    return TJS_E_NATIVECLASSCRASH      // -1008

adaptor.native = engine               // raw overwrite, no prior teardown
return TJS_S_OK
```

成功 attach 的四个机器级 store 分别位于 Android ARM64 `0x689E08`、Android ARMv7
`0x56A37A`、iOS ARM64 `0x1001C6060`、iOS ARMv7 construct helper 的对应成功块。没有
`if (oldNative)`、`_deleteInstance()` 或 sticky 写入。fresh `CreateNew` 的普通路径安全，是因为
CreateEmpty 先创建 native=null 的 shell；若对 populated receiver 重入构造器，旧 Engine 会泄漏。

## 5. adaptor owner、producer 集与 teardown

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x68367C` | `0x565154` | `0x1001B90D0` | `0x1B87BC` |
| empty finalize | `0x6836A8` | `0x565174` | `0x1001B90FC` | `0x1B87DC` |
| shared native teardown | `0x6836B0` | body `0x5651C8`（cluster `0x565178`） | `0x1001B9180` | `0x1B883A` |
| complete destructor | `0x6836F4` | entry `0x56517C`（same cluster） | `0x1001B9108` | `0x1B87E8` |
| deleting destructor | `0x683754` | entry `0x5651A4`（same cluster） | `0x1001B914C` | `0x1B8814` |
| Invalidate | vtable/inline | same Thumb cluster | thunk `0x1001B9104` | thunk `0x1B87E2` |

CreateEmpty 只建立 `{vptr,native=null,sticky=false}`。Factory attach 不设置 sticky，所以正常
EmotePlayer object 的 TJS adaptor 是 Engine 的唯一 owner。Invalidate、完整析构与 deleting
destructor 汇合到：

```text
if native != null && !sticky:
    native.~EmoteEngine()
    operator delete(native)
native = null
sticky = false
```

完整 class-ID/xref 审计没有发现 `CreateAdaptor(existing EmotePlayer *)` caller，也没有
`setSticky()`、`CreateAdaptor(..., true, ...)` 或其他 boxing helper。故 producer 集恰好为 typed
Factory 一条；consumer 则是 70 个 member wrapper 对 receiver adaptor 的普通解包。

## 6. 无 term callback、重试与泄漏边界

`EmotePlayerPreRegist` 属于 emoteplayer module 的 pre-registration chain，但没有配对 term callback。
因此正常 unload 不会执行以下模板能力：

- `Setup(false)` / `ncbRegistSubClass::UnregistEnd`；
- `ClassInfo.Clear()`；
- 从 Motion 删除 `EmotePlayer`；
- 删除同一 PreRegist 随后注入的 decrypt setter；
- 显式 Release `TVPGetScriptDispatch()` 返回的 global。

这不是因为模板没有 Clear，而是当前模块根本没有把该卸载动作登记进 lifecycle。PreRegist 获取的
global 带引用，四端函数返回前都没有相应 Release，因而其长期保留同样属于当前二进制行为。

最危险的异常/重试序列是：

```text
first module load:
  Setup(true)
    RegistBegin publishes ClassInfo.classObject
    some later member registration throws
  module registration not marked complete

retry:
  Setup(true) sees non-null classObject -> false
  PreRegist ignores false
  direct-publish old/partial class to Motion.EmotePlayer
  continue setter injection
```

因此注册并非 atomic transaction，也不能把 Setup bool 当作已被 caller 正确处理。直接 publication
使旧/partial class 在 retry 上重新成为 public surface。

## 7. 本地源码同步与回归

本轮按四参考证据修正了 executable behavior：

- `cpp/plugins/motionplayer/EmotePlayer.h`：把 raw callback 声明改为
  `static EmotePlayer *factory(tTJSVariant rmDispatch)`；
- `cpp/plugins/motionplayer/EmotePlayer.cpp`：typed factory 直接 `new EmotePlayer(rmDispatch)`，
  让 ncbind 生成正确的 lower-bound、Void sentinel、by-value owner 与 surplus-ignore wrapper；
- `cpp/plugins/motionplayer/main.cpp`：注册仍为 `Factory(&EmotePlayer::factory)`，注释改为四端
  typed one-Variant 语义；
- `analysis/motionplayer_lifecycle_four_binary_2026-08-11.md`：删除旧“首参可选/零参 normalizer”
  叙事，并补入四端 outer FuncCall 地址和 required-arg0 证据；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：新增注册级回归，分别验证零参
  BADPARAMCOUNT/null、唯一 Void 的 empty shell，以及两参数仍只使用 arg0 并忽略 surplus。

没有为了安全化而在 attach 前删除旧 native；重复构造泄漏是四端真实边界，仍由 ncbind generated
wrapper 保留。

## 8. Recovery IDB 回写

四份 recovery IDB 已原位保存。本轮新增：

- 8 个 typed data item：4 个 EmotePlayer InfoT + 4 个 static guard；
- 4 个带显式 padding 的 ABI 类型：2 个 LP64 32 B、2 个 ILP32 16 B；
- 70 个 semantic rename：Android ARM64 18、Android ARMv7 18、iOS ARM64 17、iOS ARMv7 17；
- 64 个最终函数签名：Android ARM64 18、Android ARMv7 18、iOS ARM64 14、iOS ARMv7 14；
- 84 条 function/line comment：四端各 21；
- 4 个 V199 bookmark；
- 74 次成功 force-recompile request：Android 两端各 19、iOS 两端各 18；
- 四个 InfoT 与 guard 全部 read back 为零；Setup 和 outer typed FuncCall 均在强制刷新后重新
  decompile，直接显示 classObject gate、Void sentinel、`numparams < 1` 与 attach 分支；
- A32 保留真实 Thumb lifecycle coalescing；A64 保留 inlined RegistEnd/AddDummy；没有为了表格
  对齐强拆伪函数。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 9. 验证

本轮行为修正后的验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均通过；新增 Factory 注册级回归在两种配置下都完成编译，唯一输出是既有
  `_tss` literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均重新编译
  `EmotePlayer.cpp`、`main.cpp` 及受影响依赖并完成最终链接；其余输出只是既有 `_tss`、
  `nodiscard`、pthread/JSPI 和 JS-library warning；
- Node `WebAssembly.Module` 对两份最终 `index.wasm` 都解析成功；Web imports/exports 为
  `539/69`，headless 为 `538/69`，与 V198 相同；
- 精确产物审计如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

相对 V198，两份产物都精确增加 7,198 B；FUNCTION `+0xC`、CODE `+0x636`、DATA `+0x60`、
name `+0x157C`，GLOBAL 和 import/export 数量不变。两种配置完全相同的增量与“raw callback
改为生成 typed one-Variant wrapper”相符；这次不是纯注释/IDB metadata 变更，不能沿用 V198
的 byte-identical 结论。

两套 `ctest --output-on-failure` 都成功退出，但构建树仍报告 `No tests were found!!!`；因此当前
新增回归的可执行保障来自两套完整 TU 编译与最终双产物链接，而不是一个已登记的 CTest runner。
scoped tracked `git diff --check` 通过，仅显示工作树既有 LF/CRLF warning；本文单独的
trailing-whitespace 扫描也通过。

## 10. 与既有报告的分工

- `motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-14.md`：70 项成员、2 个常量和各
  method/property wrapper 的注册目标；
- `motionplayer_module_dependency_registration_lifecycle_four_binary_2026-08-14.md`：模块依赖、
  pre-registration 总顺序和 global publication；
- `motionplayer_lifecycle_four_binary_2026-08-11.md`：Engine/Player/EmoteObject payload 链和正常
  构造析构顺序；
- 本文：独立 ClassInfo、delayed Setup、直接 Motion publication、typed Factory 完整 wrapper、
  adaptor owner、唯一 producer、重复 attach 与 no-unregister/retry 边界。
