# MotionLayer face / auto mode / bitmap-method 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致把 MotionLayer 的绘制模式解析分成三层：

1. `refreshFace` 从 owner 读取 `face`，仅当 raw face 为 128 时再读取 `type` 并分类；
2. operate-family 的 input mode 为 128 时，`resolveAutoMode` 独立读取一次 `type`；
3. `resolveBitmapMethod` 总会重新 refresh face，再将 `(mode, face)` 映射成 engine bitmap
   method，失败时不发布 output。

`face` 和 `type` 都故意走无 member hint 的 `getIntValue`：先以
`TJS_MEMBERMUSTEXIST` 和 null hint 探测，探测成功才以 flags 0 和 null hint 再读一次并转成
Integer。它们不能复用邻近的 `holdAlpha`、clip、geometry 或任何其他 hint word。

本地运行时代码的分类与 method matrix 已经符合四参考。本轮修复的是 recovery IDB 和测试
契约：Android arm64 仍未命名的 standalone bitmap-method helper 现已恢复语义名；现有 V176
测试原先漏算 `face` existence probe，若真正执行会失败，而两个构建树又没有注册 CTest，
所以此前只有编译没有暴露该错误。本轮把 probe/value 两阶段顺序、missing-default 非对称、
signed auto comparison 与 invalid-mode cutoff 全部写入回归。

## 四端函数映射

| 目标 | `MotionLayer_refreshFace_guess` | `MotionLayer_resolveAutoMode_guess` | `MotionLayer_resolveBitmapMethod_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x69AB18` | `0x69ACD0` | `0x69ED70` |
| Android armv7 | `0x57551C` | `0x5755E4` | `0x577684` |
| iOS arm64 | `0x1000F936C` | `0x1000F94CC` | `0x1000FC29C` |
| iOS armv7 | `0xF63F8` | `0xF6558` | `0xF9328` |

Android arm64 的 `0x69ED70` 本轮前仍为 `sub_69ED70`。fresh decompile 证明它与另外三端的
named helper 逐分支一致，因此恢复为 `_guess` 语义名；stripped references 不提供原始 C++
identifier，不能去掉 `_guess`。

## compiler / ABI 差异与 xref topology

Android arm64 优化器同时保留 standalone bitmap-method helper，又把完整 matrix 内联进
`operateMesh` 和 `operateBezierPatch`：

| 目标 | refreshFace callers | auto-mode callers | standalone matrix callers |
|---|---:|---:|---:|
| Android arm64 | 5 | 2 | 0 |
| Android armv7 | 3 | 2 | 2 |
| iOS arm64 | 3 | 2 | 2 |
| iOS armv7 | 3 | 2 | 2 |

arm64 Android 的五个 refresh xrefs 来自 standalone matrix、两个 copy entries 以及两个已
内联 matrix 的 operate entries。其余三端由 standalone matrix 代表两个 operate entries，
加上两个 copy entries，所以 refresh 只有三个直接 caller。四端 auto-mode 都只被两个
operate entries 调用。

这只是优化策略差异：Android arm64 的 standalone matrix 并非不同版本的 dead semantics；
两个实际 operate callsites 的内联分支与它逐项一致。64-bit attached object 的 cached face 在
`+8`，32-bit 在 `+4`，是 owner pointer 宽度造成的布局差异。

Android arm64 还把 `getIntValue` 的 probe/read body 内联到 `refreshFace` 与
`resolveAutoMode`；其余三端常显示对 `ncbPropAccessor_getIntValue_guess` 的调用。三份可见
helper 和 arm64 inline body 的共同语义是：

```text
probe = owner.PropGet(
    flags=TJS_MEMBERMUSTEXIST,
    member=name,
    hint=null,
    result=&probeVariant,
    objthis=owner)
destroy probeVariant

if probe is negative:
    return callerDefault

value = owner.PropGet(
    flags=0,
    member=name,
    hint=null,
    result=&valueVariant,
    objthis=owner)
ignore second status
return Integer(valueVariant)
```

因此一次逻辑 `getIntValue` 最多触发两次脚本可见 PropGet。probe failure 只触发一次；probe
成功后脚本重入仍可以在第二次读取前改变/删除成员，native 不复用 probe result，也不检查
第二次 HRESULT。

## UTF-16LE literal 消歧

`face` 和 `type` 都是全程序高频成员名，普通字符串命中不能建立 MotionLayer 调用链。本轮按
UTF-16LE 原始字节重新搜索，并以函数内 xref 确认 MotionLayer 实际使用的 literal：

