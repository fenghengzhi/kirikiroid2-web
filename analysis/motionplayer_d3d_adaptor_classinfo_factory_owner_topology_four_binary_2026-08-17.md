# D3DAdaptor 独立 ClassInfo / Factory shell / owner topology 四参考恢复（V197）

## 1. 本轮结论

本轮只使用 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7
四份当前参考，重新闭合 `Motion.D3DAdaptor` 的独立 ClassInfo、Setup/RegistBegin/End、
Factory constructor descriptor、空 shell adaptor、owner gate、Motion subclass item 与完整 ClassID
xref topology。既有报告已经恢复 16 个公开 row、typed wrapper、native factory、native ctor/dtor、
software-texture map 和进程级 shared renderer；本轮补的是此前没有从四端串成一条所有权链的
ncbind 模板层。

四端共同结论：

1. `D3DAdaptor` 拥有自己的 `ncbClassInfo<T>::InfoT`、class ID、class object、Setup、
   `ncbInstanceAdaptor<T>` 与 `ncbSubClassItem<T>`，不复用 SLA、Player 或 shared renderer 的身份；
2. LP64 InfoT 为 32 B、ILP32 为 16 B；name/classObject 都是 borrowed pointer。Set/Clear 不
   AddRef/Release，也没有 lock、atomic publication 或 rollback；
3. `Factory(&D3DAdaptor::factory)` 是以动态类名 `D3DAdaptor` 注册的 constructor descriptor，
   不是字面名为 `Factory` 的公开 method。它是 16 个 row 的第一项并设置 `constructorSeen`，
   因而正常路径不安装 retained `-1002/TJS_E_NOTIMPL` dummy constructor；
4. NativeClass 的 CreateEmpty 先建立 `{native=null, sticky=false}` shell。generated Factory 的
   exactly-one-Void 分支在调用 native factory 和使用 receiver 前直接成功，因此能留下空 shell；
5. 普通 factory 成功后只向 shell 写 `native`，不写 `sticky`，也不先销毁旧 native。非法地对
   已 attach receiver 再调用 Factory 会覆盖并泄漏旧 native；
6. attach 的 receiver/query/null-adaptor 任一失败都会析构并 delete 本次新 native，返回
   `TJS_E_NATIVECLASSCRASH/-1008`；
7. 四端完整 ClassID xref 集合没有任何 plugin-side
   `existing native -> CreateAdaptor -> sticky=true` producer。两个 Player xref 都只是消费
   script-supplied D3DAdaptor；
8. `Player::draw` 后半段的 process-global shared D3DAdaptor 是独立 raw owner，只有同一函数的
   raw load/new/store，永远不进入 D3DAdaptor ClassInfo、shell 或 sticky adaptor；
9. adaptor Invalidate/析构只在 `native != null && !sticky` 时析构并 delete native，随后总是清
   native/sticky。参考插件的唯一正常 NCB producer 是 factory，所以正常脚本对象由 adaptor 所有；
10. Motion 发布的是另一个 vptr-only subclass item：GetDispatch 返回 D3D class object，flags
    为 `0x10000`、type 为 0；没有 parent ID、cast thunk、native offset，也不拥有 class object。

本轮还纠正一条确实过时的分析文字：V196 SLA 报告曾把 `-1008` 标成
`TJS_E_INVALIDOBJECT`；当前 `tjsErrorDefs.h` 与 generated ncbind 路径都表明它是
`TJS_E_NATIVECLASSCRASH`，现已修正。地址只保留在本分析与 recovery IDB；可编译注释不写
参考绝对地址。

## 2. ClassInfo 数据布局与静态初始化

| target | InfoT | guard | static init | layout |
|---|---:|---:|---:|---|
| Android arm64 | `0x1AB5810` | `0x1AB5830` | `0x42F144` | LP64 32 B |
| Android armv7 | `0x1111B34` | `0x1111B44` | `0x30162C` | ILP32 16 B |
| iOS arm64 | `0x101ADF728` | `0x101ADF748` | `0x10014FBC0` | LP64 32 B |
| iOS armv7 | `0x1831830` | `0x1831840` | `0x151BEC` | ILP32 16 B |

