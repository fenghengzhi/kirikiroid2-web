# MotionPlayer Engine progress controller-slice 无序选择四端审计（2026-08-16）

## 1. 结论

`EmoteEngine::progress` 的 controller-slice 上限不是 C/C++ 数学函数
`fmin(remaining, 1.1)`。四份当前参考共同支持的源码形状是：

```cpp
while (remaining > 0.0 || dirty) {
    const double slice = std::min(remaining, 1.1);
    const float controllerDt = static_cast<float>(slice);
    dirty = false;
    stepControllerFamilies(controllerDt);
    remaining -= slice;
}
```

这里的操作数身份不可交换：`remaining` 是保留的第一个操作数，`1.1` 是仅在有序
`remaining > 1.1` 时覆盖它的 cap。若 `remaining` 为 NaN 且 dirty byte 强制进入一次
循环，四端都会把 NaN 传给控制器；本地旧 `std::fmin` 却会选择唯一数值参数 `1.1`。

## 2. 当前四参考映射

| 目标 | Engine core | loop gate / dirty fallback | slice select | exact double subtract |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x67A3F8` | `0x67A464..0x67A480` | `0x67A490` | `0x67A470` |
| Android armeabi-v7a | `0x55FEF0` | `0x55FF86..0x55FF96` | `0x55FF1A..0x55FF2A` | `0x55FF82` |
| iOS arm64 | `0x1001B4304` | `0x1001B43B8..0x1001B43C4` | `0x1001B4340` | `0x1001B43B4` |
| iOS armv7 | `0x1B3E10` | `0x1B3EB0..0x1B3EC0` | `0x1B3E44..0x1B3E54` | `0x1B3EAC` |

这是对 `reference/binaries/` 中四份参考的 fresh 复核；旧 `libkrkr2.so` 注释和
Hex-Rays 显示的泛化 `fmin` 只作为导航线索，不能覆盖指令的真实 NaN 语义。

## 3. 两份 AArch64：`FMIN`，不是 `FMINNM`

Android arm64 先把原始 dt 复制到 `D12`，把 `1.1` 载入 `D10`，然后执行：

```text
FCMP D12, #0.0
B.HI controller_iteration
... dirty-byte fallback ...
FMIN D13, D12, D10
FCVT S9, D13
...
FSUB D12, D12, D13
```

iOS arm64 对应寄存器为 remaining `D11`、cap `D10`、slice `D12`：

```text
FMIN D12, D11, D10
FCVT S9, D12
...
FSUB D11, D11, D12
```

两端都是 `FMIN` 而不是选择数值操作数的 `FMINNM`。quiet NaN 因此传播到 double
slice，再在 `FCVT` 后传播到 float controllerDt。loop gate 的 `B.HI` 是有序大于；
NaN 不会靠 `remaining > 0` 入环，只能由非零 dirty byte 进入。

## 4. 两份 ARMv7：先保留 remaining，只在有序 GT 时覆盖

Android armv7 与 iOS armv7 的指令形状一致：

```text
VCMPE.F64 remaining, cap_1_1
VMRS APSR_nzcv, FPSCR
VMOV.F64 slice, remaining
IT GT
VMOVGT.F64 slice, cap_1_1
VCVT.F32.F64 controllerDt, slice
```

无序比较不会满足 `GT`，所以 `slice` 保持原始 remaining。它与标准库常见的
`std::min(a,b)` 展开 `b < a ? b : a` 在此操作数顺序下完全一致，也与两份 AArch64
面对 NaN 的结果一致。

## 5. 边界矩阵

下表只描述 dirty 已经令本次迭代发生时的 slice 选择；dirty 为 false 时，非正数和 NaN
都无法通过有序 `remaining > 0` gate。

| remaining | 四端 slice | controllerDt | subtract 后 remaining |
|---|---:|---:|---:|
| quiet NaN | NaN | NaN(float) | NaN |
| `+Inf` | `1.1` | `1.1f` | `+Inf`，因此循环不终止 |
| `-Inf` | `-Inf` | `-Inf` | NaN (`-Inf - -Inf`) |
| `-0.0` | `-0.0` | `-0.0f` | `+0.0` |
| `+0.0` | `+0.0` | `+0.0f` | `+0.0` |
| finite `<= 1.1` | 原值 | 窄化后的原值 | 通常 `+0.0` |
| finite `> 1.1` | `1.1` | `1.1f` | 原值减 `1.1` |

controller family 或其后续写回若再次置 dirty，NaN residual 可以继续触发额外迭代；
本纵切面不把该重入效果伪装成固定迭代次数。可确定的是每次 slice 选择、float 窄化与
double 扣减都使用同一个传播后的值。

## 6. 本地修正与回归

- `cpp/plugins/motionplayer/EmoteEngine.h/.cpp`
  - 新增语义名 `internal::controllerSliceTime_guess`；
  - helper 明确执行 `std::min(remaining, 1.1)`；
  - 生产 controller loop 改用该 helper；
  - 注释不再把操作误写成可交换的 `fmin`。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 覆盖 NaN 传播；
  - 覆盖 `-0.0` 的符号保留；
  - 覆盖负有限值、精确 cap、cap 上方值与正无穷。
- `analysis/motionplayer_progress_entry_four_binary_2026-08-11.md`
  - 把泛化 `min` 收紧为带操作数身份的 `std::min`，并链接本 fresh 证据。

## 7. Recovery IDB 回写

四份 recovery IDB 的 Engine core、loop gate、slice select 与 exact subtract 已追加
2026-08-16 注释；slice select 处统一加入书签
`progress controller slice unordered operand identity (2026-08-16)`。四份数据库随后均
原位保存成功。

## 8. 验证

- 普通 Web 配置的完整 `motionplayer-dll.cpp` Emscripten syntax-only 检查通过；
- `KRKR2_WASMTIME_HEADLESS=1` 配置的同一测试翻译单元 syntax-only 检查通过；
- `cmake --build --preset "Web Debug Build"` 成功重编并链接
  `libmotionplayer.a` 与最终 `index.html/index.wasm`；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`
  成功链接 Headless `libmotionplayer.a`；
- 诊断仅有仓库既存的 `_tss`、imagepacker attribute、pthread memory-growth、JSPI 与
  Emscripten JS-library warnings；
- scoped `git diff --check` 与旧 `std::fmin(dt, 1.1)`/旧注释扫描通过。
