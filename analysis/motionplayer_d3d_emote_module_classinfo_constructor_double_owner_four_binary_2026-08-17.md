# D3DEmoteModule 独立 ClassInfo、零参 constructor 与 root/adaptor 双 owner 四参考闭环（2026-08-17）

## 1. 范围与本轮纠错

本轮在既有七成员/对象布局审计之上，闭合 `global.D3DEmoteModule` 的独立 ClassInfo、
DrawDeviceD3D 注册事务、零参数 typed constructor、adaptor producer、root module-map、
双重所有权和无卸载生命周期。四份当前 `reference/binaries/` 共同证明：

1. `D3DEmoteModule` 有独立 `ncbClassInfo<D3DEmoteModule>::InfoT`：LP64 32 B，
   ILP32 16 B；name 与 class object 都是 borrowed raw pointer；
2. 旧报告标成 ClassInfo init 的四个地址实际是随后构造七个 DrawDeviceD3D auto-register
   object 的 registration bundle。真正 static init 是
   `0x42CB18 / 0x2FEFD4 / 0x10024CA40 / 0x24E628`；
3. 该类发布为 `global.D3DEmoteModule`，不是 `Motion.D3DEmoteModule`；它和
   `global.D3DEmotePlayer` 都属于 `DrawDeviceD3D.dll` ClassRegist list；
4. `NCB_CONSTRUCTOR(())` 表示 C++ 零参数 signature，不表示 exact argc。outer wrapper 对
   每个非负 argc 都调用 constructor 并完全忽略 argv；仅“恰好一项 Void”是 empty-adaptor
   sentinel；负 argc 返回 `TJS_E_BADPARAMCOUNT`；
5. standalone constructor 分配/default 一个 32/28 B module，再 raw attach 到 fresh non-sticky
   adaptor。populated receiver 重入会覆盖旧 native 而不 teardown，旧 module 泄漏；
6. `D3DEmotePlayer.module` native getter 从 parent/root 的 class-ID map 取/建 raw module；
   result 为 null 时 map 是唯一 owner，result 非 null 时 generated pointer converter 调唯一
   `CreateAdaptor(native,false,false)`，成功后 root map 与 TJS adaptor 同时拥有同一 pointer；
7. module 没有 root-map back-pointer。wrapper-first 会留下非 null dangling map value；root-first
   会留下仍存活 wrapper 指向 freed storage；第二次 teardown、重新 getter/boxing 会形成
   UAF/double free；
8. CreateAdaptor 保留 null、non-null empty dispatch、populated non-sticky dispatch 三态；getter
   的 strict result converter 在 null path 仍无保护 Release adaptor；
9. generated Unregist wrapper 确实能删除 global member 并 Clear ClassInfo，但四端 integrated
   loader 都没有 unload、registered-set erase 或 AllUnregist caller；正常成功状态 process-lived。

以上结论来自四端真实 ClassInfo xref、constructor outer/invoke、raw attach、getter native body、
result converter、CreateAdaptor、adaptor vtable、root destructor 和 loader，而不是旧
`libkrkr2.so` 注释。

## 2. 真正的独立 InfoT 与 static init

| 目标 | InfoT | guard | true ClassInfo static init | 旧误标 registration bundle |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x1AAF698` | `0x1AAF6B8` | `0x42CB18` | `0x42CBD8` |
| Android ARMv7 | `0x110E220` | `0x110E230` | `0x2FEFD4` | `0x2FF094` |
| iOS ARM64 | `0x101AEE4B8` | `0x101AEE4D8` | `0x10024CA40` | `0x10024CB00` |
| iOS ARMv7 | `0x1838E9C` | `0x1838EAC` | `0x24E628` | `0x24E6D8` |

LP64 自然布局：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[7]
+0x08  const tjs_char *name        // borrowed
+0x10  int32 classID
+0x14  uint8 pad[4]
+0x18  iTJSDispatch2 *classObject  // borrowed
sizeof = 0x20
```

