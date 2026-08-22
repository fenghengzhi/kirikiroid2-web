# MotionPlayer `updateLayers` root invariant / diagnostic isolation（四参考二进制，2026-08-14）

## 1. 结论

四个当前参考二进制的 `Player_updateLayers_guess` 都把 node deque 的第 0 条记录当作
构造完成后的强不变量：函数入口清 `needsInternalAssignImages` 后直接解析并读写 root，
没有 `empty()`、begin/end 相等、size==0 或返回分支。空 node storage 属于无效 Player
状态，不是该函数负责恢复的边界。

四份完整函数的直接调用集和字符串引用也共同否定了本地旧实现中的另一条额外路径：
native `updateLayers` 不读取 motion lookup context，不把 `tTJSVariant` 转为 `ttstr`，不做
UTF-16→UTF-8 窄化，也不构造 motion-path 字符串。本地无条件调用
`matchedMotionPath()` 只是 Web 诊断 sidecar；即使日志关闭，它仍会转换持久 Variant，
可能分配并引入 native 不存在的异常点。

本轮因此：

1. 删除 `if(nodes.empty()) return`，恢复四端共同的 root 强不变量和 malformed-state 边界；
2. 只在 `logoChainTraceEnabled()` 已开启时才物化 `motionPath`；默认执行路径不再读取或
   转换 `_findMotionContextVariant`；
3. 保留显式开启 Web 诊断时的原有日志内容和目标 path 过滤；
4. 把仍声称诊断 sidecar 由旧 `libkrkr2.so` 验证的源码注释改为不依赖旧目标的描述。

## 2. 四端函数与入口证据

| 目标 | `Player_updateLayers_guess` | producer flag clear | root storage resolve | 第一次 root 写入 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B871C` | `0x6B8748`，`Player+0x265=0` | `0x6B8744`，取 `Player+0xC8` | `0x6B8758`，root dirty/complete byte |
| Android armv7 | `0x5856E0` | `0x585700`，`Player+0x19D=0` | `0x5856F8`，取 `Player+0xA0` | `0x58570C`，root dirty/complete byte |
| iOS arm64 | `0x10010E544` | `0x10010E570`，`Player+0x1F5=0` | `0x10010E574..0x10010E594`，`Player+0xA8/+0xC0` deque map/index | `0x10010E5A8` |
| iOS armv7 | `0x10BE5C` | `0x10BE7C`，`Player+0x15D=0` | `0x10BE84..0x10BEB0`，`Player+0x8C/+0x98` deque map/index | `0x10BEBC` |

Android 两端的 libstdc++ deque iterator 已在 Player 中保存直接 current pointer；iOS 两端
的 libc++ deque 要从 map 与 packed index 解析当前 block/slot。这个 ABI 形状不同，但四端
控制流一致：root storage 被无条件解析，随后 camera velocity 的非零分量立即把 root 的
状态 byte 置 1，并累加到 root evaluated transform。入口没有从 begin/end 算 size 后检查
零，也没有在第一次 root load/store 前跳到 epilogue。

共同源级边界可写成：

```text
needsInternalAssignImages = false
root = nodes[0]                 // precondition, not a checked lookup

if cameraVelocityX != 0:
    root.dirty = true
    root.posX += delta * cameraVelocityX
