# MotionPlayer Player constructor unwind ladder（四参考，2026-08-18）

## 结论

V259 恢复了 `Player` constructor 的完整初值与 Dictionary→root 正常调用顺序。本轮继续检查
normal return之后的 compiler EH区域，闭合每个可能抛出点对应的 partial-construction cleanup。

四个产品的 EH编码不同：

- Android arm64 / armv7：Itanium EH，多枚 landing entry 跳入 constructor 尾部的共享清理
  ladder；
- iOS arm64：Itanium EH，一个带多个合法内部入口的 compiler fragment；
- iOS armv7：SJLJ，constructor 明确写 `call_site`，独立 dispatcher switch选择清理起点。

但 source-level 语义完全一致：

1. 只销毁进入异常点前已经成功构造/发布的 local和member owners；
2. 两个 Dictionary factory-return raw owners按 colors → descriptor 逆序释放；
3. 自动 member cleanup严格按 V258 的 declaration reverse order；
4. raw POD、`SeparateLayerAdaptor *` 空槽和 final residual dispatch没有自动 cleanup；
5. 最后原样 `_Unwind_Resume` / `_Unwind_SjLj_Resume`，不 catch、不翻译、不吞异常；
6. cleanup 自己若传播异常，进入 terminate path，而不是用第二个异常替换原异常。

当前源码在 V259 调整后已通过 declaration order、`DispatchReleaseGuard_guess` 局部声明顺序和
root位置自然生成该拓扑；V260 不需要额外运行时代码改动。

## 1. iOS armv7：显式 SJLJ call-site 状态机

iOS armv7 constructor 在第一个可能抛出的 persistent ResourceManager CopyRef 前注册
`SjLj_Function_Context`，随后在每个 observable throwing frontier 前写 `fctx.call_site`：

| stored call_site | constructor operation | dispatcher case | first cleanup frontier |
|---:|---|---:|---|
| 1 | CopyRef find-source RM Variant | 0 | rootContent 及更早 members |
| 2 | CopyRef source-cache RM Variant | 1 | find-source RM，然后 case 0 |
| 3 | construct `tTVPComplexRect drawRegion` | 2 | pending strings / source workspace，然后更早 |
| 4 | CopyRef canonical RM Variant | 3 | live strings/event vector/drawRegion，然后更早 |
| 5 | create descriptor Dictionary | 4 | late maps/Variants，然后更早；无 local raw owner |
| 6 | assign descriptor member Variant | 5 | release descriptor local，然后 case 4 |
| 7 | create colors Dictionary | 6 | descriptor-only local release，然后 case 4 |
| 8 | assign colors member Variant | 7 | colors → descriptor locals，然后 case 4 |
| 9 | `descriptor.color = colors` PropSet | 8 | colors → descriptor locals，然后 case 4 |
| 10 | synthetic-root append / initialization | 9 | colors → descriptor locals，然后 case 4 |
| 11 | normal-tail colors local Release | 10 | terminate-on-cleanup-throw path |
| 12 | normal-tail descriptor local Release | 11 | terminate-on-cleanup-throw path |

SJLJ personality把 stored 1-based call-site映射为 dispatcher 的 0-based case。cases 12/13覆盖
其他 cleanup-in-progress terminate edges；case 14 直接 abort，default 是不应到达的 trap。

dispatcher anchors：

| role | address |
|---|---:|
| SJLJ dispatcher | `0x11DA22` |
| colors-local entry | `0x11DA48` |
| descriptor-local entry | `0x11DA84` |
| automatic-member ladder | `0x11DAC4` |
| final node-label cleanup / resume | `0x11DB92 / 0x11DB9E` |

这份显式状态机排除了“所有异常都调用完整 destructor”的误解。构造失败时对象从未成为完整
`Player`，dispatcher只回滚已完成前缀；例如第一个 RM CopyRef失败不会尝试析构尚未构造的
source-cache owner或任何 Dictionary local。

## 2. 共同 reverse member ladder

从最晚 frontier进入时，四端都执行下列 source-level顺序；较早 frontier只从对应段中间进入，
然后继续向下：

```text
colors factory-return raw owner (if published)
descriptor factory-return raw owner (if published)

variableLabelScopes deque
variableSnapshotMap (HM4)
perNodeLayerStateMap (HM3)

tagFrameSource Variant
meshline Variant
outline Variant
findMotionContext Variant
canonical ResourceManager Variant

stealthMotion ttstr
motionKey ttstr
stealthChara ttstr
chara ttstr
pendingEvents vector
drawRegion

pendingStealthChara ttstr
pendingStealthMotion ttstr

internalSourceWorkLayer Variant
sourceColors Variant
internalRenderLayer Variant
sourceDescriptor Variant
sourceCacheObject Variant
findSourceResourceManager Variant

rootContent Variant
priorityFrameSource Variant
motionContent Variant
emoteMotionList Variant
emoteDivision Variant

parameterRampMap
parameterEntries vector
evalResultValues HM2
evalCascadeMap HM1
nodes deque
nodeLabelMap

resume original exception
```

