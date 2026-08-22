# D3DEmotePlayer 独立 ClassInfo、typed Factory、clone producer 与 listener owner 四参考闭环（2026-08-17）

## 1. 范围与本轮纠错

本轮把 `DrawDeviceD3D.dll` 中 `D3DEmotePlayer` 的成员面之外四条长期缺失的链合并为一个
完整纵切面：独立 ClassInfo 和注册事务、typed Factory、native listener shell/adaptor owner、
typed clone 到 existing-native boxing。四份当前 `reference/binaries/` 共同证明：

1. `D3DEmotePlayer` 有自己的 `ncbClassInfo<D3DEmotePlayer>::InfoT`；LP64 为 32 B，
   ILP32 为 16 B，不借用 `D3DImage`、`D3DLayer`、`D3DEmoteModule` 或相邻类的 class ID；
2. 注册项是返回 `D3DEmotePlayer *` 的 typed Factory。普通调用必须提供 arg0；恰好一项
   Void 是 ncbind 的 empty-adaptor sentinel，surplus 参数被接受但不读取；
3. Factory 和 clone 的 arg0 都通过 `ncbInstanceAdaptor<D3DLayer>` 解箱。旧报告、旧 IDB
   字段和旧注释中的 `D3DImage *` 是相邻 class-ID state 误归，现已按四端共同 unboxer 纠正；
4. native shell 继承带状态的 D3DLayer listener base，保存 borrowed `D3DLayer *`，构造时立即
   `AddListener(this)`，析构末尾 `RemoveListener(this)`；它不 AddRef/Release owner；
5. Factory 直接把新壳写入 fresh non-sticky adaptor。重复对 populated receiver 调构造器会
   覆盖旧 native 而不 teardown，旧 listener shell 永久泄漏；
6. `clone(D3DLayer *targetOwner)` 是唯一的 existing-native producer。它只复制 primary
   EmoteObject；result 为空时不装箱并泄漏 copy，result 非空时调用唯一一条
   `CreateAdaptor(copy,false,false)`；
7. CreateAdaptor 不只有成功/失败两态，而有 null、non-null empty shell、正常 populated shell
   三态。前两态都不回收传入的 clone；随后 strict result converter 还会在 null 路径无保护
   Release adaptor，形成可崩溃边界；
8. 生成的 auto-register object 保留 Unregist 虚槽，但当前 integrated loader 只执行 Regist，
   没有 term callback、registered-set erase 或 AllUnregist；成功注册后的 ClassInfo 和 global
   member 都是 process-lived；
9. typed new-expression 的机器顺序是先分配 raw shell storage、再转换 D3DLayer 参数。四端
   EH cleanup 都会在转换/构造异常时释放未构造 storage；这与 native clone body 在 primary
   clone 抛出后泄漏已经注册 listener 的 shell 是两条不同边界。

本轮重新从四个参考的 ClassInfo init、auto-register vtable、Factory outer/invoke、EH landing
pad、clone wrapper、CreateAdaptor 和 loader caller/xref 建证；不沿用旧 `libkrkr2.so` 地址或
旧 D3DImage 推断。

## 2. 独立 InfoT、guard 与字段发布

| 目标 | InfoT | guard | static init |
|---|---:|---:|---:|
| Android ARM64 | `0x1AAF6C0` | `0x1AAF6E0` | `0x42CB48` |
| Android ARMv7 | `0x110E234` | `0x110E244` | `0x2FF004` |
| iOS ARM64 | `0x101AEE4E0` | `0x101AEE500` | `0x10024CA70` |
| iOS ARMv7 | `0x1838EB0` | `0x1838EC0` | `0x24E654` |

LP64 自然布局为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[7]
+0x08  const tjs_char *name        // borrowed
+0x10  int32 classID
+0x14  uint8 pad[4]
+0x18  iTJSDispatch2 *classObject  // borrowed
sizeof = 0x20
```

ILP32 为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[3]
+0x04  const tjs_char *name        // borrowed
+0x08  int32 classID
+0x0C  iTJSDispatch2 *classObject  // borrowed
sizeof = 0x10
```