ILP32：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[3]
+0x04  const tjs_char *name        // borrowed
+0x08  int32 classID
+0x0C  iTJSDispatch2 *classObject  // borrowed
sizeof = 0x10
```

true static init 只在 guard 低 bit 未置位时清四个逻辑字段并写 guard=1；没有锁、原子 once 或
`__cxa_guard_acquire`。`Set` 在 initialized 已真时返回 false；否则按 name、classID、
classObject、initialized-last 的顺序发布。`Clear` 只写零，不 Release classObject，也不重置 guard。

Android 两端保留同形 trivial leaves：

| leaf | Android ARM64 | Android ARMv7 |
|---|---:|---:|
| GetName | `0x52E2E8` | `0x493DE4` |
| GetID | `0x52E2F8` | `0x493DF0` |
| GetClassObject | `0x52E308` | `0x493DFC` |
| IsSubClass | `0x52E318` | `0x493E08` |
| Set | `0x52E320` | `0x493E0C` |
| Clear | `0x52E358` | `0x493E34` |
| InfoT ctor | `0x52E374` | `0x493E48` |

iOS 两端内联这些 trivial 操作。registration bundle 的职责是构造
DrawDeviceD3D/D3D/D3DLayer/D3DImage/D3DPicture/D3DEmoteModule/D3DEmotePlayer 七个
auto-register object，并把它们头插到 ClassRegist；它不能替代任何一类的独立 InfoT init。

## 3. 注册事务、global publication 与 dormant Unregist

| stage | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| member registrar | `0x52E388` | `0x493E54` | `0x100232078` | `0x230DB0` |
| auto Regist | `0x540EC4` | `0x4A2B40` | `0x100244320` | `0x2446D4` |
| auto Unregist | `0x540F54` | `0x4A2BC4` | `0x100244388` | `0x244788` |
| RegistBegin | `0x541070` | `0x4A2C40` | `0x1002443E4` | `0x244838` |
| CreateEmpty | `0x5411C4` | `0x4A2D28` | `0x1002444FC` | `0x244994` |
| finalize | `0x5411F0` | `0x4A2D48` | `0x100244528` | `0x2449B4` |
| Invalidate | `0x5411F8` | `0x4A2D4C` | `0x100244530` | `0x2449B8` |
| complete dtor | `0x541238` | `0x4A2D68` | `0x100244570` | `0x2449D4` |
| deleting dtor | `0x541294` | `0x4A2DA4` | `0x1002445D0` | `0x244A0E` |
| RegistEnd | `0x5412E0` | `0x4A2DD4` | `0x100244658` | `0x244ADC` |
| UnregistEnd | wrapper 内联 | `0x4A2E94` | `0x100244754` | `0x244C10` |
| AddDummy | RegistEnd 内联 | `0x4A2ED4` | `0x1002447C4` | `0x244C50` |
| dummy callback | `0x541440` | `0x4A2F10` | `0x100244828` | `0x244C86` |
| RegisterItem | `0x541448` | `0x4A2F78` | `0x1002448B8` | `0x244D8C` |

RegistBegin 建立 NativeClass 和 typed CreateEmpty，取得 class ID，发布 borrowed ClassInfo，
把同一 ID 写回 NativeClass，再安装空 finalize；随后 registrar 按 constructor + 七成员顺序发布。
constructor row 设置 `constructorSeen=true`，所以正常 RegistEnd 不安装 dummy，并把 class object 作为
global 的 `D3DEmoteModule` 成员发布。成员注册异常发生时 ClassInfo 和成员前缀已经可见；不是
atomic transaction。

auto-register vtable 中 Unregist 的数据槽为 Android ARM64 `0x19FF6C0`、Android ARMv7
`0x10AD57C`、iOS ARM64 `0x101AF3388`、iOS ARMv7 `0x183B5FC`。生成体会逆向遍历成员，
删除 global member 并 Clear InfoT；但四端 loader
`0x701DE8 / 0x5BA8E8 / 0x10029FDE4 / 0x2A48FC` 只执行 Regist 并记录 loaded set，
没有 erase/unload。故这只是 dormant template capability。

## 4. 零参数 typed constructor 的真实 argc 边界

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| outer FuncCall | `0x5416A8` | `0x4A3060` | `0x100244A08` | `0x244EF8` |
| typed invoke | `0x54177C` | `0x4A30F0` | `0x100244AA8` | `0x244F64` |
| successful raw attach | `0x541824` | `0x4A315A` | `0x100244B48` | `0x24501C` |

四端 outer wrapper 完全一致：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // -1001

if argc == 1 && arg0.Type == Void:
    return TJS_S_OK                   // exact one-Void empty-shell sentinel

if result != null:
    result.Clear()

if argc < 0:
    return TJS_E_BADPARAMCOUNT        // -1004

return constructAndAttach(objthis)    // argv and every nonnegative argc ignored
```

