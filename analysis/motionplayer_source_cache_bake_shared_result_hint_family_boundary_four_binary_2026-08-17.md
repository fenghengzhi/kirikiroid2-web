# SourceCache bake 共享 result 与连续 method-hint family 四参考复原（2026-08-17）

## 结论

四个 `reference/binaries/` 目标共同证明，`SourceCache::bakeSource` 过去仍有一个会被脚本
dispatch 直接观察到的 ABI 偏差：原版在 `drawLayer` 之前构造一个函数级 Void
`tTJSVariant`，随后把**同一个非空 result 地址**依次传给最多六个动态调用：

1. `source.drawLayer(entry.layer)`；
2. `bufLayer.setSize(width, height)`；
3. `bufLayer.copyRect(...)`；
4. `entry.layer.fillRect(...)`；
5. `entry.layer.operateRect(...)`；
6. 仅 low-blend mode 2 的 `entry.layer.adjustGamma(...)`。

所有普通 `tjs_error` 返回值仍被忽略，而且 result 不在调用间清空。因此一个 callback 即使返回
负 HRESULT，只要写了 result，下一 callback 就能观察该值；如果失败且不写，下一 callback 会
继续看到更早的值。result 在最后一个 Layer accessor receiver 释放之后才析构。旧端口把这六处
result 全传 null，现已按四端共同证据恢复。

同一纵切面还闭合了 `drawLayer/setSize/copyRect/operateRect/adjustGamma` 五个独立的
process-wide method-hint word。它们在四端都是连续五个 4-byte data object，下一槽
`primaryLayerMemberHint_guess` 是已证明的边界，不属于 bake method family。`setSize` 和
`operateRect` 各有其它 renderer consumer；连续布局不表示 owner、结果存储或调用 ABI 共享。

## 五槽数据 family 与下一边界

| data object | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLayerMemberHint_guess` | `0x1AB52C0` | `0x11117CC` | `0x101B69788` | `0x187D490` |
| `setSizeMemberHint_guess` | `0x1AB52C4` | `0x11117D0` | `0x101B6978C` | `0x187D494` |
| `copyRectMemberHint_guess` | `0x1AB52C8` | `0x11117D4` | `0x101B69790` | `0x187D498` |
| `operateRectMemberHint_guess` | `0x1AB52CC` | `0x11117D8` | `0x101B69794` | `0x187D49C` |
| `adjustGammaMemberHint_guess` | `0x1AB52D0` | `0x11117DC` | `0x101B69798` | `0x187D4A0` |
| next boundary `primaryLayerMemberHint_guess` | `0x1AB52D4` | `0x11117E0` | `0x101B6979C` | `0x187D4A4` |

四端间没有 pointer-width padding：每一槽都正好比上一槽大 4 bytes。恢复数据库中已把六个地址
分别重建为 size-4 unsigned integer data item；不能把它们合成数组，因为各槽具有不同名称、
字符串和 xref topology。`fillRectMemberHint_guess` 由另一已恢复 family 提供，虽然 bake 也把
共享 result 传给 `fillRect`，但它不是上表连续五槽的一员。

## xref 与 consumer topology

raw data xref 数受指令集地址 materialization、literal pool 和 function chunk 影响：

| word | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 归一化 consumer |
|---|---:|---:|---:|---:|---|
| `drawLayer` | 2 | 3 | 1 | 2 | 仅 `SourceCache_bakeSource_guess` |
| `copyRect` | 2 | 3 | 1 | 2 | 仅 `SourceCache_bakeSource_guess` |
| `adjustGamma` | 2 | 3 | 1 | 2 | 仅 `SourceCache_bakeSource_guess` |
| `operateRect` | 4 | 6 | 2 | 4 | bake + `Player_renderToCanvas_guess` |
| `setSize` | 20 | 30 | 10 | 20 | 10 calls / 7 functions，见 V183 |
| `primaryLayer` | 6 | 9 | 3 | 8 | SourceCache ctor + build-render-commands |

这里的 `setSize` 结论沿用并交叉验证 V183 已闭合的 10-call topology；本轮的新信息是它处于
bake 五槽 family 的第二槽。`primaryLayer` 的 raw xref 归一为 SourceCache constructor 与
`Player_buildRenderCommands_guess`，其紧邻位置只证明 data-section 边界，不把 accessor 与
method calls 合为同一语义 owner。

## 字符串搜索与 literal 归属

本轮按 ordinary string、ASCII bytes、UTF-16LE、UTF-32LE 四条路径搜索到 cursor 结束。
`drawLayer/copyRect/operateRect/adjustGamma/primaryLayer` 的 ordinary find 在四库均为空；ASCII
与 UTF-32LE 也没有定位到本纵切面 literal。相关 TJS 宽字符串为：

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLayer` | `0x14D540A` | `0xD84FBE` | `0x10195B74C` | `0x174DAB0` |
| `copyRect` | `0x14D5808` | `0xD85360` | `0x10195BC20` | `0x174DF84` |
| `operateRect` | `0x1507286` | `0xD85372` | `0x10195BC32` | `0x174DF96` |
| `adjustGamma` | `0x14D581A` | text-adjacent `0x57A730` | `0x10195BC4A` | `0x174DFAE` |
| `primaryLayer` | `0x14CB19E` | `0xD7DE9E` | `0x10195BC62` | `0x174DFC6` |

