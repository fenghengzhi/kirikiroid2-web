# MotionPlayer common-mesh 裁剪与数值边界四端复原（2026-08-16）

## 1. 本轮纠正

Player direct/D3D 与 `PrivateMotionGLL` 共用的 common-mesh helper 的三层裁剪
主干此前已恢复，但旧文档与 portable C++ 把所有 point upper bound 都写成了：

```text
signed32_saturating_trunc(point + 1.0)
```

fresh 逐指令核对四个当前参考二进制后，真实数据流是：

```text
outer boundsPoints[0]:
    lower = signed32_saturating_trunc(point)
    upper = wrapping_signed32_add(lower, 1)

outer boundsPoints[1..], mesh-point rescan, cell AABB:
    lower = signed32_saturating_trunc(point)
    upper = signed32_saturating_trunc(point + 1.0)
```

所以首点 `-0.5` 的 lower/upper 是 `0/1`，不是旧 portable 的 `0/0`。这不是
NaN 才会触发的病态边界，而是普通有限负小数即可观察到的分支差异。portable
现以独立 `initializeOuterBoundsFromFirstPoint_guess` 表达首点特例；后续 point 与
cell 仍共用 `pointUpperBound_guess(point + 1.0)` 语义。

同时闭合了两处旧 C++ UB：

- double 到 signed int32 显式执行 toward-zero saturation：NaN 为 0，正溢出为
  `INT32_MAX`，负溢出为 `INT32_MIN`；
- `divisionX * divisionY` 显式保留 native `MUL W`/`MULS` 的低 32-bit word，
  再按 signed int32 解释后传给 `vector<int>::reserve`。

## 2. 四份 helper 与首点指令

| 目标 | helper | cell-count 乘法 | 首点转换 | 首点 wrapping `+1` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x69AFE4` | `0x69B31C` | `0x69B36C`–`0x69B370` | `0x69B374`–`0x69B378` |
| Android armv7 | `0x575800` | `0x575976` | `0x575996`–`0x57599A` | `0x5759AA`–`0x5759AE` |
| iOS arm64 | `0x1000F974C` | `0x1000F9938` | `0x1000F9958`–`0x1000F995C` | `0x1000F9960`–`0x1000F9964` |
| iOS armv7 | `0xF685C` | `0xF6A5E` | `0xF6A76`–`0xF6A7A` | `0xF6A86`–`0xF6A8A` |

四端首点都是先把 x/y 分别转换成 signed 32-bit，再由整数 `ADD #1` 形成
right/bottom。没有先做 floating `+1.0`。整数加法不检查 overflow，因此
`INT32_MAX + 1` 变成 `INT32_MIN`；portable 用 unsigned word 加法与 bit-preserving
还原，避免 C++ signed-overflow UB。

后续 outer point 明确先做 floating `+1.0` 再转换：

| 目标 | 后续 outer-point lower/upper 更新 |
|---|---:|
| Android arm64 | SIMD `0x69B410`–`0x69B450`；scalar tail `0x69B534`–`0x69B560` |
| Android armv7 | `0x5759DA`–`0x5759E6` |
| iOS arm64 | `0x1000F999C`–`0x1000F99C4` |
| iOS armv7 | `0xF6AB6`–`0xF6AC2` |

### Android arm64 的 malformed-only 向量化差异

Android arm64 的中间整块 outer-point loop 被编译器向量化为
`FCVTZS V?.2D, V?.2D`，随后以 `XTN` 只取每个 signed-64 result 的低 32 位；首点、
尾部 scalar loop 与另外三份二进制则直接转换到 signed 32-bit。因而超出 int32
但仍在 int64 内的 malformed coordinate 在 Android arm64 的结果会随 point 所处
vectorized/tail 位置变化。这不是四端共同的 MotionPlayer 源算法，不能伪装成统一
边界规则。

portable 选择四端所有 scalar site 共同的 signed-int32 saturation profile；它与
三个目标的完整 loop、Android arm64 的首点和 scalar tail 一致。有效 int32
coordinate domain 内 SIMD 与 scalar 完全一致。该例外在源码注释与本文件中显式
保留，不再把它错误宣称为“四端共同饱和”。

## 3. 三层裁剪的精确顺序

### 3.1 `boundsPoints != meshPoints` 快路径

先用 control/bounds points 构造整体整数包围盒。四端随后按同一顺序：

1. clip 必须满足 `left < right && top < bottom`，否则 Release source、返回 false；
2. bounds 也必须非空；
3. clip 全包含 bounds 时，按升序加入全部 `0 .. cellCount-1`，不读取 mesh point；
4. 严格无交时返回 false；
5. 只有部分相交才进入 point/cell scan。

