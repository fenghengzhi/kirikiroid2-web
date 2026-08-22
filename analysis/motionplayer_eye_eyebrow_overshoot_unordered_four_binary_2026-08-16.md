# MotionPlayer Eye/Eyebrow overshoot unordered 条件链四参考闭环（2026-08-16）

## 结论

Eye/Blink 与 Eyebrow 的 state-2 ramp 都曾在 portable C++ 中写成同一个对称表达式：

```cpp
(direction > 0 && target <= next) ||
(direction < 0 && target >= next)
```

这个写法对正常 resolver 产生的 `direction == +1/-1` 与有限 target 足够，但不等价于
四份参考二进制的完整 ARM condition-code CFG。Eye 与 Eyebrow 还存在一项真实的
unordered 分型：

```cpp
// Eye/Blink
if (direction > 0)
    overshoot = target <= next;          // ordered LS
else if (!(direction >= 0))
    overshoot = !(target < next);        // negative or unordered; GE/LT complements
else
    overshoot = false;                   // ordered zero

// Eyebrow
if (!(direction <= 0))
    overshoot = !(target > next);        // positive or unordered; LS/LE complements
else if (direction >= 0)
    overshoot = false;                   // ordered zero
else
    overshoot = !(target < next);        // ordered negative; LT complement
```

结果是：

| 输入边界 | Eye/Blink | Eyebrow |
| --- | --- | --- |
| `direction=+1`, target/next unordered | 不 overshoot | overshoot |
| `direction=-1`, target/next unordered | overshoot | overshoot |
| `direction=NaN` | overshoot | overshoot |
| `direction=+0/-0` | 不 overshoot | 不 overshoot |

这不是纯理论上的不可达值。Eye 与 Eyebrow 状态字典都把 `v` 直接映射到
`trackDir`，把 `target` 映射到 `trackTarget`；restore 只执行 Variant 到 float 的
转换，没有 finite clamp。脚本状态、clone/unserialize 或损坏 snapshot 都可以把 NaN
写回 state 2。

## Eye/Blink 四端指令链

函数入口：

| 目标 | step |
| --- | ---: |
| Android ARM64 | `0x660FBC` |
| Android ARMv7 | `0x552472` |
| iOS ARM64 | `0x1001A27A0` |
| iOS ARMv7 | `0x1A19D8` |

各端的四段比较链：

| 目标 | direction 首分支 | 正向 target | direction 负向复核 | 负向 target | overshoot commit |
| --- | --- | --- | --- | --- | --- |
| Android ARM64 | `0x6611A0` / `0x6611A8 B.LE` | `0x6611B0` / `0x6611B4 B.LS` | `0x6611B8` / `0x6611BC B.GE` | `0x6611C4` / `0x6611C8 B.MI` | `0x6611CC` |
| Android ARMv7 | `0x552568` / `0x552578 BLE` | `0x55257E` / `0x552586 BLS` | `0x552588` / `0x552590 BGE` | `0x552596` / `0x55259E BMI` | `0x5525A0` |
| iOS ARM64 | `0x1001A28B8` / `0x1001A28BC B.LE` | `0x1001A28C4` / `0x1001A28C8 B.LS` | `0x1001A28CC` / `0x1001A28D0 B.GE` | `0x1001A28D8` / `0x1001A28DC B.MI` | `0x1001A28E0` / `0x1001A2950` |
| iOS ARMv7 | `0x1A1AC6` / `0x1A1AE6 BLE` | `0x1A1AEC` / `0x1A1AF4 BLS` | `0x1A1AF6` / `0x1A1AFE BGE` | `0x1A1B04` / `0x1A1B0C BMI` | `0x1A1B0E` |

首个 `LE` 把小于、相等和 unordered 都送到非正向块；其中 `GE` 只排除 ordered
非负，unordered 会继续。负向 target 的 `MI` 只排除 ordered `target < next`，所以
相等、大于和 unordered 都提交 overshoot。正向 target 的 `LS` 则不含 unordered。

