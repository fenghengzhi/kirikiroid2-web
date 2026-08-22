# MotionPlayer shared easing nested NCB、segment base 与 unordered 边界四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新从 `reference/binaries/` 的 Android arm64、Android armv7、
iOS arm64、iOS armv7 四份产品代码复核共享 easing evaluator。恢复名继续保守使用
`VariableTrackEasing_evaluate_guess` / `evaluateVariableTrackEasing_guess`；参考产品已剥离，
目前没有足以去掉 `_guess` 的符号证据。

本轮推翻了两条旧结论：

1. stride-three 搜索得到的是 segment 的末端索引，四个控制点必须从
   `segmentEnd - 3` 开始读取；旧 portable 实现曾错误地从 `segmentEnd` 开始。
2. 四端 endpoint 分支不是“unordered 时不命中”的普通 ordered 比较。第一次
   `FCMP/VCMPE` 后用 `PL`，第二次用 `LE`；两者都会接受 unordered。因而
   `t=NaN` 在读取 `Count` 和 `x[0]` 后立即返回 `y[0]`，不会进入 stride loop。

同时确认 evaluator 不是 VariableTrack 私有实现：其调用面覆盖 timeline 的多种
position/angle/color 插值、position helper、VariableTrack、MotionSub 以及若干短 helper；
`x` / `y` member-hint 槽也被点字典、position control curve、LayerGetter 顶点字典和
Player camera-offset 字典共同复用。

## 四端函数映射

| target | recovered entry | size |
|---|---:|---:|
| Android arm64 | `0x697B34` | `0x4F8` |
| Android armv7 | `0x573D40` | `0x228` |
| iOS arm64 | `0x1000F78C0` | `0x2D8` |
| iOS armv7 | `0xF4648` | `0x2CE` |

函数 xref 数分别为 22、23、23、23。Android arm64 的一处差异来自优化期的
inline/tail ownership，不构成不同的源级 evaluator。四端共同 caller surface 包括：

- `Player_evaluateTimeline_guess` 内的多种字段插值；
- position、angle、packed-color 等共享插值 helper；
- VariableTrack 的值插值路径；
- MotionSub 的相关 wrapper；
- 少量被优化成不同 tail 形状的短 helper。

因此 portable 源只保留一份 out-of-line evaluator，所有这些 caller 共享其动态属性
访问次序、异常边界、浮点分组和 malformed-input 行为。

## `x` / `y` hint 是插件级共享身份

四端 evaluator 使用的准确槽地址如下：

| target | `x` hint | `y` hint | xrefs per slot |
|---|---:|---:|---:|
| Android arm64 | `0x1AB5234` | `0x1AB5238` | 12 |
| Android armv7 | `0x1111768` | `0x111176C` | 18 |
| iOS arm64 | `0x101B696FC` | `0x101B69700` | 6 |
| iOS armv7 | `0x187D42C` | `0x187D430` | 15 |

xref 数量因 ADRP/literal materialization、tail ownership 和 inline 形状而不同；身份本身
在各端是唯一的。逐 xref 复核得到的共同使用族为：

- Quad point dictionaries；
- PositionControlCurve 的 main/nested `x` / `y`；
- 本 shared easing evaluator；
- LayerGetter vertex dictionaries；
- Player camera-offset dictionary。

因此旧的 `variableTrackEasingXHint_guess` / `variableTrackEasingYHint_guess` 命名会把
共享槽误描述为单一 helper 私有状态。本地统一使用
`motion::detail::xMemberHint_guess` / `yMemberHint_guess`。

2026-08-17 V175 follow-up：重新取得四端完整 xrefs，并对 Android arm64 五类 consumers
fresh decompile 后，已证明该端同样把两个地址作为独立 word 传址，不存在 aggregate
访问语义。四份 recovery IDB 现均已拆成、命名为两个 size-4 data items；此前“Android
arm64 只写 offset comment、不拆 aggregate”的保守记录已被取代。完整写回证据见
`analysis/motionplayer_xy_shared_hint_idb_boundary_completion_four_binary_2026-08-17.md`。

## nested NCB source tree 与对象生命周期

四端共同的 owner tree 是：

```text
copied easing Variant
└─ retained root ncbPropAccessor
   ├─ typed Variant x  ── retained x ncbPropAccessor
   └─ typed Variant y  ── retained y ncbPropAccessor
```

准确构造与读取顺序为：

1. 复制 easing 输入，并以该复制值构造函数级 root `ncbPropAccessor`；构造 accessor
   所用 conversion Variant 在第一次 named read 前析构。
2. root accessor 对 root receiver/objthis 读取 typed Variant `x`，flags 为 0，hint 为
   插件级共享 `x` 槽。
3. 同一个 root accessor 对同一个 receiver/objthis 读取 typed Variant `y`，flags 为 0，
   hint 为插件级共享 `y` 槽。
