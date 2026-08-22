# MotionPlayer prepared-item 构建/排序诊断隔离（四参考二进制，2026-08-14）

## 1. 结论

四个当前参考二进制的 recursive prepared-item builder 与 `prepareRenderItems` wrapper 都没有
motion-path、trace、snapshot、logger、格式化字符串或排序键诊断副本。recursive builder 的
直接调用集合只包含生产数据流：Variant 数值读取、type-3/type-4 child 递归、prepared-item
创建/重用、颜色与 source-clip 运算、mesh/vector copy、draw-affine 矩形变换，以及 raw pointer
vector append/insert。wrapper 则只执行 motion-content gate、neutral recursive append 和
stable sort。

本地旧 sidecar 在普通每帧路径中仍然：

- 每次 builder（包括所有 child recursion）无条件 `matchedMotionPath()`；
- 每次 child merge 无条件 `child->matchedMotionPath()`；
- wrapper 无条件再次解析 parent path；
- wrapper 无条件建立与 `mainList` 同长度的 `vector<double>` 排序前键值副本；
- 即使 logger/snapshot 未开启，也会先构造若干 `std::string`/`fmt` 参数。

本轮将这些行为全部收进一次缓存的 opt-in trace/snapshot control domain。默认 builder、child
recursion 和 wrapper 不再读取 motion-context Variant，不再创建 child-path/string 临时量，也
不再分配诊断 `double` vector；生产 prepared-item 数据流、stable-sort comparator 和平台 STL
buffer 策略均保持不变。

## 2. 四端 fresh mapping

| 目标 | recursive builder | size | prepare wrapper | size | native string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6BF714` | `0x17D0` | `0x6D2544` | `0x100` | 两函数均 0 |
| Android armv7 | `0x58B178` | `0xAEA` | `0x596DF0` | `0x8E` | 两函数均 0 |
| iOS arm64 | `0x1001148F8` | `0xCEC` | `0x100122F68` | `0xB8` | 两函数均 0 |
| iOS armv7 | `0x1123D8` | `0xB34` | `0x121FDC` | `0xE4` | 两函数均 0 |

这里的 string-ref 计数来自四份 recovery IDB 的 fresh function analysis，不沿用旧
`libkrkr2.so` 注释。四个大 builder 又按 100 instruction page 完整扫描，共覆盖：

| 目标 | instruction count | direct call instructions | diagnostic/string/path call refs |
|---|---:|---:|---:|
| Android arm64-v8a | 1507 | 51 | 0 |
| Android armv7 | 944 | 65 | 0 |
| iOS arm64 | 820 | 45 | 0 |
| iOS armv7 | 1034 | 50 | 0 |

共同 call-set 中可识别的生产 helper 包括：

```text
VariantObject_getIntByIndex_guess
VariantObject_getCount_guess
ParticleArray_getNativePlayerAt_guess
Player_appendPreparedRenderItems_guess  // self recursion
ensureNodePreparedRenderItem_guess
multiplyPackedColorWeights_guess
remapPackedColorsForSourceClip_guess
MeshPointVector_copy/assign...
transformAndRoundRectByDrawAffine_guess
PreparedRenderItemPtrVector_insertRange_guess
```

平台内联程度不同：Android arm64 内联了部分 color/rect/ensure helper，其余目标保留更多独立
call；但四端都没有 `basic_string`、Variant-to-string、`fprintf`、logger、fmt、stack trace、
path matcher 或 snapshot helper。

## 3. wrapper 临时分配边界

四个 wrapper 的完整 direct-call set 只有 recursive builder、各平台 stable-sort
temporary-buffer/driver、delete/unwind（按 ABI 存在差异）。它们都不遍历 main 创建第二份
`double` key vector。

native stable-sort 的 allocation 不能因“诊断隔离”而删除：

- Android arm64 使用 nothrow pointer buffer，失败时缩小请求并可降级到无 buffer path；
- Android armv7 使用 libstdc++ temporary-buffer helper；
- iOS 两端使用 libc++ driver，超过各自 128 pointer element threshold 才请求 buffer；
- 所有 buffer 都只保存 non-owning `PreparedRenderItem *`，不是 `double sortKey` 副本。

因此本地 `beforeSortKeys` 现在是 `optional<vector<double>>`，且只有 path-specific trace 已启用
时才 `emplace/reserve/push_back`。普通路径仍调用同一个 `std::stable_sort`，没有改变全范围排序、
相等键稳定性、trusted raw pointer comparator 或 NaN 边界。

## 4. recursive builder 的诊断 control domain

builder 入口现在只缓存两个 Web 总开关。二者都关闭时，`motionPath` 保持空串且不会调用
`matchedMotionPath()`：

```cpp
const bool traceEnabled = logoChainTraceEnabled();
const bool snapshotEnabled = logoSnapshotMarkEnabled();
std::string motionPath;
if(traceEnabled || snapshotEnabled) {
    motionPath = matchedMotionPath();
}
const bool traceForPath = traceEnabled && enabledForPath(motionPath);
const bool snapshotForPath = snapshotEnabled && snapshotForPath(motionPath);
```

实际源代码使用完整 namespace/helper 名；以上伪代码只突出 control domain。父 motion 的
`m2logo.mtn` 和 frame-window 条件也一次计算为 `snapshotWindow`。

child merge 在 recursion 返回后只在 `traceForPath || snapshotWindow` 时解析 child path。
snapshot 输出和 `prepare.childMerge` trace 分别受自己的缓存 Boolean 控制；默认路径不会为了
随后被 helper 丢弃的日志构造 `std::string("<none>")` 参数。

每 item 的 expected-corners、RGBA unpack、三组 `fmt::format`/trace check 都已经位于
`traceForPath` 分支；SNAPPREP 只位于 `snapshotWindow` 分支。这里没有移动 production
transform、paintBox/viewport、color remap、prepared-item publication 或 stencil composite
post-pass。

## 5. wrapper control domain

motion-content Void gate 仍然最先执行；false 路径不读取 diagnostic flags/path，也不触碰
caller vectors。非 Void 路径才读取 Web 总开关，并仅在至少一个开关开启时解析 parent path。

顺序保持为：

```text
content gate
diagnostic gates/path only if requested
recursive append
optional before-sort key projection only if traceForPath
stable sort whole caller main range
optional trace formatting
optional snapshot scan/output
return true
```

诊断开启时原有 trace/snapshot 内容和 frame filters 保留。诊断关闭时，wrapper 对 native
observable production state的行为仍是 `gate -> append -> stable_sort -> true`。

## 6. recovery IDB 回写

四份 recovery IDB 均已在 builder 与 wrapper function comment 中写入：

- string-ref 为零且完整 direct-call set 不含 path/trace/snapshot/formatting；
- child recursive call 自身不解析 motion path，Web child-path 是 opt-in sidecar；
- wrapper 的 allocation 属于 stable-sort pointer buffer，不存在诊断 double-vector。

另在各端一个 type-3 wrapper-child recursion call site 写入 child-path sidecar 边界。四份 IDB
已原位保存。

## 7. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 检查通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerRenderItems.cpp`，成功链接
  `libmotionplayer.a` 和最终 Web/Wasm 输出；
- source scan 确认 builder/wrapper 的 parent path 只在总开关内物化，child path 只在
  trace/snapshot 命中时物化，`beforeSortKeys` 只在 trace 分支 emplace；
- `git diff --check` 在文档写入后再次执行，结果记录于本轮计划状态。

本纵切面只恢复诊断 sidecar 与 native builder/wrapper 的隔离边界，不把四个不同 STL
stable-sort 实现强行统一成某一个参考平台的内部算法。
