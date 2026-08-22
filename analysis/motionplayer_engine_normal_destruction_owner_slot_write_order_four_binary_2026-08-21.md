# EmoteEngine 正常析构、尾部容器与 owner slot 写序（四参考二进制，2026-08-21）

## 1. 本纵切面的目的

本报告是 V264，对 `EmoteEngine` **正常析构**做一次从 raw wind owner、HM7–HM4、
三个 `tTJSVariant`、七个 direct controller owner 到 Player owner 的四端逐指令复核。
重点不是再次罗列已经恢复的高层逆声明顺序，而是闭合旧报告仍然含糊、且有一句写反的
owner-slot 边界：

- pointee destructor/operator-delete 与 owner member 写 null 的先后关系；
- Android arm64 把下一 owner load 与上一 owner null-store 交错的具体方式；
- iOS 两端先清 slot、Android 两端后清 slot 的稳定差异；
- raw `_windEmitter` 为什么四端都只有 delete、没有 slot clear；
- 这些 concrete instruction schedules 应如何归一回 portable source，而不把单一 ABI 的
  优化结果手写进 Web 端。

此前的总序报告是
`analysis/motionplayer_engine_destruction_order_four_binary_2026-08-11.md`。本轮重新读取的是
`reference/binaries/` 当前四份 recovery IDB，不沿用旧 `libkrkr2.so` 注释作为证据。

## 2. 四端入口与结论摘要

| 目标 | 正常析构入口 | 指令数 | STL/ABI 形态 |
|---|---:|---:|---|
| Android arm64 | `0x67C898` | 304 | old libstdc++，大量 unordered 容器析构内联 |
| Android armv7 | `0x5610E8` | 71 | old libstdc++，容器与 owner-slot helper 化 |
| iOS arm64 | `0x1001B8B4C` | 97 | libc++，尾部容器与 deque helper 化 |
| iOS armv7 | `0x1B814E` | 99 | libc++，同一 source order 的 Thumb lowering |

四端共同的 source-level 顺序仍是：

```text
delete raw wind owner; do not clear the dying Engine slot

destroy HM7 variable-values
destroy HM6 variable-controller-refs
destroy HM5 variable-ranges
destroy HM4 instant-variable-labels
destroy Variant variableFrameLists
destroy Variant variableLabels
destroy Variant variableLabelsBase

reset parts owner
reset hair owner
reset bust owner
reset angle owner
reset color owner
reset scale owner
reset position owner
reset Player owner

destroy active/diff/main timeline vectors
destroy HM3 timeline states
destroy HM2 mirror-miss set
destroy HM1 mirror-match set
destroy mirror-pattern vector
destroy controller/spring deque #10 -> #1
```

本轮新增的精确结论是：

| 目标族 | direct owner 的 concrete slot write |
|---|---|
| Android arm64 | pointee dtor/delete，读取下一 owner，清零上一 owner slot；Player 后读取下一 vector prefix，再清 Player slot |
| Android armv7 | owner helper 内 pointee dtor/delete，最后清零当前 slot |
| iOS arm64 | 读取当前 owner，立即清零当前 slot，再做 null test、pointee dtor/delete |
| iOS armv7 | 与 iOS arm64 相同：读取、清零、null test、pointee dtor/delete |

因此旧报告中 Android armv7 helper“取出、清零并执行析构/delete”的描述是写反的；已经原位
改成“取出、若非空则析构/delete、最后清零”。同时也不能再把 Android arm64 的 store
流水化笼统成“四端都会把 store 移到相邻 load 后面”：iOS 两端清楚地保留了先清 slot 的
次序。

## 3. 尾部字段四 ABI 映射

以下按声明正序列出；析构按表的逆序执行。

### 3.1 direct owners 与 wind

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player | `+0x428` | `+0x214` | `+0x2B8` | `+0x15C` |
| position | `+0x430` | `+0x218` | `+0x2C0` | `+0x160` |
| scale | `+0x438` | `+0x21C` | `+0x2C8` | `+0x164` |
| color | `+0x440` | `+0x220` | `+0x2D0` | `+0x168` |
| angle | `+0x448` | `+0x224` | `+0x2D8` | `+0x16C` |
| bust outer-force | `+0x450` | `+0x228` | `+0x2E0` | `+0x170` |
| hair outer-force | `+0x458` | `+0x22C` | `+0x2E8` | `+0x174` |
| parts outer-force | `+0x460` | `+0x230` | `+0x2F0` | `+0x178` |
| raw wind emitter | `+0x468` | `+0x234` | `+0x2F8` | `+0x17C` |

Player 与七个 controller 字段是单指针 owner；wind 是不同的 raw-owner replacement
协议，不能因为它在地址上紧邻 owners 就把它归为同一 `unique_ptr` specialization。

