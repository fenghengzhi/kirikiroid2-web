# MotionPlayer controller duration unordered 分流四参考闭环（2026-08-16）

## 结论

对五个 controller setter 的 20 个当前参考函数重新读取原始 ARM 指令后，
`duration` 的 NaN 边界不是统一行为，而是一个需要逐 family 保留的源码级分叉：

| controller | 四端条件分支 | NaN duration | portable C++ 等价写法 |
| --- | --- | --- | --- |
| Var | `B.LS/BLS` | 进入 queue 路径 | `duration <= 0.0f` 为 immediate |
| Angle | `B.LE/BLE` | immediate | `!(duration > 0.0f)` 为 immediate |
| Blink | `B.LE/BLE` | immediate | `!(duration > 0.0f)` 为 immediate |
| Eyebrow | `B.LE/BLE` | immediate | `!(duration > 0.0f)` 为 immediate |
| Mouth | `B.LE/BLE` | immediate | `!(duration > 0.0f)` 为 immediate |

因此不能把反编译器显示的 `duration <= 0` 机械复制到全部 portable C++，也不能
反过来把全部 setter 统一成“not ordered-positive”。本轮只修改 Angle、Blink、
Eyebrow 与 Mouth；Var 保持 `duration <= 0.0f`，并新增一条同时覆盖两种 NaN
路由的回归测试。

## 四端原始指令映射

### Var：unsigned lower-or-same

| 目标 | 函数 | compare | branch | immediate 目标 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6646E0` | `0x664710 FCMP S0,#0` | `0x664720 B.LS` | `0x664778` |
| Android ARMv7 | `0x5542B0` | `0x5542C2 VCMPE` | `0x5542D6 BLS` | `0x5542FC` |
| iOS ARM64 | `0x1001A4C44` | `0x1001A4C64 FCMP` | `0x1001A4C68 B.LS` | `0x1001A4C98` |
| iOS ARMv7 | `0x1A418C` | `0x1A419C VCMPE` | `0x1A41A8 BLS` | `0x1A41CE` |

### Angle：signed less-or-equal

Angle 先用截断的 `6.2832f` 对 `endRad` 做循环归一化，再执行 duration gate。

| 目标 | 函数 | compare | branch | immediate 目标 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x663870` | `0x6638E0 FCMP S10,#0` | `0x6638E4 B.LE` | `0x6638FC` |
| Android ARMv7 | `0x553AD4` | `0x553B32 VCMPE S0,#0` | `0x553B3E BLE` | `0x553B5C` |
| iOS ARM64 | `0x1001A4308` | `0x1001A436C FCMP S1,#0` | `0x1001A4370 B.LE` | `0x1001A439C` |
| iOS ARMv7 | `0x1A3798` | `0x1A37EC VCMPE S0,#0` | `0x1A37F8 BLE` | `0x1A3816` |

### Blink

| 目标 | 函数 | compare | branch | immediate 目标 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x660C90` | `0x660CC0 FCMP` | `0x660CC8 B.LE` | `0x660CF8` |
| Android ARMv7 | `0x5522FC` | `0x552310 VCMPE` | `0x552324 BLE` | `0x55234E` |
| iOS ARM64 | `0x1001A2568` | `0x1001A258C FCMP` | `0x1001A2590 B.LE` | `0x1001A25C4` |
| iOS ARMv7 | `0x1A1850` | `0x1A1862 VCMPE` | `0x1A186E BLE` | `0x1A1896` |

### Eyebrow

| 目标 | 函数 | compare | branch | immediate 目标 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6626B4` | `0x6626E4 FCMP` | `0x6626EC B.LE` | `0x66271C` |
| Android ARMv7 | `0x553170` | `0x553184 VCMPE` | `0x553198 BLE` | `0x5531C2` |
| iOS ARM64 | `0x1001A3764` | `0x1001A3788 FCMP` | `0x1001A378C B.LE` | `0x1001A37C0` |
| iOS ARMv7 | `0x1A2B6C` | `0x1A2B7E VCMPE` | `0x1A2B8A BLE` | `0x1A2BB2` |

