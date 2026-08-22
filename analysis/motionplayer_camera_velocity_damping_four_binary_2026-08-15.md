# MotionPlayer camera velocity / damping 入口段（四参考，2026-08-15）

## 结论

四份当前参考的 `Player::updateLayers` 都在其余 root/node 求值之前执行同一段 camera
积分：三个 camera velocity double 逐轴与零比较，非零轴先把 root delta 的 dirty byte
写为 1，再执行 `rootDeltaPos += frameDelta * velocity`。三轴完成后，若 camera damping
不精确等于 `1.0`，计算 `pow(damping, frameDelta / 60.0)` 并无条件乘到三个 velocity。

当前 portable 算法已经与四端一致；本轮迁移的是仍把 Android arm64 地址区间和
`Player+592` 写进源码的旧单目标注释。绝对地址和 ABI 偏移改为只保留在本文与恢复
IDB 中。

## 四端函数与字段矩阵

| 目标 | `Player_updateLayers_guess` | frame delta | damping | velocity X/Y/Z |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6B871C` | `+592` | `+600` | `+784/+792/+800` |
| Android armv7 | `0x5856E0` | `+392` | `+400` | `+512/+520/+528` |
| iOS arm64 | `0x10010E544` | `+480` | `+488` | `+672/+680/+688` |
| iOS armv7 | `0x10BE5C` | `+328` | `+336` | `+448/+456/+464` |

root 是 `MotionNode` deque 的 element zero。四端进入 camera 段前都已经直接解析 root
storage，没有 empty/size recovery branch；libstdc++ 两端保留直接 current pointer，
libc++ 两端通过 map 与 start index 算 element zero。

| 目标 | root delta dirty | root delta X/Y/Z |
| --- | ---: | ---: |
| Android arm64 | node `+1584` | `+1592/+1600/+1608` |
| Android armv7 | node `+1344` | `+1352/+1360/+1368` |
| iOS arm64 | node `+1600` | `+1608/+1616/+1624` |
| iOS armv7 | node `+1312` | `+1320/+1328/+1336` |

这些偏移差异来自 MotionNode 结构与 STL deque ABI，不表示四份源码有不同 camera
成员。portable `MotionNode::delta` 和语义字段名覆盖共同源结构，不复制 native byte
layout。

## 精确控制流

去掉寄存器分配和 deque 展开后，四端共同逻辑为：

```cpp
root = nodes[0];

if (cameraVelocityX != 0.0) {
    root.delta.dirty = true;
    root.delta.posX += frameDelta * cameraVelocityX;
}
if (cameraVelocityY != 0.0) {
    root.delta.dirty = true;
    root.delta.posY += frameDelta * cameraVelocityY;
}
if (cameraVelocityZ != 0.0) {
    root.delta.dirty = true;
    root.delta.posZ += frameDelta * cameraVelocityZ;
}

if (cameraDamping != 1.0) {
    double factor = pow(cameraDamping, frameDelta / 60.0);
    cameraVelocityX *= factor;
    cameraVelocityY *= factor;
    cameraVelocityZ *= factor;
}
```

顺序是 X、Y、Z 积分，随后一次 damping factor 和 X、Y、Z velocity 写回。每个 active
轴的 dirty store 都在对应 multiply/add 之前；不存在“先算三个值，最后统一提交”的
事务边界。damping 使用同一个 factor 乘三个分量，且发生在本帧 root 积分之后，因此
本帧位移使用衰减前速度，衰减结果供后续帧使用。

## 浮点边界

三个 velocity gate 都是浮点 compare 后只跳过 equal：

- `+0.0` 和 `-0.0` 均等于零，既不置 dirty，也不改变对应 root delta position；
- NaN 与零 unordered，不走 equal 分支，因此会先置 dirty，再把 NaN 传播到该轴位置；
- 非零 velocity 即使 `frameDelta == 0`，也仍会置 dirty，然后加上乘积；
- infinity、subnormal 和普通有限值没有额外 finite/range gate。

damping gate 同样只跳过与 `1.0` 精确相等的值：

- 没有 `damping > 0` 或 `frameDelta > 0` 条件；
- NaN damping 会调用外部 libm `pow`，其结果再乘到全部 velocity；
- 零/负 base、负/无穷/NaN exponent 的具体 `pow` 数值和异常标志由各目标 libm
  所有，plugin 本体不做归一化或错误处理；
- factor 即使为 1、0、NaN 或 infinity，三个 velocity store 仍全部执行；
- damping 精确为 1 时不调用 `pow`，也不重写 velocity bit pattern。

除数是 binary64 常量 `60.0`，顺序是先做 double division，再以结果调用 double
`pow`；不能改写成 float、预乘近似倒数或对三个轴分别调用 `pow`。

## 生命周期与后续数据流

这四个 double 是 Player 的持久状态。普通 Player 构造将 velocity 初始化为零、damping
初始化为一；type-4 particle child 的 spawn/update 路径会写入 velocity 和 acceleration
ratio（作为 damping）。`updateLayers` 本帧把当前 velocity 投影到 root delta，root delta
稍后复制到 evaluated transform，再对 velocity 做持久衰减。字段不属于 EmoteEngine，
也不是临时 per-node scratch。

root dirty 会参与同帧后续 root/node 重建；camera-constraint dirty 则是另一枚跨帧 Player
byte，两者不能合并。空 node deque 仍是原版无效状态，camera 段不会友好返回。

## V247 布局补证（2026-08-18）

本报告最初闭合了 camera integration/damping 算法和字段偏移，但尚未恢复它们在共同源码声明中的
相邻关系。V247 重新检查四端 constructor、destructor、updateLayers 与 particle child writer 后确认：

- `_cameraDamping` 直接位于 frame delta 后；
- damping 后是 `noUpdateYet/reverseSeek/cameraConstraintDirty/drawAffineNonIdentity` 四 byte；
- 三个 velocity 并不与 damping 相邻，而是直接位于 pending stealth motion/chara 后；
- velocity triple 后直接开始 draw-affine six-scalar block，再接 particle outside rect；
- V246 所称 draw-affine 前“未知 24-byte POD”就是这三个持久 velocity double。

因此算法表中的四个 double 偏移仍然正确，但不能据此把它们声明成连续四 double。完整字段顺序、
particle writer 和最终 IDB/build 基线见
`analysis/motionplayer_player_camera_velocity_affine_layout_four_binary_2026-08-18.md`。

## IDB 命名边界

`Player_updateLayers_guess` 是 stripped 产物上的恢复语义名，保留 `_guess`。本轮在四份
恢复 IDB 的首个 velocity load 和 damping compare 处记录了逐轴积分、dirty 提交、
`pow(damping, frameDelta/60)` 与浮点 gate；没有为内联 block 伪造独立 native 函数。