因此：

- argc=0 是普通成功构造；
- argc=1 且 Void 只留下 empty adaptor；
- argc=1 且非 Void 仍构造，但参数不转换；
- argc=2+ 全部构造并忽略每个参数；即使 arg0 是 Void，只要 argc 不等于 1 就不是 sentinel；
- 负 argc 是唯一 bad-param-count 分支；
- sentinel 发生在 result Clear 之前；普通路径先 Clear 再构造。

这类 generated zero-arg adapter 的 `argc < 0` gate 看似奇怪，但四端逐指令一致，不能在本地注释
中写成 exact-zero constructor。

## 5. standalone constructor payload 与 adaptor owner

typed invoke 创建：

| 字段 | LP64 | ILP32 | 默认值 |
|---|---:|---:|---:|
| vptr | `+0x00` | `+0x00` | D3DEmoteModule vtable |
| maskMode | `+0x08` | `+0x04` | 1 |
| maskRegionClipping | `+0x0C` | `+0x08` | false |
| mipMapEnabled | `+0x0D` | `+0x09` | true |
| protectTranslucentTextureColor | `+0x0E` | `+0x0A` | false |
| alphaOp | `+0x10` | `+0x0C` | 0 |
| pixelateDivision | `+0x14` | `+0x10` | 100 |
| max width/height | `+0x18/+0x1C` | `+0x14/+0x18` | 0/0 |
| total | 32 B | 28 B | — |

adaptor 为 24/12 B `{vptr,native,sticky}`。CreateEmpty 设 native=null、sticky=false。constructor
向 `objthis.GetNativeInstance(classID)` 取得 adaptor 后只做一个 raw native store，不写 sticky；失败
则调用 module deleting destructor 并返回 `TJS_E_NATIVECLASSCRASH/-1008`。fresh CreateNew 安全是因为
adaptor 为空；对 populated receiver 重入 constructor descriptor 时没有 old-native gate，旧 module
会泄漏。

Invalidate/complete dtor/deleting dtor 都遵循：

```text
if native != null && sticky == false:
    native deleting destructor
native = null
sticky = false
```

module 自身 deleting destructor 只有 `operator delete(this)`，没有 root-map back-pointer、erase callback
或引用计数。

## 6. root module-map producer 与 result-null 分支

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| native `getModule` | `0x52FF78` | `0x494864` | `0x100232B68` | `0x2317C0` |
| property outer | `0x542DB4` | `0x4A42B4` | `0x1002460AC` | `0x246910` |
| getter invoke + boxing | `0x542FB4` | `0x4A4460` | `0x1002461C4` | `0x246A4C` |
| CreateAdaptor call | `0x543000` | `0x4A4490` | `0x100246230` | `0x246ABC` |
| unchecked Release | `0x543058` | `0x4A44C4` | `0x100246288` | `0x246B02` |
| CreateAdaptor | `0x5430A4` | `0x4A44FC` | `0x100246344` | `0x246B54` |

native getter 等价于：

