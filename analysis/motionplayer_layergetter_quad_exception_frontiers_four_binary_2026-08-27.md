# LayerGetter / Quad 上层容器、adaptor 与 producer 异常前沿（2026-08-27）

## 1. 结论

本轮闭合四个共享 owner/EH 问题的 coverage row：

- `MP-L09-QUAD-P-EH`；
- `MP-L10-LAYERGETTER-SHAPE`；
- `MP-L10-LAYERGETTER-ARRAY-EH`；
- `MP-L10-LAYERGETTER-PRODUCER-EH`。

底层 TJS `Items` deque 的 map/block reserve 已由
`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 闭合；
本报告继续展开上层 getter/accessor/adaptor/producer 的每个 active owner。

四端共同源码仍是普通自动 Variant/accessor 与 raw facade/native pointer 的组合，但
异常 codegen 呈现稳定的 3/1 矩阵：

- Android arm64：主函数 return 后或同一合并函数内有 landing；
- iOS arm64：Mach-O LSDA 指向紧邻 normal body、普通 code xref 为 0 的 cold cleanup；
- iOS armv7：SjLj function context 指向独立 cleanup dispatcher；
- Android armv7：完整主 body 和相邻 function catalog 都没有本帧 cleanup。

在前三端，自动 Array Variant、当前 Dictionary accessor、当前 element Variant 和 adaptor
的 Void 参数 Variant按 active state 析构；Android armv7 保留无本帧 cleanup 的目标边界。
但四端对 raw pointer 的语义一致：预分配的 shape copy/LayerGetter facade 不受 RAII
保护，`CreateNew` 抛出时不会删除；CreateAdaptor 在调用前取得的 global script dispatch
引用也不会由 cleanup 补偿。因此普通失败与 thrown failure 的泄漏边现在都有精确四端
证据，本地 C++ 无需新增 `unique_ptr`、scope guard 或 `try/catch`。

## 2. fresh 审计分母

### 2.1 Array/Dictionary getters

表中数字是完整 normal-body 指令数；所有 disassembly cursor 为 `done=true`。

| getter | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| LayerGetter.coord | `0x6994B0`；91 | `0x574A7C`；39 | `0x1000F8764`；29 | `0xF54A4`；67 |
| LayerGetter.mtx | `0x6996DC`；110 | `0x574BC8`；43 | `0x1000F88A4`；33 | `0xF5654`；74 |
| LayerGetter.color | `0x699A88`；194 | `0x574D64`；100 | `0x1000F8AB8`；41 | `0xF58FC`；85 |
| LayerGetter.bezierPatch | `0x699D90`；102 | `0x574E80`；61 | `0x1000F8B80`；50 | `0xF5A14`；91 |
| LayerGetter.vtx | `0x699894`；124 | `0x574C44`；77 | `0x1000F893C`；69 | `0xF5744`；119 |
| Quad.p | `0x68F0D4`；122 | `0x56E7F8`；75 | `0x1000F0C5C`；66 | `0xECDA4`；115 |

iOS arm64 相邻 cold cleanup：

| getter | cold cleanup | 指令数 |
|---|---:|---:|
| coord | `0x1000F87D8` | 5 |
| mtx | `0x1000F8928` | 5 |
| color | `0x1000F8B5C` | 9 |
| bezierPatch | `0x1000F8C48` | 6 |
| vtx | `0x1000F8A68` | 18 |
| Quad.p | `0x1000F0D7C` | 17 |

iOS armv7 SjLj cleanup 对应为 `0xF555A`（12）、`0xF5716`（12）、`0xF59DC`
（17）、`0xF5B06`（14）、`0xF5888`（44）和 `0xECEDE`（44）。

### 2.2 shape/adaptor/producer

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| LayerGetter.shape getter | `0x699F28`；34 | `0x574F34`；27 | `0x1000F8C60`；29 | `0xF5B38`；66 |
| shape dispatch builder | `0x68F2C0`；431 | `0x56E914`；53 | `0x1000F0DC4`；64 | `0xECF54`；53 |
| Player.getLayerGetter | `0x6D0CD4`；41 | `0x595EF4`；19 | `0x100121D64`；19 | `0x120B2C`；19 |
| Player.getLayerGetterList | `0x6D2368`；117 | `0x596CD4`；73 | `0x100122DC0`；51 | `0x121E18`；90 |
| LayerGetter CreateAdaptor | `0x6F2B1C`；92 | `0x5AFB24`；82 | `0x1001452D0`；65 | `0x145B88`；115 |
| Variant wrapper | caller inline | `0x595F20`；30 | `0x100121DB0`；29 | `0x120B58`；67 |

Android arm64 把四个 geometry class 的 CreateAdaptor body 合并在 431 条 builder 函数中；
其余三端是四个独立 tail target：

- Android armv7：`0x56E9A4 / 0x56EA8C / 0x56EB74 / 0x56EC5C`；各 82 条；
- iOS arm64：`0x1000F0ED4 / 0x1000F0FFC / 0x1000F1124 / 0x1000F124C`；各
  65 条 normal body，并各有 6 条相邻 LSDA cold cleanup；
- iOS armv7：`0xECFE4 / 0xED13C / 0xED294 / 0xED3EC`；各 115 条，并各有
  14 条 SjLj cleanup。

## 3. scalar Array getters 的精确 cleanup

coord、mtx、color 与 bezierPatch 都先创建 fresh Array closure，逐个向 native Items
追加 scalar Variant，最后才复制到脚本返回槽。底层 append 的 map/block 提交顺序由 TJS
容器报告承接；本层只持有一个自动 Array Variant。

### 3.1 Android arm64

四个函数 return 后的 landing 分别从 `0x699604`、`0x69987C`、`0x699D68`、
`0x699F0C` 开始。coord/mtx 各一条清理入口；color 的四个 integer append入口先汇合；
bezierPatch 的循环 append入口汇合。共同逻辑均为：

```text
save exception
destroy local Array Variant
_Unwind_Resume(exception)
```

### 3.2 iOS arm64

六个相邻 cold function 都没有普通 code xref，入口由 LSDA call-site table选择。coord/mtx
各五条、color 以四个 branch stub汇合、bezierPatch以一个 branch stub汇合，最后均调用
Variant destructor并 resume。它们与主函数精确相邻且复用主栈 Array slot；因此
`xrefs_to(cold)==0` 不是 dead-code/无 cleanup 证据。

### 3.3 iOS armv7

SjLj call-site state覆盖每次 append 与 result copy：coord cases 0..3、mtx 0..4、color
0..4、bezierPatch 0..2 都汇合到同一个 local Array Variant destructor，再把 state 置
`-1` 并 resume；越界/终止 state进入 abort/UDF。

### 3.4 Android armv7

四个完整函数的 normal destroy 后只剩 epilogue/stack-check，相邻地址也没有 cold cleanup
function。该目标异常穿越 getter 时没有可观察的本帧 Array 主动清理；已追加前缀不会写入
脚本返回槽，但 local Array owner可能保持未释放或由目标异常/终止模式结束进程。

## 4. vtx 与 Quad.p 的嵌套 Dictionary/Array cleanup

两个 getter 的共同 owner 形状：

```text
outer = fresh Array
repeat four times:
    current = fresh Dictionary accessor
    current.SetValue("x", ...)
    current.SetValue("y", ...)
    outer.Items.emplace_back(ObjectVariant(current.dispatch,
                                           current.dispatch))
    destroy current
