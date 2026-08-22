# MotionPlayer source-clip 四角颜色重映射：四参考二进制联合复原（2026-08-13）

## 1. 结论

prepared render-item 构造阶段先把节点的四个累计 packed color 分别与有效
`colorWeight` 相乘，再把所得四角颜色按持久 `SourceState` 的
`clipLeft/clipTop/clipRight/clipBottom` 做两阶段双线性重采样。四个参考目标共同证明：

1. 单位 clip 矩形先早退；
2. 四角颜色全相等再早退；
3. 两次早退之后才构造一个默认、因而为 Void 的曲线/Variant 值；
4. 先对上边和下边分别做 left/right 水平插值，再对两组结果做 top/bottom
   垂直插值；
5. source-clip 与 timeline color 共用同一个 packed-color 标量插值 helper；
6. 标量 helper 的 `from == to` 早退发生在曲线求值和浮点到整数转换之前；
7. 非相等路径使用有符号 8-bit 定点权重和 `0x00FF00FF` 双通道 lane 运算，
   所有中间算术按 `uint32_t` 回绕。

Android arm64 把外层调用完全内联，并把第一组水平权重
`{clipLeft, clipRight}` 向量化成 signed-int64 conversion 后再窄化到 32 位；这只在
NaN/无穷/超大非法 clip 上与另外三端及该函数其余标量转换不同。它是优化器在
C++ 越界浮点转整数未定义域内形成的编译产物差异，不是第二套共享源码算法。

## 2. 四文件函数映射

| 参考二进制 | source-clip 外层函数 | packed-color 标量插值 helper | prepared-item 唯一调用点 |
|---|---:|---:|---:|
| Android arm64-v8a `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `remapPackedColorsForSourceClip_guess` `0x695568`，大小 `0x2CC` | 内联在外层中；没有本调用链上的独立 helper | `0x6C0974`，所属 `Player_appendPreparedRenderItems_guess` `0x6BF714` |
| Android armv7 `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `remapPackedColorsForSourceClip_guess` `0x571E6C`，大小 `0x106` | `interpolatePackedColor_guess` `0x571F90` | `0x58B554`，所属 prepared-item builder `0x58B178` |
| iOS arm64 `Kirikiroid2_1.3.9_iOS_arm64` | `remapPackedColorsForSourceClip_guess` `0x1000F5800`，大小 `0x144` | `interpolatePackedColor_guess` `0x1000F5964` | `0x100114D6C`，所属 prepared-item builder `0x1001148F8` |
| iOS armv7 `Kirikiroid2_1.3.9_iOS_armv7` | `remapPackedColorsForSourceClip_guess` `0xF2270`，大小 `0x17E` | `interpolatePackedColor_guess` `0xF2428` | `0x11271E`，所属 prepared-item builder `0x1123D8` |

四个外层函数都只有上述一个 prepared-item xref。Android armv7、iOS arm64、
iOS armv7 的独立标量 helper 各还有一个 timeline evaluator xref，分别为：

- Android armv7：`0x5735C2`；
- iOS arm64：`0x1000F70A0`；
- iOS armv7：`0xF3D5A`。

对应 shipped timeline 路径把同一个 active packed color 同时作为 `from` 和 `to`，
所以 helper 在读取 `cccVariant` 或计算 ratio 之前即相等早退。Android arm64 的同一
源码 helper 在这两条路径中都已被优化器内联，不能因缺少独立函数边界误判为两个
源码实现。

## 3. 四端共同控制流

以输入角顺序 `[topLeft, topRight, bottomLeft, bottomRight]` 表示：

```cpp
if(clipLeft == 0.0 && clipTop == 0.0 &&
   clipRight == 1.0 && clipBottom == 1.0) {
    return;
}
if(color[0] == color[1] && color[1] == color[2] &&
   color[2] == color[3]) {
    return;
}

CurveOrVariant colorCurve; // 默认/Void；生命周期从这里开始

topLeft     = interpolate(colorCurve, color[0], color[1], clipLeft);
topRight    = interpolate(colorCurve, color[0], color[1], clipRight);
bottomLeft  = interpolate(colorCurve, color[2], color[3], clipLeft);
bottomRight = interpolate(colorCurve, color[2], color[3], clipRight);

color[0] = interpolate(colorCurve, topLeft,  bottomLeft,  clipTop);
color[1] = interpolate(colorCurve, topRight, bottomRight, clipTop);
color[2] = interpolate(colorCurve, topLeft,  bottomLeft,  clipBottom);
color[3] = interpolate(colorCurve, topRight, bottomRight, clipBottom);

// colorCurve 在八次调用全部结束后析构
```