| 目标 | referenced `face` literal | referenced `type` literal | operate error literal |
|---|---:|---:|---:|
| Android arm64 | `0x1518B1E` | `0x1519D52` | `0x14D5506` |
| Android armv7 | `0xDBDF0C` | `0xDBF140` | `0xD850DE` |
| iOS arm64 | `0x10195B760` | `0x10195B272` | `0x10195B8E4` |
| iOS armv7 | `0x174DAC4` | `0x174D5D6` | `0x174DC48` |

raw search 的 `face` 命中数分别为 4/10/13/13，`type` 为 11/11/18/18；很多命中属于
TextRender、Emote、geometry NCB、Player timeline 等无关逻辑。上述地址的 MotionLayer xref
才是本轮证据。operate error literal 每端只有一个，并同时被 mesh 与 Bezier operate entry
引用，证明两个入口故意共享文本 `operateMesh: not drawable face type.`；Bezier operate
并没有单独的 `operateBezierPatch` error string。

## `refreshFace` 精确分类

共同伪代码为：

```text
cachedFace = getIntValue(owner, "face", default=0, hint=null)
if cachedFace != 128:
    return

type = getIntValue(owner, "type", default=0, hint=null)
if type == 2 || 13 <= type <= 28:
    cachedFace = 0
else if type == 12:
    cachedFace = 4
else:
    cachedFace = 1
```

边界行为：

- 缺失 `face`：probe failure 后直接提交 cached face 0，不读取 `type`；
- raw face 不是 128：原值原样保留，包括负数、2、5 或任意未识别值，matrix/copy caller
  再决定是否拒绝；
- raw face 128 且缺失 `type`：type 默认 0，因此进入最后分支，提交 face 1；
- type 2 与 13..28：face 0；type 12：face 4；
- 其余 type，包括负数、0、1、3..11、29 及更大值：face 1。

ARM listings 中 13..28 常呈现为 unsigned `(type - 13) < 16`，它和上述 signed interval 对
32-bit Integer 输入等价；负值不会误入范围。

`refreshFace` 每次都重新读取 owner，不把 cached face 当作跨 entry memoization。字段只是当前
一次后续 matrix/copy 分支的中间状态。

## `resolveAutoMode` 的 signed 边界

共同伪代码为：

```text
type = getIntValue(owner, "type", default=0, hint=null)
return type > 28 ? 1 : type       // signed comparison
```

因此：

- type 29 或更大值折叠成 mode 1；
- type 28 原样保留为 mode 28；
- type 0、负数原样保留，不做 unsigned clamp；
- 缺失 type 只做一次 probe，返回默认 mode 0；
- auto-mode 读取并不会更新 cached face；紧接着 matrix 的 `refreshFace` 会独立再次读取
  face，并可能再次读取 type。

这造成一个有意的 missing-type 非对称：

```text
face=128 + missing type  -> classifier default type 0 -> face 1
mode=128 + missing type  -> auto default type 0       -> invalid mode 0
```

两处使用相同 `getIntValue` default，但 default 进入的下游 state machine 不同。

## bitmap-method 完整矩阵

helper 首先无条件 refresh face，然后按下表解析。`—` 表示失败：

| input mode | face 0 | face 1 | face 4 | 其他 face |
|---:|---:|---:|---:|---:|
| 1 | 1 | 0 | 15 | — |
| 2 | 3 | 2 | 14 | — |
| 3 | 4 | 4 | 4 | 4 |
| 4 | 5 | 5 | 5 | 5 |
| 5 | 6 | 6 | 6 | 6 |
| 6 | — | — | — | — |
| 7 | — | — | — | — |
| 8 | 7 | 7 | 7 | 7 |
| 9 | 8 | 8 | 8 | 8 |
| 10 | 9 | 9 | 9 | 9 |
| 11 | 10 | 10 | 10 | 10 |
| 12 | 13 | 11 | 12 | — |
| 13..28 | `mode + 3` | `mode + 3` | `mode + 3` | `mode + 3` |
| 其他（含负数、0、29+、未先解析的 128） | — | — | — | — |

mode 3/4/5/8/9/10/11 和 13..28 虽然不依赖 face 值，仍然先调用 refresh；脚本 getter 的
side effect/异常不能被优化掉。mode 1/2/12 只接受 face 0/1/4。

output method 只在 success branch 写入，helper 返回 true；failure branch 返回 false，不发布
任何 fallback method。portable caller 虽然把局部 output 初始化为 0，native 语义仍是“失败
不写”；caller 随即抛出 shared operate error，且不会读取 `holdAlpha` 或 clip quartet。