return outer
```

失败可观察性严格分层：

- x 成功、y 抛出：current Dictionary已有 x，但还未追加到 outer；
- x/y 成功、append 抛出：current 已有两键，先前轮 Dictionary 已由 outer Items持有；
- 任意失败都发生在 outer 复制到脚本返回槽之前；
- Android 的 map reserve 可先提交内部 map-only change，但不改变已追加逻辑前缀；
- Player/shape 原始字段只读，不发生业务状态回滚问题。

Android arm64、iOS arm64 与 iOS armv7 的 ordinary cleanup 都先按 active state判断 current
Dictionary 是否 live；live 时恢复 accessor destructor vptr并虚 `Release`。随后无条件析构
outer Array Variant，于是 current 与所有先前 Items一起释放，最后 resume。若 accessor/
Dictionary destructor再次抛出，Android arm64 的 `sub_52138C`、iOS arm64 的
`sub_10001E2D0` 和 iOS armv7 的 `clang_call_terminate` 路径终止。

Android armv7 的两个完整函数及相邻 catalog没有对应 cleanup：该目标可能保留 current/
outer owner，但仍不会返回 partial Array。

因此 `MP-L09-QUAD-P-EH` 和 vtx 所属的 `MP-L10-LAYERGETTER-ARRAY-EH` 均已闭合。

## 5. shape 分配、CreateAdaptor 与 thrown CreateNew

### 5.1 native copy 与 tail-call

shape builder 先检查 embedded HitData type 0..3，再分配 `0x80`（前三目标）或 `0x7c`
（iOS armv7）native geometry copy并 memcpy完整 record。分配本身抛出时 native 尚未存在；
memcpy不抛。随后 builder恢复自身栈帧并 tail-enter对应 Point/Circle/Rect/Quad CreateAdaptor，
所以 caller没有 `unique_ptr` 或 delete landing。

### 5.2 CreateAdaptor 的 local owner

每个 class-specific adaptor body：

1. 读取 process-static ClassInfo class object；
2. 取得 global script dispatch；
3. 在栈上构造一枚 Void Variant作为唯一 CreateNew 参数；
4. 调 class object's CreateNew；
5. normal return后 Release global dispatch；
6. ordinary error/null返回 null，成功则查 class-specific native adaptor并附着 raw copy；
7. normal尾部析构 Void Variant。

精确 thrown CreateNew cleanup：

- Android arm64：四个合并 body各有 landing，只析构 Void Variant后 resume；
- iOS arm64：四个相邻 6 条 LSDA cold cleanup只析构 Void Variant后 resume；
- iOS armv7：四个 14 条 SjLj cleanup的 ordinary cases只析构 Void Variant后 resume；
- Android armv7：四个 82 条完整 body及相邻 catalog无 cleanup。

前三端也没有删除传入 native copy或 Release尚未到 normal平衡点的 global dispatch；
Android armv7当然同样没有。因此 CreateNew 自身抛出时，四端都泄漏 geometry copy 与
global dispatch引用；自动 Void Variant在三个目标被析构，但它本身无 payload owner。
这正对应 portable 源中 raw `new Shape` 直接传入 `CreateAdaptor`、无 scope guard 的形状。

普通 ClassInfo-null、CreateNew error/null 与 incompatible adaptor 的既有泄漏/返回边界不变。
LayerGetter.shape getter只在 builder返回 raw dispatch后构造 Object/ObjThis Variant；其
wrapper cleanup在 Android arm64、iOS arm64 cold `0x1000F8CD4` 和 iOS armv7 SjLj
`0xF5BE2` 中析构 active return Variant。Android armv7 wrapper无本帧 cleanup。

由此 `MP-L10-LAYERGETTER-SHAPE` 的 CreateNew exception metadata 不再开放。

## 6. LayerGetter producers 的 raw facade 与局部 owner

### 6.1 单对象 producer

`getLayerGetter` 先同步 resolve raw label；miss 在任何分配前返回 Void。命中后：

```text
facade = operator new(pointer-size)
facade.node = borrowed MotionNode*
dispatch = LayerGetter_CreateAdaptor(facade)
wrap dispatch into Object/ObjThis Variant or Void
```

operator new失败时 facade尚不存在。CreateAdaptor/CreateNew 抛出时 facade已经是 raw
pointer且没有自动 owner，四端都泄漏它；CreateAdaptor在三端只清理 Void参数，在 Android
armv7无 cleanup。Android arm64把 Variant wrapper内联在 producer并有 local Variant
landing；iOS arm64独立 wrapper的相邻 cold cleanup `0x100121E24`、iOS armv7 SjLj cleanup
`0x120C02` 清理 active return Variant；Android armv7 wrapper无 cleanup。

### 6.2 list producer

outer Array 在循环前创建。每轮 raw-allocate facade、构造 local LayerGetter Variant、append
到 Items，再销毁 local element。异常提交点：

- facade operator new失败：没有 facade；outer 已存在；
- CreateAdaptor抛出：raw facade泄漏，local element尚未完成；
- wrapper成功、append抛出：local element仍拥有 script shell，outer拥有此前成功元素；
- result copy抛出：完整 outer仍只在局部 owner中。

Android arm64 landing按入口选择是否先析构 current element，随后总是析构 outer Array；
iOS arm64 `Player_getLayerGetterList_unwind_cleanup_guess@0x100122E8C` 用两个入口实现相同
选择；iOS armv7 `...@0x121F0E` 的 states 0..2只清 outer，state 3先清 current再清 outer。
三端最后 resume。Android armv7无 local cleanup。

普通 adaptor null仍产生 Void element；incompatible non-null shell仍产生 Object element；
异常 cleanup不会改变这些 normal语义。由此 `MP-L10-LAYERGETTER-PRODUCER-EH` 已闭合。

## 7. 共同伪代码与必须保留的差异

```text
portable source:
    automatic Array/Dictionary/Variant owners clean themselves on source unwind
    raw preallocated geometry/facade is handed to CreateAdaptor without guard

