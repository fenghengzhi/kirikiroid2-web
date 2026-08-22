# Motionplayer continuous-event hook/handler 容器、投递与压缩纹理生命周期（四参考二进制）

## 1. 结论

V287 对 `reference/binaries/` 中四个发布目标重新做 fresh 反编译，闭合此前 V282 只恢复了
`TVPAdd/RemoveContinuousEventHook`、但尚未恢复的完整 continuous-event 数据流：

- `TVPDeliverAllEvents` 如何消费 pending flag 并进入 continuous delivery；
- raw callback hook 与 `tTJSVariantClosure` handler 的两套独立 vector；
- add/remove、重复项、AddRef/Release、静态析构与 backing storage 的分工；
- callback 内追加、扩容、自移除、移除未来项和递归投递的精确行为；
- null tombstone 的延迟压实、`TVPExclusiveEventPosted` 提前返回和 `TVPEventDisabled`；
- handler 返回失败、handler 抛异常、异常类型转换/显示与 processing flag 恢复；
- `tTVPSoftwareTexture2D_compress` 的 multiple-inheritance secondary-base 注册、刷新、回调和析构；
- Android libstdc++、iOS libc++、AArch32 SJLJ 与 64-bit DWARF EH 的实现差异。

四端共同的源码级结论是：当前两套 `std::vector`、墓碑式 remove、live `size()` 循环、三层
`try`/异常宏和 compressed texture 的 `PixelFrameLife` 逻辑原本就是正确方向。真正的默认路径偏差是
后来加入的 WCHAIN continuous diagnostics：它在未打开 trace 时也会从 add/remove/delivery 以及
SystemControl Begin/End/pump 跨入 JS、构造 `URLSearchParams`、递增额外静态计数，并制造参考目标不
存在的分配/异常点。本轮将它隔离为 Emscripten 专用且默认关闭的编译期诊断；普通构建不再产生任何
相关调用、计数或依赖。

## 2. 证据范围与来源纪律

本轮只把四个 reference binary 当作原始实现权威，当前 C++ 只用于最后逐行对照。四个 packed IDB
均从 canonical 库重新打开，函数逐个 fresh decompile；大函数的远端 landing pad 使用局部 disassembly
核对，避免把 Hex-Rays 隐去的 EH 当成“不存在”。

| 目标 | ABI / STL / EH | 关键特点 |
|---|---|---|
| Android arm64-v8a | AArch64、libstdc++ 风格 vector、DWARF EH | public delivery 与大部分 landing pad 保持在一个 IDA function |
| Android armeabi-v7a | ARM EABI、libstdc++、DWARF EH | 4-byte hook、8-byte closure；handler growth 入口被 IDA 错并入前一 allocator function |
| iOS arm64 | AArch64、libc++、DWARF EH | vector slow path 使用临时 buffer/swap；delivery EH 被拆成多个函数 |
| iOS armv7 | Thumb-2、libc++、SJLJ EH | delivery 主体与约 `0x5ae` 字节的 SJLJ 状态机分离，明确出现 `"continuous event"` |

没有使用旧 `libkrkr2.so` 地址、注释或反编译作为本轮结论的来源。旧报告仅用于指出待闭合问题，
其措辞若与本轮四端机器码冲突，以本文为准。

## 3. 四端函数映射

