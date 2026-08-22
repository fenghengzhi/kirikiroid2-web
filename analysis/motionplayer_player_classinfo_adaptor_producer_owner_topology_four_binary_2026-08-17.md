# Motion.Player 独立 ClassInfo、adaptor producer 与 owner topology 四参考闭环（2026-08-17）

## 1. 范围与新增结论

本轮不重复 `motionplayer_player_ncb_surface_four_binary_2026-08-14.md` 已闭合的 92 项成员表、
typed constructor 基本参数边界和 `clear` descriptor family，而是继续向其外层恢复完整的类注册、
对象发布和 native owner 图。四份当前 `reference/binaries/` 共同证明：

1. `Player` 拥有独立的 `ncbClassInfo<Player>::InfoT`，不复用相邻
   `D3DAdaptor`、`LayerGetter` 或任何其他类型的 name/id/class-object；
2. InfoT 在 LP64 为 32 B，在 ILP32 为 16 B；name 与 classObject 都是 borrowed raw pointer，
   Set/Clear 没有 AddRef、Release、锁、原子发布或事务回滚；
3. `ncbSubClassItem<Player>::Setup` 先发布 ClassInfo，再注册恰好 92 项成员；异常 cleanup 调
   RegistEnd/UnregistEnd，因此已完成前缀可见，而不是全有或全无；
4. Motion 发布的是另一个独立、只有 vptr 的 `ncbSubClassItem<Player>`：flags `0x10000`、type
   `0`，不携带父类、cast helper 或 native-pointer offset；
5. NativeClass 的 instance factory 先分配 24 B/12 B 的
   `{vptr,native=null,sticky=false}` shell；普通脚本构造再把完整 Player attach 到 shell；
6. typed constructor attach 只写 native slot。若在已有 native 的 receiver 上再次调用构造器，
   旧 Player 不会先析构，因而被覆盖并泄漏；
7. plugin 内 existing-native Player adaptor producer 恰好只有两条：type-3 节点构建和 type-4
   粒子生成；两者统一调用 `CreateAdaptor(child,false,false)`，不存在 sticky Player producer；
8. 成功时 non-sticky adaptor 接管 Player，并在 Invalidate/析构时 delete；失败时 supplied native
   从不由 `CreateAdaptor` 回收；
9. 非抛错 `CreateAdaptor` 还有一个容易漏掉的第三态：CreateNew 成功但 GetAdaptor 失败时，
   返回值仍是非空 dispatch，只是 native slot 保持 null。调用者仅检查 dispatch，因此会发布一个
   Object 型 empty shell，同时 native child 泄漏；它不同于 null 返回形成的 Void Variant；
10. A64 把 adaptor-to-Variant wrapper 内联到两名 producer；A32/iOS64/iOS32 复用一个 out-of-line
    wrapper。iOS64 又以 X8 hidden result pointer 返回 Variant。这些都是 ABI/优化差异，不是四份源码。

以上结果全部来自四个参考二进制的完整 helper xref、Setup/RegistBegin/teardown 反编译以及 fresh
decompile；不再沿用旧 `libkrkr2.so` 的地址或注释身份。

## 2. 独立 InfoT 布局与静态初始化

| 目标 | InfoT | guard | static init |
|---|---:|---:|---:|
| Android ARM64 | `0x1AB5838` | `0x1AB5858` | `0x42F174` |
| Android ARMv7 | `0x1111B48` | `0x1111B58` | `0x30165C` |
| iOS ARM64 | `0x101ADF760` | `0x101ADF780` | `0x10014FBF0` |
| iOS ARMv7 | `0x1831854` | `0x1831864` | `0x151C18` |

