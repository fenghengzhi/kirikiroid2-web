# MotionPlayer parent Bezier mesh deformation and patch runtime（四参考二进制，2026-08-14）

## 1. 范围与结论

本轮重新检查 `reference/binaries/` 四个当前目标中由
`Player_updateLayers` 调用的 parent-mesh child deformation，不沿用旧
`libkrkr2.so` 的 `sub_69AE74` 地址或其变量名。交叉引用又把同一条数据流向上追到：

1. 固定 4×4 patch 的 scalar evaluator；
2. 对 vector 做尺寸诊断的 wrapper；
3. ARM+NEON runtime evaluator pointer promotion；
4. Player NCB registrar 末尾创建的进程级 4×4 identity patch；
5. timeline mesh interpolation 对空 slot patch 使用 identity patch 的 fallback。

本地旧实现有以下可见偏差，现均已修正：

- 把 source width/height 的非正值替换成 `1.0`，而原生直接除；
- 在 double 域完成 origin/secondary 输入和 pixel-space 输出，漏掉原生多处
  double→float→double 量化；
- 用第一次 patch 的输出坐标继续采导数，原生始终用第一次调用之前的原始 `u/v`；
- angle 的两次 `atan2` 参数次序均不对，而且错误地使用 double `atan2`；
- angle 和 scale 各自重复执行四次 patch，原生在二者任一开启时只构造一套四样本并共享；
- 仅在 `size >= 16` 时求值，原生要求的诊断尺寸是恰好 16，但诊断后仍无条件调用固定
  16 点 evaluator；
- timeline 一端 patch 为空时使用本地空 vector，原生使用进程级 identity patch；
- 漏掉 scalar/NEON 双 evaluator、全局函数指针和 NCB registrar 尾部 promotion。

## 2. 四端函数与全局对象映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| parent deformation | `0x698254` (`0x374`) | `0x574168` (`0x362`) | `0x1000F7DD8` (`0x348`) | `0xF4B88` (`0x352`) |
| vector/size wrapper | `0x6985C8` (`0xC8`) | `0x5744F8` (`0x6A`) | `0x1000F8120` (`0x78`) | `0xF4EFC` (`0xCC`) |
| scalar 4×4 evaluator | `0x696480` (`0x104`) | `0x5727F4` (`0x102`) | `0x1000F6300` (`0x110`) | `0xF2EAC` (`0x120`) |
| NEON 4×4 evaluator | `0xA96F44` (`0x15C`) | `0x7B1FE0` (`0x13E`) | `0x10018A704` (`0x174`) | `0x188972` (`0x160`) |
| evaluator pointer | `0x1AA10E8` | `0x11020A0` | `0x101ADF7E8` | `0x1831888` |
| identity vector begin | `0x1AB5108` | `0x1111648` | `0x101B695D0` | `0x187D30C` |

四份 recovery IDB 已分别将上述函数命名为：

- `deformChildByParentBezierPatch_guess`
- `evaluateBezierPatchVector_guess`
- `evaluateBezierPatch4x4Scalar_guess`
- `evaluateBezierPatch4x4Neon_guess`

两个全局对象命名为 `bezierPatchEvaluator_guess` 和
`defaultBezierPatchPoints_guess`。名字均保留 `_guess`，因为二进制不含原 C++ 标识符。

## 3. 进程级 patch runtime 的构造与选择

### 3.1 初始状态

evaluator pointer 的映像初值指向 scalar kernel：

| 目标 | pointer bytes | scalar target |
|---|---|---:|
| Android arm64 | `80 64 69 00 00 00 00 00` | `0x696480` |
| Android armv7 | `F5 27 57 00`（Thumb bit） | `0x5727F4` |
| iOS arm64 | `00 63 0F 00 01 00 00 00` | `0x1000F6300` |
| iOS armv7 | `AD 2E 0F 00`（Thumb bit） | `0xF2EAC` |

identity patch 是普通 `std::vector<MeshPoint>` 三指针对象，静态构造后为空，进程卸载时由
对应 `InitFunc_49`/global destructor path 释放；它不是 `constexpr` array，也不属于某个
Player 实例。

### 3.2 Player NCB registrar 的尾部初始化