three targets with visible cleanup:
    destroy active automatic owners
    never reclaim raw geometry/facade
    never compensate global dispatch when CreateNew throws before normal Release

Android armv7 target:
    full frame has no local cleanup body
    raw leaks remain, automatic owner cleanup is not observable in this codegen
```

不能为了让异常路径“更安全”而把 raw pointer改成 `unique_ptr`、在 CreateAdaptor失败时 delete，
或给 global dispatch加 scope guard；这些都会消除四端共同存在的 reference leak。也不能因
Android armv7无 landing而在 portable源显式 release/leak：其它三端清楚揭示共同自动对象
结构，目标差异属于异常 codegen。

## 8. 本地逐行对照

- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:57-94`：raw shape allocation、完整 HitData
  copy、class-specific CreateAdaptor与无失败 delete；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:101-123`：raw LayerGetter facade、borrowed
  MotionNode pointer、CreateAdaptor普通/thrown失败泄漏与 Object/ObjThis包装；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:189-260`：coord/mtx/vtx/color/bezierPatch 的
  fresh Array、顺序 append、局部 Dictionary accessor与末尾 return；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:263-265`：shape wrapper；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:275-288`：Quad.p 四 Dictionary/Array owner；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:716-733`：single/list producer、root排除、
  Void元素保留与末尾发布；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:449-468`：fresh Array closure与 borrowed Items。

本轮不修改 C++。自动 owner、raw leak、插入顺序、发布点和 borrowed lifetime均与联合证据
一致；修复异常泄漏会偏离 reference。

## 9. IDB 改进与 disposition

本轮在四个 IDB 中完成：

- 24 个 Array/Dictionary getter body的 fresh decompile/disassembly；
- shape builder、四类 CreateAdaptor、LayerGetter adaptor、single/list producer与 wrapper的
  四端 fresh 审计；
- iOS arm64 14 个 LSDA-only cold cleanup 和 iOS armv7 14 个 SjLj cleanup命名/注释；
- Android armv7四个 shape class adaptor语义命名；
- 关键 owner/leak/terminate cleanup 书签与四端函数注释。

四个 coverage row 均升级为 `IMPLEMENTED`。四个 IDB 已保存；正式 Debug build仍因当前
环境缺少 CMake/Ninja/Emscripten工具链不可执行，不声称已完成构建验证。
