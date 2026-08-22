# MotionSub/type-6 共享 position derivative 双采样选择（四参考二进制，2026-08-16）

## 结论与旧文更正

新鲜复核 `Player_updateMotionSubNodes_guess` 的 angle-mode-3 双调用点后，旧文档与旧 helper
的 `if(next >= 1) + fmin(next,1)` 组合被四端原始指令推翻。MotionSub 和 type-6 emitter
实际使用相同的两级选择：

```cpp
candidate = ratio + 0.0001;
first = candidate < 1.0 ? ratio : 0.9999;
second = std::min(candidate, 1.0);
```

四端 first 对 NaN 都取 `0.9999`。second 在两个 ARM64 传播 NaN、两个 ARMv7 取
`1.0`。旧 MotionSub helper 的 first=NaN/second=1 组合没有匹配四个参考中的任何一个。

## MotionSub 新鲜指令表

| 目标 | 主函数 | ratio/candidate | first 选择 | second 选择 | 两次 position call |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x6BB4A0` | `0x6BBA84..0x6BBAB8` | `0x6BBAC0..0x6BBACC` | `0x6BBAE4` | `0x6BBAE8` / `0x6BBB08` |
| Android ARMv7 | `0x587E00` | `0x588318..0x58832E` | `0x588332..0x588340` | `0x58834A..0x588360` | `0x588346` / `0x58836C` |
| iOS ARM64 | `0x100110EEC` | `0x100111098..0x1001110CC` | `0x1001110D0..0x1001110DC` | `0x1001110E0` | `0x100111108` / `0x100111124` |
| iOS ARMv7 | `0x10E68C` | `0x10EDD4..0x10EE1A` | `0x10EE26..0x10EE30` | `0x10EE38..0x10EE52` | `0x10EE34` / `0x10EE60` |

## compare/select 身份

两个 ARM64 都先比较 candidate 与 `1.0`，再以 `FCSEL ...,LT` 选择 ratio，否则选择
`0.9999`。两个 ARMv7 先装入 `0.9999`，仅以 `VMOVLT` 覆盖 ratio。unordered 不满足 LT，
所以四端 first 都落到 endpoint fallback。

second 在 Android/iOS ARM64 分别由 `FMIN D8,D2,D11` 与 `FMIN D10,D1,D14` 形成；两条
都不是 `FMINNM`，NaN 必须传播。ARMv7 先让结果保持 `1.0`，只在 ordered LT 时复制
candidate，NaN 因而得到 `1.0`。

这与 type-6 emitter 的 `FCSEL/FMIN` 和 `VMOVLT` 两套序列逐目标相同。两个消费者的
position curve、目标/源坐标和最终用途不同，但采样时间生成可以安全共享同一个 source-shaped
helper。

## 数据流与异常边界

MotionSub 的 first/second 会依次进入 position interpolation。first 由 NaN 更正为
`0.9999` 后，第一调用不再必然把 NaN 送入 easing/control-curve getter；第二调用仍按目标
分化。这会改变脚本 getter 序列、异常位置、atan2 输入和最终 child angle，不能视为只影响
诊断输出。

两次 interpolation 之间没有事务：第一调用的脚本副作用在第二调用失败时保留；angle 只在
两次调用和 coordinate-mode 差分成功后提交。分母仍没有零/finite guard。

## 源码与回归

- `PlayerUpdateLayersInternal.h`：删除错误的 MotionSub 专用 `>= + fmin` helper，保留共享
  `positionDerivativeSampleTimes_guess`；
- `PlayerUpdateChildMotion.cpp` 与 `PlayerUpdateParticles.cpp`：都把返回的 first/second 直接
  交给各自两次 position interpolation；
- 单元测试把 MotionSub NaN 期望改为 first=`0.9999`、Web/source second=NaN，并保留
  near-end、正负无穷边界；
- 四份 recovery IDB 在 MotionSub first/second 选择和双调用处补充注释、bookmark，并给
  type-6 helper追加“与 MotionSub 同型”的更正。

## 验证

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种宏环境下，完整
  `motionplayer-dll.cpp` 均以真实 Emscripten response file 通过 `-fsyntax-only`；
- `Web Debug Build` 重编 5 个共享-header 消费单元，成功归档 motionplayer 并链接最终
  `index.html`；
- `Wasmtime Headless Debug Build --target motionplayer` 重编对应生产路径并成功归档；
- 旧 `motionSubNodeDerivativeSampleTimes_guess` 与 emitter-only helper 名均已从源码/测试清除；
- 输出只有仓库既有 `_tss` 与 Emscripten 链接警告；当前预设没有该 Catch 翻译单元的可
  运行 executable，故不把语法检查描述成执行测试。