registrar 在全部 Player member descriptor 建完后执行：

| 目标 | identity init | CPU gate / pointer store |
|---|---:|---:|
| Android arm64 | `0x6D64A8..0x6D6610` | `0x6D6614..0x6D6644` |
| Android armv7 | `0x59884A..0x5988A8` | `0x5988AA..0x5988D2` |
| iOS arm64 | `0x100125310..0x10012538C` | `0x100125390..0x1001253C0` |
| iOS armv7 | `0x124540..0x1245B8` | `0x1245BA..0x1245EE` |

共同伪代码：

```cpp
if(defaultBezierPatchPoints.empty()) {
    defaultBezierPatchPoints.reserve(16);
    for(int i = 0; i != 16; ++i) {
        defaultBezierPatchPoints.push_back({
            float(double(i & 3) / 3.0),
            float(double(i >> 2) / 3.0)
        });
    }
}

if((TVPCPUType & (TVP_CPU_HAS_NEON | TVP_CPU_FAMILY_MASK)) ==
   (TVP_CPU_HAS_NEON | TVP_CPU_FAMILY_ARM)) {
    bezierPatchEvaluator = evaluateBezierPatch4x4Neon;
}
```

重要边界：

- vector 只以 `empty()` 为 gate；若外部异常地留下任意非零长度，registrar 不修补到 16 点；
- evaluator pointer 只有 promotion，没有不匹配时的 scalar reset；初始 scalar 值由映像负责；
- identity 点的顺序是 row-major `{x=column/3, y=row/3}`；
- 四端常数 gate 都是 `0x0200000F` mask 与 `0x02000003` expected。

本地现在把 initialization call 放回 `NCB_REGISTER_SUBCLASS(Player)` 尾部；inline global
vector/pointer 保留单一进程级身份和退出析构。

## 4. 两个 4×4 evaluator

### 4.1 basis 与点顺序

scalar 和 NEON kernel 都先在 float 域计算：

```text
B0(t) = (1-t) * ((1-t) * (1-t))
B1(t) = (t * ((1-t) * (1-t))) * 3
B2(t) = (t * (t * (1-t))) * 3
B3(t) = t * (t * t)
weight[i] = Bv[i >> 2] * Bu[i & 3]
```

随后按 `i=0..15` 读取连续 `{float x,float y}`。scalar 路径分别维护 x/y 两个 float
accumulator；NEON 路径把一个 MeshPoint 当作 2-lane vector，并按完全相同的 16 点次序做
lane multiply-accumulate。

关键循环证据：

| 目标 | scalar loop | NEON loop |
|---|---:|---:|
| Android arm64 | `0x696524..0x696560` | `0xA96FE4..0xA97078` (`FMLA`) |
| Android armv7 | `0x57289C..0x5728DE` | `0x7B2088..0x7B2100` (`VMLA`) |
| iOS arm64 | `0x1000F63AC..0x1000F63E4` | `0x10018A7B4..0x10018A848` (`FMLA`) |
| iOS armv7 | `0xF2F62..0xF2FAE` | `0x188A2A..0x188AA2` (`VMLA`) |

AArch64 的 `FMLA` 与 ARMv7 的 `VMLA` 是真实 ISA 差异；本地 portable NEON-form helper
按 pointer width 区分 fused `fma` 与 ordinary multiply-plus-add，避免把两个参考 ABI 的
末位舍入强行混成一个假想行为。

### 4.2 vector wrapper 的异常边界

| 目标 | byte-size compare | log call | indirect evaluator call |
|---|---:|---:|---:|
| Android arm64 | `0x6985F0..0x6985FC` | `0x698614` | `0x69863C` |
| Android armv7 | `0x574512..0x57451A` | `0x574526` | `0x57453E` |
| iOS arm64 | `0x1000F813C..0x1000F8148` | `0x1000F815C` | `0x1000F8180` |
| iOS armv7 | `0xF4F26..0xF4F54` | `0xF4F78` | `0xF4FA2` |

比较的是 `end - begin == 0x80`。不相等时构造并记录精确 UTF-16 文本：

```text
invalid size of bezier patch.
```