```text
root = shell.d3dLayerOwner.Parent       // no null guard
id = D3DEmoteModule ClassInfo.classID
module = root.Modules.find(id)
if missing or value == null:
    module = new D3DEmoteModule
    root.Modules[id] = module
return module
```

map key/value 与 root 析构 owner 语义四端一致；Android 的 libstdc++ tree 和 iOS 的 libc++ tree
只改变布局/遍历 helper。若 node allocation/store 抛出，raw candidate 没有 RAII owner，会泄漏。
若 result slot 为 null，getter 仍执行并取/建 map entry，但本次 converter 不调用 CreateAdaptor；
对本次新建且此前没有 wrapper 的 module，map 因而是唯一 owner。若同一 map value 早已被别次
getter 装箱，旧 wrapper 仍可能存在；result-null 只表示本次不新增 owner。result 非 null 才进入
pointer boxing。

完整 xref 只发现 getter 一条 existing-native D3DEmoteModule CreateAdaptor producer；standalone
constructor 不调用 CreateAdaptor，而是向预建 empty adaptor raw attach。

## 7. CreateAdaptor 三态与双重所有权

getter 固定调用：

```text
CreateAdaptor(module, sticky=false, raiseOnError=false)
```

CreateAdaptor 共同状态机：

```text
if ClassInfo.classObject == null:
    return null

dispatch = ClassInfo.classObject.CreateNew(exact one Void)
if CreateNew failed or dispatch == null:
    return null

adaptor = dispatch.GetNativeInstance(classID, raiseOnError=false)
if adaptor != null:
    adaptor.native = module
    adaptor.sticky = false
return dispatch
```

| 状态 | 返回 | root map | TJS adaptor owner |
|---|---|---|---|
| class/CreateNew fail | null | 仍持有 module | 无；strict converter 最后会 null Release |
| CreateNew ok、adaptor lookup fail | non-null empty dispatch | 仍持有 module | 无 native；返回空壳 |
| 正常 attach | populated dispatch | 持有 module | non-sticky，亦会 delete module |

CreateAdaptor 不回收 supplied module。这里前两态不像 D3DEmotePlayer clone 那样让 native 完全 orphan：
module 已经在 root map 中；但 null strict-release 仍可崩溃，empty dispatch 仍向脚本伪装成功。

正常成功态产生原版最危险的 owner split：

```text
root.Modules[id] --------------------+
                                      +--> same D3DEmoteModule*
non-sticky TJS adaptor.native -------+
```

root destructor 四端入口为 `0x53244C / 0x49606C / 0x100233E1C / 0x232B14`，会逐个对
非 null map value 调 deleting destructor；它不知道 wrapper 状态。module deleting destructor
`0x533B98 / 0x497466 / 0x1002361D0 / 0x234F74` 又不会擦 map。因此：

1. wrapper 先销毁：module 被 delete，map 留非 null dangling value；
2. 再读 `player.module`：getter 把 dangling value 当 hit 并再次 non-sticky boxing，访问 UAF；
3. root 后析构：对 dangling value 再调 deleting destructor，double free；
4. root 先析构：仍活着的 wrapper 指向 freed storage；wrapper 以后失效再次 delete；
5. 多次 getter 可创建多个 non-sticky adaptor 共同拥有同一 map pointer，使风险进一步放大。

目标是 1:1 复原，不能用 sticky boxing、reference return、shared ownership、map erase callback 或
root 析构跳过 values 静默修复。

## 8. 本地同步与回归

executable 数据流原本已经由 ncbind 模板产生上述风险，本轮没有安全化：

- `DrawDeviceD3D.cpp` 的 `NCB_REGISTER_CLASS(D3DEmoteModule)` 保持 `NCB_CONSTRUCTOR(())` 和
  七成员顺序，新增注释限定 independent ClassInfo、nonnegative argc、exact-one-Void、raw attach、
  sole getter producer、double owner 和 no-unload；
- `EmotePlayer.cpp::getModule` 保持 `FindParentModule -> new -> SetParentModule -> pointer return`，
  把旧“只有 root 拥有”注释改成 root/non-sticky adaptor 双 owner；