这条 ladder 也解释三个看似特殊但正确的边界：

- final raw dispatch在 late deque之后，但从不出现在 cleanup；它不是 owner；
- scalar/control cluster位于 late Variants和 HM3之间，但全是 trivial storage，landing pad直接跨过；
- raw `SeparateLayerAdaptor *` 在 constructor 中仍为 null且自身是 POD；constructor unwind不调用
  Player explicit destructor，也不执行 adaptor delete分支。

## 3. root allocation failure 的精确状态

root append 是 call-site最晚的正常 body throw frontier。进入它之前：

- 33 个 nontrivial Player members均已完成构造；
- descriptor/colors两个 persistent member Variants已分别拥有 Dictionary；
- `descriptor.color` 已成功增加 colors引用；
- descriptor/colors factory-return raw owners仍各持一份引用；
- node deque已 default-constructed但尚无成功提交的 synthetic root。

若 deque growth或 MotionNode construction抛出：

1. colors raw owner Release；
2. descriptor raw owner Release；
3. 从 variable deque开始执行完整 automatic-member ladder；
4. 到 node deque时，STL只销毁实际成功构造/记入 size的元素；半构造 root由 deque实现自己的
   insertion rollback处理；
5. node-label map清理后恢复原异常。

这正是 V259 将 root从 Dictionary之前移到 PropSet之后的生命周期理由。若只比较成功路径的
最终对象状态，会完全看不到该差异。

## 4. Android arm64 Itanium-EH 形态

Android arm64 把 landing entries与完整 reverse ladder保留在 `Player_ctor_guess` 的尾部，normal
`RET` 在 `0x6CC634`，随后是 allocation slow-path、landing stubs和 cleanup blocks：

| role | address |
|---|---:|
| call-site landing entry cluster | `0x6CC67C..0x6CC6E8` |
| temporary Variant / colors / descriptor local guards | `0x6CC6EC..0x6CC738` |
| variable deque begins automatic-member ladder | `0x6CC738` |
| late Variants / strings / event / rect | `0x6CC7EC..0x6CC88C` |
| pending strings / source workspace | `0x6CC894..0x6CC8E4` |
| root/type-1 Variants / parameter containers | `0x6CC8E4..0x6CCA08` |
| node deque / node-label map / resume | `0x6CCA08..0x6CCA20` |

Android libstdc++ maps/deques有 eager allocation，因此 landing ladder内可见逐 node销毁、bucket
memset、非-inline bucket delete等更展开的实现；这只是 container ABI细节，source-level owner
顺序仍与两个 iOS libc++目标相同。

## 5. Android armv7 Itanium-EH 形态

Android armv7 normal return在 `0x593916`。IDA没有把紧随其后的 cleanup fragment定义成独立
source function，但 listing 中全部指令和 landing labels完整存在：

| role | address |
|---|---:|
| terminate / landing entry cluster | `0x59391C..0x593976` |
| colors / descriptor guards | `0x593978..0x5939AC` |
| variable deque / HM4 / HM3 | `0x5939AC..0x5939BE` |
| late owners / strings / event / rect | `0x5939BE..0x593A1C` |
| pending strings / workspace | `0x593A1C..0x593A56` |
| root/type-1 / parameter / early containers | `0x593A56..0x593AA6` |
| resume | `0x593AAE` |

该 fragment与 A64相同地让多个 landing entry直接跳到不同 label，而不是运行时维护显式整数
switch；最终 reverse序列逐项一致。

## 6. iOS arm64 Itanium-EH 形态

iOS arm64 的 compiler fragment有七个内部 entry stubs：

```text
0x10011EFB4 -> descriptor-local frontier
0x10011EFC0 -> automatic-member frontier
0x10011EFC8 -> live-string/event/rect frontier
0x10011EFD0 -> pending/source-workspace frontier
0x10011EFD8 -> root/type-1 frontier
0x10011EFE0 -> early-container frontier
0x10011EFE8 -> colors then descriptor local frontier
```

主要 anchors：

| role | address |
|---|---:|
| compiler unwind fragment | `0x10011EFB4` |
| colors / descriptor guards | `0x10011EFE8..0x10011F024` |
| variable deque begins full ladder | `0x10011F024` |
| live owners / workspace / type-1 | `0x10011F088..0x10011F114` |
| early containers / node deque / map | `0x10011F114..0x10011F18C` |
| resume | `0x10011F190..0x10011F194` |

该 fragment不是原始 C++ 可调用 private method；recovery IDB 名称
`Player_ctor_unwind_ladder_guess` 只是语义导航名，保留 `_guess` 防止冒充原始 spelling。

## 7. cleanup-throws / terminate 边界