### 3.2 Variant 与 HM4–HM7

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `variableLabelsBase` Variant | `+0x4B8` | `+0x280` | `+0x348` | `+0x1C4` |
| `variableLabels` Variant | `+0x4CC` | `+0x28C` | `+0x35C` | `+0x1D0` |
| `variableFrameLists` Variant | `+0x4E0` | `+0x298` | `+0x370` | `+0x1DC` |
| HM4 instant-label set | `+0x4F8` | `+0x2A4` | `+0x388` | `+0x1E8` |
| HM5 variable-range map | `+0x530` | `+0x2C0` | `+0x3B0` | `+0x1FC` |
| HM6 variable-ref map | `+0x568` | `+0x2DC` | `+0x3D8` | `+0x210` |
| HM7 variable-value map | `+0x5A0` | `+0x2F8` | `+0x400` | `+0x224` |

Android 的 old-libstdc++ unordered object 是 0x38/0x1C 字节，iOS libc++ 对应对象是
0x28/0x14 字节；这个布局差异解释了各列步长，不改变 HM7→HM4 的逆声明析构顺序。

## 4. 四端逐指令调用链

### 4.1 Android arm64

入口 `0x67C898`：

1. `0x67C8AC` 读取 wind `+0x468`；非空时 `0x67C8B4` 直接调用 scalar
   `operator delete`。没有对 `+0x468` 的零写；`EmoteWindEmitter` 的析构为 trivial，故没有
   单独 destructor call。
2. `0x67C8B8..0x67C910` 内联 HM7，`0x67C910..0x67C968` 内联 HM6，
   `0x67C968` 调 HM5 helper，`0x67C970..0x67C9C8` 内联 HM4。
3. `0x67C9C8/0x67C9D0/0x67C9D8` 依次析构 frame-lists、labels、labels-base
   三个 Variant。
4. `0x67C9E0` 开始 parts→hair→bust→angle→color→scale→position→Player。

arm64 的 owner slot 形成稳定的“落后一格”流水：

```text
load parts(+0x460)
if non-null: controller dtor; operator delete
load hair(+0x458)
store 0 -> parts(+0x460)

if hair non-null: controller dtor; operator delete
load bust(+0x450)
store 0 -> hair(+0x458)
...

load Player(+0x428)
store 0 -> position(+0x430)
if Player non-null: Player dtor; operator delete
load active-vector begin/end(+0x410/+0x418)
store 0 -> Player(+0x428)
```

关键指令对包括：

- parts delete 后，`0x67C9F8` 先 load hair，`0x67C9FC` 才清 parts；
- hair delete 后，`0x67CA14` 先 load bust，`0x67CA18` 才清 hair；
- angle 对象的 deque 析构被内联，但 `0x67CA7C` load color、`0x67CA80` 清 angle 的
  流水形态不变；
- Player 在 `0x67CAE0/0x67CAE8` dtor/delete，`0x67CAEC/0x67CAF0` 先读取下一
  timeline vector prefix，`0x67CAF4` 才清 Player slot。

若某 owner 原本就是 null，对应 dtor/delete 被跳过，但流水化的 slot-zero 仍在下一阶段执行。

### 4.2 Android armv7

入口 `0x5610E8` 是最紧凑的 71-instruction wrapper：

```text
0x5610EE  wind delete, no slot clear
0x5610F8  HM7 helper
0x561100  HM6 helper
0x561108  HM5 helper
0x561110  HM4 helper
0x561118  frame-lists Variant
0x561120  labels Variant
0x561128  labels-base Variant
0x561130  parts owner helper
0x561138  hair owner helper
0x561140  bust owner helper
0x561148  angle owner helper
0x561150  color owner helper
0x561158  scale owner helper
0x561160  position owner helper
0x561168  Player owner helper
```

这里跨 owner 没有 arm64 的 caller-side 流水，因为写序封装在小 helper 内：

- `0x56351C`：普通 `EmoteVarController` owner slot；load，若非空则 dtor/delete，最后
  store null；
- `0x563C44`：angle owner slot；angle deque dtor/delete，最后 store null；
- `0x563C5E`：Player owner slot；Player dtor/delete，最后 store null。

即使输入 slot 已经为 null，helper 末尾仍把 slot 写成 null。旧报告把这三个 helper 写成
clear-before-delete，V264 已纠正。

### 4.3 iOS arm64

入口 `0x1001B8B4C`：

```text
0x1001B8B5C  wind delete, no slot clear
0x1001B8B68  HM7
0x1001B8B70  HM6
0x1001B8B78  HM5
0x1001B8B80  HM4
0x1001B8B88  frame-lists Variant
0x1001B8B90  labels Variant
0x1001B8B98  labels-base Variant
0x1001B8BA0  parts owner begins
0x1001B8C2C  Player owner begins
```