### 3.1 event pump、delivery、add/remove

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TVPDeliverAllEvents` / pump caller | `0x8DDB80` | `0x6B0D20` | `0x1000E470C` | `0xE0518`，normal call `0xE153C` |
| `TVPDeliverContinuousEvent` + inlined inner | `0x8DEC8C` | `0x6B17C8` | `0x1000E553C` | `0xE1754` |
| delivery current-closure/outer EH | 同函数远端块 `0x8DEFA4`、`0x8DF004` 起 | 远端块 `0x6B19DC`、`0x6B1A1E` 起 | `0x1000E57F8` | SJLJ `0xE1A4C` |
| typed exception conversion/show | 同函数 `0x8DF010..0x8DF464` | `0x6B1A2E..0x6B1CAC` | `0x1000E587C` 及后续 landing pads | 同 SJLJ `0xE1A4C` |
| add raw hook | `0x8DFDEC` | `0x6B23A8` | `0x1000E6424` | `0xE279E` |
| remove raw hook | `0x8DFEE0` | `0x6B2410` | `0x1000E6488` | `0xE27DC` |
| add closure handler | `0x8DFF20` | `0x6B244C` | `0x1000E64BC` | `0xE280E` |
| remove closure handler | `0x8DFFEC` | `0x6B24E8` | `0x1000E6590` | `0xE2888` |
| Begin scheduling | `0x906284` | `0x6C6E48` | `0x1001D764C` | `0x1D5A24` |
| End scheduling | `0x906400` | `0x6C6F4C` | `0x1001D7748` | `0x1D5BAC` |

四个 pump caller 都只在 continuous pending byte 为真时清掉该 byte 并调用 delivery；正常事件泵还在
此前/此后处理普通、idle 与 window-update 队列。`TVPDeliverContinuousEvent` 自身不读取 pending byte，
所以直接调用仍会执行；pending 是 pump 层的门。

### 3.2 vector growth、静态初始化与析构

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| hook slow growth | add body内联 | `0x6B35E0` | `0x1000E8254` | `0xE486C` |
| handler slow growth | `0x8E1C70` | distinct entry `0x6B36A8`，IDA并入 `0x6B368C` | `0x1000E848C` | `0xE4AFC` |
| logical closure destroy | `0x8DFD7C` | `0x6B2358` | `0x1000E6398` | `0xE2750` |
| hook storage dtor | `0x8DFD6C` | `0x6B2344` | thunk `0x1000E6394` | thunk `0xE274C` |
| handler storage dtor | `0x875E00` | `0x672C58` | thunk `0x1000C60BC` | thunk `0xC3DC4` |
| static init | `0x42FD3C` | `0x302180` | `0x1000E87D0` | `0xE4ED4` |

Android armv7 的 `0x6B36A8` 有独立 prologue、参数约定和 return，却被 IDA 作为前一个 allocation helper
的尾块。为避免破坏 canonical function range，本轮没有强制 undefine/redefine，而是在真实入口留下完整
语义注释。其机器码仍足以确认：分配 `8 * newCapacity`，写新 closure pair，逐 8-byte raw copy 旧 pair，
删除旧 storage，再发布 begin/end/cap。

## 4. 两套内部容器与所有权

### 4.1 raw hook vector

结构是普通三指针 `std::vector<iTVPContinuousEventCallbackIntf *>`：

| ABI | element | vector triplet |
|---|---:|---|
| 64-bit | 8-byte raw pointer | begin / end / cap，各 8 bytes |
| 32-bit | 4-byte raw pointer | begin / end / cap，各 4 bytes |

它是严格非 owning 容器：

- add 不 `AddRef` callback；
- delivery 只虚调用 slot 0；
- remove 只写 null；
- compaction 只 raw `memmove`；
- static destruction 只 delete backing allocation；
- 从未虚析构、Release 或 delete callback object。

`TVPAddContinuousEventHook(cb)` 无 null/duplicate guard：先 `TVPBeginContinuousEvent()`，再 append。慢扩容
抛异常时 scheduling 已启动，而 pointer 尚未进入 vector；四端均无 rollback catch。

`TVPRemoveContinuousEventHook(cb)` 扫描实时 `[begin,end)`，把**所有**等于 `cb` 的项写 null。`cb == null`
也合法，此时只是把已有 tombstone 再写 null。remove 不 erase、不缩容、不 End。

### 4.2 closure handler vector

`tTJSVariantClosure` 正好是 raw `(Object, ObjThis)` pair：64-bit 为 16 bytes，32-bit 为 8 bytes；类本身
没有 RAII lifetime responsibility。容器所有权完全由 surrounding code 手工管理：

```text
Add(clo):    Object->AddRef(); ObjThis->AddRef(); raw append pair
Remove:      Object->Release(); ObjThis->Release(); pair = {null,null}
AtExit:      对每个 live pair 做同样 Release；end = begin
Storage dtor:只释放 allocation
```

add 先 `std::find` 精确比较两个 pointer，已有相同 pair 则完全 no-op。不存在时的顺序是：

```text
TVPBeginContinuousEvent()
clo.Object->AddRef()   // if non-null
clo.ObjThis->AddRef()  // if non-null
vector.emplace_back(raw pair)
```

四端 slow-growth unwind 都只释放临时 storage，不会调用 closure `Release()`。因此 allocation failure 会
同时保留 Begin side effect，并让本次刚取得的 Object/ObjThis refs 没有 container owner。这是参考实现的
异常边界，不能用 RAII transaction 静默“修好”。

remove 只找第一个 exact pair；由于 add 禁止 live duplicate，正常情况下足够。找到后按 Object、ObjThis
顺序 Release 并清零；不 erase、不 End。

`Object == null, ObjThis != null` 甚至全 null 的 malformed closure 也可被 add。delivery 不调用它，
但会把它当 tombstone；compaction 仍调用 `Release()`，所以残留 ObjThis ref 会被释放。

## 5. 共同 delivery 伪代码

下面省略了 ABI register 与 STL helper 差异，但保留所有可观察顺序：

```text
TVPDeliverContinuousEvent():
    if processing:
        return
    processing = true

    try:
      try:
        try:
          TVPStartTickCount()
          tick = TVPGetTickCount()       // once per pass

          if hooks not empty:
              sawNull = false
              i = 0
              while i < hooks.size():   // live size, reloaded after callback
                  cb = hooks[i]          // live base, safe after reallocation
                  if cb:
                      cb->OnContinuousCallback(tick)
                  else:
                      sawNull = true

                  if TVPExclusiveEventPosted:
                      return             // no compaction, no End
                  ++i

              if sawNull:
                  erase every null hook  // raw shift, retain capacity

          if !TVPEventDisabled && handlers not empty:
              sawNull = false
              vtick = Variant(int64(tick))
              i = 0
              while i < handlers.size():
                  if handlers[i].Object:
                      try:
                          er = handlers[i].FuncCall(..., &vtick, null)
                      catch:
                          handlers[i].Release()
                          handlers[i] = {null,null}
                          throw

                      if TJS_FAILED(er):
                          handlers[i].Release()
                          handlers[i] = {null,null}
                          sawNull = true

                      if TVPExclusiveEventPosted:
                          return         // no compaction, no End
                  else:
                      sawNull = true
                  ++i                   // live size/base again

              if sawNull:
                  for every Object-null pair:
                      pair.Release()     // catches residual ObjThis
                      erase raw pair

          if hooks.empty() && handlers.empty():
              TVPEndContinuousEvent()

        catch (...):
          processing = false
          throw
      TJS_CONVERT_TO_TJS_EXCEPTION
    TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION("continuous event")

    processing = false