临时 `ttstr` 销毁后，wrapper 重新载入 vector begin，仍经函数指针调用固定 16 点 kernel。
因此：

- `size==16`：正常；
- `size>16`：记录错误，只读取前 16 点；
- `0<size<16`：记录错误后仍读取 16 点，保留原生越界/UB；
- deformation 外层先排除 empty vector，所以该调用链不会把 `size==0` 送入 wrapper；
  wrapper 本身没有 empty 特判。

## 5. parent deformation 的 gate 与精度链

### 5.1 入口 gate

四端共同要求：

```text
patch.begin != patch.end
parent.accumulated.active
parent.source.valid
(parent.meshFlags & 1) != 0
parent.meshType == 1
```

关键最终 gate 位点为 `0x6982C0`、`0x5741B4`、`0x1000F7E38`、`0xF4BD4`。
外层 `updateLayers` 的 `parent.meshType != 0` 只决定是否调用；helper 内部仍严格拒绝
非 type-1 mesh。

### 5.2 normalized input 的非对称 float 边界

原生不是简单的全 double：

```cpp
float totalOX = float(slot.ox + source.originX);
float totalOY = float(slot.oy + source.originY);
float secondary = float(coordinateMode ? child.posZ : child.posY);
float u = float((child.posX + double(totalOX)) / source.width);
float v = float(double(totalOY + secondary) / source.height);
```

X 保留 child.posX 的 double 精度直到除法；Y/Z secondary 先降为 float，再与 float origin
相加。source width/height 在四端都是直接 `FDIV/VDIV`：

| 目标 | width/height division |
|---|---:|
| Android arm64 | `0x698320`, `0x698328` |
| Android armv7 | `0x574228`, `0x57422C` |
| iOS arm64 | `0x1000F7E88`, `0x1000F7EA0` |
| iOS armv7 | `0xF4C48`, `0xF4C4C` |

没有 `width > 0 ? width : 1` 或任何 zero guard；零维度自然产生 Inf/NaN 并流入 patch。

### 5.3 position 输出量化

第一次 wrapper call 返回 float `(deformedX,deformedY)`：

```cpp
child.posX = double(float(double(deformedX) * width  - double(totalOX)));
secondary = double(float(double(deformedY) * height - double(totalOY)));
```

也就是说 pixel-space 乘减之后还要再做一次 double→float→double。四端对应 demote 位点为
`0x69836C`、`0x574274`、`0x1000F7EC8`、`0xF4C94`。本地旧实现把最后结果直接保存在
double 中，既改变有限数末位，也改变大 origin 的取消行为。

## 6. angle/scale 共用的四样本

两个开关独立：

```text
angleEnabled = (parent.meshFlags & 2) && (child.inheritFlags & 0x10)
scaleEnabled = (parent.meshFlags & 4) && (child.inheritFlags & 0x60)
```

若二者都 false，position 后立即返回；否则只计算一次：

```text
left  = patch(u-0.0001f, v)
right = patch(u+0.0001f, v)
minus = patch(u, v-0.0001f)
plus  = patch(u, v+0.0001f)
```

四端第一样本调用：`0x6983EC`、`0x5742E8`、`0x1000F7F68`、`0xF4D0E`。
它们都从第一次 position call 之前保存的原始 `u/v` stack slots/registers 重装；没有一端
使用 deformed output。

### 6.1 angle

原生两个单精度调用是：

```cpp
float aV = atan2f(minus.x - plus.x, plus.y - minus.y);
float aU = atan2f(right.y - left.y, right.x - left.x);
child.angle += ((double(aV) + double(aU)) * 0.5 * 360.0) / 6.28318531;
```

operand setup / first call 位点：

| 目标 | first `atan2f` |
|---|---:|
| Android arm64 | `0x69847C..0x698484` |
| Android armv7 | `0x574352..0x574362` |
| iOS arm64 | `0x1000F7FF8..0x1000F8000` |
| iOS armv7 | `0xF4D84..0xF4D94` |

本地旧公式将两对轴互换/取反，在 `{x=v,y=-u}` 的纯旋转 patch 上给出 `+90°`，而四端
公式给出约 `-90°`。

### 6.2 scale

