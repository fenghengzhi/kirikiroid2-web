# MotionPlayer Selector index 转换与 unordered duration 四端复原（2026-08-16）

## 1. 结论

`EmoteSelectorController` 有三个把 raw float selection 消费成 `selectedIndex` 的位置：

1. enqueue 的 immediate 分支；
2. step 取出并 pop front command 后；
3. reset 读取 queue back command 时。

四个当前参考二进制在三个位置都使用 ARM signed-int32 floating conversion，round
toward zero：

| float selection | index |
|---|---:|
| `+0.0` / `-0.0` | `0` |
| 有限且 int32 可表示 | 向零截断 |
| `>= +2^31` / `+Inf` | `INT32_MAX` |
| `<= -2^31` / `-Inf` | `INT32_MIN` |
| NaN | `0` |

旧 portable 的三个裸 `static_cast<int>` 在 NaN/越界处属于 C++ UB，且旧文档只写
“普通 C++ 截断、没有 clamp”，没有恢复 target-instruction 边界。现抽出 TU-local
`selectorIndexFromFloat_guess`，显式复现 conversion；`_guess` 表明这是 portable
源码级抽取，参考二进制没有可证明的同名函数。

第二处源级偏差是 duration gate。四端都只允许 ordered `duration > 0` 入队；0、负数
和 unordered NaN 全部走 immediate clear/apply。当前源码原写 `duration <= 0`，对 NaN
为 false，因而错误地把 NaN duration 入队。现改为 `!(duration > 0.0f)`。

## 2. 函数与 conversion 指令

| 目标 | enqueue | reset | step |
|---|---:|---:|---:|
| Android arm64 | `0x6655C4` | `0x665774` | `0x665850` |
| Android armv7 | `0x554AB8` | `0x554B68` | `0x554BC4` |
| iOS arm64 | `0x1001A5640` | `0x1001A56D4` | `0x1001A5790` |
| iOS armv7 | `0x1A4C10` | `0x1A4C7E` | `0x1A4CF6` |

三个消费点的 conversion：

| 目标 | immediate enqueue | reset back | step front |
|---|---:|---:|---:|
| Android arm64 | `0x665658` `FCVTZS W1,S8` | `0x6657C8` `FCVTZS W1,S0` | `0x665900` `FCVTZS W1,S10` |
| Android armv7 | `0x554B28` `VCVT.S32.F32 S0,S16` | `0x554B94` `VCVT.S32.F32 S0,S0` | `0x554C26` `VCVT.S32.F32 S0,S16` |
| iOS arm64 | `0x1001A56B0` `FCVTZS W1,S8` | `0x1001A5738` `FCVTZS W1,S0` | `0x1001A583C` `FCVTZS W1,S10` |
| iOS armv7 | `0x1A4C5E` `VCVT.S32.F32 D0,D8` | `0x1A4CC6` `VCVT.S32.F32 D0,D0` | `0x1A4D86` `VCVT.S32.F32 D0,D8` |

iOS armv7 用双 lane 形式，但调用只消费 low lane；enqueue 还先把 selection 复制到
两个 lane。它与另外三个 scalar 形式具有相同 low-lane signed-int32 数值语义，不是
第二套 selector 算法。

## 3. duration compare 与 NaN 路由

| 目标 | compare | immediate branch |
|---|---:|---:|
| Android arm64 | `0x6655F0` `FCMP S10,#0` | `0x6655F8` `B.LE` |
| Android armv7 | `0x554AD0` `VCMPE.F32 S0,#0` | `0x554AE4` `BLE` |
| iOS arm64 | `0x1001A5664` `FCMP S1,#0` | `0x1001A5668` `B.LE` |
| iOS armv7 | `0x1A4C22` `VCMPE.F32 S0,#0` | `0x1A4C30` `BLE` |

ARM floating compare 的 unordered flags 满足这里的 `LE` condition，因此 NaN 与零/
负 duration 一起进入 immediate 分支。等价源表达是：

```cpp
if (!(duration > 0.0f)) {
    queue.clear();
    selState = 0;
    applySelection(convert(selection), 0.0f, 0.0f);
    return;
}
```

`append` 在 immediate 分支完全不参与决定：即使 `append=true`，queue 仍先 clear。

## 4. 三个消费点的提交与异常顺序

### immediate enqueue

严格顺序是 clear 整条 command deque、`selState=0`、转换 selection、tail-call
`applySelection(index,0,0)`。conversion 本身不抛；`applySelection` 先写
`selectedIndex`，再按 option 顺序更新 borrowed transition controllers。

### step

idle 且 queue 非空时：

1. 按值复制 front 的 `{selection,duration,fade}`；
2. pop front；跨 block 时可释放旧 deque block；
3. 转换已经复制的 selection；
4. 调用 `applySelection`；
5. 只有 apply 正常返回才写 `invDuration=1/duration`、`selState += 1`、`accum=0`；
6. 将 signed selectedIndex 转成 float，同时写 `*out` 并返回。

因此 apply 抛异常时 command 已被消费，selectedIndex/早先 option side effect 可能已提交，
但 ramp state 的后三项尚未写。本轮只替换 conversion helper，没有移动这些 commit 点。

### reset

queue 非空时先 `selState=0`，读取 back selection、转换并 apply；只有 apply 正常返回后
才 clear queue。apply 抛异常时 queue 原样保留。queue 为空但 state 非零时则把 state
清零并重用现有 signed selectedIndex，不发生 float conversion。

## 5. 回归与验证范围

新增 selector unit case 固定：

- NaN duration 即使 `append=true` 也 clear queue、idle 并立即应用；
- immediate selection 的 NaN、正负无穷与正负分数；
- queued step 在 pop 后把 `+Inf` 转为 `INT_MAX`，再设置 ramp fields，并以 signed-int
  到 float conversion 返回同一 index；
- reset 的 back selection 为 NaN 时转为 0，apply 后 clear queue。

测试 TU 已在 ordinary Web 与 Wasmtime headless 的真实 Emscripten 参数下通过
`-fsyntax-only`；Web Debug preset 已重编 `EmoteSelectorController.cpp`、归档并完成
最终 Wasm/HTML link，Wasmtime Headless Debug preset 也已重编并生成
`motionplayer` archive。唯一测试 TU 诊断仍是仓库既有 `_tss` literal-operator
弃用 warning；Web link 只有既有 pthread/memory-growth、JSPI 与 JS library warnings。
当前 preset 不生成可直接运行的 `motionplayer-dll` test executable，因此本记录只
声明测试已编译，不声明已运行。
