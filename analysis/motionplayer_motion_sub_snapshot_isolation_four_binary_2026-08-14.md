# MotionPlayer MotionSub snapshot isolation（四参考二进制，2026-08-14）

## 1. 结论

四个当前参考二进制的 `Player_updateMotionSubNodes_guess` 不读取 parent/child motion path，
不构造 SNAP 字符串，也不调用 `fprintf`、logger 或 snapshot gate。它在 preview 状态直接
返回；正常路径只执行 type-3 child unwrap、replay/teardown、`src` slash split、child
transform/time publication、child `frameProgress/updateLayers` 和 event prepend/clear。

本地旧代码却在 preview test 之前无条件 `matchedMotionPath()`，所以即使 preview 或 Web
snapshot 完全关闭，每帧仍会转换 parent 的持久 motion-context Variant。两个可选 snapshot
block 内又分别调用 `child.matchedMotionPath()` 两次。

本轮把 preview return 恢复为最早业务 gate，随后只在 `logoSnapshotMarkEnabled()` 为 true
时物化 parent path 和计算 path-specific gate；两个 snapshot block 复用该 Boolean，并在
进入 block 后只物化一次 child path。默认 MotionSub phase 不再有 parent/child path
conversion 或相应分配/异常点。

## 2. 四端 fresh mapping

| 目标 | `Player_updateMotionSubNodes_guess` | size | native string refs |
|---|---:|---:|---|
| Android arm64 | `0x6BB4A0` | `0xD10` | 0；`'/'` 为字符 immediate |
| Android armv7 | `0x587E00` | `0x9D8` | 0；`'/'` 为字符 immediate |
| iOS arm64 | `0x100110EEC` | `0xB1C` | 1，唯一内容为 production split delimiter `"/"` |
| iOS armv7 | `0x10E68C` | `0xBE2` | 同一 `"/"` 的两个 xref |

iOS 编译器/library 把 slash delimiter 表现为一字符 string object，Android 路径把分隔符
作为 `tjs_char('/')`/immediate 传递。该差异只属于 `src` 解析，不能把 iOS 的 `"/"`
string ref 误认成 motion-path diagnostic。四份 fresh Hex-Rays keyword scan 均无
`log/trace/stack/motionPath/fprintf/SNAP/fmt/narrow/ToString/AsString` 表达式。

## 3. native phase boundary

共同高层控制流仍为：

```text
if player.preview:
    return

for each non-root node:
    if node.type != MotionSub(3): continue
    parameterMode = node.parameterEntry ? node.parameterEntry.mode : 0
    child = strict unwrap node.childPlayer
    childRoot = child.nodes[0]

    if parameterMode != 0 or node.accumulated.dirty:
        if active slot done or src owner null:
            teardown child
            continue
        if replay flag consumed:
            split src by '/'
            set child chara/play request
            synchronize queued time
        publish angle/origin/transform/visibility/color into child/root

    propagate clip/mesh/visible-ancestor metadata
    child.frameProgress(parent.deltaTime)
    child.updateLayers()
    prepend child events into parent and clear child vector
```

preview return 之前没有 path/string work；shared child step 前后也没有 diagnostic side
effects。teardown `continue` 跳过 shared step，与既有专项一致。

## 4. 本地修复

入口现在是：

```cpp
const double currentTime = _clampedEvalTime;
if(_preview) return;

const bool snapshotEnabled = logoSnapshotMarkEnabled();
std::string motionPath;
bool snapshotForPath = false;
if(snapshotEnabled) {
    motionPath = matchedMotionPath();
    snapshotForPath = logoSnapshotMarkEnabledForPath(motionPath);
}
```

`SNAPPLAY` 与 `SNAPCHILD` 都先检查 `snapshotForPath`、`m2logo.mtn` 和各自 frame range；
只有命中后才做 source/motion/path narrow。child path 由一个局部 owning `std::string`
保存并同时用于 empty test 与 `c_str()`，不再调用两次：

```cpp
const auto childMotionPath = child.matchedMotionPath();
fprintf(...,
        childMotionPath.empty() ? "<none>" : childMotionPath.c_str(), ...);
```

这也使 `c_str()` owner 的 full-expression 生命周期显式可读。snapshot 开启时输出内容、
frame window 与 child state 字段保持不变；默认关闭时 parent/child context 都不读取。

本轮没有改动 parameter mode、replay flag、src split、child invalid/root boundary、teardown
owner order、angle modes、shared child step 或 pending-event vector 生命周期。

## 5. IDB 回写

四份 recovery IDB 的 MotionSub 主函数已追加：

- 各目标 native string-ref 数量与 slash delimiter 身份；
- preview/production traversal 没有 motion-path/SNAP/fprintf/logging 数据流；
- Web parent/child path conversion 必须处于 opt-in snapshot control domain。

四份 IDB 已原位保存。

## 6. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerUpdateChildMotion.cpp`，成功链接
  `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 源码检索确认 preview return 早于 snapshot/path work，parent path 只在 snapshot 总 gate
  内物化，两个 child path 均只在各自命中的 block 内物化一次；
- 既有 MotionSub regressions 随完整 TU 编译，继续覆盖 replay/teardown、child invalid
  boundary、time/angle/visibility publication、shared child step 与 pending-event prepend。

snapshot 开关来自 Web JS 环境，不扩张为 native Player API；本轮只声称完整测试翻译单元
与 Web link 通过，不虚构当前 preset 不提供的 Catch2 runtime executable。