LP64 的精确布局为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[7]
+0x08  const tjs_char *name          // borrowed
+0x10  int32 classID
+0x14  uint8 pad[4]
+0x18  iTJSDispatch2 *classObject   // borrowed
sizeof = 0x20
```

ILP32 的精确布局为：

```text
+0x00  uint8 initialized
+0x01  uint8 pad[3]
+0x04  const tjs_char *name          // borrowed
+0x08  int32 classID
+0x0C  iTJSDispatch2 *classObject   // borrowed
sizeof = 0x10
```

四个 static init 都只在 guard 低位尚未置位时把四个逻辑字段清零，随后把 guard 置 1。正常
unregister 会清 InfoT，但不会清 guard；后续 re-register 依靠普通 Setup/RegistBegin 重写 InfoT，
而不是重新跑静态构造。

Android 两端仍保留可独立识别的 ClassInfo 叶函数：

| 角色 | Android ARM64 | Android ARMv7 |
|---|---:|---:|
| GetName | `0x6D3D08` | `0x597E58` |
| GetID | `0x6D3D18` | `0x597E64` |
| GetClassObject | `0x6D3D28` | `0x597E70` |
| IsSubClass | `0x6D3D38` | `0x597E7C` |
| Set | `0x6D3D40` | `0x597E80` |
| Clear | `0x6D3D78` | `0x597EA8` |
| InfoT ctor | `0x6D3D94` | `0x597EBC` |

iOS 两端把这些 trivial getter/Set/Clear 访问折叠进 consumer/Setup，没有必要虚构独立函数边界。
Android Set 的共同状态机为：

```text
if initialized:
    return false
name        = suppliedName
classID     = suppliedID
classObject = suppliedClassObject
initialized = true
return true
```

Clear 则依次把 name、classID、classObject 和 initialized 清零。两者都没有引用计数、同步或
失败后的字段回滚。

## 3. Motion wrapper、Setup 与注册阶段

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Motion wrapper/publication | inline at `0x6D730C` | `0x5996F4` | `0x100125FEC` | `0x125104` |
| Player Setup | `0x6FB0E4` | `0x5B677C` | `0x10014DC04` | `0x14F880` |
| Setup unwind | inline/landing blocks | folded | `0x10014DC88` | `0x14F94A` |
| RegistBegin | `0x6FB254` | `0x5B683C` | `0x10014DC9C` | `0x14F974` |
| RegistBegin unwind | landing blocks | folded | `0x10014DD90` | `0x14FA96` |
| member registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| RegistEnd/UnregistEnd | inline in Setup | `0x5B6810` | `0x10014DEA0` | `0x14FB68` |
| AddDummyConstructor | inline in Setup | `0x5B69B8` | `0x10014DEF0` | `0x14FC14` |
| dummy callback | `0x6FB4D0` | `0x5B69F4` | `0x10014DF54` | `0x14FC4A` |

Setup 的源码级控制流四端一致：

```text
if isRegist && ClassInfo.classObject != null:
    return false

construct delegate(name)
construct registration guard(delegate, isRegist)
if isRegist:
    RegistBegin()

register/unregister 92 Player rows in fixed order

if isRegist:
    if !constructorSeen:
        publish dummy class-name constructor returning -1002
else:
    ClassInfo.Clear()

return !isRegist || ClassInfo.classObject != null
```

RegistBegin 的顺序同样稳定：

1. 复制/拥有一次传入类名；
2. 分配并构造 native class object；
3. 安装 Player CreateEmpty callback；
4. 注册 native class ID；
5. 检查 InfoT 尚未初始化；
6. 发布 borrowed name、ID、class object、initialized；
7. 把 ID 写入 native class object 自身；
8. 注册名为 `finalize` 的 no-op native method；
9. 进入 92 项成员注册。

第 6 步早于第 9 步。成员 descriptor 创建、PropSet 或异常路径失败时，iOS 两端显式 unwind helper
和 Android landing blocks 都会运行 RegistEnd/UnregistEnd cleanup 后继续抛出；它们不会撤销此前
已成功发布的成员前缀，也没有完整 transaction rollback。

正常 92 项表含一个 typed constructor，故 constructorSeen 为真，`-1002` dummy 仅作为模板保留，
不在正常路径发布。Motion wrapper 在 Setup 失败时抛 `SubClass registration failed.`；成功时另行
分配 subclass item 并注册到 Motion。A64 只把该 wrapper 和 publication 内联进 Motion registrar，
并未消除 Setup 或 item 两个源码角色。

## 4. CreateEmpty、typed constructor 与重复 attach

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x6FB3A8` | `0x5B6924` | `0x10014DDB4` | `0x14FAD0` |
| finalize | `0x6FB3D4` | `0x5B6944` | `0x10014DDE0` | `0x14FAF0` |
| constructor callback | `0x6F3FB0` | `0x5B0798` | `0x100146384` | `0x1468E4` |
| construct + attach | `0x6F4088` | `0x5B0828` | `0x100146428` | `0x146950` |

