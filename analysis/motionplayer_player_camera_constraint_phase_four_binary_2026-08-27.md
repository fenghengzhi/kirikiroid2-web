# Player camera-constraint phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端完整映射

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6B93E0` | 314 |
| Android armv7 | `0x586228` | 370 |
| iOS arm64 | `0x10010F22C` | 270 |
| iOS armv7 | `0x10CA04` | 319 |

四端均 fresh decompile，并从offset 0读取完整disassembly；cursor全部 `done=true`。该函数是
`updateLayers`固定十个phase3 helpers中的第一项，发生在root清cameraConstraintDirty之后、vertex
phase之前。

## 2. gate与节点选择

preview为true或node count小于2时立即返回，不读取任何node。否则按physical deque order遍历
`[1,size)`：只处理nodeType 9且active slot `done=false`的camera-constraint node。

每个constraint用active slot中的raw target label调用共享raw-label resolver；miss时使用synthetic
root。resolver/parent index都无bounds或null recovery。函数没有TJS callback、Variant owner、分配或
logger；唯一可能的非普通边界来自损坏deque/label map与浮点环境。

## 3. type remap与九个accumulator

raw anchor type先按constraint自身accumulated flipX/flipY重映射到0..8：三轴各自为minimum/direct/
maximum。重映射只交换同轴的minimum/maximum方向，不改变direct mode。

每轴维护三组flag/value：

- minimum初值 `+FLT_MAX`提升为double；仅当target coordinate小于constraint coordinate时记录差，
  多项取更小/更负值；
- direct初值+0；每次mode命中直接覆盖，因此physical order中最后一个direct constraint获胜；
- maximum初值 `-FLT_MAX`提升为double；仅当target coordinate大于constraint coordinate时记录差，
  多项取更大/更正值。

比较是ordered `< > <= >=`。NaN不进入minimum/maximum候选；direct差可以原样成为NaN。

## 4. offset选择优先级

三轴分别执行同一选择：

```text
if hasDirect:  offset = direct
else if hasMaximum: offset = maximum
else if hasMinimum: offset = minimum
else offset = +0
```

这不是把min/max相加，也不选择absolute最小值。direct存在时无条件压过两种limit；maximum又压过
minimum。对应conditional-select/branch形状跨ISA不同，但共同结果和signed-zero/NaN operand顺序一致。

## 5. publication

只有 `offsetX != 0 || offsetY != 0 || offsetZ != 0` 时才：

1. 写Player `cameraConstraintDirty=true`；
2. 第二次遍历全部nonroot nodes；
3. 按X/Y/Z顺序把三个offset加到每个node accumulated position。

root永远不平移。若三个offset都是±0，dirty保持root在调用前清出的false，节点完全不写。ordered
`!=`使NaN视为非零，因而会置dirty并把NaN传播给全部nonroot nodes。第二个loop没有callback，正常
内存store不抛；损坏容器属于未防御边界。

## 6. ABI与本地对照

Android/libstdc++通过deque iterator-difference求live size，iOS/libc++读取保存count；record stride
仍为2632/2272/2648/2228。64位用更多conditional-select，32位用VFP compare/branch。无需在portable
source复刻offset或block公式。

本地 `updateLayersPhase3_CameraConstraint` 的preview/count gate、node filter、raw-label-or-root、flip
remap、九种mode、direct/max/min优先级、nonzero dirty gate和nonroot全量平移均逐项匹配。没有发现
需要修改的运行语义。

四个IDB已统一命名、添加注释/bookmark并保存。

## 7. 验证限制

本slice执行coverage严格12列、duplicate-ID与`git diff --check`。当前环境缺
CMake/Ninja/Emscripten正式工具链，不能声称unit/Web build通过。

