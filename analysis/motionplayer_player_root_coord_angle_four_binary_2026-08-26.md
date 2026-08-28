# Player 根坐标与角度 #44–#50（四参考二进制，2026-08-26）

## 1. 范围

闭合表面：

- `angleDeg`、`angleRad`；
- `setCoord`；
- `x/y/left/top`。

四端 registrar 证明 `left` 与 `x` 完全共用 getter/setter callback，
`top` 与 `y` 完全共用另一对；它们不是四套薄 wrapper。

本轮还发现并修复 Player radians/degrees 常量的 bit-exact 偏差。

## 2. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getAngleDeg | `0x6BEB60` | `0x58AB30` | `0x10011408C` | `0x111ABC` |
| setAngleDeg | `0x6BE364` | `0x58A540` | `0x1001139C8` | `0x1113DC` |
| getAngleRad | `0x6CA4A0` | `0x5926F0` | `0x10011D208` | `0x11BB9C` |
| setAngleRad | `0x6CA4CC` | `0x592720` | `0x10011D250` | `0x11BBE8` |
| setCoord | `0x6CA3D8` | `0x592620` | `0x10011D060` | `0x11BA0C` |
| getX/getLeft | `0x6D6C88` | `0x59900A` | `0x1001256E8` | `0x12490C` |
| setX/setLeft | `0x6CA408` | `0x592662` | `0x10011D0BC` | `0x11BA76` |
| getY/getTop | `0x6D6C94` | `0x59901C` | `0x100125710` | `0x12493C` |
| setY/setTop | `0x6CA428` | `0x592688` | `0x10011D104` | `0x11BABC` |

Android armv7 的 Y setter 起点 `0x592688` 原被 IDA 错标成 data dword；本轮按
`LDR.W R0,[R0,#0xA0]` 到 `BX LR@0x5926AC` 的完整指令边界恢复为独立 Thumb
函数后 fresh decompile。

## 3. Player 与 synthetic-root 坐标

| 逻辑字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player directEdit | `+0x1E2` | `+0x13A` | `+0x172` | `+0x0FE` |
| Player emoteAngle | `+0x1D0` | `+0x128` | `+0x160` | `+0x0EC` |
| root delta.dirty | node `+0x630` | node `+0x540` | node `+0x640` | node `+0x520` |
| root delta.posX | node `+0x638` | node `+0x548` | node `+0x648` | node `+0x528` |
| root delta.posY | node `+0x640` | node `+0x550` | node `+0x650` | node `+0x530` |
| root delta.angle | node `+0x650` | node `+0x560` | node `+0x660` | node `+0x540` |

Android 两端的 deque block capacity 为 1，因此 root address 被简化为直接 block
pointer；iOS 两端保留 libc++ segmented-deque 公式。四端都直接取 index 0，没有
size/null guard；该安全性依赖 Player 构造器已成功 append synthetic root。

## 4. x/y 与 setCoord

共同伪代码：

```cpp
double getX() const { return root.delta.posX; }
double getY() const { return root.delta.posY; }

void setX(double v) {
    if (root.delta.posX != v) {
        root.delta.posX = v;
        root.delta.dirty = true;
    }
}

void setY(double v) {
    if (root.delta.posY != v) {
        root.delta.posY = v;
        root.delta.dirty = true;
    }
}

void setCoord(double x, double y) {
    if (root.delta.posX != x || root.delta.posY != y) {
        root.delta.posX = x;
        root.delta.posY = y;
        root.delta.dirty = true;
    }
}
```

边界：

- 比较使用普通 IEEE `!=`；
- 相等的 +0/-0 不更新，也不 dirty；
- NaN 与任何值（包括自身）不等，因此触发写和 dirty；
- 单轴 setter 先写值、后写 dirty；
- `setCoord` 短路先比较 X，再比较 Y；任一不同就同时写两轴，最后 dirty。
  因而当只有 Y 不同时，传入 X 的 signed-zero bit pattern 仍会覆盖旧 X；
- left/top 是 callback 级 alias，不增加边界。

## 5. angleDeg

getter：

```cpp
return directEdit ? emoteAngle : root.delta.angle;
```

setter：

```cpp
if (directEdit) {
    while (degrees < 0.0)   degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    emoteAngle = degrees;
    initEmoteMotion(2);
} else if (root.delta.angle != degrees) {
    root.delta.dirty = true;          // 角度路径是 dirty-before-value
    root.delta.angle = degrees;
}
```

精确边界：

- direct-edit 使用重复加减循环，不是 `fmod`；
- NaN 跳过两循环，写 NaN 并调用 `initEmoteMotion(2)`；
- +∞ 在第二循环中不收敛，-∞ 在第一循环中不收敛；
- -0 跳过两循环并保留 sign bit；
- ordinary 模式不归一化；+0/-0 相等时保留旧 bit pattern；
- ordinary 模式 NaN 每次都 dirty；
- ordinary angle 的写序与 x/y 相反：先 dirty、后 angle。

四端 direct-edit 末端共同调用：
`0x6B0270 / 0x5807E0 / 0x100107D38 / 0x105350`，参数固定为 2。

## 6. angleRad 与精确常量

四端 getter constant 的 binary64 bit pattern 完全相同：

```text
degrees -> radians: 0x3F91DF46A2529E00
hex-float:           0x1.1DF46A2529E00p-6
source decimal:      0.0174532925
```

setter constant：

```text
radians -> degrees: 0x404CA5DC1A63C000
hex-float:           0x1.CA5DC1A63C000p+5
source decimal:      57.2957795
```

`getAngleRad` 选择与 degree getter 同一源字段后乘第一个常量；
`setAngleRad` 先乘第二个常量，再进入完整 degree setter。Android arm64 内联后者，
其余三端保留 tail-call，语义相同。

本地原先使用 full-precision π/180 与 180/π：

- `0x3F91DF46A2529D39`
- `0x404CA5DC1A63C1F8`

这会让脚本结果在低位不同。本轮已改为参考短十进制常量的精确 hex-float，并新增
bit-exact 单元用例，分别用 `setAngleDeg(1)` 和 `setAngleRad(1)` 锁定两个方向。

## 7. 状态

本纵切面已 `IMPLEMENTED`；四个 IDB 已统一命名、签名、注释并保存。现有机器缺少
正式 CMake/Ninja/Emscripten 工具链，因此新单测尚未实际执行，不能升级为
`VERIFIED`。