LP64：

```text
+0x00 uint8 initialized
+0x01 uint8 pad0[7]
+0x08 const tjs_char *name
+0x10 int32 classID
+0x14 uint8 pad1[4]
+0x18 iTJSDispatch2 *classObject
sizeof = 0x20
```

ILP32：

```text
+0x00 uint8 initialized
+0x01 uint8 pad0[3]
+0x04 const tjs_char *name
+0x08 int32 classID
+0x0c iTJSDispatch2 *classObject
sizeof = 0x10
```

四个 static init 都只测试 guard 的低 bit；未设置时清零整个 InfoT，再把 8/4 B guard 写 1。
没有 `__cxa_guard_acquire`、mutex 或原子状态机。unregister 清 InfoT，但不清 guard；之后重新注册
依赖 Setup/RegistBegin 显式重填 InfoT，而不是重跑 static init。

Android 两端保留独立 ClassInfo leaf：

| leaf | A64 | A32 |
|---|---:|---:|
| GetName | `0x6AA1D4` | `0x57CBE8` |
| GetID | `0x6AA1E4` | `0x57CBF4` |
| GetClassObject | `0x6AA1F4` | `0x57CC00` |
| IsSubClass | `0x6AA204` | `0x57CC0C` |
| Set | `0x6AA20C` | `0x57CC10` |
| Clear | `0x6AA244` | `0x57CC38` |
| InfoT ctor | `0x6AA260` | `0x57CC4C` |

iOS 两端把相同操作内联进 setup/factory/wrapper，不存在可独立恢复的同形 leaf。

Set 的共同语义：

```cpp
if (Info.initialized)
    return false;
Info.name = borrowedName;
Info.classID = id;
Info.classObject = borrowedClassObject;
Info.initialized = true;       // publication last
return true;
```

Clear 把四项都写零，不调用 classObject 的 Release。初始化 flag 最后发布只能表达当前单线程
调用顺序，不能把它误写成 acquire/release 或线程安全 once。

## 3. Motion wrapper、Setup 与注册事务

| stage | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Motion wrapper | inline call `0x6D7514` | `0x599848` | `0x100126244` | `0x12526C` |
| Setup | `0x6FC6D8` | `0x5B74C8` | `0x10014EDB4` | `0x150C70` |
| RegistBegin | `0x6FC848` | `0x5B7588` | `0x10014EE4C` | `0x150D64` |
| member registrar | `0x6AA274` | `0x57CC58` | `0x1001039A4` | `0x100D94` |
| dynamic member registrar | `0x6ECB58` | `0x5AAE9C` | `0x10013E378` | `0x13F124` |
| RegistEnd | inline | `0x5B755C` | `0x10014F050` | `0x150F58` |
| AddDummyConstructor | inline | `0x5B7704` | `0x10014F0A0` | `0x151004` |
| dummy callback | `0x6FCAC4` | `0x5B7740` | `0x10014F104` | `0x15103A` |

Setup 的 register 分支在 `Info.classObject != null` 时不重复注册并返回失败；unregister 分支即使
classObject 已空也会沿同一事务走 member registrar 和 RegistEnd。RegistBegin 的共同顺序为：

1. 从动态类名构造 NativeClass；
2. 安装 typed CreateEmpty callback；
3. 取得 class ID；
4. 把 borrowed name、ID、class object 写入 InfoT；
5. 把同一 ID 写入 NativeClass；
6. 注册 `finalize`；
7. member registrar 依次处理 16 个 row。

iOS arm64 `0x10014EE38` 与 iOS armv7 `0x150D3A` 证明 Setup 异常清理仍调用 RegistEnd 后再
resume；它不是回滚已发布 prefix 的事务。iOS 两端 RegistBegin allocation/string cleanup 分别为
`0x10014EF40` 与 `0x150E86`，只清未发布的局部 storage/temporary。