四端 static init 都只检查 guard 的低位状态，首次执行时清零 InfoT 并写 guard；没有锁、原子
publication 或 `__cxa_guard_acquire`。`Set` 在 initialized 已真时返回 false；否则按
name、class ID、class object、initialized-last 的顺序保存。name 和 class object 都不 AddRef。
`Clear` 只写零，不 Release class object，也不重置 guard。

Android 两端保留独立 trivial leaves：

| leaf | Android ARM64 | Android ARMv7 |
|---|---:|---:|
| GetName | `0x52E844` | `0x494008` |
| GetID | `0x52E854` | `0x494014` |
| GetClassObject | `0x52E864` | `0x494020` |
| IsSubClass | `0x52E874` | `0x49402C` |
| Set | `0x52E87C` | `0x494030` |
| Clear | `0x52E8B4` | `0x494058` |
| InfoT ctor | `0x52E8D0` | `0x49406C` |

iOS 两端把同样操作内联进 static init、RegistBegin、CreateAdaptor 和 end transaction；没有为了
跨平台表格对齐而拆出不存在的 leaf。

## 3. DrawDeviceD3D 注册事务与无 unload

| stage | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| member registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| auto Regist | `0x542178` | `0x4A3AD0` | `0x100245634` | `0x245D28` |
| auto Unregist | `0x5422DC` | `0x4A3B54` | `0x10024569C` | `0x245DDC` |
| RegistBegin | `0x542434` | `0x4A3BD0` | `0x1002456F8` | `0x245E8C` |
| CreateEmpty | `0x542588` | `0x4A3CB8` | `0x100245810` | `0x245FE8` |
| finalize | `0x5425B4` | `0x4A3CD8` | `0x10024583C` | `0x246008` |
| Invalidate | `0x5425BC` | `0x4A3CDC` | `0x100245844` | `0x24600C` |
| complete dtor | `0x5425FC` | `0x4A3CF8` | `0x100245884` | `0x246028` |
| deleting dtor | `0x542658` | `0x4A3D34` | `0x1002458E4` | `0x246062` |
| RegistEnd | `0x5426A4` | `0x4A3D64` | `0x10024596C` | `0x246130` |
| UnregistEnd | folded/generated | `0x4A3E24` | `0x100245A68` | `0x246264` |
| dummy callback | `0x542804` | `0x4A3EA0` | `0x100245B3C` | `0x2462DA` |
| RegisterItem | `0x54280C` | `0x4A3EAC` | `0x100245BCC` | `0x2463E0` |

RegistBegin 的共同顺序为：

```text
create NativeClass("D3DEmotePlayer")
install typed CreateEmpty callback
register/obtain native class ID
ClassInfo.Set(borrowed name, ID, borrowed class object)
write same ID into NativeClass
register empty finalize
register member rows in fixed order
```

Factory row 把 transaction 的 `constructorSeen` 置真；重复 constructor 会记录日志但仍继续
RegisterNCM。正常 RegistEnd 因 constructorSeen 已真，不安装返回 `-1002/TJS_E_NOTIMPL` 的
dummy。最后把 class object 发布为 global 的 `D3DEmotePlayer` 成员。

四个 integrated loader 分别在 Android ARM64 `0x701DE8`、Android ARMv7 `0x5BA8E8`、
iOS ARM64 `0x10029FDE4`、iOS ARMv7 `0x2A48FC`。它们遍历/执行注册对象后只记录已注册状态；
没有对称 unload。生成 Unregist 只通过 auto-register vtable 的
`0x19FFB98`、`0x10AD7E8`、`0x101AF3860`、`0x183B868` 虚槽保持可调用能力，当前 loader
没有 caller。因此 UnregistEnd 的 global delete 和 ClassInfo.Clear 是 dormant template path，
不能在本地生命周期注释中描述为正常模块卸载行为。

