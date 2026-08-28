# Player 时间属性 #4/#5/#21/#22（四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合：

- read-only `lastTime` / `loopTime`（毫秒域别名）；
- read/write `tickCount`（毫秒域 cursor）；
- read/write `frameTickCount`（帧域 cursor）。

共 24 个 callback。Android armv7 原 IDB 中五个 callback 只是裸 label；本轮依据
完整 disasm 和 literal-pool 边界定义为独立 Thumb 函数后重新反编译，未用其他三端
替代缺失证据。

## 2. 四端 callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getLastTime | `0x6D6800` | `0x598D50` | `0x100125454` | `0x124650` |
| getLoopTime | `0x6D6828` | `0x598D88` | `0x10012547C` | `0x124684` |
| getTickCount | `0x6D6A80` | `0x598ED0` | `0x1001255C0` | `0x1247C4` |
| setTickCount | `0x6D6AA0` | `0x598F00` | `0x1001255E0` | `0x1247F0` |
| getFrameTickCount | `0x6D6AE0` | `0x598F68` | `0x100125620` | `0x12484C` |
| setFrameTickCount | `0x6BE2D4` | `0x58A490` | `0x1001138E8` | `0x1112EC` |

相关 Player 字段：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| clamped evaluation cursor | `+0x1C8` | `+0x120` | `+0x158` | `+0x0E4` |
| queuing / firstFrame bytes | `+0x1E0/+0x1E1` | `+0x138/+0x139` | `+0x170/+0x171` | `+0x0FC/+0x0FD` |
| raw frameTickCount | `+0x460` | `+0x308` | `+0x3F0` | `+0x2C4` |
| raw cached lastTime | `+0x468` | `+0x310` | `+0x3F8` | `+0x2CC` |
| raw loopTime | `+0x470` | `+0x318` | `+0x400` | `+0x2D4` |

## 3. lastTime / loopTime

共同伪代码：

```cpp
double getLastTime() const {
    double v = cachedTotalFrames;
    return v > 0.0 ? v * 1000.0 / 60.0 : v;
}

double getLoopTime() const {
    double v = loopTimeFrames;
    return v > 0.0 ? v * 1000.0 / 60.0 : v;
}
```

边界：

- 比较是 ordered `>`，不是 `>=`、`std::max` 或 absolute value；
- 只有严格正值做“先乘 1000、再除 60”；
- 负数、+0、-0、NaN 原样返回；-0 的 sign bit 不丢失；
- +∞ 进入转换仍是 +∞；
- 两个属性公开只读；raw `frameLastTime/frameLoopTime` 由另两个 leaf 直接返回同一字段。

## 4. tickCount / frameTickCount

恢复的共同源结构：

```cpp
void setFrameCursor(double cursor) {
    if (cursor < 0.0)
        cursor = 0.0;

    rawFrameTickCount = cursor;

    double evaluation = cursor;
    if (evaluation > cachedTotalFrames)
        evaluation = cachedTotalFrames;

    clampedEvaluationCursor = evaluation;
    queuing = true;
    firstFrame = true;
}

double getFrameTickCount() const {
    return rawFrameTickCount;
}

void setFrameTickCount(double frames) {
    setFrameCursor(frames);
}

double getTickCount() const {
    return rawFrameTickCount * 1000.0 / 60.0;
}

void setTickCount(double milliseconds) {
    setFrameCursor(milliseconds * 60.0 / 1000.0);
}
```

精确边界：

- 两个 getter 都无正值门控；毫秒 getter 固定先乘后除；
- 毫秒 setter 固定先乘 60 后除 1000，再进入共同 cursor transition；
- 下界只处理 ordered `cursor < 0`；源码形状不把 NaN 归零；
- raw cursor 保留超过总帧数的值；只对 evaluation cursor 做上界；
- 上界比较也是 ordered `cursor > cachedTotalFrames`：
  cursor 或 total 为 NaN 时不 cap；
- 两个 setter 都把相邻字节一次提交为 `{queuing=1, firstFrame=1}`；
- setter 不修改 cachedTotalFrames 或 loopTime；
- 这些函数没有可抛 C++ 调用，四端 store 排序的小差异不形成单线程脚本异常前沿。

## 5. AArch64 / ARMv7 浮点 lowering 差异

两端 AArch64 的下界使用标量 `FMAX Dcursor, Dcursor, +0`；两端 ARMv7 使用
`VCMPE cursor,#0`、转移 flags、仅在 MI（ordered less-than）时 move +0。
其余上界都使用 ordered greater-than selection。

这说明：

- 共同源结构由 ARMv7 的显式 `if (cursor < 0) cursor=0` 清楚保留；
- ARMv7 对 -0 的比较为 equal，因此保留原 -0；unordered NaN 不走 MI；
- AArch64 的 `FMAX` 是编译器 lowering 产生的机器级平台边界：不同符号零的选择和
  NaN quiet/default 行为按 AArch64 FPCR/指令语义执行，不能假装四个机器码逐 bit 相同；
- 本地 Web 源保留 ordered-compare 形状，与两端可直接恢复源结构的 ARMv7 一致；
  若未来要求模拟某个 AArch64 FPCR 下的 bit-exact NaN payload/signed-zero，必须作为
  平台专用 boundary layer，而不能改变公共 cursor 算法。

## 6. 本地逐项对照与验证现状

本地 `Player.h::setFrameCursorState_guess` 与共同源结构逐行一致；
`getLastTime/getLoopTime/getTickCount/getFrameTickCount` 的比较和算术顺序也一致。

现有单元用例已经覆盖：

- raw-frame 与毫秒别名分离；
- 只读属性；
- negative、-0、NaN、+∞；
- raw cursor 不受上界 cap；
- evaluation cursor 受上界 cap；
- queuing/firstFrame 联动；
- NCB Integer/Void 到 double setter 的转换。

当前机器没有 CMake/Ninja/Emscripten，无法在本轮实际执行正式单元和 Web build；
因此状态保持 `EVIDENCED_4_4`，不冒充 `VERIFIED`。