4. `x` 和 `y` 结果 Variant 都是独立的函数级 owner。
5. 从 `x` 的复制值构造 x accessor；其 conversion Variant 随即析构。
6. 从 `y` 的复制值构造 y accessor；其 conversion Variant 随即析构。
7. 后续唯一一次 `Count` 和全部 x numeric reads 都通过 x accessor；全部 y numeric reads
   都通过 y accessor。

普通返回的逆构造顺序是：

```text
y accessor
x accessor
y persistent Variant
x persistent Variant
root accessor
```

这意味着脚本 getter 即使重入清空 caller 持有的 easing、x 或 y Variant，root 与两个
array source 仍必须分别存活至其最后一次动态读取。它也意味着不能把两个 named read
改成两个 full-expression accessor，不能只保留 x/y 的裸 dispatch 指针，也不能在取得
数组对象后提前释放 root accessor。

## dispatch ABI、结果写入与 HRESULT

所有动态读取均使用普通 flags 0：

- root `x` / `y`：typed `tTJSVariant`，共享 named hint，root receiver 也作为 objthis；
- x `Count`：只读一次，null hint，x receiver 也作为 objthis；
- x/y numeric element：typed `tjs_real`，null hint，对应 array receiver 也作为 objthis。

四端都呈现 NCB typed getter 的共同边界：dispatch 已经把结果写入 Variant 后，即使返回
ordinary failure HRESULT，后续转换/计算仍使用该写入值；没有额外的 HRESULT gate。
对象取得或 `tjs_real` 转换本身抛出的异常仍自然传播，并由上述 owner tree 负责 unwind。

本轮 portable 实现因此直接表达三角色 `ncbPropAccessor`，不再经过会隐藏 accessor
生命周期的旧 `motionPropGet` wrapper。

## Count、索引和 wrap 行为

`Count` 只取自 x accessor，y 不读 Count。随后所有索引运算都按四端共同的 32 位机器
算术处理：

```text
last       = wrap32(count - 1)
segmentEnd = wrap32(segmentEnd + 3)
base       = wrap32(segmentEnd - 3)
index      = wrap32(index + 1)
```

因此：

- `count == 0` 不会得到空数组 fallback，而会以 32 位 bit pattern `0xFFFFFFFF`
  访问 `x[-1]`；
- stride loop 没有 `segmentEnd < count` 或任何迭代上限；
- malformed 或非 `3n+1` 数组没有 sanitizer；
- 溢出必须按 32 位 wrap 复原，不能在 portable C++ 中依赖 signed-overflow UB。

## endpoint 的准确机器条件

四端三处关键 branch 完全一致：

| target | first endpoint | last endpoint | stride repeat |
|---|---|---|---|
| Android arm64 | `FCMP 0x697D14`; `B.PL 0x697D18` | `FCMP 0x697D34`; `B.LE 0x697D38` | `FCMP 0x697D58`; `B.MI 0x697D5C` |
| Android armv7 | `VCMPE 0x573DF8`; `BPL 0x573E00` | `VCMPE 0x573E14`; `BLE 0x573E1C` | `VCMPE 0x573E36`; `BMI 0x573E3E` |
| iOS arm64 | `FCMP 0x1000F79DC`; `B.PL 0x1000F79E0` | `FCMP 0x1000F79FC`; `B.LE 0x1000F7A00` | `FCMP 0x1000F7A20`; `B.MI 0x1000F7A24` |
| iOS armv7 | `VCMPE 0xF4786`; `BPL 0xF478E` | `VCMPE 0xF47A8`; `BLE 0xF47B0` | `VCMPE 0xF47CE`; `BMI 0xF47D8` |

不要把这些 branch 仅写成高层“ordered comparison”。对普通非 NaN 值，它们分别表现为
`firstX >= t`、`lastX <= t`、`segmentX < t`；但 unordered 时标志行为不同：

- `PL`：ordered `>=` **或 unordered** 都跳转；
- `LE`：ordered `<=` **或 unordered** 都跳转；
- `MI`：只在 ordered `<` 时重复，unordered 不重复。

准确决策顺序是：

```text
count = x.Count
firstX = real(x[0])
if unordered(firstX, t) or firstX >= t:
    return real(y[0])

last = wrap32(count - 1)
lastX = real(x[last])
if unordered(lastX, t) or lastX <= t:
    return real(y[last])

segmentEnd = 0
do:
    segmentEnd = wrap32(segmentEnd + 3)
while ordered(real(x[segmentEnd]) < t)
```

可观察边界包括：

- `t=NaN`：第一次 compare 已 unordered，读取顺序严格是 Count、`x[0]`、`y[0]`，
  返回 `y[0]`；不读 `x[last]`。
- `x[0]=NaN`：同样立即返回 `y[0]`。
- 当 first gate 未命中而 `x[last]=NaN` 时：第二次 compare unordered，返回 `y[last]`。
- stride 位置为 NaN 时：`MI` 不命中，停止在该 `segmentEnd`，随后读取该 segment 的
  四对控制点。

为防止 Emscripten/Clang 的浮点优化把 unordered 语义折回普通 `>=` / `<=`，portable
实现以 binary64 exponent/fraction bit test 显式识别 NaN，再组合对应关系。它不调用
可能受 fast-math lowering 影响的高层分类表达式。

