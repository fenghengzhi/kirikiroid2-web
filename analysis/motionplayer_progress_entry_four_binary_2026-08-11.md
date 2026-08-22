# MotionPlayer progress 入口与零步长边界四端审计（2026-08-11）

## 结论

当前四份参考把进度推进拆成三个源码角色：

1. 一个接收 frame 单位 `double` 的 `EmoteEngine` 共享核心；
2. `Motion.EmotePlayer.progress` 把毫秒乘 `60.0 / 1000.0`，然后无条件进入核心；
3. `D3DEmotePlayer.progress` 接收 frame 单位，只在 `dt != 0.0` 时从 shell 经
   primary `EmoteObject` 取 Engine 并进入核心。

因此零步长门属于 D3D 包装器，不属于 Engine 核心。原本地实现依据旧
`libkrkr2.so` 注释把门下沉到了 `EmoteEngine::progress`，使
`Motion.EmotePlayer.progress(0)` 无法排空 `_dirty`；同时两个包装器还写入原版
不存在的 `_progress` 累加器和 `_modified` 旁路状态。本轮已纠正这些偏差。

## 四端映射

| 目标 | Emote 注册点 | Emote 毫秒包装 | Engine 共享核心 | D3D 注册点 | D3D 零门包装 | progress 字符串 |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x67CF68` | `0x67EC94` | `0x67A3F8` | `0x52FB88` | `0x530E3C` | 共享 `0x14BEE6C` |
| Android armeabi-v7a | `0x561334` | `0x561D08` | `0x55FEF0` | `0x4944CC` | `0x49501E` | 共享 `0xD76C48` |
| iOS arm64 | `0x1001B519C` | `0x1001B5C68` | `0x1001B4304` | `0x100232970` | `0x100233470` | Emote `0x1019602B0`；D3D `0x1019701EA` |
| iOS armv7 | `0x1B4E3C` | `0x1B586C` | `0x1B3E10` | `0x2315C6` | `0x2321AE` | Emote `0x1752614`；D3D `0x1762596` |

三类函数在四端统一命名并应用如下类型：

```cpp
void __fastcall EmoteEngine_progressCore_guess(void *self, double dt);
void __fastcall EmotePlayer_progress_guess(void *self, double dt);
void __fastcall D3DEmotePlayer_progress_guess(void *self, double dt);
```

Android 的共享字符串命名为 `aProgress_utf16_guess`；iOS 的独立 literal 命名为
`aEmotePlayerProgress_utf16_guess` 和
`aD3DEmotePlayerProgress_utf16_guess`。

## 两个入口的共同伪代码

四端 D3D 包装完全一致，只有指针宽度不同：

```cpp
void D3DEmotePlayer_progress(D3DEmotePlayer *shell, double frameDt) {
    if (frameDt != 0.0) {
        EmoteEngine *engine = shell->primaryObject->engine;
        EmoteEngine_progressCore(engine, frameDt);
    }
}
```

64 位对象链读取为 `*(*(shell + 24) + 8)`；32 位为
`*(*(shell + 16) + 4)`。没有 null guard，没有毫秒换算，也没有任何调用前/后的
shell 或 Engine 字段写入。

四端 `Motion.EmotePlayer` 包装也完全一致：

```cpp
void EmotePlayer_progress(EmoteEngine *engine, double milliseconds) {
    EmoteEngine_progressCore(engine, milliseconds * 60.0 / 1000.0);
}
```

它没有 `dt != 0` 分支。输入为正零、负零或换算后为零时仍进入核心。

## Engine 核心的精度与零步长语义

四端核心的相关骨架是：

```cpp
void EmoteEngine_progressCore(EmoteEngine *engine, double frameDt) {
    preProgress(engine, false, frameDt);

    double remaining = frameDt;
    while (remaining > 0.0 || engine->dirty) {
        const double slice = std::min(remaining, 1.1);
        const float controllerDt = (float)slice;
        engine->dirty = false;

        stepControllerFamilies(engine, controllerDt);
        applyDirectControllers(engine, controllerDt);
        stepWindIfEnabled(engine, controllerDt);

        remaining -= slice;
    }

    bindHM7ToPlayer(engine);
    applyClampControls(engine);
    playerProgress(engine->player, nullptr, frameDt);

    if (frameDt != 0.0 && !engine->syncWaiting)
        stepPhysicsTail(engine, (float)frameDt);
}
```

关键边界：

- 核心参数和 `remaining` 都是 `double`；每个 `std::min(remaining, 1.1)` 结果先保留为
  `double`，只把送入各控制器的副本转成 `float`，循环扣减仍用 double slice。
- `remaining` 是 `std::min` 的第一个操作数，而不是 `std::fmin` 的可交换数值参数。
  2026-08-16 fresh 四端指令复核证明：dirty 强制迭代且 `remaining=NaN` 时，两份
  AArch64 的 `FMIN` 与两份 ARMv7 的有序 `GT` 覆盖链都保留 NaN；本地旧
  `std::fmin` 会错误选择数值端 `1.1`。完整证据与边界矩阵见
  `analysis/motionplayer_progress_controller_slice_unordered_four_binary_2026-08-16.md`。
- `preProgress` 在核心入口发生。核心没有全局零门。
- `preProgress` 自己维护另一份跨 active timeline 共享的 residual；loop wrap 会消耗它，
  后续 label 不会重新取得原始 `frameDt`。它与下方 controller-slice 循环的局部
  `remaining` 不是同一个源级变量。该内部状态机的 2026-08-15 fresh 四端复核见
  `analysis/motionplayer_pre_progress_shared_residual_ordered_erase_four_binary_2026-08-15.md`。
- `frameDt == 0` 且 `dirty == true` 时仍执行一个 `controllerDt == 0` 的 slice，清掉
  dirty，然后继续 HM7 bind、clamp 和 Player bridge；只有 physics tail 被零值门挡住。
- `frameDt == 0` 且 dirty 为 false 时不执行控制器 slice，但仍执行核心的 post-loop
  阶段。
- D3D 的外层零门会跳过上述全部行为；EmotePlayer 的毫秒包装则保留它们。
- 负非零 D3D 输入通过外门；核心是否执行一个负 slice 取决于 dirty，保持原版
  `remaining > 0 || dirty` 与 `min(remaining, 1.1)` 组合的边界。

## Android arm64 IDB 函数边界修复

Android arm64 编译器把两个入口的尾跳转都指向 `0x67A3F8` 的共享核心。原 IDB
把这个大块错误地作为 `0x530E3C` / `0x67EC94` 的共享 tail chunk，导致反编译
D3D 包装时混入整个 Engine 核心，并把 core 错标为 D3D 函数。

本轮删除错误的 tail-chunk 函数归属，重新定义：

- `0x530E3C..0x530E54`：D3D 零门/对象链 thunk；
- `0x67EC94..0x67ECB0`：Emote 毫秒换算 thunk；
- `0x67A3F8..0x67A8B0`：独立 Engine 共享核心。

强制重编译后，两个 thunk 都得到短伪代码并显式调用
`EmoteEngine_progressCore_guess`。Android armv7 的 `0x561D08` 原来只有 Thumb
代码而没有函数边界，本轮也按 `0x561D08..0x561D24` 重新定义后得到完整包装伪代码。

## 本地修正与测试

- `EmoteEngine::progress` 参数由 `float` 改为 `double`；保留 double slice，仅把控制器
  和 physics 调用参数转成 float。
- 删除 Engine 核心顶层的 `dt == 0` return。
- `D3DEmotePlayer::progress` 现在独占 `dt != 0` 门，并移除 `_progress` / `_modified`
  写入。
- `EmotePlayer::progress` 只做毫秒换算并无条件调用核心，也不写旁路字段。
- 删除不属于二进制对象结构、且未在 NCB 表暴露的 `_progress` 字段及 C++ get/set。
- 新增零步长回归：构造时 dirty 的 Engine 在 `progress(0.0)` 后必须清掉 dirty。
- 既有 D3D 测试不再断言端口自造的 progress 累加器。

## 验证结果

- `cmake --build --preset "Web Debug Build"`：通过；随后增量确认返回
  `ninja: no work to do.`。
- `cmake --build --preset "Wasmtime Headless Debug Build"`：通过；随后增量确认返回
  `ninja: no work to do.`。
- `git diff --check`：通过（PowerShell/Git 仅报告仓库既有的 LF/CRLF 行尾警告）。
- 原生 Catch2：Windows 测试构建仍在首次配置依赖；完成后补记本轮定向用例与完整
  `motionplayer-dll` 结果。

## 2026-08-15 当前四参考复核

切换到 `reference/binaries/` 四份当前恢复库后重新反编译共享 core，入口与 slice loop
裁决不变：Android ARMv7、iOS ARM64 与 iOS ARMv7 都直接恢复成
`remaining > 0.0 || dirty` 的 `for`；Android ARM64 因大函数 CFG 形状显示为正 dt 入口
跳入内层、dirty 外层重入，但指令级条件和扣减顺序完全相同。

本轮删除 compiled source 中仅来自 Android ARM64 Hex-Rays 的 `goto LABEL_*` 说明，改成
四端共同的 remaining/dirty、double slice、float controller copy 与 double subtraction
语义。四份 recovery IDB 的 loop head 同步加入该平台无关注释；算法和测试无需修改。