Motion wrapper 在 Setup 成功后、且仅在 register 模式，另分配一个指针宽度的 subclass item，
以 `D3DAdaptor` 名发布到 Motion。unregister 不创建新 item。

## 4. Factory row 与 constructorSeen

四端 registrar 的精确顺序保持：

```text
0  Factory(&D3DAdaptor::factory)          // dynamic name D3DAdaptor
1  setPos
2  setSize
3  setClearColor
4  setResizable
5  removeAllTextures
6  removeAllBg
7  removeAllCaption
8  registerBg
9  registerCaption
10 unloadUnusedTextures
11 visible RW
12 alphaOpAdd RW
13 captureCanvas
14 canvasCaptureEnabled RW
15 clearEnabled RW
```

Factory descriptor 交给 dynamic member registrar 时，member name 与 transaction class name 是
同一个指针/字符串。四端 registrar 都先设置 state 中的 `constructorSeen=true`，再执行
RegisterNCM。若已经为 true，会记录 multiple-constructor 日志但仍继续 publication。

RegistEnd 只在 register 且 `constructorSeen==false` 时安装 dummy；因此 D3D 正常注册永远不会
触发该分支。四端仍保留返回 `-1002/TJS_E_NOTIMPL` 的 callback，这是 ncbind template 的
fallback code，不是第二个 D3D constructor。

## 5. CreateEmpty、Factory attach 与 owner gate

### 5.1 adaptor 布局

LP64：

```text
+0x00 vptr
+0x08 D3DAdaptor *native
+0x10 bool sticky
sizeof = 0x18
```

ILP32：

```text
+0x00 vptr
+0x04 D3DAdaptor *native
+0x08 bool sticky
sizeof = 0x0c
```

| stage | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x6FC99C` | `0x5B7670` | `0x10014EF64` | `0x150EC0` |
| finalize no-op | `0x6FC9C8` | `0x5B7690` | `0x10014EF90` | `0x150EE0` |
| Factory FuncCall | `0x6ECDB8` | `0x5AB004` | `0x10013E548` | `0x13F384` |
| native factory | `0x6AA8F8` | `0x57CEBC` | `0x100103C30` | `0x100FD4` |

generated Factory 的共同控制流：

```cpp
if (membername != nullptr)
    return TJS_E_MEMBERNOTFOUND;          // result untouched
if (argc == 1 && argv[0].Type() == tvtVoid)
    return TJS_S_OK;                      // receiver/result untouched

D3DAdaptor *native = nullptr;
tjs_error hr = nativeFactory(&native, argc, argv, objthis);
if (TJS_FAILED(hr))
    return hr;                            // result untouched

ncbInstanceAdaptor<D3DAdaptor> *adaptor = nullptr;
if (objthis &&
    TJS_SUCCEEDED(objthis->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, Info.classID, &adaptor)) &&
    adaptor) {
    adaptor->native = native;             // sticky stays false
    return TJS_S_OK;
}

