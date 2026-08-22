# Motionplayer `tTVPLayerManager::DetachPrimary` 重入、容器与异常边界（四参考二进制）

## 1. 结论

V280 继续只以 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、iOS armv7
四个 shipped target 为原生权威，fresh 反编译了 `DetachPrimary` 以及它可直接到达的 focus、mouse
capture、touch capture、last-mouse、modal 和 tree cleanup 链。

四端共同恢复出的源码级主序列为：

```cpp
if(Primary) {
    SetFocusTo(nullptr, true);
    ReleaseCapture();
    ReleaseTouchCaptureAll();
    ForceMouseLeave();

    auto *parted = Primary;          // 前四步之后才读取
    OverallOrderIndexValid = false;
    BlurTree(parted);
    ReleaseCaptureFromTree(parted); // 与 BlurTree 共用同一快照
    Primary = nullptr;              // 仅 normal tail；清当前 live slot
}
```

当前 portable source 的语句结构与这条链一致。本轮没有加入 RAII snapshot、临时 AddRef、容器 copy、
scope guard 或“尽量清到底”的 catch；这些安全化都会改变参考边界。本轮只把四端共同但容易被现代化
重构破坏的 publication、owner、live-vector 与异常部分提交规则写成源码注释。

最重要的边界是：

- 入口 `Primary` **不被保存**。前四个 helper 的回调若把它从 A 改成 B，后半段 part 的是 B；
- `Primary` 在前四步之后只读取一次，`BlurTree` 与 `ReleaseCaptureFromTree` 共用该快照；若它们的
  回调再把 live slot 从 B 改成 C，第二个 tree helper 仍处理 B，最终却无条件清掉 C；
- `DetachPrimary` 自己没有 catch。任一步异常都会跳过其后的动作，最终 `Primary=nullptr` 也不会执行；
- focus 的 blur/focus callback 有专门 catch 做 owner 平衡并清 focus lock，但后续 owner、IME 与
  attention 调用不在该 catch 内；
- touch/modal cleanup 都遍历 live `std::vector`，在可能导致对象析构/重入的 Owner release 或 focus
  callback 之后才读 marker、erase 或推进 iterator；原版没有 iterator 稳定化；
- `ReleaseTouchCaptureAll` 只在全部 Owner release 正常返回后 clear vector/marker。中途异常会留下完整
  vector，其中前缀 entry 对应的 owner reference 已经被释放；
- `ForceMouseLeave` 与 `LeaveMouseFromTree` 先清 manager slot，再发 `FireMouseLeave`，最后从保存的
  layer 重新读取 Owner；callback 异常会让 slot 已清但 held owner 未 release。

## 2. 四端函数映射

名字统一保留 `_guess`，因为私有 C++ 符号并未完整保留；身份由 caller/callee、主虚表 slot、字段偏移、
容器步长、callback 顺序和四端同构控制流共同确认。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `DetachPrimary` | `tTVPLayerManager_DetachPrimary_guess@0x8345E8` | `...@0x64AE72` | `...@0x10031B970` | `...@0x320AB6` |
| `SetFocusTo` | `tTVPLayerManager_SetFocusTo_guess@0x8346C4` | `...@0x64AEC0` | `...@0x10031B9E8` | `...@0x320AFC` |
| focus EH landing | main tail `0x834920` | `tTVPLayerManager_SetFocusTo_landing_guess@0x64AFAC` | `...@0x10031BB70` | SjLj `...@0x320CAE` |
| `ReleaseTouchCaptureAll` | inline in detach | `...@0x64AFE8` | `...@0x10031BBC4` | `...@0x320D40` |
| `ForceMouseLeave` | inline in detach | `...@0x64B018` | `...@0x10031BC40` | `...@0x320D86` |
| `NotifyPart` | `tTVPLayerManager_NotifyPart_guess@0x834A1C` | `...@0x64B03A` | `...@0x10031BC88` | `...@0x320DA8` |
| `BlurTree` | `tTVPLayerManager_BlurTree_guess@0x834A8C` | `...@0x64B086` | `...@0x10031BCF8` | `...@0x320DF4` |
| `ReleaseCaptureFromTree` | `...@0x834B58` | `...@0x64B0CE` | `...@0x10031BD84` | `...@0x320E3C` |
| `LeaveMouseFromTree` | inline in `BlurTree` | `...@0x64BBE6` | `...@0x10031CBDC` | `...@0x321A9A` |
| virtual `ReleaseCapture` | `...@0x8360F0` | `...@0x64BC1C` | `...@0x10031CC44` | `...@0x321AD0` |
| `RemoveTreeModalState` | `...@0x836150` | `...@0x64BC92` | `...@0x10031CCA4` | `...@0x321B00` |
| modal EH landing | main tail `0x836280` | `...@0x64BD52` | `...@0x10031CDC4` | SjLj `...@0x321C42` |