CreateEmpty 只分配 adaptor：LP64 24 B，ILP32 12 B。字段为 vptr、null native 和 false sticky；
它不分配 Player。typed callback 的精确前缀保持既有四参考审计：

- membername 非空返回 `TJS_E_MEMBERNOTFOUND/-1001`；
- 恰好一项 Void 在 result clear 之前返回成功，是 internal empty-shell sentinel；
- 其他路径先清 result；少于一项返回 `TJS_E_BADPARAMCOUNT/-1004`；
- 一项以上只 CopyRef `param[0]`，其余完全忽略；
- Player 完整构造后才按 class ID 从 receiver 取得 adaptor；
- receiver/lookup/adaptor 失败时析构并 free 新 Player，返回
  `TJS_E_NATIVECLASSCRASH/-1008`；
- C++ exception 按完成前缀 unwind 后继续抛出。

本轮新增的是 attach 写入边界。四端成功分支都只有：

```text
adaptor.native = newlyConstructedPlayer
return TJS_S_OK
```

没有 `if (adaptor.native)`、没有 `_deleteInstance()`、没有 sticky 改写。NativeClass 正常 CreateNew
提供 fresh empty shell，所以普通脚本 `new Player(...)` 安全；但若脚本/原生路径在已经 populated 的
receiver 上重入 constructor callback，新 Player 会直接覆盖旧指针，旧 Player 永久失去 owner。

## 5. CreateAdaptor 的完整三态边界

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| CreateAdaptor | `0x6EEB74` | `0x5ACAA8` | `0x1001409D0` | `0x141C74` |
| GetAdaptor helper | inline | `0x5ACB90` | `0x100140AF8` | `0x141DCC` |
| adaptor-to-Variant wrapper | inline at producers | `0x58185C` | `0x1001092A0` | `0x106B08` |

CreateAdaptor 的源码级状态机为：

```text
classObject = PlayerClassInfo.classObject
if classObject == null:
    if err: throw "No class object."
    return null

dispatch = classObject.CreateNew(one Void argument)
if HRESULT failed || dispatch == null:
    if err: throw "Can't create instance"
    return null

adaptor = GetAdaptor(dispatch, err)
if adaptor != null:
    adaptor.native = suppliedPlayer
    if sticky:
        adaptor.sticky = true

return dispatch
```

最后一行不位于 `if adaptor != null` 内。这产生三个可观察结果：

| 条件 | 返回 dispatch | wrapper Variant | supplied Player |
|---|---|---|---|
| classObject 缺失或 CreateNew 失败，`err=false` | null | Void | 泄漏/仍由 caller 负责 |
| CreateNew 成功、GetAdaptor 失败，`err=false` | 非空 empty shell | Object | 未 attach，泄漏/仍由 caller 负责 |
| GetAdaptor 成功 | 非空 populated shell | Object | non-sticky 时由 adaptor 接管 |

`err=true` 会在相应位置抛出；四个 internal producer 都传 false。CreateAdaptor 在任何失败分支都不
delete supplied native，也不会因 adaptor lookup 失败而 Release/Invalidate 新 dispatch。wrapper 只测
dispatch 是否为空，不读 `adaptor.native`，因此第二态会被当作成功 Object 发布。

## 6. 完整 producer xref topology

| producer | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| type-3 node child | direct call `0x6B1A68` | wrapper call `0x58135C` | wrapper call `0x100108CA0` | wrapper call `0x106470` |
| type-4 particle child | direct call `0x6BCD40` | wrapper call `0x588B9E` | wrapper call `0x1001121E8` | wrapper call `0x10F9FA` |

