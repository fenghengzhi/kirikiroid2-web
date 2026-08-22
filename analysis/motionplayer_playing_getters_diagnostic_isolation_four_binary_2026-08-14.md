# MotionPlayer `playing` / `allplaying` getter diagnostic isolation（四参考二进制，2026-08-14）

## 1. 结论

`Motion.Player.playing` 与 `allplaying` 是两个不同的 native getter：

- `playing` 只读取当前 Player 的 local playing byte；
- `allplaying` 从 node index 1 开始扫描 type-3 nested-motion node，严格 unwrap child
  Player 并递归；任一 child 为 true 就立即返回 true，否则回退当前 Player 的同一个
  local byte。

四端两个 getter 都没有 motion context、path/string conversion、logger 或 format 数据流，
所有八个函数的 string reference count 都是 0。本地业务逻辑已经与该递归边界一致，但
三个返回分支此前会在 `logoChainTraceEnabled()` 为 false 时仍先执行
`matchedMotionPath()`，从而给最简单的属性查询增加 Variant-to-text、UTF narrow、分配和
异常点。

本轮不改变返回值、递归、container live-size reload 或 malformed child 边界，只把
PRTDIAG 所需 path construction 移入其全局 trace/logger gate。默认路径现在与四参考一样，
不接触 `_findMotionContextVariant`。

## 2. 四端映射与 local byte

| 目标 | `Player_getPlaying_guess` | body size | local byte | `Player_getAllplaying_guess` | body size |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D6B74` | `0x8` | `Player+1099` | `0x6CA214` | `0x164` |
| Android armv7 | `0x598FEA` | `0x6` | `Player+751` | `0x5924EC` | `0xE8` |
| iOS arm64 | `0x1001256C8` | `0x8` | `Player+987` | `0x10011CEA4` | `0x104` |
| iOS armv7 | `0x1248EC` | `0x6` | `Player+687` | `0x11B8C4` | `0xAE` |

`getPlaying` 的完整反编译在四端都只有：

```text
return player.localPlayingByte != 0
```

没有 prologue-owned temporary、callee、branch 或 child traversal。32/64 位 body size 差异
只来自指令集编码。

## 3. `getAllplaying` 的共同控制流

fresh 四端 decompile 可归一为：

```text
for index = 1; index < live nodes.size(); ++index:
    node = nodes[index]
    if node.type != MotionSub(3):
        continue

    child = strictUnwrapPlayer(node.childPlayerVariant)
    if child.getAllplaying():
        return true

return localPlayingByte != 0
```

共同边界：

- synthetic root/index 0 不参与递归；
- particle-system/type 4 child 不参与；
- 非 object Variant 在转换 helper 中抛出；
- object 为 null、类不匹配或 native instance 为空时，返回的 null 仍被递归调用，保留
  native malformed-tree crash/UB 边界；
- 当前 Player 的 local byte 只在所有合格 child 都为 false 后读取，因此 parent local
  true 不能掩盖更早的非法 type-3 child；
- Android arm64/armv7 与 iOS arm64 在 child 调用后重新 load deque size；iOS armv7
  把 entry size 保存在寄存器中并在回边复用。这个差异符合 const traversal 中没有受支持
  node-container mutation 的源码不变量；并发写本来就是 data race，非法 self-cycle 也不会
  从递归返回，不能把三端 reload 误写成四端共同可观察 API；
- child true 分支直接返回，不继续扫描后续 node。

直接调用只涉及 Variant 类型转换/native-instance unwrap 与 self recursion；没有
`TJSObjectToString`、`ttstr`/UTF conversion、motion-path helper、logger 或 format。两个
getter在四端均无字符串引用。

## 4. 本地诊断偏差

修复前 `getPlaying` 与 `getAllplaying` 的 child-true/local-fallback 分支形如：

```cpp
const auto motionPath = matchedMotionPath();
if((logoChainTraceEnabled() ||
    logoChainTraceEnabledForPath(motionPath)) && LOGGER) {
    ...
}
```

`logoChainTraceEnabledForPath(path)` 自身定义为
`logoChainTraceEnabled() && isTargetLogoMotionPath(path)`，所以布尔表达式实际上等价于
`logoChainTraceEnabled() && LOGGER`。它不会在全局 trace 关闭时输出，但 path 已经在
条件之前物化；而当全局 trace 开启时，左侧为 true，旧代码本来也会为所有 path 输出，
不会执行 target-only 过滤。

修复后保留这一 sidecar 可观察范围：

```cpp
if(detail::logoChainTraceEnabled() && LOGGER) {
    const auto motionPath = matchedMotionPath();
    ...
}
```

因此：

- 默认关闭：不读/转换 context Variant，不构造 `<none>` fallback，不调用 logger；
- 显式开启且 logger 存在：仍为所有 Player path 输出同样的 PRTDIAG，而不是擅自改成
  仅 logo target；
- getter 的递归次数、返回分支和 native child-invalid boundary 均未改变。

## 5. IDB 回写

四份 recovery IDB 的八个 getter 均追加 diagnostic-boundary 注释：

- local getter 是对应 Player byte 的完整 load/return body；
- aggregate getter 仅包含 live deque traversal、type-3 strict unwrap、递归和 local fallback；
- native return branches 不读取 motion context，不构造 path/string，不记录日志。

四份 IDB 已原位保存。

## 6. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerCore.cpp`，成功链接
  `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 既有 `Player playing and allplaying preserve local and recursive boundaries` regression
  仍覆盖 local false/true、type-4 排除、type-3 descendant true、parent-local fallback 与
  非法 type-3 Variant 即使 parent local true 仍抛出的顺序边界；完整测试 TU 编译证明本次
  gate 重排没有破坏该接口。

PRTDIAG 的开关来自 Emscripten JS 环境，不作为 native getter 的公开测试输入；默认路径
隔离由源码控制域、四端 0-string/direct-call 证据与上述完整构建共同固定。
