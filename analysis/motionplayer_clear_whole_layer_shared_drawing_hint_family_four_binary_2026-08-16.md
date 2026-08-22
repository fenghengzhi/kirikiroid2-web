# `clearWholeLayer` 共享 geometry/drawing member-hint family（四参考，2026-08-16）

## 结论

`MotionLayer_clearWholeLayer_guess` 没有私有的
`neutralColor/height/width/fillRect` member-hint backing words。四个当前参考二进制都把这四次
dispatch 绑定到既有 process-wide family：

- `height`、`width` 复用 geometry 全局槽；
- `neutralColor` 与 `Player::buildRenderCommands` 复用同一槽；
- `fillRect` 与 SourceCache bake、alpha-mask 操作以及多条 Player render 路径复用同一槽。

旧 `MotionLayerExtensions.cpp` 在 helper 内定义四个 function-local static，把脚本可观察的
hint-cache 状态错误隔离。V170 删除这四个局部实体，恢复原版的跨函数、跨翻译单元地址
identity；清屏的参数、调用顺序、owner 生命周期与 ordinary-status 行为不变。

## 函数与 caller 映射

| 参考 | `MotionLayer_clearWholeLayer_guess` | `meshCopy` caller | `bezierPatchCopy` caller |
|---|---:|---:|---:|
| Android arm64 | `0x69EF1C` | `0x69F220` | `0x69FE4C` |
| Android armv7 | `0x577774` | `0x57798C` | `0x577FA6` |
| iOS arm64 | `0x1000FC4B8` | `0x1000FC7AC` | `0x1000FD03C` |
| iOS armv7 | `0xF9410` | `0xF971E` | `0xF9FD4` |

四端 `xrefs_to` 都只有这两个 code callers。两个 wrapper 均在 `clear == true` 时调用 helper，
然后继续各自的 mesh/Bezier render 路径；helper 本身是共享 standalone body，不是两份内联
副本。

## 四个 backing word

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `widthMemberHint_guess` | `0x1AB520C` | `0x1111740` | `0x101B696D4` | `0x187D404` |
| `heightMemberHint_guess` | `0x1AB5210` | `0x1111744` | `0x101B696D8` | `0x187D408` |
| `neutralColorMemberHint_guess` | `0x1AB526C` | `0x111179C` | `0x101B69734` | `0x187D460` |
| `fillRectMemberHint_guess` | `0x1AB5270` | `0x11117A0` | `0x101B69738` | `0x187D464` |

每端 `neutralColor` 与 `fillRect` 都是相邻的两个 32-bit word；`width/height` 是此前 V169 已
闭合的全局 geometry pair。fresh decompile 在四端都直接显示四个精确地址，force-recompile
命名后又都读回四个语义 symbol 各一次。因此这里不是按相同 literal 猜测合并。

## 跨函数 consumer 集合

### `neutralColorMemberHint_guess`

四端 data xrefs 的语义函数集合完全相同：

1. `MotionLayer_clearWholeLayer_guess`；
2. `Player_buildRenderCommands_guess`。

xref 原始数量因 32 位 address materialization 与 function-chunk 恢复形式不同而是
`4/6/2/5`，但最终只有上述两个 source-level consumers。

### `fillRectMemberHint_guess`

四端共同的 source-level consumer 集合为：

1. `MotionLayer_clearWholeLayer_guess`；
2. `SourceCache_bakeSource_guess`；
3. `Motion_doAlphaMaskOperation_guess`；
4. `Player_buildRenderCommands_guess`；
5. `Player_renderToCanvas_guess`；
6. `Player_renderAccurateSeparateLayerAdaptor_guess`；
7. `Player_drawToLayerRecursive_guess`。

四端 raw xref 数分别为 `30/45/15/32`，差异来自一次 source call 对应的多条地址形成指令、
alpha-mask body 中多次 `fillRect` 和平台优化；七类语义 consumers 保持一致。这个集合也直接
否定“clear helper 私有 fillRect cache”的旧注释模型。

## 共同数据流与生命周期

四端共同伪代码为：