## 4. Typed Factory 的 outer wrapper

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| outer FuncCall | `0x542A6C` | `0x4A3FF0` | `0x100245D1C` | `0x24654C` |
| typed invoke | `0x542B44` | `0x4A4080` | `0x100245DC0` | `0x2465B8` |
| D3DLayer unbox | `0x542CB8` | `0x49EE98` | `0x10023F8C0` | `0x23F19E` |
| successful raw attach | `0x542C2C` | `0x4A4108` | `0x100245E9C` | `0x2466A6` |
| new-expression EH cleanup | `0x542C70..0x542CB0` | `0x4A413C..0x4A4174` | `0x100245ED0` | `0x2466DA` |

outer wrapper 四端共同执行：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // -1001

if argc == 1 && arg0.Type == Void:
    return TJS_S_OK                   // before result clear; empty adaptor only

if result != null:
    result.Clear()

if argc < 1:
    return TJS_E_BADPARAMCOUNT        // -1004

return typedInvoke(arg0, objthis)     // argv[1..] ignored
```

这是一参数 lower bound，不是 exact arity。零参普通调用被拒绝；一项 Void 只服务于
`NativeClass::CreateNew` 先建立 empty adaptor 的内部协议；两项或更多参数仍只读取 arg0。

typed invoke 的机器级顺序是：

```text
raw = operator new(56 B LP64 / 36 B ILP32)
owner = unbox arg0 through D3DLayer ClassInfo, raiseOnError=true
shell = construct D3DEmotePlayer(raw, owner)
adaptor = objthis.GetNativeInstance(D3DEmotePlayerClassID)
if adaptor lookup failed:
    shell deleting destructor
    return TJS_E_NATIVECLASSCRASH      // -1008
adaptor.native = shell                 // no old-native gate, sticky remains false
return TJS_S_OK
```

raw storage 在参数转换之前分配是 C++ new-expression 的机器顺序。四端 cleanup 都区分未构造与
已构造状态：D3DLayer conversion 或 constructor 抛出时销毁临时 Variant 并只
`operator delete(raw)`；listener shell 已构造后的异常则调 deleting destructor，先拆 native
slots，再 RemoveListener，最后释放 storage，然后 rethrow。这个路径不会把异常压成 error code。

成功 attach 只是一个 raw store。对 fresh CreateEmpty shell，旧 native 为 null；若脚本或内部
caller 对 populated receiver 重入 constructor callback，旧 native 不会先 Invalidate/delete，
因而旧 shell 及其 D3DLayer listener registration 永久失去 owner。

## 5. D3DLayer listener shell 与 adaptor owner

四端恢复的 shell 布局为：

| 字段 | LP64 | ILP32 | 语义 |
|---|---:|---:|---|
| listener/derived vptr | `+0x00` | `+0x00` | derived deleting dtor / listener callbacks |
| `D3DLayer *owner` | `+0x08` | `+0x04` | borrowed；不 AddRef/Release |
| tag | `+0x10` | `+0x08` | `8` |
| bias | `+0x14` | `+0x0C` | `-0.5f` |
| primary raw owner | `+0x18` | `+0x10` | 初始 null；shell 独占 |
| secondary raw owner | `+0x20` | `+0x14` | 初始 null；shell 独占 |
| base/user scale | `+0x28/+0x2C` | `+0x18/+0x1C` | `1.0f/1.0f` |
| visible/smoothing | `+0x30/+0x31` | `+0x20/+0x21` | false/false |
| total | `0x38`（56 B） | `0x24`（36 B） | — |

Android ARM64 把构造完全内联进 Factory/clone；其余三端的 ctor 为 Android ARMv7
`0x497824`、iOS ARM64 `0x100236300`、iOS ARMv7 `0x235022`。构造先建立 listener base，
owner 非空就调用它的 AddListener 虚槽，然后初始化 derived slots/scalars/flags。析构顺序相反：
先销毁 secondary，再销毁 primary，最后 listener base 对 borrowed owner 调 RemoveListener。

adaptor 是标准 ncbind instance adaptor：

```text
LP64: { vptr@0, native@8, sticky@16, padding }   sizeof 24
ILP32: { vptr@0, native@4, sticky@8, padding }  sizeof 12
```

CreateEmpty 把 native 和 sticky 都清零。Invalidate、complete dtor、deleting dtor 的 owner gate 为：

```text
if native != null && sticky == false:
    native deleting destructor