### Mouth

| 目标 | 函数 | compare | branch | immediate 目标 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x663214` | `0x663240 FCMP` | `0x663248 B.LE` | `0x663278` |
| Android ARMv7 | `0x553788` | `0x55379C VCMPE` | `0x5537B0 BLE` | `0x5537D0` |
| iOS ARM64 | `0x1001A3EE0` | `0x1001A3F04 FCMP` | `0x1001A3F08 B.LE` | `0x1001A3F34` |
| iOS ARMv7 | `0x1A3358` | `0x1A336A VCMPE` | `0x1A3376 BLE` | `0x1A3396` |

## 为什么 `LS` 与 `LE` 在 NaN 上分叉

ARM `FCMP/VCMPE` 遇到 unordered 输入时产生 `N=0, Z=0, C=1, V=1`。

- `LS` 的条件是 `C==0 || Z==1`，unordered 不满足，所以 Var 继续执行 queue
  路径；C++ 的 `duration <= 0.0f` 也在 NaN 上为 false。
- `LE` 的条件是 `Z==1 || N!=V`，unordered 因 `N!=V` 成立，所以另外四类跳到
  immediate 路径；C++ 必须写成 `!(duration > 0.0f)` 才保留这个行为。

对有限数、正负零与无穷，两种写法的路由相同：正数（含 `+Inf`）入队，负数
（含 `-Inf`）和 `+0/-0` immediate。只有 NaN 把二者分开。

## immediate 路径的数据流与副作用

四个 `LE` family 在 NaN duration 下与非正 duration 完全走同一个 basic block：

- Angle：先完成 `endRad` 归一化，再清 12B queue、写 `state=0`、把归一化结果写入
  `currentRad`；不写 `phase/invDuration/powCount/startRad/targetRad`。
- Blink：依次清 12B primary queue 与 8B resolved secondary queue，写
  `trackValue=value`、`trackState=0`。
- Eyebrow：与 Blink 相同地清两条 queue，并写 `trackValue`、`trackState=0`。
- Mouth：清 12B queue，写 `currentValue=value`、`state=0`。

这四条路径都不会读取 `append`；即使调用者传 `append=true`，NaN duration 仍会
清掉已有命令。Var 则会读取 `append`：`append=true` 保留旧队列和 state，再把
NaN duration 原位写入新 20B keyframe 的 duration word。

## 本地修正与回归边界

源码将以下四处 gate 从 `duration <= 0.0f` 改为 `!(duration > 0.0f)`：

- `EmoteAngleController_setTarget_guess`
- `EmoteBlinkController_enqueueValue_guess`
- `EmoteEyebrowController_enqueueValue_guess`
- `EmoteMouthController_setTarget_guess`

`EmoteVarController_setTarget_guess` 明确保留 `duration <= 0.0f`。新增回归在同一个
用例中先证明 Var 的 NaN 命令追加且不提交当前值，再证明 Angle、Blink、Eyebrow、
Mouth 的 NaN 命令清 queue、归零 state 并立即提交目标；Blink/Eyebrow 还同时验证
resolved 8B queue 被清空。

## IDB 与验证

四份 recovery IDB 的五个入口、duration compare/branch 和 immediate basic block
均记录了 `LS`/`LE` 的 unordered 分流与 portable C++ 等价式，并各增加一枚本轮
bookmark；四份数据库均已原位保存。

验证结果：

- `motionplayer-dll.cpp` 用真实 Web Emscripten 参数完成语法编译；
- 同一测试 TU 加 `KRKR2_WASMTIME_HEADLESS=1` 后再次完成语法编译；
- `Web Debug Build` 完整编译并链接 `index.html`；
- `Wasmtime Headless Debug Build` 成功归档 `motionplayer`；
- 输出只有仓库既有的 `_tss`、`nodiscard` 和 Emscripten 链接警告。

当前两个 preset 都不生成可运行的 `motionplayer-dll` test executable，因此新增
case 已被两套宏配置编译覆盖，但本轮没有运行 Catch2 测试进程。
