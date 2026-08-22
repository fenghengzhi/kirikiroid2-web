# MotionPlayer render-layer ID latch 持久化、释放与异常重入四端复原（V237，2026-08-18）

## 1. 结论

V237 把 `PreparedRenderItem::rawFlag20` 与 `renderLayerId` 从两个相邻字段闭合为一个跨帧、
跨 command-build pass 的持久 latch，并把它与旧 node tree reset 的唯一显式释放 consumer 连成
完整生命周期。四个当前参考二进制共同证明：

- selective constructor 只把 `rawFlag20` 初始化为 false；`renderLayerId` 本身保持 dormant allocator
  bytes，不能补零；
- `drawFlag == false` 时两个字段和 `rawFlag21/clipRect` 全部不动；
- drawable gate 或 clip intersection 失败时只写 `rawFlag21=false`，既不清 latch，也不清旧 ID；
- clip 成功时先写 `rawFlag21=true` 与 `clipRect`，再惰性构造 persistent
  `SeparateLayerAdaptor`，最后才检查/申请 render-layer ID；
- `rawFlag20 == true` 时不再访问 ResourceManager，直接在后续 leaf/render 路径复用旧
  `renderLayerId`；
- false-latch 路径每次独立保有 ResourceManager，零参数调用 `requireLayerId`，忽略普通
  HRESULT，把结果 `AsInteger()` 后写 ID，再发布 latch；
- 普通失败 status 不是 gate：未写 result 时默认 Void 转成整数 0，仍会发布 `ID=0/latch=true`；
- FuncCall/转换异常发生在两个字段提交前；但 clip 和可能的新 adaptor 已经提交，不回滚；
- reset 是当前恢复范围中该 render ID 的唯一显式 release consumer。它按每个非 root node 的
  `layerId1 -> layerId2 -> latched renderLayerId` 顺序调用同一 retained ResourceManager；
- release 前不清 latch/ID，普通失败 status 被忽略并继续 teardown，异常则阻止 suffix erase 与
  label-map clear，旧 item/tree 保持发布状态，后续 retry 可以再次释放同一 ID；
- `PreparedRenderItem` 析构本身不调用 ResourceManager，也不释放这个数值 ID。

因此 `rawFlag20` 的语义不是“本帧已准备”或“当前可绘制”，而是“这个 persistent item 曾经完成
过一次 render-layer ID publication，reset 时需要回收其数值槽”。

## 2. 四目标 command-builder 映射

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| common builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| invalid clip: `rawFlag21=false` | `0x6C324C` | `0x58D2D4` | `0x1001174A0` | `0x11439C` |
| valid clip publication | `0x6C2364..0x6C2370` | `0x58C8D4..0x58C8E0` | `0x1001168A8..0x1001168B8` | `0x1143A2..0x1143B6` |
| `rawFlag20` gate | `0x6C237C` / `0x6C2524..0x6C2528` | `0x58C99C..0x58C9A4` | `0x1001169A0..0x1001169A4` | `0x1144CC..0x1144D0` |
| RM copy/AsObject/temp clear | `0x6C252C..0x6C2574` | `0x58C9AC..0x58C9BA` | `0x1001169B0..0x1001169C4` | `0x1144D2..0x1144F2` |
| no-arg `requireLayerId` | `0x6C25AC` | `0x58C9DE` | `0x1001169FC` | `0x11452C` |
| ID publication | `0x6C2614` | `0x58C9EC` | `0x100116A08` | `0x11453C` |
| latch publication | `0x6C2620` | `0x58C9E8` | `0x100116A10` | `0x114542` |
| result/RM release tail | `0x6C2624..0x6C2638` | `0x58C9F0..0x58C9FC` | `0x100116A18..0x100116A28` | `0x114544..0x11455C` |

Android arm64 为 existing-adaptor fast path 和 lazy-adaptor path 复制了物理 gate：前者在
`0x6C237C` 直接读 latch，后者构造并 begin pass 后在 `0x6C2524` 读同一 byte；两个 false
分支汇入同一个 allocation block。这是 CFG/优化差异，源级只有 adaptor gate 后的一次
`if (!rawFlag20)`。

Android armv7 又有另一种纯调度差异：`AsInteger()` 返回后，编译器先存
`rawFlag20=true`，紧接着才存 `renderLayerId`。两条 store 之间没有 call、析构、volatile access
或其他可能抛异常的操作；四端共同 C++ 源级边界仍是“转换成功后连续提交 ID 与 latch”，不能把
Thumb-2 的 store scheduling 误写成一个可重入的半 latch 状态。

## 3. 自然布局与 dormant 槽

V233 已恢复 item 的 selective constructor 与自然布局。V237 在 consumer 处再次确认：