```

`TVPEventDisabled` 只屏蔽 TJS closure handler；raw hooks 仍会执行。这使 compressed texture 的 buffer
expiry 不依赖 script-event enable state。若仅 handler vector 非空且事件被禁用，delivery 不调用 handler，
也不会 End，因为 vector 仍非空。

## 6. live mutation、墓碑与重入边界

四端循环都不是 begin/end snapshot。回调返回后，机器码按 index 重新读取 vector begin 和 end；因此
vector reallocation 不会留下旧 iterator，append 出来的项也属于本轮。

| callback 中的操作 | 本轮结果 |
|---|---|
| append 一个新 hook/handler | 新项位于新 end 之前，本轮稍后执行 |
| append 触发 reallocation | index 保留，base/end 重读；仍安全且同轮执行 |
| remove 当前 hook | 当前 slot 调用前已被判定 non-null，`sawNull` 不会因此置位；若后面没有 null，墓碑留到下轮 |
| remove 当前 handler 后成功返回 | 同上；pair 变 null，但本轮不自动标记 `sawNull` |
| remove 未来项 | 循环后来看到 null，置 `sawNull`，正常尾部压实所有 tombstone |
| remove 已访问的早先项 | 除非后面另有 null，否则本轮可能不知道它已成为 tombstone |
| nested `TVPDeliverContinuousEvent` | processing 已为 true，立即 no-op |
| hook/handler remove-self 后 re-add self | old slot 成 tombstone，新 pair append；新 pair 本轮再次执行 |
| 每次回调都 remove-self + re-add | end 与 index一起增长，可形成同轮无界循环/增长，直到 exclusive、异常或 allocation failure |

raw-hook loop 在 null slot 后也检查 exclusive；handler loop 只在 non-null handler 调用完成后检查，纯 null
handler slot 不检查。通常 pump 在进入 continuous phase 前已排除 exclusive，但直接调用 public function 或
回调期间置位时，这个细节可观察。

exclusive 在 callback 后、compaction 前检查。因而：

- callback 已经产生的 tombstone 保留；
- current closure 的 negative-status Release/清零已经发生，但 erase 未发生；
- `TVPEndContinuousEvent` 不执行；
- processing 仍通过 public function 的正常 return tail 清零。

如果 exclusive 每轮都在同一点重新置位，含 tombstone 的逻辑 vector 可以长期保持非空，continuous
scheduling 也继续存在。

64-bit vector 的 allocator 理论上允许远超过 `2^32` 个 element，但源码和四端机器码的 delivery index
都是 32-bit `tjs_uint`/`tjs_uint32`。在不可现实分配但语义明确的超大边界上，index 会回绕；32-bit
vector 的 max_size 小于该阈值。

## 7. handler 失败与异常

### 7.1 negative `tjs_error`

`FuncCall` 返回负值时，当前 slot 立即按 Object、ObjThis 顺序 Release、清零并置 `sawNull`。随后仍检查
exclusive；若未置位，继续 live loop，正常尾部 erase。若置位，清零已提交但 tombstone 未压实。

### 7.2 `FuncCall` 抛异常

四端都有 current-slot 专用 catch：

1. 用保存的 index/byte offset重新读取**当前** vector base；所以 handler 内 append 导致 reallocation 后仍
   能找到原 slot；
2. Release Object；
3. Release ObjThis；
4. 两个字段清零；
5. rethrow。

随后 tick variant 被析构，outer catch 在异常转换前把 processing 清零。该异常路径不执行 compaction 或
End，当前 tombstone 留给以后。

### 7.3 conversion/show

Android AArch64 的 typed landing pads位于 `0x8DF010..0x8DF464`；Android armv7 为
`0x6B1A2E..0x6B1CAC`；iOS arm64 从 `0x1000E587C` 开始；iOS armv7 的 `0xE1A4C` SJLJ switch
直接展示各 type case。共同结构与当前宏一致：

- 支持的 std/TJS exception 转为 `eTJSError`、`eTJSScriptError`、`eTJSScriptException` 或
  `eTJSSilent`；
- script error/exception 以 `"continuous event"` 为上下文显示/处理；
- unsupported/unmatched exception 继续 rethrow / `_Unwind_Resume`；
- 无论是被处理还是继续传播，processing 都已先恢复为 false。

raw hook 抛异常没有 current-slot Release，因为 hook vector不 owning。若 hook 在抛出前自移除，其墓碑
保留；否则 pointer 仍 live，下轮会再次调用。

## 8. compressed software texture 的 secondary-base 生命周期

共同 object layout：

| member/base | 64-bit | 32-bit |
|---|---:|---:|
| primary object | `+0` | `+0` |
| `BmpData` | `+32` | `+24` |
| `tTVPContinuousEventCallbackIntf` secondary base | `+40` | `+28` |
| `PixelFrameLife` | `+48` | `+32` |

32-bit 和 iOS arm64 明确保留 thunk：vector 中存 adjusted secondary pointer，virtual thunk 分别减 28/40
后进入 complete-object callback body。Android arm64 在优化后的函数形态中直接以 adjusted pointer访问
`callback+8` life、以 `callback-40` 访问 primary fields；语义完全相同。

共同流程：

```text
GetPixelData:
    if BmpData == null:
        allocate bitmap bits
        decode until dst reaches Pitch * Height
    if PixelFrameLife == 0:
        TVPAddContinuousEventHook(adjusted secondary this)
    PixelFrameLife = 3
    return BmpData

