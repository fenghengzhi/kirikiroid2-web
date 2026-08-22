# type-6 emitter crossfade 双采样的 unordered 边界（四参考二进制，2026-08-16）

## 结论

`Player_updateEmitterCrossfadeDelta_guess` 用两个相邻时间调用 position interpolator。当前
源码曾把第一时间写成“先保留 ratio，仅当 `candidate >= 1` 时覆盖为 `0.9999`”。这对普通
数值成立，但与四个参考的 unordered 路由不等价：参考实现实际使用 ordered `<` 选择，
NaN 必须进入 `0.9999` fallback。

恢复后的共享源码形状为：

```cpp
candidate = ratio + 0.0001;
first = candidate < 1.0 ? ratio : 0.9999;
second = std::min(candidate, 1.0);
```

第二选择在四端产生真实后端差异：两个 ARM64 传播 NaN，两个 ARMv7 选择 `1.0`。

## 函数地图

| 目标 | helper | ratio/candidate | 第一选择 | 第二选择 |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x6BE920` | `0x6BE988..0x6BE998` | `0x6BE9A8..0x6BE9B0` | `0x6BE9C8` |
| Android ARMv7 | `0x58A978` | `0x58A9EA..0x58A9F6` | `0x58A9FA..0x58AA08` | `0x58AA10..0x58AA22` |
| iOS ARM64 | `0x100113ECC` | `0x100113F10..0x100113F1C` | `0x100113F24..0x100113F30` | `0x100113F34` |
| iOS ARMv7 | `0x1118B0` | `0x11190A..0x11191A` | `0x111924..0x11194A` | `0x111952..0x111964` |

## 第一采样：四端共同 ordered `<`

两个 ARM64 都执行 `FCMP candidate,1.0`，随后用 `FCSEL ...,LT` 在 ratio 和字面量
`0.9999` 之间选择。两个 ARMv7 同样 `VCMPE candidate,1.0`，结果预置为 `0.9999`，
只有 `VMOVLT` 才写 ratio。因此共同真值表为：

| candidate | first |
|---|---|
| ordered `< 1.0` | 原 ratio |
| `== 1.0` 或 `> 1.0` | `0.9999` |
| NaN / unordered | `0.9999` |

旧写法 `if(candidate >= 1.0) first=0.9999` 的 NaN 比较为 false，会错误保留 NaN ratio；
这不是反编译器排版差异，而是可传入 position curve、继而改变脚本 getter/异常与最终 offset
的真实数据流偏差。

## 第二采样：ARM64/ARMv7 分化

Android/iOS ARM64 分别在 `0x6BE9C8`、`0x100113F34` 执行 `FMIN`。指令不是
`FMINNM`，所以 candidate 为 NaN 时 second 仍为 NaN。

两个 ARMv7 都先让结果寄存器保持 `1.0`，比较 `candidate` 与 `1.0`，只在 ordered LT 时
用 `VMOVLT` 复制 candidate。unordered 时条件不成立，second 因而为 `1.0`。普通有限值、
正负无穷以及零的结果四端一致；分化只需以 candidate=NaN 即可观察。

本地 Web/Wasm 使用 `std::min(candidate,1.0)` 保存共享源码操作数顺序与 ARM64 的 unordered
结果；ARMv7 的编译产物分化明确保存在分析和 IDB 中，不用一个 Web 断言伪装成四端一致。

## 2026-08-16 后续更正：与 MotionSub 同型

紧随本纵切面的新鲜四端复核推翻了旧 MotionSub 文档中的 `fmin` 结论。MotionSub
angle-mode-3 与本 helper 的两级选择逐端同型：first 的 ordered `<`、ARM64 的 `FMIN`、
ARMv7 的预置 `1.0` + `VMOVLT` 全部一致。本地因此使用共享的
`positionDerivativeSampleTimes_guess`，而不是保留两个会再次漂移的近似 helper。完整
交叉函数证据见
`motionplayer_shared_position_derivative_sample_select_four_binary_2026-08-16.md`。

## 源码、测试与 IDB

- `PlayerUpdateLayersInternal.h` 新增 production/test 共用的
  `positionDerivativeSampleTimes_guess`，把两次选择放在一个窄 helper 中；
- `PlayerUpdateParticles.cpp` 的真实 emitter helper 使用该返回 pair 调用两次 position
  interpolator；
- Catch 回归锁定 NaN 时 first=`0.9999`，并记录 Web/shared-source 的 second=NaN；另覆盖
  正负无穷；
- 四份 recovery IDB 在函数入口、第一选择和第二选择处补充语义注释及统一 bookmark。

## 验证

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种宏环境下，完整
  `motionplayer-dll.cpp` 均以真实 Emscripten response file 通过 `-fsyntax-only`；
- `Web Debug Build` 重编受共享 header 影响的 5 个 update 单元，成功归档
  `libmotionplayer.a` 并链接最终 `index.html`；
- `Wasmtime Headless Debug Build --target motionplayer` 重编相同生产路径并成功归档；
- 输出只有仓库既有 `_tss` 与 Emscripten 链接警告；
- 当前预设不生成可直接运行该 Catch 翻译单元的 executable，因此只声明真实编译/链接，
  不把 `-fsyntax-only` 冒充为运行时执行。
