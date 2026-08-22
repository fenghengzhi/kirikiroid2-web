# motionplayer aux group composedLayer：四边 target clip、逐子 CopyRef/live mask 与异常发布（四参考二进制）

日期：2026-08-18  
阶段：V240

## 1. 结论

四个参考二进制的 common command builder 在 leaf materialization 之后执行同一条 aux/group 尾部：

1. aux vector 为空则完全跳过；非空时按 stored pointer order 处理每个 group，pointer slot 一律受信任；
2. group bounds 从自身 `paintBox` 起步，只 union 当前 `rawFlag21 != 0` child 的 **paintBox**；
3. union 先与调用者传入的完整 `{left, top, right, bottom}` target clip 相交，再按 group valid
   viewport 做 `floor(left/top)`、`ceil(right/bottom)` 收窄；
4. empty test 故意检查 viewport 之前的 target-clamped bounds，所以 viewport 单独制造的倒置 final rect
   仍会进入 Layer path，并把负的 f32 width/height 提升为 Real；
5. target-clamped empty 只清 `group.rawFlag21`，不清 `rawFlag16`、旧 `clipRect`、旧
   `composedLayer` 或 child state；
6. `composedLayer` 只有在 Variant tag 为 Void 时才经
   `Window.mainWindow -> primaryLayer -> shared Layer factory` 惰性创建；任意 non-Void 值原样复用；
7. persistent composed Variant 被复制成一个 retained accessor/object，跨 `setSize`、透明 `fillRect`
   与全部 child mask calls 存活；
8. 每个 qualifying child 的 destination/source Variant owner、clip 与 mask state 存在可重入可见的固定次序；
9. `rawFlag21=true`、`rawFlag16=false` 与 final clip 只在全部 child mask 调用正常返回后一次性发布；
   Layer 与字典副作用不事务回滚。

这轮纠正了三个 portable 偏差：group clamp 不再从 extent 重建 origin-zero rectangle，Player
`maskMode` 不再在 child loop 外缓存，而且 child Variant CopyRef 与 clip/live state 的读取顺序不再交给
C++ 未指定的函数实参求值顺序。

## 2. 四端地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| common builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| aux pass / vector empty gate | `0x6C325C` | `0x58D2E2` | `0x1001174B0` | `0x114E8A` |
| child paint union loop | `0x6C3288` | `0x58D32E` | `0x1001174F4` | `0x114EC4` |
| camera/target empty decision | `0x6C3370` | `0x58D49A` | `0x1001175C8` | `0x115028` |
| composedLayer Void gate | `0x6C337C` | `0x58D544` | `0x1001175DC` | `0x11504C` |
| persistent composed assignment | `0x6C34F4` | `0x58D5B6` | `0x100117674` | `0x1150F0` |
| retained composed accessor | `0x6C3534` | `0x58D5E4` | `0x1001176B4` | `0x11511E` |
| child mask loop | `0x6C36B4` | `0x58D700` | `0x100117814` | `0x115288` |
| exact owner/snapshot split | `0x6C36C8` | `0x58D710` | `0x100117828` | `0x1152A0` |
| live maskMode / stencil loads | `0x6C36EC/0x6C36F0` | `0x58D734/0x58D72C` | `0x100117870/0x100117874` | `0x1152EE/0x1152EA` |
| doAlphaMaskOperation call | `0x6C3734` | `0x58D786` | `0x100117890` | `0x11532C` |
| success-only flag/clip publish | `0x6C3760` | `0x58D79C` | `0x1001178B0` | `0x115348` |

这里的地址只属于 `analysis/` 证据表；portable compiled comments 不保留任何单目标绝对地址。

## 3. 共同数据布局

`PreparedRenderItem` 的 LP64/ILP32 关键槽与 V231 的 selective constructor 布局一致：

