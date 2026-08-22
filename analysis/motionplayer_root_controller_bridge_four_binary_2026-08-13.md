# MotionPlayer 根控制器桥四参考二进制复原（2026-08-13）

## 结论

本轮淘汰了 `EmoteEngine.cpp` 中仍以旧 `libkrkr2.so 0x6766E0` 为权威的
注释。四个当前参考二进制的共同 helper 均按以下顺序执行，而且只存在两个
调用点：顶层状态保存的零步长刷新，以及每个 progress slice 的根控制器刷新。

```text
position.step(dt) -> Player root coord(x, y)
color.step(dt)    -> pack four float channels -> Player color-weight sink
scale.step(dt)    -> ratioDup = 1 / (ratio * scale)
                  -> Player root zoom(scale, scale)
angle.step(dt)    -> Player setAngleRad(radians)
```

旧注释把后两条分别认成了 `setSlant` 和 `setAngleDeg`。这不是名称差异：前者
会写错根节点字段，后者会漏掉 radians-to-degrees 转换，二者都改变实际画面。

## 地址与调用点

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `EmoteEngine_stepRootControllers_guess` | `0x673AC0` | `0x55BFD4` | `0x1001AFD50` | `0x1AF4A4` |
| 状态保存调用点 | `0x673460` | `0x55BBC2` | `0x1001AF7DC` | `0x1AEF8E` |
| progress 调用点 | `0x67A760` | `0x55FF6C` | `0x1001B4398` | `0x1B3E96` |
| color sink | `0x6CAB04` | `0x59290C` | `0x10011D4F8` | `0x11BEAC` |
| root zoom sink | `0x6BE334` | `0x58A4FE` | `0x10011396C` | `0x111372` |
| angle-radian sink | `0x6CA4CC` | `0x592720` | `0x10011D250` | `0x11BBE8` |

## Engine 与 controller 布局

helper 不检查 Player 或四个 controller 指针；构造器建立这些对象作为生命期
不变量。字段偏移因 ABI 不同，但角色和相对次序一致：

| 字段 | Android A64/A32 | iOS A64/A32 |
|---|---:|---:|
| Player | `+1064 / +532` | `+696 / +348` |
| position controller | `+1072 / +536` | `+704 / +352` |
| scale controller | `+1080 / +540` | `+712 / +356` |
| color controller | `+1088 / +544` | `+720 / +360` |
| angle controller | `+1096 / +548` | `+728 / +364` |
| base mesh ratio | `+1168 / +600` | `+800 / +412` |
| derived reciprocal | `+1176 / +608` | `+808 / +420` |

每次 helper 都复用同一个至少四 float 的栈区。每个 controller step 会按自身
count 写满输出：position 两项、color 四项、scale 一项、angle 一项；调用者
不预置默认值，也不在 step 后检查 controller 状态。

## 精确 sink 行为

### Position

两个 float 先扩为 double，随后进入 combined root-coordinate setter。setter
比较任一轴，发生变化时写根 DeltaState 的 x/y 并置 dirty。该行为已有独立
四库报告，本轮只确认桥接调用顺序。

### Color

AArch64 对每个 float 使用 signed truncate-to-zero，再通过 8-bit bitfield
插入；ARMv7 使用 float-to-unsigned 指令后按低八位打包。对正常 0..255 控制器
域，四端共同结果为：

```text
packed = byte0 | byte1<<8 | byte2<<16 | byte3<<24
stored = (packed & 0xFF00FF00) | byte2 | byte0<<16
```

因此 `{17,34,51,68}` 先形成 `0x44332211`，Player 内部保存
`0x44112233`；脚本 getter 再交换一次，返回 `0x44332211`。color sink 是直接
字段写，没有 equality/dirty 分支。

ARM32 与 AArch64 对负数、NaN、无穷以及超出可表示范围的浮点转整数指令边界
并不完全相同；源码当前用 C++ `int` 再截低八位表达正常输入域。不能据此声称
这些异常值已做到跨 ABI 单一语义。

### Scale

scale float 扩为 double；native 不做零、finite 或 sign guard：

```text
derived = 1.0 / (baseRatio * scale)
```

然后同一个 double 同时传给 root zoom 的 x/y。zoom setter 比较两个根
DeltaState scale 字段；任一不同则置 dirty 并写两轴。它不读写 slant。

### Angle

`EmoteAngleController_step` 输出 radians。四端都调用 radian sink；该 sink 用
共同 binary64 常数约 `57.2957795` 乘成 degrees，再进入 degree setter。
Android A64 将 degree setter 内联，其余三个保留 tail call，但 direct-edit
归一化与普通 root DeltaState dirty-before-store 语义相同。

## 源码修正与验证点

- helper 重命名为 `stepRootControllers_guess`；
- 删除旧 `libkrkr2.so` 地址和错误 sink 注释；
- 删除四个 constructor-owned controller 的本地 null guard；
- scale sink 从 `setSlant` 改为 `setZoom`；
- angle sink 从 `setAngleDeg` 改为 `setAngleRad`；
- 新测试同时锁定 coord、zoom、slant 不变、90-degree 转换、reciprocal、颜色
  R/B 交换和 dirty。

## 2026-08-13 验证结果

- Web Debug Build：通过；
- Wasmtime Headless Debug Build：通过；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 完整翻译单元
  `-fsyntax-only`：通过，仅保留项目既有 `_tss` literal-operator 弃用警告；
- `git diff --check`：通过，仅报告工作树既有的 LF→CRLF 提示。

测试没有越过 `Player` 的私有布局读取 packed 字段，而是通过公开
`getColorWeight()` 锁定控制器输入 `{17,34,51,68}` 的脚本可见值
`0x44332211`。内部 `0x44112233` 表示由上述四端 color sink 反编译直接证明；
其 getter/setter 和渲染消费者将在独立颜色权重纵切中继续闭环。