... Y/Z ...
run the remaining update phases
```

机器指令调度允许先 load root/current velocity 再做独立 flag store；这不改变无调用、无
别名入口区间的共同源级顺序。

## 3. 为什么 motion path 不属于 native 数据流

fresh full-function disassembly 得到的 direct call 集如下。地址仅用于逐端复核；相同语义
callee 已在恢复 IDB 中统一命名。

### Android arm64

`pow`、`memcpy`、`Player_interpolateVarTrackValues_guess`、
`MotionNode_rebuildLocalMatrix_guess`、`Player_evaluateTimeline_guess`、
`deformChildByParentBezierPatch_guess`、`Player_applyGroundCorrection_guess`，以及 phase-3 的：

- `Player_applyCameraConstraints_guess`
- `Player_updateLayersVertexComputation_guess`
- `Player_updateVisibility_guess`
- `Player_updateCameraNode_guess`
- `Player_updateShapeAABB_guess`
- `Player_updateShapeGeometry_guess`
- `Player_updateMotionSubNodes_guess`
- `Player_updateParticleEmitters_guess`
- `Player_updateParticleSystems_guess`
- `Player_updateAnchorFeedback_guess`

对应 call sites 从 `0x6B87E0` 到 `0x6B9088`。

### Android armv7

语义调用集相同；编译器另外发出 `__aeabi_memclr8`，而 memcpy 使用
`__aeabi_memcpy8`。对应 call sites 从 `0x5857A6` 到 `0x586026`。

### iOS arm64 / armv7

语义调用集同样相同，libc 函数分别表现为 `_pow/_memcpy` 与 `j__pow/j__memcpy`；对应
call-site 区间为 `0x10010E648..0x10010EFCC` 和 `0x10BF5E..0x10C84C`。

四份函数都满足：

- string reference count 为 0；
- fresh Hex-Rays 输出中不存在 `TJS`、`Variant`、`ttstr`、string/narrow 或 operator-new
  相关表达式；
- direct calls 中没有 Variant conversion、object-to-string、UTF 转换、diagnostic logger
  或 motion-path helper；
- phase-3 之后直接进入 node/parameter cleanup 和最终 state-byte stores。

因此，motion path 既不是 native phase 输入，也不是错误报告所需数据。它只能作为本地
可选诊断 sidecar，不能在日志关闭时改变默认分配、异常或 Variant 引用行为。

## 4. 本地偏差及修复后的数据流

修复前：

```cpp
auto &nodes = _nodes;
if(nodes.empty()) return;
const auto motionPath = matchedMotionPath();
const double currentTime = _clampedEvalTime;
```

这里有两个 native 不存在的可观察差异：

1. malformed Player 会静默返回，而且 `_needsInternalAssignImages` 已被清零、其余状态未
   更新；四端会在 root storage 上继续执行，空 deque 不受支持；
2. 正常 Player 每次更新都把持久 motion-context Variant 转成字符串，再检查日志开关。
   分配失败可在第一 native phase 之前抛出，且任意对象/数值 Variant 会被无意义地做文本
   表示。

修复后：

```cpp
auto &nodes = _nodes;
std::string motionPath;
if(detail::logoChainTraceEnabled()) {
    motionPath = matchedMotionPath();
}
const double currentTime = _clampedEvalTime;
```

默认日志关闭时，`motionPath` 保持空 SSO/default 状态，持久 Variant 不被读取或转换；后续
`logoChainTraceEnabledForPath(motionPath)` 立即为 false。显式开启 logo-chain trace 时，
路径仍按旧 sidecar 行为物化，随后用 `yuzulogo.mtn`/`m2logo.mtn` 过滤，因此现有调试能力
没有丢失。

本轮没有把 path 写入 Player、node 或 parameter 容器，也没有吞掉 native phase 抛出的
异常。诊断开启时产生的额外开销仍被明确限制在 opt-in Web 路径；它不被误称为参考实现。

## 5. IDB 回写

四份 recovery IDB 均已在函数入口、root resolve 和 producer-flag clear 指令处追加注释：

- root node 是构造后强不变量，无 empty recovery branch；
- 记录各 ABI 的 Player/root storage offsets；
- native 函数没有 motion-context/string diagnostic dataflow；
- Web diagnostic path materialization 必须留在默认执行路径之外。

四份 IDB 已分别原位保存。

## 6. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有仓库
  既有的 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 增量重编 `RuntimeSupport.cpp`、
  `PlayerUpdateLayers.cpp`，随后成功链接 `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 源码检索确认 `PlayerUpdateLayers.cpp` 不再包含 node-empty early return，也不再无条件
  物化 `matchedMotionPath()`。

这里没有为 empty deque 添加运行时单测：四参考共同边界本来就是无效状态上的直接 root
访问，构造一个 UB/death test 既不能稳定区分各平台，也会把错误的“受支持输入”含义写进
测试。现有 parameter-mode regression 会通过正常构造的 root 调用完整 `updateLayers`，覆盖
本次合法状态路径。