先把每个 sample float 分别提升到 double，再做差：

```cpp
dx = double(right.x) - double(left.x);
dy = double(right.y) - double(left.y);
Aplus  = abs(dx*(double(plus.y)-left.y)
           - dy*(double(plus.x)-left.x)) * 0.5;
Aminus = abs(dx*(double(minus.y)-left.y)
           - dy*(double(minus.x)-left.x)) * 0.5;
factor = sqrt(((Aplus + Aminus) + Aminus) + Aplus) / 0.0002;
```

`inheritFlags & 0x20` 乘 scaleX，`inheritFlags & 0x40` 乘 scaleY。scale block 入口为
`0x6984D0`、`0x5743C8`、`0x1000F8050`、`0xF4DFA`。旧实现先在 float 做 sample 差再
提升到 double，且因使用 deformed coordinates 导致非线性 patch 的 factor 明显错误；
测试中的 `{x=u²,y=v²}` 在 `(u,v)=(0.25,0.36)` 应约为 `0.6`，旧路径约为 `0.18`。

## 7. timeline 对 identity patch 的 fallback

四端 `Player_evaluateTimeline_guess` 在 `meshType==1` 且 crossfade interpolation 路径中：

- 两个 slot patch 都为空：不调用 interpolation helper，也不覆盖 node output vector；
- active 空、other 非空：`identity -> other`；
- active 非空、other 空：`active -> identity`；
- 两者非空：`active -> other`。

identity global 的引用位点为：

| 目标 | fallback refs |
|---|---:|
| Android arm64 | `0x697490`, `0x6974A0` |
| Android armv7 | `0x5736A6` |
| iOS arm64 | `0x1000F7190`, `0x1000F71A0` |
| iOS armv7 | `0xF3E4A..0xF3E88` |

本地 `interpolateTimelineMeshPayload_guess` 现改用同一 process-global identity vector。
新增测试覆盖 active empty / other shifted-identity，在 0.5 ratio 得到 identity 与目标中点。

## 8. 本地实现与验证

代码变更：

- 新增 `cpp/plugins/motionplayer/MotionBezierPatch.h`：identity vector、scalar/NEON-form
  evaluator、runtime function pointer、registrar initializer、size-log wrapper；
- `cpp/plugins/motionplayer/main.cpp`：Player NCB registrar 尾部调用 runtime initializer；
- `cpp/plugins/motionplayer/PlayerUpdateLayersInternal.h`：重写 parent deformation 的精度链、
  共享 stencil、angle/scale 和边界行为；
- `cpp/plugins/motionplayer/PlayerUpdateLayerEval.cpp`：空 slot fallback 改为 identity patch；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：增加 identity 生命周期/fallback、旋转 angle、
  非线性 original-coordinate scale、零维度 NaN 测试。

验证：

- `Web Debug Build` 完整编译、静态库与 `index.html` 链接通过；
- 完整 `motionplayer-dll.cpp` Emscripten syntax-only 通过，仅有既存 `_tss` warning；
- LLVM Wasm object disassembly 确认 deformation 保留：
  float origin/secondary add、直接 `f64.div`、输出 `f32.demote_f64` 后再 promote/store、
  四次 original-coordinate call、两次 float atan2、double sample differences 和指定 area 加法顺序；
- 四份 IDB 已写入函数/全局命名及 gate、尺寸日志、CPU promotion、输入/输出量化、
  original-coordinate stencil、angle/scale 和 identity consumer 注释，并全部保存。

## 9. 紧邻但尚未在本轮改写的后续竖切

同一 identity vector 还被四端 vertex-computation mesh-chain 使用：

```text
workingPatch[i] += ancestor.meshControlPoints[i] - identityPatch[i]
```

对应引用为 `0x6B9D90`、`0x586B1E`、`0x10010FB68`、`0x10CFBC`；在相加之前还有
`"mesh size is different."` 的精确尺寸错误路径。当前 `PlayerUpdateGeometry.cpp` 的 ancestor
mesh-chain 仍是较粗的逐祖先直接 patch evaluation，未复原这套 working-patch composition。
这是下一条高价值竖切，不能因本轮 parent-child deformation 通过构建而视为已闭合。