| ABI | `rawFlag20` | `rawFlag21` | `renderLayerId` | item size |
|---|---:|---:|---:|---:|
| LP64 | `+0x14` | `+0x15` | `+0x1A8` | `0x1B0` |
| ILP32 | `+0x0C` | `+0x0D` | `+0x144` | `0x148` |

constructor 对 `rawFlag20` 写 false，却不触及末尾 `renderLayerId`。所以任何
`rawFlag20 == false` 的观察都必须把数值槽视为 dormant/stale，不得为了日志、序列化或安全释放
先读它。reset 的双 gate正是 `preparedItem != nullptr && rawFlag20`，没有对数值槽做提前读取。

## 4. 源级共同状态机

四端共同结构可还原为：

```text
for item in mainList:
    if !item.drawFlag:
        continue                         // clip、flag20、flag21、ID 全保留

    if item.rawFlag16 || !intersect(item, targetClip):
        item.rawFlag21 = false
        continue                         // flag20、ID 仍保留

    item.rawFlag21 = true
    item.clipRect = roundedIntersection  // 已发布 prefix

    if player.separateLayerAdaptor == null:
        pending = new SeparateLayerAdaptor(Window.mainWindow.primaryLayer)
        player.separateLayerAdaptor = pending
        pending.beginLayerPass()

    if !item.rawFlag20:
        rmCopy = CopyRef(player.resourceManager)
        rm = rmCopy.AsObject()            // independent AddRef
        rmCopy.Clear()
        result = Void
        rm.FuncCall(0, "requireLayerId", sharedHint,
                    &result, 0, null, rm) // ordinary status ignored
        id = result.AsInteger()
        item.renderLayerId = id
        item.rawFlag20 = true
        result.~Variant()
        rm.Release()

    materialize local corners/mesh/leaf/group ...
```

这同时确定了三个不能合并的 state：

1. `rawFlag21` 是当前 clip 结果，可在每个 drawable pass 被真/假刷新；
2. `rawFlag20` 是跨 pass 的 allocation latch，一旦成功就不因 clip/draw 变化而清；
3. `renderLayerId` 只有在 latch 真时才是 active 数值，否则只是 dormant/stale bytes。

## 5. allocation 的 owner、status 与 partial commit

false-latch block 的 owner 顺序与 V163 的广义 layer-id owner 报告一致，但 V237 补全了它在
item 状态机中的位置：

```text
clip/rawFlag21 commit
    -> optional SLA allocation/publication/begin-pass
    -> rawFlag20 read
    -> ResourceManager retained owner
    -> result Void
    -> FuncCall(requireLayerId)
    -> AsInteger
    -> ID/latch commit
    -> result dtor
    -> RM Release
```

因此：

- `FuncCall` 返回 `TJS_E_FAIL` 但不写 result：继续执行，Void 转 0，最终 latch 为真；
- `FuncCall` 写 result 后返回非零普通 status：仍按所写值转换并 latch；
- `FuncCall` 抛出：result/RM 由 unwind 清理，ID/latch 未提交；
- `AsInteger()` 抛出：同样未提交；
- 两种异常都不回滚此前的 clip，也不销毁已经发布到 Player 的 SLA；
- ID/latch 成功后，后面的 local mesh、leaf Layer 或 group Layer 路径抛出也不会撤销 latch。

四端都没有 callback 返回后的 latch recheck。由此可推导一个原生重入风险：若
`requireLayerId` 回调在外层 flag 仍为 false 时重入同一 item 的 command build，内层可以先申请并
发布一个 ID，外层返回后仍会用自己的结果覆盖数值槽；builder 本身没有释放被覆盖 ID。这个结论是
由精确 no-prelatch/no-recheck CFG 推出的边界，不假定正常游戏脚本会触发它。

## 6. reset 的唯一显式 release consumer

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| reset function | `0x6B2AD8` | `0x581F3C` | `0x100109ACC` | `0x107358` |
| prepared-item pointer gate | `0x6B2CE8` | `0x58206C` | `0x100109C94` | `0x10751C` |
| `rawFlag20` gate | `0x6B2CF4` | `0x582082..0x582084` | `0x100109CA0` | `0x107522..0x107524` |
| ID load | `0x6B2CF8` | `0x58208C` | `0x100109CA4` | `0x10752A` |
| third `releaseLayerId` call | `0x6B2D34` | `0x5820B8` | `0x100109CE0` | `0x107568` |

reset 在任何 child Invalidate 前建立一次 retained ResourceManager，并跨越整个非 root node loop。
每个 node 的顺序严格为：

```text
releaseLayerId(node.layerId1)       // unconditional, signed Integer
releaseLayerId(node.layerId2)       // unconditional, signed Integer
if node.preparedItem && node.preparedItem.rawFlag20:
    releaseLayerId(node.preparedItem.renderLayerId)
```