| 字段 | LP64 | ILP32 |
|---|---:|---:|
| `rawFlag16` / `rawFlag21` | `+0x10/+0x15` | `+0x08/+0x0D` |
| borrowed `childItems` vector | `+0x18` | `+0x10` |
| `paintBox` | `+0xB8` | `+0xA0` |
| `viewport` | `+0xC8` | `+0xB0` |
| `clipRect` | `+0xD8` | `+0xC0` |
| `stencilComposite` | `+0xF4` | `+0xDC` |
| `leafLayer` Variant | `+0x130` | `+0x104` |
| `composedLayer` Variant | `+0x144` | `+0x110` |

LP64 的 `leafLayer` tag 位于 `+0x140`、`composedLayer` tag 位于 `+0x154`；ILP32 对应
`+0x10C/+0x118`。child selection 检查的是 Variant tag，不是独立 bool 或 portable sidecar。

## 4. union、target clip 与 viewport

共同伪代码如下：

```text
for group in auxPointers:
    union = group.paintBox
    for child in group.childItems:
        if child.rawFlag21:
            union = min/max(union, child.paintBox)

    targetClamped = intersect(union, callerTargetClip[0..3])
    final = targetClamped
    if viewport.right >= viewport.left &&
       viewport.bottom >= viewport.top:
        final.left   = max(targetClamped.left,   floor(viewport.left))
        final.top    = max(targetClamped.top,    floor(viewport.top))
        final.right  = min(targetClamped.right,  ceil(viewport.right))
        final.bottom = min(targetClamped.bottom, ceil(viewport.bottom))

    if targetClamped.left > targetClamped.right ||
       targetClamped.top  > targetClamped.bottom:
        group.rawFlag21 = false
        continue
```

关键边界有三项：

- target clip 是 common builder 的原始四 float 参数；不是 `{0,0,right-left,bottom-top}`；
- union 使用 child `paintBox`，不是 child 已裁剪的 `clipRect`；
- equal edge 不是 empty；只有严格 `>` 才清 marker。viewport 收窄后的倒置不参与 empty test。

portable 旧实现从 `canvasWidth/canvasHeight` 重建 `{0,0,w,h}`。所有现有产品 caller 虽然通常传
origin-zero clip，这仍破坏 common helper 自身的 translated-clip 边界，也会令测试/未来 caller 在
non-zero left/top 时错误清掉 group。

## 5. composedLayer 惰性创建与 owner 链

通过 geometry gate 后：

```text
widthReal  = Real(f32(final.right  - final.left))
heightReal = Real(f32(final.bottom - final.top))

if group.composedLayer.Type == Void:
    owner = Evaluate("Window.mainWindow")
    ownerAccessor = retained accessor(copy(owner))
    parent = ownerAccessor.GetValue<Variant>("primaryLayer", flags=0,
                                             sharedPrimaryLayerHint)
    created = Motion_createLayerVariant_guess(owner, parent)
    group.composedLayer = created

composedAccessor = retained accessor(copy(group.composedLayer))
composedObject = composedAccessor.GetDispatch()
composedObject.setSize(Real width, Real height)        // status ignored
composedObject.fillRect(Integer 0, Integer 0,
                        Real width, Real height,
                        Integer 0)                     // status ignored
```

factory return Variant 必须先完整构造，随后才 copy-assign 到 persistent field；factory 抛异常时 field
仍为 Void。assignment 之后的 accessor conversion、`setSize` 或 `fillRect` 抛异常，则新 composed owner
保留，但 group flags/clip 尚未发布。普通负 HRESULT 与 exception 必须区分：caller 忽略前者，后者进入
EH cleanup并终止 complete build。

一个 non-Void scalar/null Object 同样绕过 factory，后续 accessor/dispatch按自然 TJS 转换或空指针边界
失败；没有 `Type()==Object` 修复、null recovery 或“创建失败则重试”的额外门。

## 6. child mask 的 CopyRef、snapshot 与 live read

每个 child 先 live 检查：

```text
child.rawFlag21 != 0 && child.leafLayer.Type != Void
```

通过后，四端共同的严格顺序是：