literal xref 与 exact data word、call ABI 三者共同用于归属。Android armv7 的
`adjustGamma` 文本嵌在 bake 邻接只读区，不能仅凭普通 string-list 缺失认定调用没有名字。
其它 Android/iOS duplicate literal 属于 registrar 或别的模块，不计入本轮 data consumer。

## bake 函数、调用点和 result 生命周期

| 目标 | bake function | result Void init | final result destroy |
|---|---:|---:|---:|
| Android arm64 | `0x6A3FC0` | `0x6A3FF8` | `0x6A46C0` |
| Android armv7 | `0x57A168` | `0x57A188` | `0x57A592` |
| iOS arm64 | `0x1000FFB24` | `0x1000FFB60` | `0x10010010C` |
| iOS armv7 | `0xFCD68` | `0xFCD9E` | `0xFD278` |

六个 exact call site：

| call | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawLayer` | `0x6A4054` | `0x57A1C4` | `0x1000FFBBC` | `0xFCE22` |
| `setSize` | `0x6A42E0` | `0x57A2C8` | `0x1000FFD14` | `0xFCF68` |
| `copyRect` | `0x6A439C` | `0x57A358` | `0x1000FFDCC` | `0xFD000` |
| `fillRect` | `0x6A445C` | `0x57A3E4` | `0x1000FFE8C` | `0xFD09C` |
| `operateRect` | `0x6A4534` | `0x57A47E` | `0x1000FFF70` | `0xFD150` |
| `adjustGamma` | `0x6A4654` | `0x57A542` | `0x1001000A0` | `0xFD22C` |

Android arm64 六次调用均把同一局部地址 `v80` 放在 result 参数；Android armv7 为 `v51`；
iOS arm64 为 `v76`。iOS armv7 的 stack ABI 在六个 call 前都把同一个 `&var_118` 写入 result
argument 槽，并在 `0xFD278` 析构该槽。四端都没有在两个 calls 之间执行 Variant clear、destroy
或重新 construction。

共同伪代码为：

```text
result = Void Variant
source.FuncCall(0, "drawLayer", &drawLayerHint,
                &result, {entry.layer}, source)

width  = layer.GetInt("width", 0)
height = layer.GetInt("height", 0)
... corner tint / low-blend and backend gates ...

if scratch path:
    bufLayer.FuncCall(0, "setSize", &setSizeHint,
                      &result, {width, height}, bufLayer)
    bufLayer.FuncCall(0, "copyRect", &copyRectHint,
                      &result, {...}, bufLayer)
    layer.FuncCall(0, "fillRect", &fillRectHint,
                   &result, {...}, layer)
    layer.FuncCall(0, "operateRect", &operateRectHint,
                   &result, {...}, layer)
    if lowBlend == 2:
        layer.FuncCall(0, "adjustGamma", &adjustGammaHint,
                       &result, {...}, layer)