`DispatchReleaseGuard_guess` destructor、Variant/string/container destructors均处于 C++ cleanup
语境。四端 landing data都为 cleanup call本身准备 terminate入口：

- iOS armv7 SJLJ cases 10–13转入异常清理终止 helper，case 14 abort；
- Android arm64在 ladder尾部保留一串指向 terminate helper的 landing stubs；
- Android armv7 cleanup fragment前后保留 terminate helper calls；
- iOS arm64 normal/cleanup landing entries旁保留对应的 compiler terminate thunks。

因此不能把 guard destructor改成吞异常的 `try/catch(...)`，也不能在 Player constructor catch后
继续返回半构造对象。虽然 TJS `Release()` 正常 ABI不应抛 C++异常，这一终止行为仍是参考边界的
一部分。

## 8. 本地源码核对

V259 后的 constructor局部声明顺序是：

```cpp
iTJSDispatch2 *descriptor = TJSCreateDictionaryObject();
DispatchReleaseGuard_guess descriptorGuard{descriptor};
sourceDescriptor = ...;

iTJSDispatch2 *colors = TJSCreateDictionaryObject();
DispatchReleaseGuard_guess colorsGuard{colors};
sourceColors = ...;

descriptor->PropSet(...);
ensureRootNode_guess(*this);
copy(defaultTransformOrder, root.transformOrder);
```

C++局部对象逆序析构自然得到 colors guard → descriptor guard；`Player.h` 的 111-field顺序自然
得到完整 member ladder。无需手写 constructor-level `try/catch`。手写 catch反而容易重复释放
members、错误清理未构造字段或吞掉原异常。

V260 没有修改编译源码；只补 recovery IDB和分析文档。V259 构建产物因此保持不变。

## 9. recovery IDB 写回

四库各写回 4 comments / 4 bookmarks，共 **16 comments / 16 bookmarks / 2 semantic renames**：

- landing entry / SJLJ dispatcher；
- colors→descriptor local guard ladder；
- full reverse automatic-member ladder；
- original-exception resume terminal。

renames：

- iOS arm64 `0x10011EFB4` → `Player_ctor_unwind_ladder_guess`；
- iOS armv7 `0x11DA22` → `Player_ctor_sjlj_unwind_dispatch_guess`。

iOS armv7 different-path 安全保存：

- pre-V260 backup：
  `out/idb-recovery/v260-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v260.i64`，
  377,632,976 bytes，SHA-256
  `8A6EB6E604296D3D3DEFD08FF3A543A7B03560A43937AB9449129C5140F2AF48`；
- candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v260.i64`；
- `C:\IDA\idat.exe -A` candidate probe退出 0；
- candidate覆盖 canonical前确认两个 resolved paths都在 workspace内；
- canonical重新打开后回读 4 条 V260 comments和 dispatcher semantic rename，再关闭。

最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,728,756 | `958BCE4FC89B4E366DFBCEA1FC182C46A934ECD9532FA6EF4564BCF8920B10EB` |
| Android armv7 | 345,878,941 | `1CE8B16AEA2BC290A8CF30FBE5054F256DC6221AF84415E6CD1DC5F24F454B03` |
| iOS arm64 | 334,892,999 | `81AF4369EA5AC6A47F6DFE5226340476E1ED9673AC150E74A48190DA513B2228` |
| iOS armv7 | 377,673,936 | `7D49913540FBAEC727A6608F25A4D59C6256D9705C3BA614D2CA8D839E087B92` |

最终 IDA process/session 数为 0。

## 10. 验证与产物基线

V260 没有编译源码改动。为排除未收敛依赖，实际再次执行：

- Web Debug：`ninja: no work to do`；
- Wasmtime Headless Debug：`ninja: no work to do`；
- guest：`ninja: no work to do`；
- `git diff --check` 无 whitespace error，仅工作树既有 LF/CRLF warning；
- IDA process/session 数为 0。

Wasm保持 V259基线：

| wasm | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `index.wasm` | 85,655,322 | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` | `B4FB58B358283E7C48D767AC3C6B37DDE2AE452E047808FEA132EA2FBADED514` |
| Wasmtime `index.wasm` | 85,002,463 | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| guest | 151,479,107 | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` | `4851CDB6B3E6C6DFF0159D2847232426685F7F7FA7651690F12537A3436CE387` |

## 11. 本轮闭合与后续方向

V260 闭合了 `Player` constructor 从声明、初值、正常 body到异常回滚的完整生命周期。下一高价值
方向可转向 `EmoteEngine` 对 Player unique owner的 construction/publication failure boundary，核对
Player constructor抛出时 Engine member slot是否尚未发布、后续 controllers是否未构造，以及四端
Engine unwind如何串接 Player operator delete；这会把 Player内部 partial construction接到外层
对象图，而不是重复审计已经闭合的 member ladder。