if (native) {
    native->~D3DAdaptor();
    operator delete(native);
}
return TJS_E_NATIVECLASSCRASH;            // -1008
```

native factory 自身仍保持旧专项恢复的五参数下限、Window validation、四次按序 integer conversion、
构造成功后才写 out pointer，以及 surplus-ignore。这里新增确认的是 shell attach：它不清 result，
不写 sticky，也不在写槽前检查/销毁 `adaptor->native`。normal path 的 one-Void shell 初始 native
为空，所以不会泄漏；只有违反预期地对同一 receiver 再执行 Factory 才会覆盖旧指针。

### 5.2 teardown

| entry | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Invalidate | `0x6FC9D0` | `0x5B7694` | `0x10014EF98` | `0x150EE4` |
| complete dtor | `0x6FCA14` | `0x5B7698` | `0x10014EF9C` | `0x150EE8` |
| deleting dtor | `0x6FCA74` | `0x5B76C0` | `0x10014EFE0` | `0x150F14` |
| shared teardown | inline | `0x5B76E4` | `0x10014F014` | `0x150F38` |

Android armv7 的四个 Thumb entry 仍由 IDA 合并在 `0x5B7694` function boundary 中；本轮只在
四个真实 entry 写 line comment，没有破坏性拆函数。共同语义：

```cpp
if (native && !sticky) {
    native->~D3DAdaptor();
    operator delete(native);
}
native = nullptr;
sticky = false;
```

factory 是参考插件内唯一正常 native producer，且 attach 不设置 sticky，所以脚本创建的 native
由 shell adaptor 销毁。sticky 分支仍是 ncbind 通用能力，但当前 D3D production call graph 不用它。

## 6. ClassID xref 与 producer topology

四端对整个 InfoT base、classID field 和相邻 literal-pool/data xref 做了全量复核。语义 consumer
分成三组：

1. generated Factory attach；
2. 所有 typed method/property wrapper 的 native unwrap；
3. `Player::drawToLayerRecursive` 与 `Player::draw` 对 script target 的 native unwrap。

两个 Player consumer：

| consumer | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| clear/recursive target route | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| direct draw route | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |

前者 unwrap 后只读取 adaptor.native 并清 target texture；后者 unwrap 后把 Player 的 useD3D byte
置真、调用 direct D3D render 并 return。两者都不创建 shell、不写 native、不写 sticky。

完整 xref 集合没有引用 production `ncbInstanceAdaptor<D3DAdaptor>::CreateAdaptor(existing)` 的
代码。portable `ncbind.hpp` 确实提供该通用 helper，单元测试也用它构造测试 target；这些是本地
test producer，不能反推参考插件存在相同 callsite。

shared renderer 的 raw slot 仍是旧专项锁定的另一张所有权图：

| target | raw slot | only owner function |
|---|---:|---:|
| Android arm64 | `0x1AB5588` | `Player::draw` |
| Android armv7 | `0x11119F0` | `Player::draw` |
| iOS arm64 | `0x101B69A28` | `Player::draw` |
| iOS armv7 | `0x187D6B0` | `Player::draw` |

它在 Player 准备出 render items 且 sticky useD3D 已为真时执行 `raw load -> new/ctor -> raw store`；
没有 ClassID query、CreateNew、CreateEmpty、NativeInstanceSupport 或 sticky byte store，也没有正常
teardown。因此准确 topology 是：

```text
script new Motion.D3DAdaptor(window,w,h,cx,cy)
    -> NativeClass CreateEmpty non-sticky shell
    -> typed native factory allocates/constructs native
    -> ClassID query finds shell adaptor
    -> native-only attach
    -> shell owns native
    -> Invalidate/dtor destroys native

Player sticky shared-render mode
    -> process-global raw slot
    -> new/construct once, publish after ctor
    -> never wrapped as Motion.D3DAdaptor
    -> no plugin normal teardown