native = null
sticky = false
```

Factory 和 clone boxing 都使用 non-sticky，因此通常是 adaptor 独占 shell。没有发现
`CreateAdaptor(...,true,...)`、setSticky 或引用 boxing 产生的 D3DEmotePlayer shell。

## 6. `D3DImage` 旧解释为何错误

旧分析把 listener owner/typed arg 写成 `D3DImage *`，原因是 D3DImage 与 D3DLayer 的 NCB
state、registrar 和对象桥在 stripped binary 中相邻，早期工作又沿用了单个 `libkrkr2.so` 的
命名。四端当前证据能直接区分：

- Factory invoke 和 clone invoke 都调用同一 `D3DLayer_NCB_unboxArg_guess`；
- unbox helper 读取 D3DLayer 独立 registrar 初始化的 class ID，而不是 D3DImage class ID；
- 返回 adaptor 的 native field 再解为 D3DLayer payload；
- listener ctor 对 owner 调 D3DLayer 的 AddListener 虚槽；
- module getter、TransformPoint、FindParentModule 等下游 consumer 也都需要 D3DLayer API。

因此本轮把源码报告、构造签名和四个 recovery IDB 中
`D3DEmotePlayer_guess::ownerD3DImageBorrow` 全部改为 `ownerD3DLayerBorrow`。D3DImage 仍是
DrawDeviceD3D 中另一个真实类和 managed-object set element；这里只纠正 D3DEmotePlayer
listener owner，不是全局删除 D3DImage 类型。

## 7. Typed clone 与唯一 existing-native producer

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| native clone body | `0x53039C` | `0x4949D4` | `0x100232DC8` | `0x2319DC` |
| clone FuncCall | registrar 内联 | `0x4A49B8` | `0x1002469B8` | `0x247364` |
| invoke + result boxing | `0x5434E8` | `0x4A4A64` | `0x100246A38` | `0x2473E8` |
| CreateAdaptor call | `0x5435B0` | `0x4A4AE2` | `0x100246B28` | `0x2474CA` |
| unchecked final Release | `0x543608` | `0x4A4B16` | `0x100246B80` | `0x247512` |
| CreateAdaptor body | `0x543758` | `0x4A4B4C` | `0x100246BB4` | `0x247568` |

native clone 精确等价于：

```cpp
D3DEmotePlayer *clone(D3DLayer *targetOwner) {
    D3DEmotePlayer *copy = new D3DEmotePlayer(targetOwner);
    copy->primary = primary->clone_guess(); // source primary has no null guard
    return copy;
}
```

新壳在 primary clone 之前已经注册为 targetOwner listener。没有复制 secondary、scale、visible
或 smoothing；它们保持 ctor 默认值。若 source primary 为 null，直接解引用；若 primary clone
抛出，裸局部 `copy` 没有 owner cleanup，新 shell、listener registration 和可能已有的局部状态
一起泄漏。这与 Factory new-expression 的 conversion/constructor cleanup 不同。

outer clone wrapper 的共同边界是：objthis 为 null 时返回 native-class crash；否则先清 result，
`argc < 1` 返回 bad-param-count；随后解出 source receiver，并把 arg0 当 D3DLayer targetOwner
解箱。surplus 参数同样被忽略。native clone 返回后分两路：

```text
if result == null:
    return OK                         // copy is never boxed or deleted: leak

