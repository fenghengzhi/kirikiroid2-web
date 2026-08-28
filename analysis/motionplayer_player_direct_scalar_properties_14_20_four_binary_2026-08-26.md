# Player 直接 scalar 属性 #14–#20（四参考二进制，2026-08-26）

## 1. 范围与结论

本纵切面闭合 Player 表面第 14–20 行的 14 个 native leaf callback：

`completionType`、`preview`、`priorDraw`、`outsideFactor`、
`meshDivisionRatio`、`speed`、`syncActive` 的 getter/setter。

四端共同结论是：七对 callback 都只访问一个 Player 内字段，没有验证、clamp、
归一化循环、dirty 标记、节点遍历、资源调用或其他副作用。本地实现逐项一致，
本轮不修改运行 C++。

## 2. callback 与字段坐标

| 属性 | 角色 | Android arm64 | 字段偏移 | Android armv7 | 字段偏移 | iOS arm64 | 字段偏移 | iOS armv7 | 字段偏移 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| completionType | get/set | `0x6D6A04 / 0x6D6A0C` | `+0x478` | `0x598E5E / 0x598E64` | `+0x320` | `0x100125550 / 0x100125558` | `+0x408` | `0x124756 / 0x12475C` | `+0x2DC` |
| preview | get/set | `0x6D6A14 / 0x6D6A1C` | `+0x444` | `0x598E6A / 0x598E70` | `+0x2E8` | `0x100125560 / 0x100125568` | `+0x3D4` | `0x124762 / 0x124768` | `+0x2A8` |
| priorDraw | get/set | `0x6D6A28 / 0x6D6A30` | `+0x448` | `0x598E76 / 0x598E7C` | `+0x2EC` | `0x100125570 / 0x100125578` | `+0x3D8` | `0x12476E / 0x124774` | `+0x2AC` |
| outsideFactor | get/set | `0x6D6A3C / 0x6D6A44` | `+0x488` | `0x598E82 / 0x598E8C` | `+0x330` | `0x100125580 / 0x100125588` | `+0x418` | `0x12477A / 0x124784` | `+0x2EC` |
| meshDivisionRatio | get/set | `0x6D6A4C / 0x6D6A54` | `+0x498` | `0x598E96 / 0x598EA0` | `+0x340` | `0x100125590 / 0x100125598` | `+0x428` | `0x12478E / 0x124798` | `+0x2FC` |
| speed | get/set | `0x6D6A5C / 0x6D6A64` | `+0x490` | `0x598EAA / 0x598EB4` | `+0x338` | `0x1001255A0 / 0x1001255A8` | `+0x420` | `0x1247A2 / 0x1247AC` | `+0x2F4` |
| syncActive | get/set | `0x6D6A6C / 0x6D6A74` | `+0x445` | `0x598EBE / 0x598EC4` | `+0x2E9` | `0x1001255B0 / 0x1001255B8` | `+0x3D5` | `0x1247B6 / 0x1247BC` | `+0x2A9` |

Android arm64 registrar 的成对 callback 加载顺序是 setter/getter；三个 compact
registrar 的 descriptor 参数顺序是 getter/setter。表中角色由 leaf body 的 load/store
方向重新确认，不能从 ARM64 地址或加载顺序猜测。

四库已把 56 个函数统一命名为 `Player_get*/Player_set*_guess`，并应用准确的
`int/bool/double` getter 与 `void` setter 签名后保存。

## 3. 共同伪代码

```cpp
int Player::getCompletionType() const { return completionType; }
void Player::setCompletionType(int v) { completionType = v; }

bool Player::getPreview() const { return preview; }
void Player::setPreview(bool v) { preview = v; }

bool Player::getPriorDraw() const { return priorDraw; }
void Player::setPriorDraw(bool v) { priorDraw = v; }

double Player::getOutsideFactor() const { return outsideFactor; }
void Player::setOutsideFactor(double v) { outsideFactor = v; }

double Player::getMeshDivisionRatio() const { return meshDivisionRatio; }
void Player::setMeshDivisionRatio(double v) { meshDivisionRatio = v; }

double Player::getSpeed() const { return speed; }
void Player::setSpeed(double v) { speed = v; }

bool Player::getSyncActive() const { return syncActive; }
void Player::setSyncActive(bool v) { syncActive = v; }
```

## 4. 精确边界

- `completionType` 是未验证的有符号 32 位值。AArch64 leaf 使用 W 寄存器返回时
  Hex-Rays 一度显示 `unsigned int`，这是 AArch64 子寄存器零扩展表现；descriptor
  实例和 32 位实现共同证明脚本类型仍是 signed Int32。
- 三个 Boolean setter 不调用其他函数。Android arm64 显式保存 `value & 1`；
  另外三端保存低字节。它们接收的是已经由 typed NCB descriptor 转换的 C++ `bool`，
  因而脚本可见值只有 false/true。
- 四个 double 对所有 IEEE-754 bit pattern 都原样 load/store：NaN、正负无穷、
  signed zero 和任意有限值均不触发检查。
- `priorDraw` 只访问 Player 自己的字节，不访问 MotionNode 的同名/相近状态。
- `meshDivisionRatio` 不更新 EmoteEngine 的独立元数据/控制值；EmotePlayer 与
  D3DEmotePlayer 的公开属性只是转发到这个 Player 字段。
- `speed` 不修改 `syncActive`；二者是相邻但独立的控制量。

构造器默认值在四端保持一致：
`completionType=0`、`preview=false`、`priorDraw=false`、
`outsideFactor=1.5`、`meshDivisionRatio=1.0`、`speed=1.0`；
`syncActive` 从进程级 `defaultSyncActive` 字节复制一次，而不是硬编码常量。

## 5. 本地逐项对照

对应本地代码位于 `cpp/plugins/motionplayer/Player.h` 与
`cpp/plugins/motionplayer/PlayerCore.cpp`：

- 七对 getter/setter 都是单字段访问；
- `completionType` 使用 `tjs_int`；
- Boolean 与 double 类型匹配；
- `meshDivisionRatio` 的 out-of-line 实现没有额外逻辑；
- 字段默认初始化与四端构造器一致。

因此本纵切面状态为 `EVIDENCED_4_4`，本地实现已经逐行一致；正式
CMake/unit/Web build 仍因当前机器没有 CMake、Ninja、Emscripten 而不可运行。