Android arm64 optimizer把 `ReleaseTouchCaptureAll`、`ForceMouseLeave`、`NotifyPart` 的三句 body 和
`LeaveMouseFromTree` 内联到本链中，但仍保留独立 `NotifyPart`。其余三端保留 touch/mouse helper；
四端 `DetachPrimary` 都把 `NotifyPart` 展开为 `valid=false -> BlurTree -> ReleaseCaptureFromTree`。

`ReleaseCapture` 通过 manager 主虚表 slot 13 调用：64-bit address-point 字节偏移 `+0x68`，32-bit
偏移 `+0x34`。这些 ABI 偏移只用于本报告取证，不写入 portable C++。

## 3. 相关字段与容器布局

V280 再次从这些函数的实际 load/store 交叉确认 V279 的 manager 布局：

| 字段 | 64-bit | 32-bit | 本链用途 |
|---|---:|---:|---|
| `CaptureOwner` | `+0x38` | `+0x1C` | clear-before-release |
| `LastMouseMoveSent` | `+0x40` | `+0x20` | clear-before-mouse-leave |
| `TouchCapture` begin/end/cap | `+0x48/+0x50/+0x58` | `+0x24/+0x28/+0x2C` | live traversal/erase |
| `ReleaseTouchCaptureIDMark` | `+0x60` | `+0x30` | 64-bit marker |
| `ModalLayerVector` begin/end/cap | `+0x68/+0x70/+0x78` | `+0x38/+0x3C/+0x40` | live modal erase |
| `FocusedLayer` | `+0x80` | `+0x44` | raw publication + delayed owner balance |
| `Primary` | `+0x88` | `+0x48` | delayed snapshot/final live clear |
| `OverallOrderIndexValid` | `+0x90` | `+0x4C` | NotifyPart 首写 false |
| `FocusChangeLock` | `+0xD8` | `+0x7C` | focus callback guard |
| `ReleaseCaptureCalled` | `+0xE4` | `+0x88` | ReleaseCapture 首写 true |
| `EnabledWorkRefCount` | `+0xE8` | `+0x8C` | modal Save/Notify nesting |

`tTVPTouchCaptureLayer` 四端都是 `{uint32 TouchID, raw BaseLayer *Owner}` 的自然 ABI 布局：64-bit entry
为 16 bytes、layer pointer 在 `+8`；32-bit entry 为 8 bytes、pointer 在 `+4`。erase 在 Android
arm64 使用展开的 16-byte copy loop，iOS arm64 使用 `memmove`，32-bit 使用 `memmove` 或抽出的
vector-erase helper；源码级语义相同。

## 4. `DetachPrimary` 的两段 publication 时间线

四端共同控制流可分成两个 publication phase：

```text
gate: read Primary != null

phase A（没有 primary local）
  SetFocusTo(nullptr,true)
  ReleaseCapture()
  ReleaseTouchCaptureAll()
  ForceMouseLeave()

publication boundary
  parted = live Primary

phase B（固定 parted，仍保留 live Primary）
  OverallOrderIndexValid = false
  BlurTree(parted)
  ReleaseCaptureFromTree(parted)
  live Primary = null
```

因此有三类不能被“简化”的结果：