每个 owner 都是 libc++ 风格的同槽序列：

```text
load owned pointer from member
store 0 to that same member
if pointer is non-null:
    run pointee destructor
    operator delete(pointer)
```

parts 在 `0x1001B8BA0` load、`0x1001B8BA4` clear、`0x1001B8BAC` dtor、
`0x1001B8BB0` delete；Player 在 `0x1001B8C2C` load、`0x1001B8C30` clear、
`0x1001B8C38` dtor、`0x1001B8C3C` delete。中间六个 controller 全部保持相同顺序。

### 4.4 iOS armv7

入口 `0x1B814E` 与 iOS arm64 完全同构：

```text
0x1B8154  wind delete, no slot clear
0x1B815E  HM7
0x1B8166  HM6
0x1B816E  HM5
0x1B8176  HM4
0x1B817E  frame-lists Variant
0x1B8186  labels Variant
0x1B818E  labels-base Variant
0x1B8196  parts owner begins
0x1B821C  Player owner begins
```

parts 是 `load +0x178 -> store 0 +0x178 -> null test -> dtor -> delete`；Player 是
`load +0x15C -> store 0 +0x15C -> null test -> Player dtor -> delete`。hair、bust、angle、
color、scale、position 逐槽重复。与 arm64 iOS 一样，null owner 仍执行无害的 zero store，
只跳过 pointee teardown。

## 5. 生命周期与边界含义

### 5.1 raw wind 与 single-pointer owners 不是一种协议

wind normal destruction 的四端共同形态是：读取 raw member，非空则 scalar delete，随后直接
进入 HM7；没有清 slot。此时整个 Engine 正在死亡，后续析构阶段不会再读取 `_windEmitter`。
因此成员在 free 后暂时保留 dangling bits 是参考实现的 concrete boundary，而不是应在本地
“顺手安全化”为 null 的状态转换。

七个 controller 与 Player 则不同：它们在 Engine 函数体尚未结束时被 reset 成空 owner，
以便稍后的自动 member destructor 只看到空状态、不会 double-delete。其共同所有权语义和
销毁顺序是四端一致的，只有 store 的指令调度不同。

### 5.2 pointee destructor 期间 owner slot 的可见值

逐指令事实是：

- Android：pointee destructor 执行时，Engine slot 尚保留旧 pointer；
- iOS：pointee destructor 执行时，Engine slot 已经是 null；
- wind：free 前后都没有显式 null store。

这是需要保留在分析层的真实边界。如果未来发现某个 pointee destructor 通过父对象反查该
slot，平台差异可能变成可观察行为；本纵切面没有证据表明当前 controller/Player teardown
会这样重入 Engine，因此不凭空添加 callback、guard 或平台条件分支。

### 5.3 `reset()` 是正确的 portable source 恢复层级

本地字段已经恢复成 single-pointer `unique_ptr` owners，函数体显式按 native phase 调
`reset()`。这同时保证：

- pointee destructor/operator-delete 在 HM4/Variant 之后、早期 timeline/deque 成员之前；
- owner 变空，函数退出时的自动 member destruction 不重复释放；
- null owner 不触发 pointee destructor；
- 由目标标准库和优化器决定 concrete store schedule。

把 Android 的 delete-then-null 或 iOS 的 clear-before-delete 手写成两个条件实现，会把
编译器 lowering 误提升为插件 source contract，且没有四端共同证据支持这种 source 分叉。

## 6. 本地源码审计结果

`cpp/plugins/motionplayer/EmoteEngine.cpp` 的行为无需修改：

- `_windEmitter` 仍使用单独 `delete`，不追加 `_windEmitter = nullptr`；
- HM7→HM4 仍通过 `releaseContainerStorageAtNativePhase` 在精确阶段释放节点和 backing
  storage；
- 三个 Variant 仍按 frame-lists→labels→labels-base 调 `Clear()`；
- controller 与 Player 仍以八次 `reset()` 实现共同 owner semantics；
- 后续 timeline/HM/deque 阶段不变。

本轮只修正源码注释：明确 raw wind slot 不清零，并把 Android 后清零、Android arm64
流水化、iOS 先清零写成 ABI/optimizer 证据，避免将过时的单目标印象继续传播。旧总序报告中
Android armv7 helper 的先后顺序也已原位纠正。没有为了匹配某条 store 指令改变 C++ 行为。

## 7. recovery IDB 写回与 armv7 安全保存

本轮写回 **21 comments / 21 bookmarks / 1 semantic rename**：

- Android arm64：5 comments/bookmarks，覆盖 wind、HM frontier、Variant frontier、owner
  delete-then-null pipeline、Player delayed clear；