```text
1. destinationOwner = CopyRef(group.composedLayer)
2. offsetLeft = child.clipRect.left
   offsetTop  = child.clipRect.top
3. sourceOwner = CopyRef(child.leafLayer)
4. width  = s32_sat_trunc(child.clipRect.right  - child.clipRect.left)
   height = s32_sat_trunc(child.clipRect.bottom - child.clipRect.top)
5. maskMode = live Player.maskMode
   op       = live group.stencilComposite
6. doAlphaMaskOperation(destinationOwner,
       s32_sat_trunc(offsetLeft - final.left),
       s32_sat_trunc(offsetTop  - final.top),
       sourceOwner, 0, 0, width, height, 64, maskMode, op)
7. destroy sourceOwner
8. destroy destinationOwner
```

`tTJSVariant` Object CopyRef 会对 Object 与 ObjThis 分别调用虚 `AddRef`，所以步骤 2/3 的分界真实可见：
source AddRef 重入可以改变本 child 的 width/height、Player maskMode 和 group stencil，但不能改变已经
snapshot 的 destination offset。此前 portable 先计算 width/height，再把两个 lvalue Variant 直接交给
by-value function；这既提前了 size snapshot，也把两个 CopyRef 与 scalar loads 留给 C++ 实参求值顺序。

源码现在把 alpha helper 分成两层：public/native-shaped wrapper 仍接收两个 by-value Variant；builder
显式构造唯一的 destination/source owning arguments 后进入 borrowed-owned core，避免为了控制顺序再多做
一轮 AddRef/Release。core 内部仍按原 alpha helper 语义为 destination accessor 再复制一次 owner。

## 7. 发布与异常 partial state

只有全部 selected child 都正常返回，group 才执行：

```text
group.rawFlag21 = true
group.rawFlag16 = false
group.clipRect = final
release composedAccessor raw Object
```

因此：

- `setSize` exception：没有 clear/mask/publish；
- `fillRect` exception：size side effect 已发生，mask/publish未发生；
- 第 N 个 child mask exception：size、clear 与前 N-1 个 mask side effects保留，flags/clip仍是旧值；
- success：publish 后才释放 loop-wide composed raw Object owner；
- target-clamped empty：只清 rawFlag21，不构造 composed accessor，也不触碰旧 clip/rawFlag16。

不存在 catch rollback。若 lazy factory已经赋值而后续抛异常，persistent composed Variant就是下一次
build 的 reuse 输入。

## 8. 源码与测试修正

修改：

- `Player::composePreparedGroupLayers_guess` 改为接收完整 `std::array<float,4> targetClip`，移除
  non-native `canvasWidth/canvasHeight/motionPath` 参数；
- common builder 直接传入同一 target clip；
- 删除 child loop 外的 `playerStencilType = _maskMode`；
- 显式恢复 destination owner、offset snapshot、source owner、live size/mode/stencil次序；
- `applyMotionAlphaMaskOwnedVariants_guess` 复用已构造 owning arguments，public
  `applyMotionAlphaMask_guess` 的 by-value ABI不变；
- 添加 test-only group-tail入口，不注册脚本成员。

新增三个窄 probe：

1. translated target clip `{10,11,20,21}` 下，paint `{12.25,13.5,18.75,19.25}` 正常进入
   `setSize(Real 6.5, Real 5.75) -> fillRect` 并发布原四边；旧 origin-zero实现会错误判 empty；
2. `fillRect` 抛异常时，旧 raw flags、旧 clip 与 persistent composed Object全部保留；
3. source Variant AddRef 重入把 `maskMode/stencil/clip` 改写，empty alpha path 的第二次 full-clip fill
   证明 post-CopyRef live stencil进入 helper，同时 old left/top offset snapshot与 live size split被固定。

## 9. IDB 写回与恢复记录