1. phase A 把 A 替换成 B：phase B 处理 B，A 不再被本次 tree cleanup 使用；
2. phase B 把 live B 替换成 C：两个 tree helper 继续处理 snapshot B，normal tail 直接清 C；
3. 任一 phase 抛出：之后的 helper 和 final clear 全部跳过，已经发生的 clear/release/callback 不回滚。

`NotifyPart` 的独立 body 也严格是 `OverallOrderIndexValid=false -> BlurTree(lay) ->
ReleaseCaptureFromTree(lay)`，无 catch。其参数是单一 value snapshot；不能在第二个 call 前重读
`Primary`。

## 5. `SetFocusTo`：raw focus publication、owner 平衡与 catch 范围

通用 `SetFocusTo(layer,direction)` 的四端共同阶段为：

1. 对非空 candidate 做 focusable 检查；
2. candidate 非 shutdown 时先执行 `FireBeforeFocus(current,direction)`，允许返回替代 layer；这一步在
   `FocusChangeLock` 设置和本地 try/catch **之前**；
3. 再检查替代 candidate、same-pointer early return 和已有 focus lock；
4. snapshot `org=FocusedLayer`，置 lock=true，并把 raw `FocusedLayer=layer` 先发布；
5. try 区只覆盖 `org->FireBlur(layer)` 与 callback-reloaded current focus 的 `FireFocus(org,direction)`；
6. normal tail 对 callback-reloaded `FocusedLayer->Owner` 做 AddRef，再对 `org->Owner` 做 Release；
7. catch(...) 做同样的 current AddRef / original Release，清 lock 后 rethrow；
8. normal owner balance 后，IME callback 与 attention callback 分别重新读取 live `FocusedLayer`；二者在
   上述 catch 之外；最后才清 lock。

对 `DetachPrimary` 的 null candidate，`FireBeforeFocus` 被跳过；若旧 focus 的 `FireBlur(nullptr)` 抛出，
catch 仍保持已经发布的 `FocusedLayer=nullptr`，Release 旧 focus owner、清 lock并向外抛。于是 detach
停止且 `Primary` 仍非空，但 focus 已经清除。

反之，normal blur/focus 已完成后，若 current Owner AddRef、original Owner Release、IME 或 attention
调用逃逸，本函数没有覆盖它们的恢复 catch；lock 可保持 true。尤其不能把整个函数包进 scope guard，
也不能在 callback 前预先 AddRef 新 focus，否则都会改变引用计数和 reentry 时序。

四端 catch 语义一致，但 EH 形态不同：Android arm64 landing 位于主函数尾；Android armv7 的
`0x64AFAC..0x64AFE8` 原先未被 IDA 定义，V280 根据相邻空洞、Thumb 指令、`__cxa_begin_catch`、
owner slot 与 rethrow 序列补成独立 landing；iOS arm64 使用相邻独立 landing；iOS armv7 使用 SjLj
dispatcher。不能把“反编译主函数未显示 catch”误判为 shipped armv7 缺少 owner 平衡。

## 6. mouse capture 与 last-mouse 清理

### `ReleaseCapture`

四端共同语义为：

```text
ReleaseCaptureCalled = true
captured = CaptureOwner
if captured:
    CaptureOwner = null
    if captured->Owner: captured->Owner->Release()
    live LayerTreeOwner->ReleaseMouseCapture(this)
```

Android arm64/armv7 在写 flag 前先把 capture raw pointer装入寄存器，iOS 两端先写 flag再 load；中间没有
callback，属于 compiler scheduling 差异。真正可观察的共同边界是 manager capture slot在 Owner
release 前已为 null，而 `LayerTreeOwner` 在 release 返回后重新从 manager 读取。若 release 或 owner
callback 抛出，window release和整个 detach余下步骤被跳过；若 window release抛出，capture仍已清。

### `ForceMouseLeave` / `LeaveMouseFromTree`

二者都先 snapshot 符合条件的 `LastMouseMoveSent`，清 manager slot，再执行 `FireMouseLeave`，最后从
保存的 layer 重新读取当前 `Owner` 并 Release。`LeaveMouseFromTree` 只多了 self/ancestor test；非直接
相等分支会在 test 后重读一次 manager last-mouse field。没有临时 Owner AddRef，也没有 catch。