Android armv7、iOS arm64、iOS armv7 的外层反编译清楚保留八个 helper 调用。
Android arm64 把 helper 完全内联，于外层直接出现相等端点判断、转换与 pair-lane
算术；这是 inlining/向量化后的形状。反推共享源码时应保留“外层固定八次调用、
helper 自己相等早退”的结构。

## 4. 共享标量插值 helper

共同源级逻辑为：

```cpp
uint32_t interpolate(curve, uint32_t from, uint32_t to, double ratio) {
    if(from == to)
        return from;
    if(curve is not Void)
        ratio = evaluateVariableTrackEasing(curve, ratio);

    uint32_t weight = signed_trunc_to_i32(ratio * 256.0);
    uint32_t inverse = 256u - weight;
    constexpr uint32_t mask = 0x00FF00FFu;
    uint32_t highPairs =
        (((from >> 8) & mask) * inverse) +
        (((to   >> 8) & mask) * weight);
    uint32_t lowPairs =
        ((from & mask) * inverse) + ((to & mask) * weight);
    return highPairs ^ ((highPairs ^ (lowPairs >> 8)) & mask);
}
```

最后一行与 `((lowPairs >> 8) & mask) | (highPairs & (mask << 8))`
等价。这里没有 per-channel clamp；正常 `[0,1]` ratio 通过 8-bit 定点混合得到线性
插值，越界 ratio 则保留 unsigned 32-bit lane 算术的回绕结果。

曲线分支在 source-clip 路径不可达，因为外层传入默认 Void 值；它在 helper 中仍是
真实源码结构，并解释为什么同一 helper 还能由 timeline evaluator 调用。相等端点
早退位于该分支之前，所以 shipped timeline 的同端点调用不会触发 TJS property
dispatch，也不会受无效 `cccVariant` 影响。

## 5. 对象生命周期与调用链位置

prepared-item builder 中的顺序是：

```text
node.colorBytes
  -> 拷贝四个 packed uint32 到 prepared item
  -> 每角调用 multiplyPackedColorWeights_guess(color, effectiveColorWeight)
  -> remapPackedColorsForSourceClip_guess(node.source, preparedItem.colors)
  -> 继续写 opacity / stencil / coordinate mode / mesh / vertices
```

因此 clip 重映射消费的是“已经应用节点/父子/全局 colorWeight 的累计颜色”，不是
原始时间轴颜色；它直接改写 node-owned prepared item 的四角数组，不创建额外持久
容器。

外层默认曲线值只存在于非早退路径，且跨越八次插值调用后才析构。四端都能观察到
对应初始化与清理；提前在函数入口构造、在各插值调用中各建一个临时值、或像旧本地
实现那样只造一个不参与调用的 `unusedVariant`，都会改变对象生命周期和异常时序。

## 6. 逐目标转换差异

### Android armv7

独立 helper 先把 `D0` 乘 `256.0`，再用 `VCVT.S32.F64 S0, D0`。所有八个
source-clip 权重都经过此标量 helper。

### iOS armv7

与 Android armv7 同为 `D0 * 256.0` 后 `VCVT.S32.F64 S0, D0`，控制流和
pair-lane 公式一致。

### iOS arm64

独立 helper 使用 `FCVTZS W9, D0, #8`，即把 `ratio * 2^8` 向零截断到 signed
32-bit。所有八个 source-clip 权重都经过这个 helper。

### Android arm64

大多数内联站点使用 `FCVTZS Wn, Dn, #8`，与 iOS arm64 的标量语义一致。但外层
`0x695604` 附近把第一组上边水平输入 `{clipLeft, clipRight}` 合并为：

```text
FCVTZS V3.2D, V3.2D, #8   // 两个 signed int64 lane
XTN    V3.2S, V3.2D       // 只取每个 int64 的低 32 位
```

