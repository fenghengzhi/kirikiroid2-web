# MotionPlayer PrivateMotionGLL Bezier 标量顺序四端复原（2026-08-16）

## 1. 结论

本轮只依据 `reference/binaries/` 的四个当前参考目标，补完
`BezierBasis_getCubicTable_guess` 与 `PrivateMotionGLL_tessellateBezierPatch_guess` 先前明确
保留的浮点结合顺序缺口。四端共同算法不仅是数学上的 cubic Bernstein 等价式，而且有一致
的逐 scalar 操作顺序：

```cpp
double t = double(index) / double(division);
double u = 1.0 - t;
double u2 = u * u;
basis[0] = u * (u * u);
basis[1] = (t * u2) * 3.0;
basis[2] = (t * (t * u)) * 3.0;
basis[3] = t * (t * t);

weight = basisY[controlIndex / 4] * basisX[controlIndex % 4];
point.x = point.x + weight * control[controlIndex].x;
point.y = point.y + weight * control[controlIndex].y;
```

每个乘积和加法都是独立指令；没有 fused multiply-add，也没有把 cubic 项重结合成另一种等价
多项式。因而这一顺序同时决定正常有限值的末位舍入，以及 division=0 时 NaN 的传播。

## 2. 四端 fresh 映射

| 目标 | basis helper | tessellator |
|---|---:|---:|
| Android arm64-v8a | `0x69DE30` | `0x6D9138` |
| Android armeabi-v7a | `0x576C7C` | `0x59ABC8` |
| iOS arm64 | `0x1000FB4A8` | `0x1001289AC` |
| iOS armv7 | `0xF854C` | `0x127C6C` |

### basis 指令序列

四端分别使用 AArch64 `FMUL D` 或 ARMv7 `VMUL.F64`，但数据依赖完全相同：

1. `u2 = u*u`；
2. basis 0 重新执行 `u*u2`；
3. basis 1 执行 `t*u2`，再乘 `3.0`；
4. basis 2 执行 `t*u`，再由另一次 `t*previous` 形成 `t*t*u`，最后乘 `3.0`；
5. basis 3 执行 `t*t`，再执行 `t*previous`。

代表性的 AArch64 范围为 Android `0x69E020..0x69E070` 与 iOS
`0x1000FB564..0x1000FB600`；ARMv7 对应 Android `0x576D22..0x576D98` 与 iOS
`0xF85F8..0xF866E`。四端都没有 `FMLA/VFMA`。

### patch accumulation 指令序列

四端每个 control point 都先单独乘 `basisY*basisX`，再用同一 weight 分别乘 x/y；每个坐标
最后以一条独立的 add 累加。代表范围为 Android arm64 `0x6D9208..0x6D9228`、Android
armv7 `0x59AC82..0x59ACA2`、iOS arm64 `0x100128A64..0x100128A80`、iOS armv7
`0x127D20..0x127D40`。循环严格按 control index `0..15` 前向累加，没有 pairwise reduction。

## 3. 边界与容器提交

- cache miss 先按 `division + 1` resize 外层 vector，再检查 `division < 0`；因此 `-1` 形成
  空表，而更小负值会把回绕后的巨大 size 交给 vector resize，通常在进入浮点循环前抛出；
- division=0 仍生成一行：`0.0/0.0` 产生 NaN，四个 basis 项均为 NaN；
- tessellator 先取得 X/Y 两张 cache 表；只有之后才检查负的 Y division；
- 每个目标点从两个 `+0.0` accumulator 开始，按 16 个 control points 前向提交；
- point vector 仅在完整算出一对坐标后 append。单个 basis/coordinate 运算本身不分配。

本轮没有把 reference 依赖的 16-point 输入 invariant 改成防御检查，也没有改变 cache 所有权、
map 查找/默认插入或 vector 扩容异常边界。

## 4. 源码与回归

`cpp/plugins/motionplayer/MotionRenderBackend.cpp` 原有括号和赋值顺序已经与四端指令一致；因此
不改算法，只添加防止未来重结合/FMA 化的语义注释。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增位级回归：

- hard-code division=3 两个 interior basis row 的 IEEE-754 words；
- 验证 division=0 的四个 NaN 和 division=-1 的空表；
- 使用大幅度消去、极大/极小量混合的 16 个 control points，锁定四个内部 patch 点的 x/y
  bit pattern；
- 验证 divisionX=divisionY=0 时单点结果的两个坐标均为 NaN。

四份 recovery IDB 的 basis 与 tessellator arithmetic sites 均已写入共同语义注释，并加入
`PrivateMotion Bezier scalar order (2026-08-16)` 书签；四库原位保存成功。

验证结果：

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer Catch2 TU syntax-only 通过；
- `Web Debug Build` 完整编译 `MotionRenderBackend.cpp`、归档并最终链接 `index.html`；
- `Wasmtime Headless Debug Build` 完整编译、归档并最终链接 `index.html`；
- 本纵切面文件的 `git diff --check` 通过。

输出只有项目既有 `_tss`、pthread memory-growth 和 JSPI 实验性警告。当前 preset 不生成可直接
运行的 Catch2 motionplayer 可执行文件，因此上述位级回归已通过翻译单元编译检查，但不冒充
运行时 Catch2 结果。