因此 `FireMouseLeave` 重入可改变 saved layer 的 Owner，尾部释放的是 callback 后 Owner；callback
抛出则 slot 已清而原 held Owner reference 未释放。`DetachPrimary` 先调用 `ForceMouseLeave`，随后
`BlurTree` 又调用 `LeaveMouseFromTree`，第二次通常为空，但前面的 callbacks 可以重新发布 last-mouse，
所以不能删除“重复”调用。

## 7. touch capture 的 live-vector 边界

### `ReleaseTouchCaptureAll`

四端均从 live begin 迭代到 live end，对每个 entry 无条件解引用 layer pointer，再条件 Release
`layer->Owner`。发生 Release 后会重新读取 vector end；循环正常结束后才 clear vector 并把 64-bit
marker 写为 `-1`。不存在：

- vector snapshot；
- entry 逐个 erase；
- layer null guard；
- Owner 临时 AddRef；
- 异常 cleanup 或已释放前缀记录。

因此 callback/reentry 若使 vector reallocate/erase，当前 iterator 可悬空；append 且不 reallocate 时，
live end reload 可能让新增记录进入本轮；release第 k 项抛出时，vector仍保留全部记录，marker也保留旧值，
但前 k-1 项的 owner reference 已经下降。重试可能再次 Release 同一批 entry。

不同机器码对 trivial `vector::clear` 与 marker store有可重排差异：部分32-bit目标先写 marker 的两个
dword再收 end，部分64-bit目标先收 end再写 marker。它们之间没有 callback；共享源码仍应保持普通
`TouchCapture.clear(); ReleaseTouchCaptureIDMark=-1;`，不人为制造平台分支。

### `ReleaseCaptureFromTree`

该函数先检查 live `CaptureOwner` 是否是 `layer` 的 self/descendant，命中则虚调 `ReleaseCapture`；随后
遍历 live touch vector。每个命中 entry 的严格次序是：

```text
savedLayer = itr->Owner
savedLayer->Owner->Release()       // 若非空
if live/stale itr->TouchID == marker:
    marker = -1
itr = TouchCapture.erase(itr)      // 读取 live end，移动 tail，收 end
```

owner release 位于 marker read 和 erase 之前。若其析构/重入修改 `TouchCapture`，后两步仍使用原
iterator address，可能读 stale ID、错移 tail 或访问已释放 storage；这是原版边界，不是 portable
实现应该用 copy/索引/二阶段提交修掉的问题。异常同样留下尚未 erase 的当前 entry，且跳过 Primary
最终 clear。

## 8. `BlurTree` 与 modal live-vector

`BlurTree(root)` 四端共同顺序为：

```text
RemoveTreeModalState(root)
LeaveMouseFromTree(root)
focused = live FocusedLayer
if !focused or !focused->IsAncestorOrSelf(root): return false
next = root->GetNextFocusable()
if next != live FocusedLayer: SetFocusTo(next,true)
else:                         SetFocusTo(nullptr,true)
return true
```

这里返回 true 表示进入了 focus处理分支，不代表内部 `SetFocusTo` 一定返回 true。modal cleanup可先把
focus重新设到 `root->GetNextFocusable()`，final focus block再按 callback后的 current field判定。

`RemoveTreeModalState` 对 `ModalLayerVector` 的每个命中项执行：

1. 第一次命中先置 `do_notify=true`，再 `SaveEnabledWork()`；
2. Release 当前 live entry 的 Owner；
3. 每次重新计算 `root->GetNextFocusable()` 并 `SetFocusTo(...,true)`；
4. 最后才在原 iterator 位置 erase；
5. 正常退出或 catch 路径在 `do_notify` 为真时各平衡一次 `NotifyNodeEnabledState()`。

这同样不是 snapshot traversal。Owner destruction、focus callback 或 enabled-state recursion若改变 modal
vector，erase仍使用旧 iterator。append-no-reallocation可能进入当前 live loop；reallocation/erase可让
iterator失效。