这里没有 `id > 0`、`drawFlag`、`rawFlag21` 或“本帧在 main list 中”的附加 gate；0 和负值同样被
转成 Integer Variant 并发送。每次调用结果为 null，普通 status 被忽略。全部调用正常返回后，函数
才 erase 非 root suffix 并清 label map。

release 前不把 latch 改成 false，也不重写 ID。因而回调中可观察到完整旧状态；如果回调抛异常，
当前及后续 node 不会被 erase，label map 也不清，RAII 只负责释放 reset-local ResourceManager。
retry 会再次经过同一个 latch。类似地，回调若重入 reset，可以在外层未清状态时重复释放同一 ID；
native 没有防重入 token 或先行 state exchange。

## 7. 源码与确定性回归

生产执行顺序本来已经与四端一致，V237 没有修改行为，只补充两处经过四端验证的语义注释：

- `PlayerRenderExecute.cpp`：记录 clip/adaptor 已先发布、allocation 不 pre-latch/recheck，以及异常时
  dormant/stale 数值槽与 false latch 保持；
- `PlayerMotionLoad.cpp`：记录第三 release 的精确 gate、release 前不清状态和 exception-before-erase。

test-only `LayerIdDispatchProbeState` 增加两个互不污染 production 的 oracle：

1. `requireLayerId` 返回普通 `TJS_E_FAIL` 且不写 result，`dispatchRequireLayerId()` 仍把 Void
   转为 0；
2. 第一个非 root node 的第三次 release（render ID 13）进入回调后抛 `eTJSError`，随后确认：
   - 已观察到调用序列 `11, 12, 13`；
   - node deque 仍含 root 与 child；
   - child item 的 `rawFlag20 == true`、`renderLayerId == 13`；
   - label map 中 `child -> 1` 仍存在。

旧 ordinary-failure 测试继续令每次 release 返回 `TJS_E_FAIL`，并证明 reset 仍走完整个序列、erase
suffix 和清 map；它与新增 throwing case共同区分普通 status 与 C++/TJS exception。

## 8. IDB 写回

四个 canonical recovery IDB 均按顺序打开、重新读取、写回、保存、health probe、关闭：

- Android arm64：10 条 comment、4 个 bookmark；额外标明 existing/lazy adaptor 两个物理 gate；
- Android armv7：9 条 comment、4 个 bookmark；标明 flag-before-ID 仅是无异常点的 store scheduling；
- iOS arm64：9 条 comment、4 个 bookmark；
- iOS armv7：9 条 comment、4 个 bookmark。

总计 37 条 comment、16 个 bookmark；没有新 rename/type。注释覆盖 constructor/latch 身份、
invalid/valid clip prefix、RM owner、零参数 call、ID/latch commit、reset gate 与 no-clear release。
最终 IDA session audit 为 0。

## 9. 验证与产物

- complete motionplayer Catch2 TU 的 ordinary/headless Emscripten syntax compilation：通过；
- Web Debug 完整构建：4 steps，通过；
- Wasmtime Headless Debug 完整构建：6 steps，通过；
- Node `WebAssembly.Module` 构造通过，imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- 两棵 CTest tree 按当前 `ENABLE_TESTS=false` 配置均报告 `No tests were found`，未虚报 runtime test；
- `git diff --check` 为零，仅有仓库既有 LF→CRLF warning；
- 生产变动只有注释，两个产品与 V235/V236 字节级一致。

| product | size | CODE | SHA-256 |
|---|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,324 B | `0x1A41979` | `ABF151F420BA5966A9DF12EBCB634D48572FE48852569F31402DC3F9BA349779` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,465 B | `0x19E9927` | `CAD49F5B55252F9E416DB67B4B23BEB83D6A788B363B8EBD81596AE19FC51AFA` |

唯一编译 warning 仍是仓库既有 `_tss` literal-operator deprecation；链接 warning 仍是既有
pthread/memory-growth、JSPI 和 Emscripten JS-library warning。

## 10. 下一边界

V237 只闭合 common builder 中 render-layer ID 的 lifetime prerequisite。V238 follow-up 已确认
local corners/mesh并非 persistent publication，恢复了 call-local affine Real与16-caller owning
mesh-point Array Variant helper；详见
`analysis/motionplayer_mesh_point_array_variant_leaf_local_geometry_four_binary_2026-08-18.md`。下一纵切面
继续恢复 leaf source descriptor/color/resolver/size 的精确 owner与异常 prefix，再进入 composed/group
Layer。private-GLL builder 的同形 latch仍在其自身完整 materialization 纵切面中单独闭合。