input mode 128 不属于 matrix 内部 case。两个 operate caller 会先调用 auto-mode resolver，再
把解析结果送入 matrix；其他调用者若直接传 128，matrix 本身会失败。

## 数据流、重入与生命周期

每个 resolver 都从 attached object 的 raw owner pointer 构造 `ncbPropAccessor`。accessor 对
owner 建立当前调用范围的 retained reference，并在返回或异常展开时释放；它不会改变
attached object's non-owning owner field。

probe/value 各有独立临时 Variant，probe Variant 在 value read 前析构。脚本可以在两次
PropGet 之间重入并修改 face/type；第二次结果决定转换，没有 snapshot consistency。若第一
次返回负 HRESULT，则不会发生第二次读取。null member hint 表示每次都不依赖 motionplayer
全局/TU-local lookup cache。

`refreshFace` 先写 raw/default face，再在 raw 128 分支覆盖分类值。若 type getter/Integer
conversion 抛出，cached face 已经是 128，不会回滚；若 mapping 完成则覆盖成 0/1/4。
`resolveAutoMode` 不写 object state。matrix 先允许 refresh side effect，再决定 output commit。

## 源码与回归修改

运行时代码不改控制流，只补充两条无地址注释：

- `refreshFace` 的 null-hint probe/value 双读取；
- auto-mode 的 signed `> 28` 比较。

V170 的 clear recorder 也从错误的四次 property expectation 修正为五次：face probe、face
value、neutralColor、height、width；fillRect cutoff 与参数不变。

V176 的 clip/holdAlpha recorder 修正为真实七次序列：

```text
face (MEMBERMUSTEXIST, hint=null)
face (flags=0, hint=null)
holdAlpha (flags=0, shared non-null hint)
clipLeft / clipTop / clipWidth / clipHeight
```

新增 `MotionLayer face and auto mode preserve unhinted probe boundaries` 回归，通过公开
`operateMesh` 入口和 `clipHeight` exception cutoff 覆盖：

- missing face：只有 probe，default face 0，face-independent mode 3 继续到 holdAlpha；
- face 128/type 13：face 和 type 各有 probe/value 两次读取；
- face 128/missing type：type 只有 failed probe，default 0 分类为 face 1；
- auto type 29：先 type probe/value，再独立 face probe/value，折叠到 mode 1；
- auto type -1：signed negative 保留，matrix failure 发生在 holdAlpha 前；
- auto missing type：default mode 0，face refresh 后仍在 holdAlpha 前失败；
- raw face 2 + mode 1：matrix 拒绝，只有两次 face 读取。

所有 face/type probe/value 都断言 null hint 和 owner receiver；exception cutoff 避免进入真实
native layer/texture/render manager。

## recovery IDB 写回

四份 recovery IDB 已完成：

- Android arm64 `sub_69ED70` 重命名为
  `MotionLayer_resolveBitmapMethod_guess`；
- refresh/auto/matrix 三函数每库写入精确语义注释，另给 arm64 两个 inlined operate callsites
  写入优化边界注释，共 14 处；
- 三函数每库建立 bookmark，共 12 个；
- 三函数每库 force-recompile，另含 arm64 两个 inlined operate entries，共 14 个函数；
- 12 个核心函数 readback 均解析正确语义名、V177 注释与关键分支；
- 四份 recovery IDB 原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 修正 missing-type cases 与 V170 clear recorder 后再次执行双配置 syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,647,409 bytes，539 imports / 69 exports；
- Headless wasm：84,994,550 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 两份产物与 V176 精确等长，符合本轮运行时代码只有注释、行为修正只在 unit-test
  expectation 的预期；
- 两个 build tree 的 CTest 仍报告 `No tests were found`，因此回归目前由 unit-test TU 的
  ordinary/headless 双配置编译覆盖，不能声称已被 CTest 执行；
- 定向 `git diff --check` 无 whitespace error，只有 dirty worktree 既有 LF/CRLF 提示。

## 结论边界

本轮证明的是 owner `face/type` 的无-hint getter state machine 和 bitmap-method mapping，不是
对所有同名 `face/type` consumers 的全局结论。宽字符串在 TextRender、Emote、geometry、
Player 等模块大量复用；后续分析必须继续以函数/data identity 消歧，不能因 literal 相同而
给这些读取分配 MotionLayer hint 或共享其 cached face。