Eye overshoot 写 `trackState=1` 与 `trackValue=target` 后回到同一外层循环。若 8B
secondary 与 12B primary 都为空，本次调用还会把 state 1 推到 state 0，再进入 blink
phase；不 overshoot 才写 `trackAccum += delta` 并结束 ramp 阶段。

## Eyebrow 四端指令链

函数入口：

| 目标 | step |
| --- | ---: |
| Android ARM64 | `0x6629E0` |
| Android ARMv7 | `0x553280` |
| iOS ARM64 | `0x1001A38C8` |
| iOS ARMv7 | `0x1A2C56` |

| 目标 | direction 首分支 | 正向 target | direction 负向复核 | 负向 target | overshoot commit |
| --- | --- | --- | --- | --- | --- |
| Android ARM64 | `0x662B18` / `0x662B20 B.LS` | `0x662B28` / `0x662B2C B.LE` | `0x662B30` / `0x662B34 B.GE` | `0x662B3C` / `0x662B40 B.MI` | `0x662B44` |
| Android ARMv7 | `0x55339E` / `0x5533AE BLS` | `0x5533B4` / `0x5533BC BLE` | `0x5533BE` / `0x5533C6 BGE` | `0x5533CC` / `0x5533D4 BMI` | `0x5533D6` |
| iOS ARM64 | `0x1001A3A18` / `0x1001A3A1C B.LS` | `0x1001A3A24` / `0x1001A3A28 B.LE` | `0x1001A3A2C` / `0x1001A3A30 B.GE` | `0x1001A3A38` / `0x1001A3A3C B.PL` | `0x1001A3A48` |
| iOS ARMv7 | `0x1A2D78` / `0x1A2D98 BLS` | `0x1A2D9E` / `0x1A2DA6 BLE` | `0x1A2DA8` / `0x1A2DB0 BGE` | `0x1A2DB6` / `0x1A2DBE BPL` | `0x1A2DC6` |

Eyebrow 的首个 `LS` 只把 ordered `direction <= 0` 送到负向复核；unordered 留在
正向块。随后正向 `LE` 把 unordered target/next 直接视为 overshoot。iOS 的负向
`PL` 与 Android 的“`MI` 则跳过，否则落入 commit”只是 CFG 反转，均表示
`!(target < next)`，包含 unordered。

Eyebrow 不像 Eye 那样同帧 re-entry。overshoot 会把 correction term 改为
`(target-next)*direction`，写 `trackValue=target`、`trackState=1`，再无条件提交
`trackAccum=previousAccum+correction` 并返回。

## 本地修正与回归

`EmoteBlinkController.cpp` 和 `EmoteEyebrowController.cpp` 分别增加 TU-local `_guess`
predicate helper，按上述两套 CFG 显式使用 ordered 比较及其补集。没有把两者合并成
一个公共 helper，因为正向 unordered target 的行为确实不同。

新增回归覆盖三组边界：

1. `direction=+1,target=NaN`：Eye 留在 state 2，Eyebrow 提交 state 1；
2. `direction=-1,target=NaN`：两者都 overshoot，但 Eye 同帧走到空队列 state 0，
   Eyebrow 停在 state 1；
3. `direction=NaN,target=7`：两者都发布有限 target；Eyebrow correction/accum 为 NaN。

四份 recovery IDB 的两个 step 入口、四段 compare/branch 与 commit block 都追加了
unordered 语义注释，并各增加一枚本轮 bookmark；四份数据库均已原位保存。

验证结果：

- `motionplayer-dll.cpp` 以真实 Web Emscripten 参数完成语法编译；
- 同一测试 TU 加 `KRKR2_WASMTIME_HEADLESS=1` 后再次完成语法编译；
- `Web Debug Build` 重新编译 Eye/Eyebrow、归档插件并成功链接 `index.html`；
- `Wasmtime Headless Debug Build` 成功归档 `motionplayer`；
- 除仓库既有 `_tss` 与 Emscripten 链接 warning 外，并行初始化 emsdk 时出现一次
  临时环境脚本清理竞争；四个验证命令均返回 0，不影响编译/链接产物。

当前 preset 不生成可运行的 `motionplayer-dll` test executable，因此三组新增 Catch2
section 已被两套宏配置编译，但本轮没有执行测试进程。
