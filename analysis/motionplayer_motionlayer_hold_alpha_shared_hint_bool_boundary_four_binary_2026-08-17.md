# MotionLayer `holdAlpha` 共享 hint 与 Bool 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致把 MotionLayer 的 `meshCopy`、`operateMesh`、`bezierPatchCopy`、
`operateBezierPatch` 四条渲染入口绑定到同一个、翻译单元生命周期的 4-byte member-hint
word。四处都通过 typed `Bool` getter、`flags = 0`、MotionLayer owner receiver 读取
`holdAlpha`，随后把布尔值传给 mesh/Bezier renderer；它不是四个独立 getter cache，也不与
相邻的 clip quartet 或跨翻译单元的 `update` word 别名。

本地实现此前四处都使用无 hint 的 `getIntValue(..., 0) != 0`。虽然普通 0/1 输入的可见
结果相近，但这同时错失了参考的 TJS Bool 转换边界和 dispatch member-cache 身份，属于真实
语义偏差。本轮将四条路径改为同一 TU-local `motionLayerHoldAlphaMemberHint_guess` 上的 typed
Bool getter，并扩展异常截止回归锁定调用顺序、receiver、共享/不别名关系。

## 四端函数与数据映射

| 目标 | `meshCopy` | `operateMesh` | `bezierPatchCopy` | `operateBezierPatch` | `motionLayerHoldAlphaMemberHint_guess` | data xrefs |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x69F150` | `0x69F304` | `0x69FD7C` | `0x69FF30` | `0x1AB5274` | 8 |
| Android armv7 | `0x577924` | `0x577A44` | `0x577F3C` | `0x57805C` | `0x11117A4` | 12 |
| iOS arm64 | `0x1000FC6E8` | `0x1000FC864` | `0x1000FCF78` | `0x1000FD0F4` | `0x101B6973C` | 4 |
| iOS armv7 | `0xF9654` | `0xF97F4` | `0xF9F08` | `0xFA0A8` | `0x187D468` | 8 |

四端的语义 consumer 都严格是上述四个函数，每个函数一次。data-xref 数量的差异来自
ADRP/ADD、MOV/literal pool、PC-relative address materialization 等 ABI/指令选择，不代表
额外 getter 或额外 owner。四库 fresh decompile 与 force-recompile readback 都把四个调用点
解析到同一个新命名 word，旧 `unk_*` 引用为零。

该 word 与 V172 已闭合的 `clipLeft`、`clipTop`、`clipWidth`、`clipHeight` 数据靠近，但
native BSS 邻接不是别名证据。四端各自都保留独立的 size-4 data item；`holdAlpha` 也不等于
V172 证明跨翻译单元共享的 `detail::updateMemberHint_guess`。

## UTF-16LE literal 消歧

本轮重新按 UTF-16LE 原始字节搜索 `holdAlpha`，避免把相同脚本成员名的无关模块消费者混进
MotionLayer hint 集合：

| 目标 | motionplayer 使用的 literal | 其他同名命中 |
|---|---:|---:|
| Android arm64 | `0x14D54AE` | 无第二份 literal；同一 literal 另被 motionplayer 之外的大函数引用 |
| Android armv7 | `0xD85086` | 无第二份 literal；同一 literal 另被 motionplayer 之外的大函数引用 |
| iOS arm64 | `0x10195B88C` | `0x101957BBE`，属于无关消费者 |
| iOS armv7 | `0x174DBF0` | `0x1749F22`，属于无关消费者 |

Android 的宽字符串 literal 本身被无关大函数共享，iOS 则保留两份同名 literal；两种布局都
说明“字符串相同”不足以证明 hint 身份。只有同时引用本轮 data word、位于上述四个
MotionLayer functions 内的调用点才纳入本结论。

## 四条路径的共同调用形状

四端 getter 的逻辑形状一致：

```text
holdAlpha = owner.GetValue(
    member  = "holdAlpha",
    type    = Bool,
    flags   = 0,
    hint    = &motionLayerHoldAlphaMemberHint_guess,
    objthis = owner)
```

getter 返回值随后直接进入 mesh 或 Bezier renderer 的 `holdAlpha` 参数。部分 decompiler
listing 在调用边界显示 `value & 1`；这是布尔参数规范化/ABI lowering 的表示差异，不是第二次
脚本转换，也不把该值写回 owner。

该 hint word 只缓存 dispatch lookup 状态：它不拥有 receiver、不 AddRef source/target layer，
也不保存本次 Bool 值。四条入口结束或抛出后，owner/layer/临时参数仍按各自原有生命周期
释放；共享 cache 不改变对象所有权。

## copy 与 operate 的控制流边界

### `meshCopy` / `bezierPatchCopy`

两个 copy 方法先刷新 cached face，并以 `false` 初始化局部 `holdAlpha`：

```text
refresh cached face
holdAlpha = false
if face == 1:
    holdAlpha = typed Bool getter with shared hint