一个容易漏掉的异常边界是 `do_notify` 在 `SaveEnabledWork` **之前**置 true：如果
`Primary->SaveEnabledWork()` 自身在 `EnabledWorkRefCount++` 之前抛出，catch仍执行
`NotifyNodeEnabledState()`，可能把原 0 降到 -1。四端 EH landing 都保留了按 call-site/do_notify 状态
选择 cleanup 的行为。normal/catch notification读取的是当时 live `Primary`，并不固定为传入 root。

Android armv7 的 `0x64BD52..0x64BD8A` 也是先前未定义的 out-of-line landing；V280 已补 function、
反编译出 count decrement / live Primary notify / rethrow，与其他三端一致。

## 9. 失败点与部分提交状态

| 逃逸位置 | 已提交状态 | 明确跳过 |
|---|---|---|
| focus `FireBlur/FireFocus` | raw focus已发布；catch平衡 owner并清 lock | capture/touch/mouse/tree、Primary clear |
| focus normal owner/IME/attention | focus与部分 owner/owner通知已提交；lock可仍为 true | detach余下全部 |
| capture Owner release | `ReleaseCaptureCalled=true`，capture slot已清 | window release及后续全部 |
| window `ReleaseMouseCapture` | capture owner已 release、slot已清 | touch/mouse/tree、Primary clear |
| touch-all第 k 个 release | vector未 clear；前缀 owner已 release；marker未重置 | mouse/tree、Primary clear |
| `FireMouseLeave` | last-mouse slot已清 | saved layer Owner release、tree、Primary clear |
| modal Save/Owner/focus/erase | modal/owner/focus可能部分提交；catch只平衡 enabled-work | 后续 blur/tree、Primary clear |
| `BlurTree` | overall index已 invalid；modal/mouse/focus可能部分提交 | capture-from-tree、Primary clear |
| touch-tree Owner release/erase | earlier entries可能已 erase；当前 entry可能仍在 | remaining entries、Primary clear |

这些状态说明 `DetachPrimary` 不是 transaction，也不是“析构保证清理函数”。V279 已确认真正 manager
destructor本身不会 fallback清这些 owner；normal BaseLayer invalidation要求本链正常返回后才继续 window
unregister 与 manager Release。

## 10. 当前源码判定与改动

逐项对照后：

- `DetachPrimary` 的 helper次序、延迟 Primary read、单一 NotifyPart参数快照和 final live clear一致；
- `SetFocusTo` 的 before-focus范围、raw publication、callback后 field reload、Owner AddRef/Release与 catch
  范围一致；
- `ReleaseCapture`、`ForceMouseLeave`、`LeaveMouseFromTree` 的 clear-before-callback/release一致；
- `ReleaseTouchCaptureAll`、`ReleaseCaptureFromTree`、`RemoveTreeModalState` 都保持 live vector 与
  callback-before-erase语义；
- `NotifyPart`、`BlurTree`、Save/Notify nesting与四端一致；
- 四端只在 inlining、tail-copy/memmove和 EH ABI上不同，无需共享源码条件分支。

本轮没有语义修补；在 `cpp/core/visual/LayerManager.cpp` 添加了上述证据注释。注释不含 native绝对地址；
地址、ABI slot和字段 offset只保留在本报告。

## 11. IDB 写回

四个 canonical IDB 均写入 V280 semantic `_guess` rename与 owner/reentry/EH注释。Android armv7 额外把
两个原先未定义的 Thumb landing空洞恢复为 function：

- `tTVPLayerManager_SetFocusTo_landing_guess`；
- `tTVPLayerManager_RemoveTreeModalState_landing_guess`。

写回计数：

| 目标 | comments | renames | 新定义 function | canonical fresh readback |
|---|---:|---:|---:|---:|
| Android arm64 | 9 | 7 | 0 | 9 |
| Android armv7 | 12 | 12 | 2 | 9 |
| iOS arm64 | 12 | 12 | 0 | 9 |
| iOS armv7 | 12 | 12 | 0 | 9 |
| 合计 | 45 | 43 | 2 | 36 |

iOS armv7 先把 session状态保存为独立 candidate，fresh打开并回读 6 个关键函数；发布前 canonical
完整备份，再以 candidate覆盖 canonical，随后 canonical独立 auto-analysis/save。四端最终均在移开
loose work component 后，再从 packed `.i64` fresh打开并确认：