destroy Layer accessor receiver
destroy result
```

每次调用的 flags 都是 0，argc/argv 与既有低 blend 报告一致，objthis 等于对应 receiver。
ordinary HRESULT 不参与任何 branch。异常仍按 C++/TJS unwind 传播；已经构造的 argument、
accessor 和函数级 result 按栈展开销毁，原版没有 catch、重试、result rollback 或 transactional
scratch-layer recovery。

## `operateRect` 跨消费者反证

`Player_renderToCanvas_guess` 也复用完全相同的 `operateRectMemberHint_guess`，但明确传 null
result：

| 目标 | render-to-canvas `operateRect` call | result ABI |
|---|---:|---|
| Android arm64 | `0x6C5938` | `X4 = XZR` |
| Android armv7 | `0x58F216` | stack/result argument null |
| iOS arm64 | `0x1001194E4` | `X4 = 0` |
| iOS armv7 | `0x117AE4` | stack/result argument null |

因此 process-wide hint identity 与 result-storage identity 是两个独立维度：共享 hint 只缓存 member
解析状态，不决定 caller 是否需要 result。端口不能把“某个 method 通常不读取返回值”抽象成
全局 null-result wrapper；必须逐 call chain 恢复原 ABI。

## portable 源码与探针

修改覆盖：

1. `cpp/plugins/motionplayer/SourceCache.cpp`
   - 在 `drawLayer` 前构造函数级 `tTJSVariant dispatchResult`；
   - 六处动态调用改传 `&dispatchResult`；
   - 保持 ignored HRESULT、原调用次序、条件 gate、receiver 和参数完全不变；
2. `cpp/plugins/motionplayer/MotionDispatch.h`
   - 把五个 word 记录为精确连续 family，说明 `setSize/operateRect` 跨 consumer，并标出下一
     `primaryLayer` 边界；
3. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - `drawLayer` 要求非空、初始 Void result，写入 sentinel；
   - 五个 scratch calls 要求同一个 pointer，并依次读到前一值、写入新值后返回 `TJS_E_FAIL`；
   - 由后续 call 仍观察新值锁定“失败不清空、不 gate”；
   - 逐处锁定 exact hint pointer，并验证五槽彼此 distinct，且不 alias `primaryLayer` 或
     `fillRect`。

compiled source comment 没有写入参考库绝对地址；所有 stripped 恢复名继续保留 `_guess`。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 每库将五槽 family 和 `primaryLayer` boundary 共 6 个地址建立为独立 size-4 unsigned int，
  并写入对应 `_guess` 名；
- 每库写入 16 条注释：6 个 data、bake function、result init/dtor、render-to-canvas null-result
  boundary 和 6 个 bake calls，共 64 条；
- 每库建立一个 V184 bookmark，共 4 个；
- 每库 force-recompile bake、render-to-canvas、SourceCache ctor、build-render-commands，共 16 次；
- fresh decompile/disassembly readback 在四端显示 exact symbol 与同一 result 地址；iOS armv7 的
  隐藏 stack arguments 由 call-site disassembly 单独闭合；
- 四份 recovery IDB 均原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,952 bytes，539 imports / 69 exports；
- Headless wasm：84,996,093 bytes，538 imports / 69 exports；
- 相较 V183 两份 wasm 都增加 73 bytes，import/export ABI 表面不变；
- Node `WebAssembly.Module` 成功解析两份产物；
- `llvm-objdump -h` 成功解析两份完整 section table；
- Web GLOBAL 保持 `0xD5B2`，CODE 从 `0x1A40B79` 增至 `0x1A40BC2`；Headless GLOBAL
  保持 `0xD5DA`，CODE 从 `0x19E8B27` 增至 `0x19E8B70`；两个 CODE 差值都精确为
  `0x49`；
- 两个 build tree 的 CTest 均成功返回 `No tests were found`，没有虚报 runtime unit test；
  recorder 由 ordinary/headless syntax-only 编译覆盖；
- `git diff --check` 通过，仅输出 working tree 既有 LF/CRLF 转换提示；
- 四库 string/bytes、xref、make-data、命名、注释、bookmark、force-recompile/readback 与 save
  全部成功。

首次 full build 在 CMake 自动 regeneration 阶段暴露旧 cache 中空 EMSDK 展开出的
`/upstream/...` toolchain 路径；在不改变项目配置语义的前提下，以既定 EMSDK 环境重新执行
Web/Wasmtime presets 后，两套构建均完成。构建只出现项目既有 `_tss`、null-dereference、
pthread memory-growth、JSPI 与 JS-library warnings，没有新增 motionplayer 编译或链接错误。

## 结论边界

V184 闭合的是 SourceCache bake 六个动态调用共享的 result storage/lifetime、失败穿透，以及
五槽 method-hint family 到 `primaryLayer` 的 data boundary。它不声称所有同名 method call 都
使用非空 result；render-to-canvas 的 `operateRect` 已给出相反证据。它也不把连续五个 dword
恢复成原始 C++ array 类型，或把邻接 `primaryLayer` accessor cache 纳入 bake family。其它
SourceCache 构造、缓存淘汰与异常所有权边界继续由各自四端纵切面约束。