```

## 7. Motion subclass item

| method | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| GetDispatch | `0x6FCACC` | `0x5B774C` | `0x10014F10C` | `0x151044` |
| GetFlags | `0x6FCADC` | `0x5B7758` | `0x10014F11C` | `0x151052` |
| GetType | `0x6FCAE4` | `0x5B775E` | `0x10014F124` | `0x151058` |
| Release | `0x6FCAEC` | `0x5B7762` | `0x10014F12C` | `0x15105C` |

item 只有一个 vptr 字段。GetDispatch 借用返回 InfoT.classObject；Release 只 delete item 自己，
不会 Release class object。`flags=0x10000`、`type=0`，没有继承/转型 payload。这与
`NCB_SUBCLASS(D3DAdaptor, D3DAdaptor)` 的 portable 结构一致，也再次排除它承载 shared raw
renderer 的可能。

## 8. portable 源码核对

本轮未发现需要修改 executable behavior 的差异。现有 registrar 顺序、factory 五参数协议、
one-Void shell、native attach/rollback、adaptor owner gate 都由 ncbind 与当前 D3D 源码自然生成。
只补充无地址注释，明确：

- D3D 有独立 ClassInfo/Setup；
- Factory 是 constructor row，不是 public `Factory` method；
- normal factory shell non-sticky 且 attach 只写 native；
- production call graph 没有 existing-native/sticky producer；
- shared D3D renderer 是独立 raw owner；
- Motion subclass item 是 vptr-only borrowed class-dispatch item。

同时把 V196 SLA 报告中的错误常量标签从 `TJS_E_INVALIDOBJECT` 修为
`TJS_E_NATIVECLASSCRASH`；数值与原结论仍为 `-1008`。

## 9. recovery IDB 写回

四份 recovery IDB 均完成并原位保存：

- 8 个 typed data items：4 组 D3D InfoT + guard；
- 4 个带显式 padding 的 local LP64/ILP32 layout type；
- 76 次 ClassInfo/Setup/Factory/adaptor/item 语义 rename；
- 79 个成功 function signature；
- 86 条 data/function/entry comments，其中 A32 lifecycle cluster 的三个 interior entry 保持
  coalesced boundary、只写 line comment；
- 4 个 V197 bookmarks；
- 100 个相关函数 force-recompile/readback target；
- 4 组 typed struct/guard readback 均为 logical zero。

保存路径：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

所有未知原始符号仍保留 `_guess`；身份来自四端 layout、xref、vtable、控制流和调用图一致性，
不是符号表恢复。

## 10. 验证

本轮验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 `em++ -fsyntax-only` 均通过；只有既有 `_tss`
  attribute warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；其余输出仍只是
  既有 `_tss`、`nodiscard`、Emscripten pthread/JSPI/JS library warning；
- Node `WebAssembly.Module` 对两份 wasm 均解析成功；Web imports/exports 为 `539/69`，Headless 为
  `538/69`；
- `llvm-objdump -h` 与文件审计结果如下：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,654,197 B | `7346616D20C76D17D6FF0B11B82DB0BB3D37C3A2CAC36846BDE625A07F4951E4` | `0x1BD23` | `0xD5B2` | `0x1A4219A` | `0x5A3FB7` | `0x31848C0` |
| Headless | 85,001,338 B | `FAD194A44C87C8475E39BC35FDDCD2ADE7C6295915C9E1D0C581134072C4E3CC` | `0x1BA42` | `0xD5DA` | `0x19EA148` | `0x5A1207` | `0x3140756` |

两份产物的大小、SHA-256、imports/exports 与 FUNCTION/GLOBAL/CODE/DATA/name section 均与
V196/V195 逐项一致，证明本轮注释与恢复记录没有改变 executable bytes。两套 `ctest` 命令均成功，
但当前两配置仍报告 `No tests were found!!!`。scoped tracked `git diff --check` 通过，仅显示工作树中既有的
LF/CRLF warning；本文单独的 trailing-whitespace 扫描也通过。

## 11. 与既有 D3D 报告的分工

- `motionplayer_d3d_adaptor_ncb_surface_factory_four_binary_2026-08-14.md`：16-row surface、typed
  wrapper、五参数 native factory 和 result-preservation；
- `motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md`：process-global raw shared
  renderer、首次构造、target route 与并发边界；
- `motionplayer_d3d_adaptor_constructor_failure_lifecycle_four_binary_2026-08-15.md`：native ctor
  field order、Window AddRef leak 与 new-expression cleanup；
- `motionplayer_d3d_adaptor_destructor_texture_map_four_binary_2026-08-15.md`：正常 native dtor、
  texture-map node owner 与 Release 顺序；
- 本文：独立 ClassInfo/Setup、Factory constructorSeen、shell adaptor、owner gate、Motion item 与
  ClassID producer topology。