四份 recovery IDB 的 V240 目标写回为每库13条语义 comment、4个 bookmark，总计52 comment、16
bookmark；无新 rename/type。覆盖 aux gate、union、两阶段 clip、wrong-empty边界、Void gate、owner/factory
assignment、retained accessor、setSize/fill、child gate、CopyRef split、live mask/stencil与 success publication。

iOS armv7 在首次 `idb_close(save=true)` 后暴露 supervisor 持久化故障：工具返回 `saved=true`，但新 packed
`.i64` 经独立 `idat` 探针报告 `Database is empty`，loose files也已删除。损坏的403,543,562-byte文件以
SHA-256 `8573C30ED467AD116F62D50B6E8EBA6BE754E1DD0BFEEB23DB7E4C1BCD667EA4`
完整移动到 `out/idb-recovery/v240-ios-armv7/`，可恢复且未删除。

恢复步骤没有重新选择含糊的 FAT loader dialog：先由 Mach-O universal header验证 armv7
`offset=16384,size=26633888`，提取出的 thin slice SHA-256 为
`F4DCA688FEEF7EFB790E498C166167F40FCA3A805C7BFE7300BAE5227C55661A`，再让 IDA完成约1100 CPU秒的
完整 ARM auto-analysis。新库验证 `0x114118` 为1582-instruction common builder，全部 V240 address range与
旧四端证据一致；随后重写13 comment、4 bookmark及7个本轮必需的 inherited semantic function names。

持久化改用 live session 的显式 compressed-copy save；canonical 输出为374,905,040 bytes，SHA-256
`7E2ADC370CFC611A122BFB3DD4C94D878E840F62516DD9A81423A3D5DC344F1C`。验证顺序为：独立 `idat`
reopen成功 -> 从 canonical binary path经 MCP reopen成功 -> `Player_buildRenderCommands_guess`、child gate与
CopyRef comment读回 -> `save=false`关闭 -> final session audit。此前批次只存在旧库中的 IDA comment/
bookmark无法从损坏 packed file自动取回；对应证据仍完整保存在逐批 `analysis/` 报告，损坏库也保留，
后续若能访问 Google Drive版本历史可无损合并。当前 canonical不是空壳：它包含完整重新分析结果、V240
标注与关键 inherited names，可继续用于后续纵切面。

## 10. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax compilation：通过；
- Web Debug：semantic final 11-step、comment-only final 3-step增量构建通过；
- Wasmtime Headless Debug：semantic final 20-step、comment-only final 4-step增量构建通过；
- `krkr2_wasmtime_guest`：更新 shared objects 后重新链接并完成 exnref转换；
- Node `WebAssembly.Module` construction：通过；imports/exports仍为 Web `539/69`、Wasmtime `538/69`；
- 两棵 CTest：当前配置明确 `No tests were found`；
- Web/Wasmtime/guest final no-work：通过；
- `git diff --check`：除工作树既有 LF/CRLF提示外无 whitespace error。
- final IDA supervisor audit：0 open sessions。

| product | size | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,654,657 | `0x1A40F29` | `0x5A3E40` | `0x3185E59` | `C0A75593A6AACEE2658A932E479B6487891C87523304B622A656442D5F178900` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,001,798 | `0x19E8ED7` | `0x5A1090` | `0x3141CEF` | `5844E5632F74F60A7BD9524DFE3EB217895F1B74C7DEA8DC8F1A7A30B0FD39AA` |

相对 V239 两端 module各增加359 bytes；DATA与 imports/exports不变。增长来自显式 owner sequencing、
owned-alpha core与可读符号名，不是 persistent item layout或脚本 ABI变化。

## 11. 下一边界

V241 转入 common builder 的 normal-only retired-layer tail与 caller exception传播闭环：确认内部 SLA
active/retired tree swap、leaf/group异常时跳过 cleanup的重试状态、success tail的销毁顺序，以及 builder
返回到 canvas/accurate-SLA caller 后哪些 owner/flag被继续消费。已有早期单目标或粗粒度注释只作候选，
必须重新以四参考二进制验证。