A64 的 CreateAdaptor code xref 恰好就是上表两项。其余三端的 CreateAdaptor 各只有一个 code xref，
指向共享 adaptor-to-Variant wrapper；wrapper 又恰好只有上表两名 caller。所有机器级调用都把
sticky 与 err 传为零。

因此完整 producer 集为：

```text
type-3 child native ─┐
                     ├─ CreateAdaptor(Player*, false, false)
type-4 child native ─┘
                           │
                           ├─ null -> Void Variant + leaked native
                           ├─ empty shell -> Object Variant + leaked native
                           └─ populated non-sticky shell
                                      │
                                      └─ Variant owns adaptor owns Player
```

大量 Player class-ID xref 还包括 typed property/method receiver 解包、递归 child 访问、render/bounds/
parameter consumer 与 script constructor attach；它们都只是查询/消费已有 adaptor，并不是新的
existing-native producer。四端没有 `CreateAdaptor(...,true,...)`、没有事后 `setSticky()`，也没有
第三个 hidden boxing producer。

type-3 路径在创建 adaptor 前已经完成 root/parent link、independent-layer state、z factor 和 node
字段发布。type-4 路径同样先构造 native 并写 root/parent link；之后即使 wrapper 产生 Void 或 empty
shell，仍继续通过 raw child 指针做颜色、context、z factor、chara、play 等初始化，然后把 Variant
加入 particle Array。故失败边界不是安全 early-return。

## 7. adaptor owner 与 teardown

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| shared native teardown | `0x6FB3DC` | coalesced `0x5B6948` | `0x10014DE64` | `0x14FB48` |
| complete destructor | `0x6FB420` | same Thumb cluster | `0x10014DDEC` | `0x14FAF8` |
| deleting destructor | `0x6FB480` | same Thumb cluster | `0x10014DE30` | `0x14FB24` |
| Invalidate thunk | inline/vtable | same Thumb cluster | `0x10014DDE8` | `0x14FAF4` |

共同 teardown 为：

```text
native = adaptor.native
if native != null && !adaptor.sticky:
    native.~Player()
    operator delete(native)
adaptor.native = null
adaptor.sticky = false
```

Invalidate 和普通析构共享这段逻辑；deleting destructor 之后再 free adaptor shell。正常 internal child
wrapper 为 non-sticky，故保存它的 node/particle Variant 是 native Player 的唯一所有者。父 Player 的
root/parent raw link、visitor 借用和递归 native pointer 都不延长 child 生命周期。

empty-shell failure 的 adaptor 最终只清 null native 并销毁 shell；它无法回收从未 attach 的 supplied
Player。null/void failure 连 shell owner 都没有。两种失败均与“CreateAdaptor 失败自动 delete child”
的安全化实现不等价。

## 8. Motion subclass item

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| GetDispatch | `0x6FB4D8` | `0x5B6A00` | `0x10014DF5C` | `0x14FC54` |
| GetFlags | `0x6FB4E8` | `0x5B6A0C` | `0x10014DF6C` | `0x14FC62` |
| GetType | `0x6FB4F0` | `0x5B6A12` | `0x10014DF74` | `0x14FC68` |
| Release | `0x6FB4F8` | `0x5B6A16` | `0x10014DF7C` | `0x14FC6C` |

item 只有 vptr：GetDispatch 返回 Player ClassInfo.classObject；GetFlags 恒为 `0x10000`；GetType 恒为
`0`；Release 直接 delete item。`ncbSubClassCheck<Player>::IsSubClass=true` 只选择这条 publication
模板，不表示 C++/TJS 有另一个已恢复父类关系。

## 9. 本地源码同步

本轮只增加/修正四参考语义注释，没有改变 executable behavior：

- `cpp/plugins/motionplayer/main.cpp`：补 Player 独立 ClassInfo、Setup 前缀发布、Motion item、
  CreateEmpty、typed repeated-attach leak、两个 non-sticky producer 和 CreateAdaptor empty-shell 三态；
- `cpp/plugins/motionplayer/NodeTree.cpp`：把 type-3 null/void failure 扩展为 null 与 non-null empty shell
  两种 malformed publication；
