# MotionLayer clip quartet 与共享 update hint 的四参考复原（2026-08-17）

## 结论

四个参考二进制一致证明：`MotionLayer` 的 mesh renderer 与 Bezier-patch
renderer 并不各自拥有一组 `clipLeft/clipTop/clipWidth/clipHeight` member-hint
cache。两者复用同一组、按上述顺序连续排列的四个进程生命周期 `tjs_uint32`
槽。紧随其后的第五个槽是 `update`；它不仅被两个 renderer 复用，还跨越源码翻译
单元，与 alpha-mask compositor 的 `Layer.update` 调用复用同一个缓存字。

因此旧源码中以下边界不符合参考实现：

- `renderMesh` 与 `renderBezierPatch` 各自声明四个 function-local clip hints；
- `MotionLayerExtensions.cpp` 的提交 helper 与 `PlayerRenderInternal.cpp` 的 alpha-mask
  helper 各自声明一个 `update` hint。

本轮把 clip quartet 收敛为 `MotionLayerExtensions.cpp` 的一个 TU-local family，并把
`update` 提升为 `motion::detail` 的跨 TU process-global family 成员。未知的原始 C++
标识符仍按约定保留 `_guess`。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionLayer_renderMesh` | `0x69E0D0` | `0x576E08` | `0x1000FB660` | `0xF86B0` |
| `MotionLayer_renderBezierPatch` | `0x69E630` | `0x577184` | `0x1000FBBB8` | `0xF8C00` |
| `Motion_doAlphaMaskOperation` | `0x6AC4E4` | `0x57E1E8` | `0x100104E68` | `0x10243C` |

这些名字是四参考恢复数据库中的语义名，不主张它们必然等于被 strip 前的原始符号。

## 五槽数据映射

| 槽语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `motionLayerClipLeftMemberHint_guess` | `0x1AB5258` | `0x1111788` | `0x101B69720` | `0x187D44C` |
| `motionLayerClipTopMemberHint_guess` | `0x1AB525C` | `0x111178C` | `0x101B69724` | `0x187D450` |
| `motionLayerClipWidthMemberHint_guess` | `0x1AB5260` | `0x1111790` | `0x101B69728` | `0x187D454` |
| `motionLayerClipHeightMemberHint_guess` | `0x1AB5264` | `0x1111794` | `0x101B6972C` | `0x187D458` |
| `updateMemberHint_guess` | `0x1AB5268` | `0x1111798` | `0x101B69730` | `0x187D45C` |

五个目标均是独立的 4-byte mutable data item。它们的邻接关系是参考布局证据，不能
据此把 clip quartet 建模为可索引数组：反编译代码把每个地址分别传给不同名字的 TJS
property access，且第五个地址承担不同的 `FuncCall("update")` 角色。

恢复 IDB 中原先覆盖 clip quartet 的旧 16-byte data item 已拆成四个独立 size-4 item，
再加上相邻的独立 update item；最终四库 readback 均显示五个边界和类型正确。

## fresh xref 与 consumer 边界

四端重新取得的 data-xref 计数如下：

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 语义 consumers |
|---|---:|---:|---:|---:|---|
| 每个 clip 槽 | 4 | 6 | 2 | 4 | 两个 MotionLayer renderer |
| `update` | 6 | 9 | 3 | 6 | 两个 renderer + alpha-mask compositor |

跨 ABI 的计数差异来自地址物化方式，不代表额外语义 consumers：arm64 Android 的
page/address pair、armv7 Android 的多指令绝对地址构造和 iOS armv7 的分裂引用会为一个
源码 access 产生多个数据库 xref；iOS arm64 的 PC-relative materialization 在此处通常
只形成一个。按函数重新反编译并对 operand 符号 readback 后：

- 两个 renderer 对四个 clip 符号各出现一次，并对 `update` 出现一次；
- alpha-mask function 对 `update` 出现一次；
- 没有第三个 MotionLayer clip consumer；
- alpha-mask 自己的 clip getter family 是另一组已经单独证明的全局槽，不能因为共享
  `update` 而与本轮 quartet 合并。

## 共同数据流与调用顺序

四端共同流程可以归纳为：

```text
renderMesh:
  flat point array -> owning/native point vector
  -> owner.clipLeft / clipTop / clipWidth / clipHeight
  -> clip rectangle
  -> native target/source Layer conversion
  -> mesh submission
  -> if bounds changed: owner.update(left, top, width, height)

renderBezierPatch:
  owner.clipLeft / clipTop / clipWidth / clipHeight
  -> clip rectangle
  -> control-point parse + Bezier tessellation
  -> native target/source Layer conversion
  -> mesh submission
  -> if bounds changed: owner.update(left, top, width, height)