else if face != 0 && face != 4:
    throw method-specific "not drawable face type"

if clear:
    clear destination
construct source rectangle
render mesh/Bezier with bitmapMethod=0, opacity=255, holdAlpha
```

因此 face 0 和 4 不读取脚本 `holdAlpha`，固定向 renderer 传 `false`；face 1 才读取共享槽；
其余 face 在任何 clear/render 之前抛出。getter 在 clear 之前发生，脚本 getter 抛出时也不会先
清理目标层。

### `operateMesh` / `operateBezierPatch`

两个 operate 方法先完成 method-specific mode/face/source 参数解析，再无条件读取
`holdAlpha`，然后进入相应 renderer：

```text
resolve operation mode / drawable face / source
holdAlpha = typed Bool getter with shared hint
render mesh/Bezier with caller bitmapMethod, opacity, stretchType
```

也就是说，copy 的 getter 由 face 1 门控，而 operate 在通过前置解析后总会读取。两类入口
共享 cache identity，但不共享相同的控制流门槛。

## 源码修改

`MotionLayerExtensions.cpp` 的 anonymous namespace 新增一个零初始化、TU-local：

```cpp
tjs_uint32 motionLayerHoldAlphaMemberHint_guess = 0;
```

四个旧调用：

```cpp
getIntValue(ownerAccessor, TJS_W("holdAlpha"), 0) != 0
```

均改为：

```cpp
ownerAccessor.GetValue(
    TJS_W("holdAlpha"), ncbTypedefs::Tag<bool>(), 0,
    &motionLayerHoldAlphaMemberHint_guess)
```

命名保留 `_guess`，因为 stripped references 不提供原始 C++ 变量名；compiled-source 注释不写
任何目标绝对地址。定向审计确认 token 恰好五次：一处定义、四处使用，并且已无
`holdAlpha` 的 unhinted integer getter。

## 回归设计与异常截止点

既有 `Mesh and Bezier renderers share one clip hint quartet` 回归扩展为记录 owner 每次
`PropGet` 的 member、flags、hint 与 receiver，并让 probe 的 `face` 可配置。覆盖顺序为：

1. face 0 的 `operateMesh`：先记录无 hint 的 `face` existence probe 与 value read，再记录非空
   `holdAlpha` hint，随后在 `clipHeight` getter 抛出；
2. face 0 的 `operateBezierPatch`：验证相同 `holdAlpha` 和相同 clip quartet；
3. face 1 的 `meshCopy`：证明 copy 门控路径读取同一 `holdAlpha`，仍在 native layer 转换前
   的 `clipHeight` 截止；
4. face 1 的 `bezierPatchCopy`：证明第四条路径仍共享同一槽。

选择 `clipHeight` 作为异常截止点，使测试能观察完
`face probe -> face value -> holdAlpha -> clip quartet` 的 dispatch 次序，又不需要构造真实
native layer/texture/render manager。回归另外断言：

- 四次 `holdAlpha` 的 flags 都为 0、receiver 都是 owner；
- 四次 hint 指针精确相同且非空；
- `holdAlpha` 分别不等于四个 clip hint；
- `holdAlpha` 和所有 clip hint 都不等于全局 `updateMemberHint_guess`。

copy 两条路径必须把 face 切换到 1；若使用 face 0，参考控制流本来就不会触发 `holdAlpha`
getter，因而不能证明共享身份。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 四端各建立一个独立 size-4 `unsigned int motionLayerHoldAlphaMemberHint_guess`；
- data、四个 function 与代表性 operand 注释，共 36 处；
- data 与四个函数 bookmarks，共 20 个；
- 四函数每库 force-recompile，共 16 个函数；
- readback 均解析为新名字，旧 `unk_*` 引用为零；
- 四份 recovery IDB 原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,647,409 bytes，539 imports / 69 exports；
- Headless wasm：84,994,550 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 相较 V172–V175 的最近基线，两份 wasm 均增加 98 bytes，import/export ABI 表面不变；
- 两个 build tree 的 CTest 都报告 `No tests were found`，回归由 unit-test TU 的
  ordinary/headless 双配置编译覆盖；
- 定向检查确认一处 hint 定义、四处使用、零处旧 integer getter；
- `git diff --check` 无 whitespace error，只有 dirty worktree 既有 LF/CRLF 提示。

## 结论边界

本轮证明的是四个 MotionLayer rendering entries 对一个 typed Bool dispatch cache 的共享。
同名 `holdAlpha` literal 的其他模块消费者既不能自动复用该 word，也不能据此改变 MotionLayer
owner/layer 生命周期。后续若发现新的 `holdAlpha` native getter，必须重新以四端 data-xref
地址身份确认是否属于这一 family；只凭 member name、BSS 邻接或相同转换结果都不够。