- `auto_analysis_ready=true`；
- `hexrays_ready=true`；
- string cache ready，大小分别为 `41135/44043/52273/52138`；
- module、input、imagebase均对应各自目标；
- `DetachPrimary` 和平台 EH landing 的 semantic name/comment可回读。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368,547,396 | `2CEE822CE57F74C7D0076C1BF634FC0B01AE8D276EEBB32FA0B65C8657E4F345` |
| Android armv7 | 346,739,084 | `DB6CEF2E451D5BDDFAB79F75E94149C300C92D63132989CDFC6AF8144EB00EEA` |
| iOS arm64 | 336,228,295 | `21908D7C3A55F176324F31085C61DC7B7CC339C5C4C4D6008424B0BE06D4F815` |
| iOS armv7 | 377,475,008 | `BFF462A48CDA657AE4546FB53DBF26079D58F5241D410FB862B758A06DCA33C8` |

pre-V280 iOS armv7 packed backup、candidate与其 loose component、canonical save后 loose component、
final packed-readback产生的 loose component都可恢复地保存在：

`out/idb-recovery/v280-layer-manager-detach/`

最终 `reference/binaries/` 只含四个原始二进制和四个 packed IDB：file count 8、loose count 0；
没有不可恢复删除。

## 12. 构建与审计

源码注释改动后的验证：

- ordinary与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 `motionplayer-dll.cpp` Emscripten
  syntax-only 均 exit 0，只有既有 `_tss` literal-operator warning；
- Web Debug 与 Wasmtime main 各执行 3 steps：重编 `LayerManager.cpp`、重链 `core_visual_module`、
  重链最终 `index.html/index.wasm`，均 exit 0；
- `krkr2_wasmtime_guest` 执行 1-step link + exnref conversion，exit 0；随后用正确 Emscripten环境
  串行复跑 Web/main/guest三目标，均为 `ninja: no work to do.`；
- 两个 build tree 的 CTest 均 exit 0，但当前都为 `No tests were found!!!`，没有把 syntax/build冒充
  runtime test execution；
- Node `WebAssembly.validate` 与 `new WebAssembly.Module` 对三份产物全部成功；
- Wasmtime core object定向反汇编再次确认 detach helper次序、delayed Primary load、NotifyPart参数
  handoff/final clear、capture/last-mouse clear-before-callback、touch clear-after-loop，以及
  `SetFocusTo` 的 wasm `try/catch/rethrow`；
- `git diff --check` exit 0；只有工作树既有 LF/CRLF conversion warning；
- `LayerManager.cpp/.h` 未新增 native绝对地址或 `sub_.../Like_0x...` 注释；最终 IDA MCP session 0、
  IDA GUI/batch process 0。

构建产物：

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85,655,133 | `539 / 69` | `ED302DA152AE45E40932A1547AC12029D92338A9EA6E5702AF09F67536F2D98D` |
| Wasmtime main `index.wasm` | 85,002,274 | `538 / 69` | `76205ACFBEECB3699C79E40FC2E5E6DBD998C697A5CC5CB147E4D1E0F598A128` |
| Wasmtime guest | 151,508,384 | `445 / 87` | `9C8DF95FF317BC2E80D86C639423898E3DCBEB0A6AAF42D54FDDB2EB26CC2131` |

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530821` |

Web/main 与 V279 的总大小/hash及所列 section长度相同。guest总大小减少 14 bytes，`.debug_str`
payload 增加 `0xB`，其余所列标准 section长度保持不变；总量净减来自表外 metadata/custom section，
不把它错误归因到 runtime code/data变化。

## 13. 下一层

高价值后续可以继续两条链：

1. `tTVPDestTexture` complete/deleting destructor内部 bitmap/texture storage、异常 terminate与 manager
   `delete DrawBuffer` 的交接边界；
2. `SetFocusTo` 的 `FireBeforeFocus`候选替换、IME/attention owner虚调和 BaseLayer focusability helper的
   完整四端边界，进一步闭合 focus callback自身的数据流。