- Android armv7：6 comments/bookmarks，覆盖相同阶段以及普通 controller/Player owner helper
  的 delete-then-null body；
- iOS arm64：5 comments/bookmarks，覆盖 wind、HM、Variant、owner clear-before-delete、Player；
- iOS armv7：5 comments/bookmarks，并把入口 `0x1B814E` 从 `sub_1B814E` 改为
  `EmoteEngine_dtor_guess`。

名称保留 `_guess`，因为 recovery 证明了语义角色而没有原始符号可恢复。

iOS armv7 继续使用 different-path 流程：

- pre-V264 backup：
  `out/idb-recovery/v264-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v264.i64`，
  377,764,048 bytes，SHA-256
  `C0F26AC010CF85F2C48F99991D340BA29BAC35B150BDDF677D91BF41DC7C2CD5`；
- 在同目录 candidate 上写入，关闭并保存；
- `C:\IDA\idat.exe -A` candidate probe 退出 0；
- 覆盖 canonical 前分别验证 candidate 位于 workspace、canonical 位于明确的
  `reference/binaries` 目录；
- probe 后 candidate 与最终 canonical 均为 376,770,393 bytes，SHA-256
  `59B095303D26E1266313349A58801918F6A70619C5F72CAC9585A1F2DD6AA8A9`；
- canonical 重新打开，成功回读 `EmoteEngine_dtor_guess` 和五处 V264 comment，再以
  `save=false` 关闭。

四份最终 IDB：

| IDB | size | SHA-256 |
|---|---:|---|
| Android arm64 | 366,753,243 | `630358DBE4FE44BC7D4C3EB7ABECE45F8E36FDBEFB4A54A534DAD3CE027302F2` |
| Android armv7 | 345,911,620 | `5FEDD65F41E1E8191220D193F4228FA8C4730FDA3C2EE5EC5B9229F115D1A41C` |
| iOS arm64 | 334,950,258 | `727192CF1C7D3A250ABA312BE7D8ED2F557DBE213FDEF4519ECC886A993355AC` |
| iOS armv7 | 376,770,393 | `59B095303D26E1266313349A58801918F6A70619C5F72CAC9585A1F2DD6AA8A9` |

## 8. 验证

V264 只有注释与分析修正，不改变编译语义，但仍完成全套门槛：

- 复用真实 Emscripten response file，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 ordinary 与
  `KRKR2_WASMTIME_HEADLESS=1` 两套 syntax-only；均退出 0，只有仓库既有 `_tss`
  deprecated warning；
- `out/wasmtime/debug` 完成 4-step rebuild/link；
- `krkr2_wasmtime_guest` 完成 1-step wasm link/exnref conversion；
- `out/web/debug` 完成 3-step rebuild/link；
- 三目标随后各再运行一次，全部为 `ninja: no work to do`；
- 编译 warning 仅为既有 `_tss`、pthread+memory-growth、JSPI experimental 与 JS library
  symbol warning；没有新增 error/warning 类别。

最终 Wasm：

| artifact | size | SHA-256 | FUNCTION | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|
| Web `index.wasm` | 85,655,322 | `6039AA6D8DC48FB7CCC5840CFF7630EEE9838C1AB2809BCEE5B096BCD42EEC6F` | `0x1BD31` | `0x1A4109D` | `0x5A3E40` | `0x3185F7B` |
| Wasmtime `index.wasm` | 85,002,463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` | `0x1BA50` | `0x19E904B` | `0x5A1090` | `0x3141E11` |
| Wasmtime guest | 151,479,107 | `CCEDF1B8182BFBAAA9B141827737E77C3AC2FBD5F41731AA69BA7D3C1F8DEF0E` | `0x1618E` | `0x13D7DCD` | `0x4D1630` | `0x1421EBA` |

三份 size 与关注 section size 都和 V263 基线一致；Web/Wasmtime 主产物 hash 也保持不变。
guest 因本轮实际重新链接而 content hash 更新，但其 size 与全部关注 section size 不变，符合
“仅注释、无编译行为变化”的预期。`git diff --check`、新报告 trailing-whitespace 检查与零
IDA session/process 审计也已通过。

## 9. 后续边界

本报告闭合的是正常析构的尾部 owner/container phase，不等价于整个 Engine 生命周期已经完成。
后续仍需继续从四端检查：

- normal destructor 更早的 HM1–HM3 与十组 deque element 内部释放细节是否还有未命名 helper；
- deleting destructor / outer owner 调用 normal destructor 后的 allocation 回收边界；
- Engine 被脚本 wrapper、Player/adaptor 和异常路径持有时的最终释放调用链；
- 仍残留旧 `libkrkr2.so` 或单目标地址语义的源码注释。