OnContinuousCallback(adjusted this):
    --PixelFrameLife
    if PixelFrameLife != 0:
        return
    if BmpData:
        free BmpData
        BmpData = null
    PixelFrameLife = 0
    TVPRemoveContinuousEventHook(adjusted this)

complete destructor:
    install compress primary and secondary vptrs
    if BmpData:
        free BmpData
        BmpData = null
        TVPRemoveContinuousEventHook(adjusted this)
```

可达时序细节：

- `GetPixelData` 在 life 非零时只刷新为 3，不重复注册；
- 若某个更早的 hook 在当前 delivery 中首次调用该 texture 的 `GetPixelData`，新 hook 同轮执行，life
  立即从 3 变 2；“next 3 frames”并不保证从下一次 pump 才开始计数；
- expiry callback 自移除当前 slot 时，若后续没有 null，旧 tombstone 留到下轮；
- expiry 后的更晚 hook 若同轮再次 `GetPixelData`，会重新分配、append 新 secondary pointer，新项也会
  同轮执行；旧 tombstone 和新 live entry 可同时存在；
- destructor 只在 `BmpData != null` 时 remove。正常 expiry 已经 free/null/remove，所以不重复扫描；从未
  materialize 的对象也从未注册；
- callback 在 life 0 被异常调用会把它减成 `-1` 并返回，不 free、不 remove；四端均无 underflow guard；
- hook vector 不持有 texture 引用。对象生命周期安全依赖析构在仍有 BmpData/registered hook 时执行
  tombstone remove，而不是 vector ownership。

## 9. STL/ABI 差异，但非源码语义差异

### 9.1 Android libstdc++

Android armv7 的 `_M_check_len` 上限直接可见：

- hook：`0x3fffffff` 个 4-byte pointer；
- closure：`0x1fffffff` 个 8-byte pair。

AArch64 对应上限为 `0x1fffffffffffffff` 个 8-byte pointer和 `0x0fffffffffffffff` 个 16-byte pair。
slow path 以 `oldSize + max(oldSize,1)` 增长；只有 capacity 满时进入，所以等价于 0→1、其后翻倍。

### 9.2 iOS libc++

libc++ slow path计算 `max(2 * oldCapacity, size + 1)`，在临时 vector buffer 中先写新 element，再 raw
transfer 旧 range并 swap/publish。unwind 会销毁临时 buffer allocation；hook pointer 和 closure pair
都是 trivial/raw element，不产生 callback ownership或 closure Release。

### 9.3 EH ABI

- Android 两端和 iOS arm64 使用普通 C++ unwind landing pads；
- iOS armv7 使用 `SjLj_Function_Context` 与 call-site switch；
- ABI 形态不同，但 current closure cleanup、processing reset、typed conversion、show/consume 和 unmatched
  rethrow 的源码级顺序一致。

## 10. 本轮源码修正

### `cpp/core/base/EventIntf.cpp`

- WCHAIN continuous trace 现在仅在
  `EMSCRIPTEN && TVP_ENABLE_WCHAIN_CONTINUOUS_EVENT_TRACE=1` 时编译；
- 默认构建完全移除 `<string>`、spdlog、emscripten、JS URL query、stack trace、trace sequence与 call
  counters；
- pump/add/remove/delivery 的 trace call site 也在预处理期消失，不留下默认 no-op call；
- 添加 raw hook 非 owning、closure manual ownership、priority-10 logical destruction、Begin-before-growth、
  allocation failure不回滚、live mutation、self-remove tombstone和 exclusive skip 的四端证据注释；
- 保持原有核心语句与顺序，不把已证实的异常非原子边界改写成 RAII transaction。

### `cpp/core/visual/RenderManager.cpp`

- 补充 `BmpData == null` 析构分支的两种来源；
- 记录 secondary-base offset、同轮 append 可见与 life 3→2 的可达路径；
- 记录 life 0→-1、self-remove current tombstone 可能延迟到下轮压实；
- 不改变任何运行语句。

### `cpp/core/environ/win32/SystemControl.cpp`

- 对同一编译期开关隔离 Begin、End 与 `DeliverEvents` 三个 WCHAIN call site；
- 默认构建移除 spdlog/emscripten include、JS URL probe 与 system-control trace sequence；
- 保留 `ContinuousEventCalling`、pending flag、`InvokeEvents` 和 main-thread priority 的参考顺序。

## 11. IDB 写回

四个 V287 candidate 均已逐库保存并从 packed file 冷启动验证函数名和关键注释：

```text
out/idb-recovery/v287-continuous-event/candidate/
    Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64
    Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64
    Kirikiroid2_1.3.9_iOS_arm64.i64
    Kirikiroid2_1.3.9_iOS_armv7.i64