dispatch = CreateAdaptor(copy, sticky=false, raiseOnError=false)
convert dispatch to strict object Variant
Release(dispatch)                     // no null guard
return OK
```

完整 caller/xref 审计只发现这一条 D3DEmotePlayer `CreateAdaptor(existing native)` producer；
Factory 不调用 CreateAdaptor，而是向预建 empty adaptor 直接 attach。

## 8. CreateAdaptor 的三态与 null-release 边界

归一化 CreateAdaptor 为：

```text
if ClassInfo.classObject == null:
    if raiseOnError: throw "No class"
    return null

dispatch = ClassInfo.classObject.CreateNew(one Void sentinel)
if CreateNew failed or dispatch == null:
    if raiseOnError: throw "Can't create instance"
    return null

adaptor = dispatch.GetNativeInstance(ClassInfo.classID, raiseOnError)
if adaptor != null:
    adaptor.native = suppliedNative
    if sticky: adaptor.sticky = true

return dispatch
```

所以返回状态不是普通二值：

| 状态 | 条件 | 返回值 | supplied clone |
|---|---|---|---|
| 1 | class 缺失或 CreateNew 失败 | null | 不删除，泄漏 |
| 2 | CreateNew 成功、GetNativeInstance 失败 | non-null empty dispatch | 不 attach、不删除，泄漏；dispatch 仍返回 |
| 3 | adaptor lookup 成功 | populated dispatch | 写入 native，sticky=false，dispatch 接管 |

clone 固定传 `false,false`，因此没有本地异常把状态 1 转成 catchable error。strict result converter
虽然在 AddRef/Variant construction 前检查 null，但尾部始终对 adaptor 调 Release；状态 1 会在
四端相应 unchecked site 解引用 null。状态 2 则向脚本返回一个看似成功但 native 仍为空的
对象，同时 supplied clone 已泄漏。复原代码不能把它简化成“CreateAdaptor 失败就 delete clone”
或“null 就返回 TJS_E_NATIVECLASSCRASH”，否则会改变原版边界。

## 9. 本地源码与旧报告同步

本轮 executable path 原本已使用正确公开类型：

- `cpp/plugins/motionplayer/EmotePlayer.h/.cpp` 的 Factory 和 clone 都接收 `D3DLayer *`；
- `D3DEmotePlayer` 继承 `D3DLayerListener`，由 listener base 保存 borrowed owner 并管理
  AddListener/RemoveListener；
- `cpp/plugins/DrawDeviceD3D.cpp` 继续用 `NCB_REGISTER_CLASS_DELAY` 注册独立
  D3DEmotePlayer class。

本轮没有把 clone/CreateAdaptor 的原版缺陷“安全化”，只同步并限定注释：

- `DrawDeviceD3D.cpp`：补入独立 ClassInfo、typed one-D3DLayer Factory、Void sentinel、
  non-sticky attach、clone 唯一 CreateAdaptor producer 和 no-unload 说明；
- `EmotePlayer.h/.cpp`：明确 Factory 的 D3DLayer arg、重复 attach 覆盖和 listener-owner 边界；
- `motionplayer_d3d_shell_lifecycle_four_binary_2026-08-12.md`：把 D3DImage 旧名改为
  D3DLayer，并纠正 typed new-expression 的分配/EH 顺序；
- `motionplayer_lifecycle_four_binary_2026-08-11.md`：把 clone target 和 module getter owner 链
  统一改为 D3DLayer。

这些是注释、分析和 recovery metadata 纠错；本轮没有新增 executable statement，因此最终
Wasm 应与 V200 byte-identical。

## 10. Recovery IDB 回写

四份 recovery IDB 已原位保存并关闭。本轮共完成：

- 8 个 typed data item：4 个 D3DEmotePlayer InfoT + 4 个 static guard；
- 8 次 ABI type declaration/update：四端各一份 InfoT/适用声明，以及把既有
  D3DEmotePlayer shell 字段从 `ownerD3DImageBorrow` 改为 `ownerD3DLayerBorrow`；
- 76 个 semantic rename：Android ARM64 20、Android ARMv7 22、iOS ARM64 17、
  iOS ARMv7 17；iOS 两端另恢复 typed Factory unwind helper；
- 77 个最终函数签名：Android ARM64 20、Android ARMv7 23、iOS ARM64 17、
  iOS ARMv7 17；三个独立 ctor 都改用 `ownerD3DLayerBorrow`，Android ARM64 保留真实内联；
- 112 次 function/line comment set/update：Android ARM64 28、Android ARMv7 30、
  iOS ARM64 27、iOS ARMv7 27；EH landing pad 的 disassembly comment 也已保存；
- 4 个 V201 bookmark，全部落在 CreateAdaptor 三态 producer；
- 112 次成功 force-recompile request：Android ARM64 28、Android ARMv7 32、
  iOS ARM64 28、iOS ARMv7 24；
- 四端 typed readback 均显示 InfoT 精确为 32/16 B，shell 精确为 56/36 B，owner 字段为
  D3DLayer；fresh decompile 显示 `D3DLayer_NCB_unboxArg_guess`、typed Factory raw attach、
  clone 的 `CreateAdaptor(copy,false,false)` 与 null-unguarded Release；
- Android ARMv7 的 Factory EH 是同一函数之后的无独立 function owner landing pad，未错误
  强拆为可单独调用函数；Android ARM64 ctor 继续保持编译器内联。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 11. 验证

本轮仅改注释、报告和 recovery IDB metadata，完整验证与预期一致：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均成功；顺序重跑时两套都只有仓库既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；重新编译了
  `DrawDeviceD3D.cpp`、`EmotePlayer.cpp`、`main.cpp` 和受头文件影响的 motionplayer 单元，输出只有
  既有 `_tss`、`nodiscard`、pthread/memory-growth、JSPI 与 JS-library warning；
- Node `WebAssembly.Module` 对两份最终 `index.wasm` 都解析成功；Web imports/exports 为
  `539/69`，headless 为 `538/69`；
- 精确产物审计如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

大小、hash、imports/exports 及所有表列 section 与 V200/V199 精确相同，证明本轮没有意外改变
executable behavior。两套 `ctest --output-on-failure` 均以 0 退出，但构建树仍报告
`No tests were found!!!`；因此验证来自完整 TU、双链接和最终 Wasm 审计，不虚构已登记 runner。
scoped tracked `git diff --check` 通过，只显示工作树既有 LF/CRLF warning；同一文件集的独立
trailing-whitespace 扫描无命中。对实际源码和两份被纠正旧报告的 stale-owner 扫描也无
`ownerD3DImage`、`d3dImageOwner` 或“unbox D3DImage”残留；本文保留旧字段名只用于明确记录
这次迁移。

## 12. 与既有报告的分工

- `motionplayer_d3d_emoteplayer_ncb_surface_four_binary_2026-08-14.md`：完整成员表、属性和方法
  wrapper；
- `motionplayer_d3d_shell_lifecycle_four_binary_2026-08-12.md`：native shell 字段、clear/dtor、
  typed clone 与 EmoteObject copy 内容；
- `motionplayer_d3d_shell_raw_slot_protocol_four_binary_2026-08-13.md`：primary/secondary raw-owner
  替换协议；
- `motionplayer_d3d_visibility_shell_only_four_binary_2026-08-16.md`：visible shell-local consumer；
- `motionplayer_module_dependency_registration_lifecycle_four_binary_2026-08-14.md`：DrawDeviceD3D
  模块总注册顺序；
- 本文：独立 ClassInfo、Factory wrapper/EH、listener owner 类型纠错、唯一 clone producer、
  CreateAdaptor 三态、null-release 与无 unload 生命周期。