所以仅对这两个权重，在超出 signed-int32 而仍处于/超出 signed-int64 范围的非法
输入上会出现低 32 位窄化结果，而非 signed-int32 饱和值。例如正无穷经 int64
饱和后窄化为 `0xFFFFFFFF`，负无穷窄化为 `0x00000000`；其他水平/垂直站点仍走
W-register signed-int32 conversion。合法归一化 clip `[0,1]` 上两种实现完全一致。

### 本地可移植边界选择

生成这四个二进制的共享 C++ 源很可能只是 signed integer conversion；超出目标
整数范围或 NaN 的 C++ 浮点转整数属于未定义域，优化器可合法产生上述不同结果。
WebAssembly 的原生截断指令还可能 trap，不能把任一目标的 UB 编译产物假装成四端
共同源码。

本地 `packedColorInterpolationWeightS32_guess` 因而显式采用四端所有标量站点一致的
边界：NaN 为 `0`、正溢出为 `INT32_MAX`、负溢出为 `INT32_MIN`，避免 wasm trap；
代码注释和本文明确保留 Android arm64 第一水平向量对的差异。这里没有添加
Android-only 源级分支，因为那会复刻单个优化产物而破坏四端共同源码结构。

## 7. 对旧本地实现的纠正

旧 `PlayerRenderItems.cpp` 已有正确的 pair-lane 数值公式，但存在以下结构偏差：

- helper 与 remapper 名称固化了旧 `libkrkr2.so` 地址 `0x698188`；
- timeline 另有一份重复的 packed-color 插值实现，未表达两条路径共享 helper；
- 外层自己判断局部端点相等，只在不等时调用纯数值 helper；
- 默认 `tTJSVariant` 被构造成 `unusedVariant`，没有作为每一次 helper 调用的曲线
  参数，因此只近似了析构时机，未复刻真实调用链；
- 标量边界只在 source-clip 私有函数中，timeline duplicate 仍直接执行可能越界的
  C++ conversion。

本轮实现对照共同伪代码完成：

- `PlayerInternal.h` 声明唯一共享 `interpolatePackedColor_guess`；
- `PlayerRenderItems.cpp` 定义 helper，并在 helper 内依次表达相等早退、可选曲线、
  signed 8-bit 定点转换与 pair-lane 合成；
- source-clip 外层在两个全局早退后构造默认 `colorCurve`，按确定次序做八次调用；
- `PlayerUpdateLayerEval.cpp` 删除 duplicate，timeline 改为调用同一 helper；
- 编译源码和单元测试中删除最后的旧 `sub_698188`/`0x698188` 地址式注释；四目标地址
  只保留在本文；
- 既有 prepared-item 测试继续覆盖 `0.25/0.75` clip 的四角结果；新增 helper 测试覆盖
  0、1、0.25、0.75、负外插、NaN、正负无穷及非 Void 曲线下相等端点的早退顺序。

## 8. IDB 改善与保存

四个外层函数均持久重命名为 `remapPackedColorsForSourceClip_guess`；Android armv7、
iOS arm64、iOS armv7 的独立标量 helper 均重命名为
`interpolatePackedColor_guess`。Android arm64 无独立 helper，已在内联外层的关键
站点标注默认曲线生命周期、两阶段插值和 `0x695604` 向量窄化差异。其余三端也已在
外层/helper 标注相等早退、曲线求值次序、转换与 pair-lane 语义。四个 IDB 均已原位
保存成功。

## 9. 验证

- `Web Debug Build`：最终源码成功编译并链接 `index.html/index.wasm`；一次并行重跑
  曾因前一条超时命令遗留 Ninja 持有 `index.wasm` 而报 `permission denied`，遗留
  构建自然结束后重跑为 `ninja: no work to do`，不是源码诊断。
- `Wasmtime Headless Debug Build`：成功完成 50 个受影响步骤，包含普通 motionplayer、
  guest objects 与最终 `index.html` 链接。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp`：复用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数并加入 Catch2 `test_config.h`，执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用警告。
- 当前 CMake 配置没有可直接运行本组 Catch2 用例的 motionplayer test executable；
  因此这里只把它声明为完整测试翻译单元编译验证，不冒充运行时测试。
- 四个 IDB 均保存成功。