```

两个入口的前半顺序并不完全相同：mesh 入口先把 flat coordinates 读入 point vector，
再读 owner clip；Bezier 入口先读完整 clip quartet，随后才解析和 tessellate control
points。本轮只合并有四端地址身份支持的 hint 存储，并未抹平该求值顺序差异。

clip rectangle 的边界计算保持参考的 32-bit 行为：

```text
right  = int32_wrap(clipLeft + clipWidth)
bottom = int32_wrap(clipTop  + clipHeight)
```

即不能用更宽整数的无溢出加法悄悄改变边界值。四个 getter 都以 flags `0` 访问 owner，
receiver 也是 owner。只有 mesh/backend 提交报告 bounds updated 时，才调用 Layer
`update`：flags 为 `0`，receiver 为 owner，参数精确为四个整数
`[left, top, width, height]`。没有 update 时不发出脚本调用。

alpha-mask compositor 使用自己已证明的 clip/bounds 获取过程，然后也构造同样形状的
四整数 update argv；本轮新增结论仅是它的 member-hint 指针与两个 MotionLayer renderer
完全相同。结果 `tTJSVariant` 仍由各自 caller 栈上持有并按原控制流析构，提升 hint
存储不改变 Variant、Layer 或 texture 的所有权。

## 源码修正

本轮修改：

1. `MotionLayerExtensions.cpp`
   - 新增一个 TU-local clip quartet；
   - 删除两个 renderer 合计八个 function-local clip cache；
   - 两个入口均向同四个 backing words 传递精确指针；
   - `submitLayerMesh_guess` 改用共享 `detail::updateMemberHint_guess`。
2. `MotionDispatch.h` / `RuntimeSupport.cpp`
   - 声明并定义 process-wide `updateMemberHint_guess`；
   - 编译源码注释只保留语义和字符串，不写目标绝对地址。
3. `PlayerRenderInternal.cpp`
   - 删除 alpha-mask helper 的 TU-local `update` cache；
   - 改用同一 `motion::detail::updateMemberHint_guess`。

clip quartet 仍为 TU-local，因为 fresh xref 的全部 clip consumers 都在
`MotionLayerExtensions.cpp`；`update` 必须放在共享 runtime family，因为三名 consumers
跨越两个翻译单元。

## 回归锁定

新增回归 `Mesh and Bezier renderers share one clip hint quartet` 使用自定义 owner
dispatch，并从公开的 `operateMesh` / `operateBezierPatch` 入口进入 mode 3 路径：

- 先验证前置 `face`、`holdAlpha` 读取后，clip getter 顺序严格为
  `left/top/width/height`；
- 每次 getter 的 flags 为 `0`、receiver 为 owner、hint 非空；
- quartet 内四个指针两两不同；
- mesh 与 Bezier 两次进入得到的四个指针按位置精确相等；
- 四个指针均与全局 `updateMemberHint_guess` 不同。

probe 在 `clipHeight` 主动抛出 `eTJSError`，因而停止点位于 native Layer conversion、
texture 获取和实际渲染之前。这样测试不需要伪造 native Layer，却能锁住本轮需要证明的
求值顺序、receiver、flags 与精确缓存身份；异常沿原访问路径传播，也覆盖了提前退出时
owner accessor/Variant 临时对象的正常栈展开。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery`
均已完成：

- 五个独立 data boundary 的 `tjs_uint32` 类型和语义名；
- data comments、三类 function comments 与代表性 operand comments；
- 每库三个 bookmarks；
- 三个 functions 的强制 recompile，共 12 个 function recompiles；
- 四端 symbol-usage readback：renderer 的 quartet/update 各一次、alpha-mask update
  一次；
- 四个 IDB 原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,647,311 bytes，539 imports / 69 exports；
- Headless wasm：84,994,452 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两份 wasm 均由 `llvm-objdump -h` 完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、
  GLOBAL、EXPORT、START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 相较 V171，两份产物均增加 41 bytes，import/export ABI 表面不变；
- 两种 build tree 的 CTest 均报告 `No tests were found`，所以运行时回归覆盖仍依赖
  unit-test TU 的双配置编译，而不是已注册的 CTest executable；
- 本轮定向 `git diff --check` 通过，只有仓库既有的 LF/CRLF 提示，没有 whitespace
  error。

## 尚未扩张的结论

本轮没有把所有名为 `clipLeft` 或 `update` 的 dispatch 自动合并。member 名相同不足以
证明 cache 身份；只有四端绝对数据地址相同的 callsites 才共享 backing word。后续若发现
更多消费者，仍需对四个参考分别重新取得 data xref、decompile 与 operand readback，再
决定是否扩张这五槽 family。
