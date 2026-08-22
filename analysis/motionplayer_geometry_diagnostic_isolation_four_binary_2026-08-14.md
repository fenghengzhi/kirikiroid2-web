# MotionPlayer vertex / ShapeAABB 诊断隔离（四参考二进制，2026-08-14）

## 1. 结论

`PlayerUpdateGeometry.cpp` 的两个 Web diagnostics 分属两个独立 native functions：

- quad vertex trace 位于 `Player_updateLayersVertexComputation_guess`；
- SNAPSHAPE 位于本地拆出的 `updateLayersPhase3_ShapeAABB`，对应独立
  `Player_updateShapeAABB_guess`。

四端 vertex functions 有生产 string refs/dispatch，因为 force-visible 路径会读取 TJS
`priorDraw`，并且 mesh/shape materialization 含生产属性写入；但完整 direct-call scan 没有
path、string-format、logger 或 trace。四端 ShapeAABB functions 更明确：0 string refs、0 direct
calls，只做 deque/node 字段读取、double/float bounds 计算、parent clamp 和指针 publication。

本地旧实现却在每个成功 materialize quad 的 node 内调用一次 `matchedMotionPath()`；ShapeAABB
也在每个 active type-7 node完成计算后调用一次 path conversion，即使 trace/snapshot 完全关闭。
本轮把 path/filter 缓存提升到各自 phase entry，默认 geometry 热路径不再读取 motion-context
Variant；duplicate expected vertices、src narrow、layer-label narrow 和 fprintf 仍只存在于命中的
opt-in branch。

## 2. 四端函数映射

### 2.1 vertex computation

| 目标 | function | size | instructions | direct-call instructions | production string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6B98D0` | `0x13EC` | 1265 | 58 | 4 |
| Android armv7 | `0x5866F8` | `0xD86` | 1108 | 49 | 0 |
| iOS arm64 | `0x10010F6AC` | `0xF88` | 961 | 41 | 9 |
| iOS armv7 | `0x10CE30` | `0xF58` | 1297 | 51 | 20 |

string-ref 数量的 ABI/optimizer 差异不表示算法分叉。共同生产 call-set 包括：

```text
evaluateBezierPatchVector_guess
MeshPointVector_copy/assign
buildBilinearMeshGrid_guess
setDispatchArrayRealAt_guess
setDispatchRealProperty_guess
setDispatchIntegerByteProperty_guess
unsigned division / allocation / unwind helpers as required
```

完整 call-set 没有 `basic_string`/fmt/printf、path conversion、narrow、logger、trace 或 snapshot。

### 2.2 ShapeAABB

| 目标 | function | size | instructions | string refs | direct calls |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6BB0A0` | `0x1D4` | 115 | 0 | 0 |
| Android armv7 | `0x587978` | `0x232` | 164 | 0 | 0 |
| iOS arm64 | `0x100110B20` | `0x1C0` | 112 | 0 | 0 |
| iOS armv7 | `0x10E274` | `0x1F8` | 143 | 0 | 0 |

zero-call 结论尤其排除了 `matchedMotionPath()`、snapshot environment gate、`detail::narrow`、
`fprintf` 和 frame-time query；native ShapeAABB 不接收/读取 diagnostic context。

## 3. 全编码 sidecar 文本排除

四个完整参考目标中分别搜索：

- `updateLayers.phase3.vertices`
- `vertex-pass output diverged`
- `SNAPSHAPE`
- `m2logo.mtn`

每个 term 都执行 IDA string search 与 ASCII/UTF-8、UTF-16LE、UTF-32LE byte search；四端所有
组合均为 0 matches。该结果配合 direct-call scan，区分了 vertex function 的生产 TJS member
strings 与本地 Web trace strings。

## 4. vertex trace control domain

phase entry 现在缓存：

```text
logoTraceEnabled
motionPath only if logoTraceEnabled
traceForPath once
```

node loop 中生产 quad 的八次 float store保持原位。之后只有 `traceForPath` 为 true 才：

- 构造第二份 `expectedVertices[8]`；
- 做逐元素 fabs compare；
- narrow active-slot `srcValue`；
- 构造两组 fmt strings 并调用 trace check。

旧实现虽然 expected/projection 已在 path-specific branch 内，但为了得到这个 branch condition，
每个 eligible node 都先 materialize path。现在一个 phase 最多转换一次。trace 开启时输出内容、
node admission 和 compare tolerance不变。

## 5. ShapeAABB snapshot control domain

ShapeAABB entry 现在只在 `logoSnapshotMarkEnabled()` 为 true 时 materialize parent path，并一次
计算 path filter、`m2logo.mtn` 与 43–50 frame window。node loop 只检查缓存 Boolean 与
`sn.index == 18`。

因此 snapshot 关闭时不再：

- 每 active type-7 node转换 path；
- 查询 path-specific gate；
- narrow `layerName`；
- 构造 SNAPSHAPE label/string 或调用 fprintf。

production bounds 顺序完全保持：unchecked parent/active-slot reads、double origin/axis ordering、
Z-after-Y、float narrowing、parent-priority clamp、最后 `clipAABB = shapeAABB` publication。

## 6. recovery IDB 回写

四份 vertex function comment 已记录 instruction/call/string 统计、四个 sidecar term 的全编码
零命中，以及 native 不构造 duplicate expected-vertex/src-narrow projection。

四份 ShapeAABB function comment 已记录 zero-call/zero-string 边界及“不读取 path/snapshot/
label/frame time”。四份 recovery IDB 均已原位保存。

## 7. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 检查通过；只有
  仓库既有 `_tss` warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerUpdateGeometry.cpp`，成功链接
  motionplayer 与最终 Web/Wasm 输出；
- source scan 确认 vertex path 不在 node loop 内、ShapeAABB path 不在 node loop 内，fmt/narrow/
  fprintf 仍位于各自缓存 gate；
- `git diff --check` 在文档与计划写入后执行，结果记录在本轮状态。

本纵切面没有改变 camera constraint、visibility、camera node、ShapeGeometry 或 mesh chain 的
生产算法；它只恢复 vertex/ShapeAABB 与 Web diagnostics 的数据流和异常边界。