- `cpp/plugins/motionplayer/PlayerUpdateParticles.cpp`：同样记录 particle caller 只验证 dispatch、仍用 raw
  child 继续初始化的边界。

`cpp/core/plugin/ncbind.hpp` 的现有 `ncbClassInfo`、`ncbSubClassItem`、`ncbInstanceAdaptor` 模板已经与
四端机器级行为同形，本轮没有为了“修安全”而改动其危险边界。

## 10. Recovery IDB 回写

四份 recovery IDB 已完成并原位保存：

- 8 个 typed data item：4 个 Player InfoT + 4 个 static guard；
- 4 个显式 padding 的本地类型记录：两份 LP64 32 B、两份 ILP32 16 B；
- 82 个 semantic rename：A64 20、A32 22、iOS64 20、iOS32 20；
- 84 个最终函数签名：A64 21、A32 23、iOS64 20、iOS32 20；
- 125 条 appended function/line comment：A64 30、A32 33、iOS64 31、iOS32 31；
- 4 个 V198 bookmark；
- 125 次成功 force-recompile request，覆盖 106 个 unique target；iOS64 的 Variant wrapper 在恢复
  X8 hidden-result ABI 后额外刷新自身和两名 caller；
- 四个 InfoT 的所有逻辑字段与四个 guard 均 read back 为零；
- A32 的 Invalidate/complete/deleting/shared teardown 保留真实 Thumb coalescing，不强拆伪函数；
- A64 的 Motion wrapper/adaptor-to-Variant wrapper保持真实 inline 形态；
- iOS64 wrapper 使用 `__usercall(native@X0,outVariant@X8)`，没有误写成普通双参数 ABI。

保存路径为：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 11. 验证

本轮验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 `em++ -fsyntax-only` 均通过；只有既有 `_tss`
  literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均重新编译本轮三个注释文件并
  完成最终链接；其余输出仍只是既有 `_tss`、Emscripten pthread/JSPI/JS library warning；
- Node `WebAssembly.Module` 对两份 wasm 均解析成功；Web imports/exports 为 `539/69`，Headless 为
  `538/69`；
- `llvm-objdump -h` 与文件审计结果如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,654,197 B | `7346616D20C76D17D6FF0B11B82DB0BB3D37C3A2CAC36846BDE625A07F4951E4` | `0x1BD23` | `0xD5B2` | `0x1A4219A` | `0x5A3FB7` | `0x31848C0` |
| Headless | 85,001,338 B | `FAD194A44C87C8475E39BC35FDDCD2ADE7C6295915C9E1D0C581134072C4E3CC` | `0x1BA42` | `0xD5DA` | `0x19EA148` | `0x5A1207` | `0x3140756` |

两份产物的大小、SHA-256、imports/exports 与 FUNCTION/GLOBAL/CODE/DATA/name section 均与
V197/V196 逐项一致，证明本轮注释和 recovery metadata 没有改变 executable bytes。两套 `ctest`
命令均成功，但仍报告 `No tests were found!!!`。scoped tracked `git diff --check` 通过，仅显示工作树中
既有 LF/CRLF warning；本文单独的 trailing-whitespace 扫描也通过。

## 12. 与既有 Player 报告的分工

- `motionplayer_player_ncb_surface_four_binary_2026-08-14.md`：92-row surface、typed constructor 基本
  参数边界、`clear` typed descriptor；
- `motionplayer_lifecycle_four_binary_2026-08-11.md`：Player/Engine/EmoteObject 正常构造析构总序；
- `motionplayer_node_tree_child_lifecycle_four_binary_2026-08-12.md`：type-3 node 初始化、旧树 reset 与
  null/void adaptor failure；
- `motionplayer_particle_child_lifecycle_four_binary_2026-08-12.md` 与
  `motionplayer_particle_spawn_randomization_four_binary_2026-08-15.md`：type-4 spawn、Array owner、eviction/
  worker 和随机化顺序；
- 本文：独立 Player ClassInfo/Setup、CreateEmpty、重复 attach、完整 CreateAdaptor 三态、两个 producer
  的 exhaustive xref topology、non-sticky owner teardown 与 Motion subclass item。