对应比较块：Android arm64 `0x69B574`–`0x69B5C0` 与
`0x69B75C`–`0x69B780`；Android armv7 `0x575A84`–`0x575AB4` 与
`0x57612E`–`0x576144`；iOS arm64 `0x1000F99E4`–`0x1000F9A38`；
iOS armv7 `0xF6B7A`–`0xF6BA6` 与 `0xF734E`–`0xF7364`。

### 3.2 point-inside gate

进入通用 scan 后，每个 mesh point 的判定精确为半开矩形：

```text
clip.left <= x && x < clip.right &&
clip.top  <= y && y < clip.bottom
```

NaN 与四个 ordered compare 均不成立，因此 pointInside=false。指令块为 Android
arm64 `0x69B830`–`0x69B854`、Android armv7 `0x575B56`–`0x575B84`、
iOS arm64 `0x1000F9AC4`–`0x1000F9ADC`、iOS armv7
`0xF6C18`–`0xF6C46`。

一个 cell 依次检查 `p00,p10,p01,p11`。任意 corner 的 byte 为 true 就立即加入
selectedCells，不再执行 cell AABB。由此产生一个容易漏掉的边界行为：clip 的
left edge 正好穿过两个相邻 cell 的共享 corner 时，左右两个 cell 都可能被加入；
corner 属于 clip，但左 cell 的 AABB 仅在 edge 上接触也不会再被严格 overlap 排除。

### 3.3 cell AABB fallback

四个 corner 均不在 clip 内时，才计算 cell 整数 AABB。lower 使用
`trunc(point)`，upper 使用 `trunc(point + 1.0)`，随后要求双方矩形都非空且：

```text
cell.bottom > clip.top
cell.right  > clip.left
cell.left   < clip.right
cell.top    < clip.bottom
```

转换/比较块：Android arm64 `0x69C694`–`0x69C7B0`；Android armv7
`0x575C94`–`0x575D3C`；iOS arm64 `0x1000F9BE4`–`0x1000F9C84`；
iOS armv7 `0xF6D72`–`0xF6E18`。

AABB 通过后 native 不只 append cell，还把 cell AABB 再 fold 进 point scan 已
形成的 bounds。对正常完整 mesh 这一步是幂等的，但它属于真实数据流；portable
现也保留该 fold，而不是只依赖“结果碰巧相同”。

## 4. output commit 与生命周期

point scan 先计算全部 mesh point 的 bounds，因此只提交一部分 cell 时，成功 output
仍通常是整个 mesh bounds，不是 selected-cell union。corner fast path 不修改 bounds；
AABB path 的 fold 对已扫描的完整 mesh 通常幂等。

成功尾部仍严格是：callback 一次、Release current source、写回四个 bounds word、
销毁临时 vectors、返回 true。empty clip、无交、empty selection 与 callback 异常
都不写 caller rect。尾部地址及异常生命周期已由
`motionplayer_common_mesh_bounds_commit_four_binary_2026-08-15.md` 单独闭合，本轮
没有推翻该结论。

## 5. 回归与验证范围

扩展 common-mesh unit case，固定：

- 首个 bounds point 为 `-0.5` 时得到 `[0,0,1,1]` 并走全包含提交；
- 后续 `-0.5` 仍使用 `trunc(point+1)`，不会错误扩到 1；
- 首点 NaN 转 0 后 wrapping `+1`；
- `-Inf` 饱和到 `INT_MIN` 后加一，`+Inf` 饱和到 `INT_MAX` 后加一回绕并形成
  invalid bounds，失败时 caller rect 原值不变；
- clip right edge 的半开排除只选择左 cell；
- clip left edge 命中 shared corner 时两个相邻 cell 都由 corner fast path 加入；
- clip 完全位于 cell 内且无 corner 命中时，仅由 strict AABB fallback 选择该 cell；
- partial selection 成功仍在 callback/Release 后提交 point scan 的完整 mesh bounds。

测试 TU 已在 ordinary Web 与 Wasmtime headless 两套真实 Emscripten 参数下通过
`-fsyntax-only`；Web Debug preset 已重编本 TU、归档并完成最终 Wasm/HTML link，
Wasmtime Headless Debug preset 也已重编并生成 `motionplayer` archive。唯一编译诊断
仍是仓库既有 `_tss` literal-operator 弃用 warning。当前 preset 不生成可直接运行的
`motionplayer-dll` 测试可执行文件，因此这里不会把“成功编译测试”误写成“已运行
测试”。