```

随后先核对发布前 canonical 与 V286 backup hash 完全一致，才把 V287 candidate 逐文件复制到
`G:\My Drive\reference\binaries\`。发布后的 candidate/canonical 尺寸和 SHA-256 如下；四组都逐字节
相同：

| 目标 | packed bytes | V287 SHA-256 |
|---|---:|---|
| Android arm64-v8a | `368556045` | `EB0B3D60EDD311B5877B82EA7A1699E59877F86A663A1360E3741B65774B71A2` |
| Android armeabi-v7a | `347599424` | `C923A16444574F068D3DD52A2B2401ADF15040D0129FAAF616BAA47227290824` |
| iOS arm64 | `337114215` | `359B12723A172E10F332DB4FFC9AFC02266C16F8D822A505BD994B3779380954` |
| iOS armv7 | `378836767` | `04CF165D9A8834F7A4CA3653A5DC3E121482164B7F4D939D80663F091CDAD75F` |

最终又从四个 canonical packed file 分别建立全新 IDALib session，并读回：

- Android arm64：delivery、add-handler、Begin、compressed callback 与主注释；
- Android armv7：delivery、add-handler、hook growth、`TVPDeliverAllEvents` 与主注释；
- iOS arm64：delivery、EH cleanup、add-handler、handler growth、`GetPixelData` 与主注释；
- iOS armv7：delivery、SJLJ EH、add-handler、handler growth、`TVPDeliverAllEvents` 与主注释。

四个 session 均正常关闭；最终 `idb_list` 为 0。candidate、canonical 和 thin-Mach-O 工作目录产生的
loose `.id0/.id1/.id2/.nam/.til` 均已逐目标核对并删除，只保留 packed `.i64`。此外清除了 V284/V285
遗留的 57 个可再生 loose sidecar；连同本轮 iOS armv7 的 3 个 sidecar，共回收
`4669243488` bytes，不触碰任何输入二进制或 packed IDB。

## 12. 构建与静态验证

- `cmake --build out/web/debug` 在 EventIntf/RenderManager 修正后通过；加入 SystemControl 的同期开关后
  再次通过，最终复跑为 `ninja: no work to do`；
- 从 Ninja 取出 `EventIntf.cpp` 和 `SystemControl.cpp` 的真实 Emscripten compile command，分别追加
  `-DTVP_ENABLE_WCHAIN_CONTINUOUS_EVENT_TRACE=1 -fsyntax-only`，两者均返回 0；所以默认关闭路径与显式
  开启诊断路径都经过编译器；
- 最终 Web 产物为 `index.html=87111`、`index.js=634997`、`index.wasm=85612628`、
  `vlfs.js=42548`、`assets.zip=7858873` bytes；本配置没有 `index.data`；
- 构建只出现既有 `_tss`、LZ4 deprecated 与 pthread/JSPI 配置警告，没有新增编译或链接错误。

## 13. 后续边界

V288 已完成这里列出的后续工作：四端全部 add/remove xref 已穷举，compressed texture 外仅有 Layer
transition、layerExMovie 与 MoviePlayerLayer 三类 owner；`-contfreq` limit thread、SystemControl pump、
priority-100 shutdown、回调 vtable/thunk/body、remove+re-add 与 duplicate Play 边界也已闭合。见
`analysis/motionplayer_contfreq_systemcontrol_raw_hook_registrants_four_binary_2026-08-22.md`。核心 vector、
delivery、platform scheduler 与内建注册者均不再是待恢复项。