- `motionplayer_lifecycle_four_binary_2026-08-11.md` 更正真正 ClassInfo init 地址；
- `motionplayer_d3d_emote_module_surface_lifecycle_four_binary_2026-08-14.md` 更正 global publication、
  constructor argc 与双 owner 叙事；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 新增注册级回归：普通零参成功并产生默认 payload、
  exactly-one Void 只产生 empty adaptor、两参数且 arg0 为 Void 仍构造并完全忽略 argv。

本轮唯一 executable-source 新增是测试；插件实现只改注释，因此最终 Wasm 预期与 V201/V200
byte-identical。

## 9. Recovery IDB 回写

四份 recovery IDB 已原位保存并关闭。本轮共完成：

- 8 个 typed data item：4 个 D3DEmoteModule InfoT + 4 个 static guard；
- 4 个显式 ABI type declaration：2 个 LP64 32 B、2 个 ILP32 16 B；
- 75 个 semantic rename：Android ARM64 20、Android ARMv7 22、iOS ARM64 17、iOS ARMv7 16；
- 75 个最终函数签名，分布同上；
- 113 条 function/line comment：Android ARM64 27、Android ARMv7 29、iOS ARM64 29、
  iOS ARMv7 28；
- 4 个 V202 bookmark，全部标在 root-map/non-sticky CreateAdaptor owner split；
- 103 次成功 force-recompile request：Android ARM64 27、Android ARMv7 29、iOS ARM64 24、
  iOS ARMv7 23；
- 四端 typed readback 显示 InfoT 精确为 32/16 B；fresh decompile 直接显示真正 static init、
  nonnegative argc gate、exact-one-Void、raw attach、typed ClassInfo fields、
  `CreateAdaptor(native,false,false)`、三态 attach 与 unchecked Release；
- Android ARMv7 的 tiny ClassInfo leaves 保留真实 Thumb 函数边界；iOS transaction wrapper/dispatcher
  保留真实编译器拆分，没有为表格统一强拆 landing pad。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 10. 验证

本轮完整验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均成功；新增 constructor 注册级回归在两种配置都编译，输出只有既有
  `_tss` literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；因注释
  timestamp 重编译 `DrawDeviceD3D.cpp`、`EmotePlayer.cpp` 和对应 guest objects，输出只有既有
  `_tss`、pthread/memory-growth、JSPI 与 JS-library warning；
- Node `WebAssembly.Module` 对两份最终 `index.wasm` 都解析成功；Web imports/exports 为
  `539/69`，headless 为 `538/69`；
- 精确产物如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

两份大小、hash、imports/exports 和表列 section 与 V201/V200 精确相同，符合“插件实现只改注释，
测试不进入最终产物”。两套 `ctest --output-on-failure` 均以 0 退出，但仍报告
`No tests were found!!!`；因此新增回归的可执行保障是两套完整 TU 编译和最终双链接，不虚构
已登记 runner。scoped tracked `git diff --check` 通过，只显示既有 LF/CRLF warning；独立
trailing-whitespace 扫描无命中。

## 11. 与既有报告的分工

- `motionplayer_d3d_emote_module_surface_lifecycle_four_binary_2026-08-14.md`：七成员、访问器、
  32/28 B payload 字段布局与默认值；
- `motionplayer_lifecycle_four_binary_2026-08-11.md` 第 15 节：root Modules tree ABI、map value
  destructor loop 与早期 double-owner 发现；
- `motionplayer_d3d_emoteplayer_classinfo_factory_clone_owner_topology_four_binary_2026-08-17.md`：
  相邻 D3DEmotePlayer ClassInfo、Factory、clone producer 和 listener owner；
- 本文：D3DEmoteModule 真正独立 ClassInfo init、global registration、zero-arg constructor argc、
  standalone/getter 两 producer、CreateAdaptor 三态、root/adaptor 双 owner 与 no-unload。
