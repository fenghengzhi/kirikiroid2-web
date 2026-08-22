# MotionPlayer private-GLL clip compare/select 边界四端复原（2026-08-16）

## 结论

四份当前 `reference/binaries/` 的 private-GLL builder 都用有序浮点比较加条件选择构造
clip，而不是调用具有“等值返回第一参数”约定的 `std::min/std::max`。右、下初始边界的
精确源码级次序为：

```cpp
right  = paintRight  < float(width)  ? paintRight  : float(width);
bottom = paintBottom < float(height) ? paintBottom : float(height);
```

因此 `paintRight` 或 `paintBottom` 为 NaN 时，ordered `<` 为 false，原生选择有限的 canvas
边界。旧端口写成 `std::min(paint, canvas)`；其比较方向实际是 `canvas < paint`，unordered
时会返回第一个参数 `paint`，从而错误地把 NaN 发布进 `clipRect`。

viewport 交集也保留操作数身份：

```cpp
left   = viewportLeft < left   ? left   : viewportLeft;
top    = viewportTop  < top    ? top    : viewportTop;
right  = right < viewportRight ? right  : viewportRight;
bottom = bottom < viewportBottom ? bottom : viewportBottom;
```

viewport 的 ordered 有效性门使 NaN 不进入这一段，但相等的 `+0.0/-0.0` 仍可区分。原生
相等时选择 viewport 操作数；旧 `std::max(current, viewport)` / `std::min(current,
viewport)` 相等时保留第一个 current 操作数，因而会丢失 viewport 的负零符号。

## 四端原始指令

| 平台 | clip 构造与 viewport 交集 | 最终正面积门 |
|---|---:|---:|
| Android arm64 | `0x6DC400..0x6DC46C` | `0x6DC470..0x6DC47C` |
| Android armv7 | `0x59D0F2..0x59D210` | `0x59D214..0x59D226` |
| iOS arm64 | `0x10012B9E8..0x10012BA60` | `0x10012BA64..0x10012BA6C` |
| iOS armv7 | `0x12A53C..0x12A66E` | `0x12A672..0x12A684` |

两份 AArch64 目标最直接：

- Android 在 `0x6DC410/0x6DC418` 比较并选择 right，在
  `0x6DC41C/0x6DC420` 比较并选择 bottom；
- iOS 在 `0x10012B9F8/0x10012B9FC` 与
  `0x10012BA00/0x10012BA04` 执行同样的 `FCMP` + `FCSEL ..., MI`；
- unordered 的 FP flags 不满足 `MI`，故都选择 canvas 操作数；相等同样不满足 `MI`。

Android armv7 的 `0x59D0FE..0x59D154` 和 iOS armv7 的
`0x12A548..0x12A59E` 先把默认地址指向栈上的 canvas 临时值，仅在 `MI` 时改指向 item 的
paint 字段，再加载所选 float。虽然代码形态不同，unordered/相等时仍选择第二操作数。

viewport 段在 AArch64 使用 `FRINTM/FRINTP` 后的 `FCMP/FCSEL`；armv7 则以条件 move
恢复同一选择方向。最终门独立地拒绝 equality/reversed edges：AArch64 Android 用两次
`B.PL`，iOS arm64 用 `FCCMP` 后 `B.PL`，32 位两端的组合条件与之等价。NaN 初始 paint
right/bottom 已在前一步被 canvas 替换，因此不会依赖最终门“unordered 不拒绝”来偶然通过。

## 与 accurate SLA 的关系

accurate-SLA helper 已经使用上述显式三目表达式；偏差只存在于复用
`computeD3DClip_guess` 的 private-GLL/D3D 预处理路径。两族仍保留各自最终门：accurate
SLA 只拒绝 strict reversed edge，private-GLL/D3D 则拒绝 equality 和 reversed edge。
本轮没有把两者错误合并成一个通用 validity helper。

## 源码与回归

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp` 已把 `computeD3DClip_guess` 的初始
right/bottom 和四个 viewport 交集全部改成显式 ordered compare/select，避免库算法的参数
返回身份改变 NaN 与 signed-zero 结果。生产 helper 本身保持 TU-local；
`PlayerRenderInternal.h` 只新增一个未注册到脚本的窄测试转发入口。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增回归覆盖：

1. 四个 paint 边均为 NaN 时，left/top 经 numeric `fmax` 成为 `+0`，right/bottom 选择
   canvas 的 `8/20`，clip 有效；
2. 有效 viewport 的 left/top 为 `-0.0` 且与当前 `+0.0` 相等时，输出保留两枚负零，
   同时右/下保持正值并通过正面积门。

四份 recovery IDB 已在 clip block、初始 upper-bound 选择、viewport compare/select 与最终
gate 处补充注释并加入本纵切 bookmark；保存后仍以 `out/ida-recovery/` 下四份数据库为准。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元语法检查：通过；
- 完整 `Web Debug Build`：成功重编 motionplayer 并链接最终 Web/Wasm 产物；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：成功重编并链接静态库；
- 作用域内 `git diff --check`：通过。

诊断仅有仓库既有 `_tss` literal-operator、pthread memory-growth、JSPI 和 Emscripten JS
library warning。本组 Catch2 用例已完整编译；当前预设不生成可直接执行的
`motionplayer-dll` 测试程序，因此不把 syntax-only 声称为运行时执行。