## segment 基址与动态读取次序

stride loop 的变量是 segment 末端，不是控制点起点：

```text
segmentEnd = first stride-three x index whose value is not ordered-less than t
index = wrap32(segmentEnd - 3)
repeat four times:
    discard real(x[index])
    values[i] = real(y[index])
    index = wrap32(index + 1)
```

四端的 raw evidence 以不同寄存器分配表达同一个 `-3`：

- Android arm64 明确执行 `SUB W21, W20, #3`；
- Android armv7 在 `0x573E54..0x573E60` 建立 selected-end-minus-three；
- iOS arm64 以初值 `-3` 的寄存器读取 `reg+6` 作为 stride probe，再按 3 推进，
  第一次退出时控制点 base 为 0；
- iOS armv7 使用同形的 `-3` 游标布局。

以 x=`[0,0,0,1]`、y=`[0,0,1,1]`、`t=0.5` 为例，准确动态读取是：

```text
x indices: 0, 3, 3, 0, 1, 2, 3
y indices:          0, 1, 2, 3
```

其中第二个 `x[3]` 是 stride probe，第三个 `x[3]` 是选中 segment 的 discarded-x
控制点读取。即使四个 x 控制点数值不进入最终多项式，这些 getter 仍可能产生脚本副作用
或异常，不能消除。

## 多项式分组

四端共同保留以下乘加分组，输入参数仍是原始 `t`；x 只选择 segment，不执行 inverse-X
重参数化：

```text
u = 1.0 - t
return u * (u * u) * y0
     + u * (u * 3.0) * t * y1
     + u * 3.0 * t * t * y2
     + t * t * t * y3
```

对应机器区间为 Android arm64 `0x697E18..0x697E60`、Android armv7
`0x573E82..0x573ED0`、iOS arm64 `0x1000F7A6C..0x1000F7AC0`、iOS armv7
`0xF481A..0xF486C`。这些分组会影响逐 bit 浮点结果，不能仅以代数等价为理由换成另一种
Bernstein/Horner 写法。

## portable 源与回归探针

本轮对齐了：

- `PlayerFrameProgress.cpp` 中 shared evaluator 的 root/x/y 三层 accessor source tree；
- root `x` / `y` 使用插件级共享 hint；
- Count 和全部 numeric read 使用 typed NCB getter；
- `last`、stride、base 和逐控制点 index 的显式 32 位 wrap；
- 从 `segmentEnd - 3` 开始的四对 x/y 动态读取；
- first/last endpoint 的 unordered-inclusive branch；
- 四端共同的 cubic 乘法分组；
- `PlayerInternal.h` 中 PositionControlCurve 的 main/nested x/y 也改用相同共享 hint，
  删除 easing-private hint 声明。

测试翻译单元新增或加强了三类探针：

1. 普通 interior case 锁定 named hints、一次 Count、x 索引
   `{0,3,3,0,1,2,3}`、y 索引 `{0,1,2,3}` 和结果 0.5。
2. `t=NaN` 锁定只读 `x[0]` / `y[0]`；last-x 为 NaN 的独立 case 锁定
   `x[0],x[last]` 后只读 `y[last]`。
3. reentrant owner-clear case 在 `x` getter、Count 和 y 首次 numeric getter 内分别清空
   external owner，验证 root/x/y accessor 都保活、flags/hint/objthis 准确、post-write
   failure 可继续，并验证 y accessor → x accessor → y Variant → x Variant → root accessor
   的析构边界。

## IDB 回写

四份 recovery IDB 均完成：

- evaluator 函数注释；
- 每库 27 个关键指令位置的逐地址注释；
- x/y hint 的准确 data comment；
- bookmark `V149 easing retained root/x/y + segmentEnd-3 controls`；
- 强制 recompile/decompile 与注释、xref、名称读回；
- 最终数据库保存。

早期函数注释曾把 endpoint 写成 ordered-only；fresh raw branch 复核后，四库都追加了明确的
`V149 CORRECTION` superseding comment，并覆盖 first/last compare 的逐地址注释。当前 function
comment 顶部保留历史句和明确的更正句，是 IDA 接口只能追加而不能替换现有函数注释所致；
后者才是当前结论。

## 验证

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两种 motionplayer test TU 语法检查通过；仅有既有
  `_tss` warning。
- `Web Debug Build` 完整/增量构建通过；最终 `index.wasm` 为 85,640,040 bytes。
- `Wasmtime Headless Debug Build` 完整/增量构建通过；最终 `index.wasm` 为
  84,987,181 bytes。
- 两份 Wasm 均通过 Node `WebAssembly.Module` 解析；Web 为 539 imports / 69 exports，
  headless 为 538 imports / 69 exports。
- 两份 Wasm 均通过 `llvm-objdump -h` section 解析。
- 两个 build tree 的 CTest 都报告 `No tests were found!!!`；因此这里只报告探针已编译进
  test TU，不虚报 runtime CTest 执行。
- `git diff --check` 通过。