```text
ownerAccessor(owner)              // retain owner
neutral = GetValue<int>(
    owner, "neutralColor", flags=0,
    &neutralColorMemberHint_guess)
height = GetValue<int>(
    owner, "height", flags=0,
    &heightMemberHint_guess)
width = GetValue<int>(
    owner, "width", flags=0,
    &widthMemberHint_guess)

args = [0, 0, width, height, neutral]
owner.FuncCall(
    flags=0, name="fillRect",
    hint=&fillRectMemberHint_guess,
    result=&temporary, numparams=5,
    params=args, objthis=owner)

destroy result
destroy args in reverse storage order
release ownerAccessor
```

`GetValue<int>` 路径继续忽略 ordinary HRESULT，并对返回 Variant 做严格 Integer conversion；
`FuncCall` 的 ordinary status 也不参与分支。若 getter conversion 或 callback 抛异常，已构造的
Variant 和 accessor 按 native owner scope 回滚。本次只替换四个 hint address，不改变这些
边界。

## 源码修正

`MotionLayerExtensions.cpp` 的 `clearWholeLayer_guess` 已：

- 删除 `neutralColorHint/heightHint/widthHint/fillRectHint` 四个 local static；
- 三次 getter 改为 `detail::neutralColor/height/widthMemberHint_guess`；
- `fillRect` call 改为 `detail::fillRectMemberHint_guess`。

V169 已为该翻译单元补上 `MotionDispatch.h` 的显式 include，因此 V170 不依赖偶然的传递
声明。

## 回归探针

新增 `Layer clear reuses process-wide geometry and drawing hints`，通过公共
`MotionLayerExtensions_guess::meshCopy(..., clear=true)` 进入真实 clear helper：

- `face` 先接收 null-hint `MEMBERMUSTEXIST` probe，再接收 null-hint flags-0 value read；
  两次都返回 0，使 wrapper 按 native 路径进入 clear；
- 依次为 `neutralColor/height/width` 返回 `0x10203040/7/9`；
- 精确断言 property 顺序；`face` probe flags 为 `TJS_MEMBERMUSTEXIST`、其余 value reads
  flags 为 0；全部使用 owner receiver，后三项使用对应共享 hint；
- 在 `fillRect` callback 中记录后主动抛出 `eTJSError`，从而在进入需要真实 native Layer 的
  mesh renderer 前安全截断；
- 精确断言 `fillRect` 的共享 hint、flags 0、receiver 与五参数
  `[0, 0, 9, 7, 0x10203040]`。

异常截断还使 accessor、result 和参数 Variant 的 unwind 路径参与编译覆盖。两个构建树没有
注册 CTest，故本轮能闭合 ordinary/headless syntax-only 编译，不能把探针误报为已由 CTest
执行。

## IDB 写回

四个 recovery IDB 均完成：

- `neutralColor/fillRect` 共 8 个地址重建为独立 size-4 `unsigned int` data item，并命名为
  对应 process-wide symbol；
- data item、clear helper 入口和两处代表性 getter/call 写入 V170 注释；
- 每库增加 `V170 clear layer shared drawing hints` bookmark；
- 四端 helper 强制重编译；readback 均为
  `neutralColor/height/width/fillRectMemberHint_guess` 各一次；
- 四库均原位保存成功。

## 验证

- ordinary Emscripten 测试 TU syntax-only：成功，仅有项目既有 `_tss` warning；
- `KRKR2_WASMTIME_HEADLESS=1` 测试 TU syntax-only：成功，同一 warning；
- `cmake --build out/web/debug`：成功，最终链接完成；
- `cmake --build out/wasmtime/debug`：成功，最终链接完成；
- Web wasm：`85,647,270` bytes，539 imports / 69 exports；
- Headless wasm：`84,994,411` bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两份 wasm 均由 `llvm-objdump -h` 列出完整
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- 相对 V169，两份 wasm 都精确减少 52 bytes，import/export 数不变；
- Web/Headless 两配置 CTest 均未注册测试。

本纵切面只恢复 clear helper 的四个精确 backing-word identities；不据此合并其他同名但四端
地址未闭合的绘制 helper local statics。
