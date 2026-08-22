# MotionPlayer 播放/普通初始化/建树诊断隔离四端复核（2026-08-14）

## 结论

本轮只把 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考产物当作原生实现真值，重新完整扫描了普通 motion 加载链中的
三个边界：

1. `Player_playImpl_guess`：播放门控、load、增量提交和类型分流；
2. `Player_initNonEmoteMotion_guess`：普通 motion 属性/容器初始化；
3. `Player_buildNodeTree_guess`：旧树释放、递归建树和 label 索引构造。

旧 `libkrkr2.so` 注释不参与结论。四端共同证明，这三个 production body 都没有
`PRTDIAG`、motion-path 文本化、字符串 narrow/format、logger 或诊断序号数据流；
`buildNodeTree` 也没有 per-node trace 投影。此前 portable 源码却在默认路径中：

- 无条件递增三个静态诊断序号；
- 在 `playImpl` 的 load 前后无条件调用 `matchedMotionPath()`；
- 在 `buildNodeTree` 尾部无条件物化 path；
- 即使最终不输出日志，也保留额外的字符串转换、临时对象、格式化入口和异常点。

现已把这组 Web 诊断 sidecar 收进显式 `logoChainTraceEnabled()` gate。trace 关闭时，
原有 load/result owner、字段提交、参数容器、旧树 teardown、node deque 与 label map 的
生产顺序没有改变，也不再触碰诊断计数器或 motion-context-to-string 路径。

## 四端函数边界

### `Player_playImpl_guess`

| 目标 | 入口 | 大小 | 完整指令数 | direct-call 指令数 | IDA 字符串 operand refs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6AF664` | `0x73C` | 459 | 83 | 5 |
| Android armv7 | `0x580158` | `0x302` | 281 | 40 | 6 |
| iOS arm64 | `0x100107540` | `0x3D4` | 236 | 38 | 5 |
| iOS armv7 | `0x104AE8` | `0x43A` | 386 | 42 | 14 |

四端完整 call-set 的共同 production 角色只有：

- Join 时的 `Player_resetMotionState_guess`；
- 按值字符串 owner 输入的 `Player_loadMotion_guess`；
- `ttstr`/`tTJSVariant` CopyRef、销毁、copy-assign 与 TJS 属性读取；
- `Player_initEmoteMotion_guess` / `Player_initNonEmoteMotion_guess`；
- load 失败分支的 `TVPAddLog_guess`；
- 平台栈保护和异常 unwind。

字符串 operand refs 只对应 production 的 `type`、`division`、`motionList`、
`motion not found ` 与 `/` 等宽字符串。表中的 ref 数不是逻辑字符串数：ARM32 的
PC-relative materialization 会让同一个 literal 出现多个 operand ref，AArch64 也可能
通过中间 helper 隐去某个直接 ref。四端都没有 path/trace/PRTDIAG literal。

### `Player_initNonEmoteMotion_guess`

| 目标 | 入口 | 大小 | 完整指令数 | direct-call 指令数 | IDA 字符串 operand refs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6B0A3C` | `0x61C` | 384 | 61 | 6 |
| Android armv7 | `0x580C28` | `0x24E` | 225 | 36 | 10 |
| iOS arm64 | `0x100108258` | `0x31C` | 189 | 32 | 7 |
| iOS armv7 | `0x1058F8` | `0x348` | 310 | 37 | 20 |

完整 call-set 只有 motion property acquisition、Variant owner 操作、parameter append/
finalize/list parse/select、`buildNodeTree`、`initVariables` 和平台异常支持。字符串 refs
只属于 `loopTime`、`lastTime`、`tag`、`priority`、`content`、`parameterize`、
`parameter` 与越界异常文本；没有诊断 logger 或 path consumer。

### `Player_buildNodeTree_guess`

| 目标 | 入口 | 大小 | 完整指令数 | direct-call 指令数 | IDA 字符串 operand refs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6B25D0` | `0x508` | 320 | 33 | 0 |
| Android armv7 | `0x581CC8` | `0x1D0` | 176 | 18 | 1 |
| iOS arm64 | `0x1001097C8` | `0x24C` | 142 | 15 | 1 |
| iOS armv7 | `0x107060` | `0x20E` | 207 | 19 | 3 |

该 wrapper 的共同 call-set 为 motion Object retain/release、
`Player_resetAndReleaseOldNodeTree_guess`、递归 builder、Variant count、raw-label lookup、
label map/vector/deque 操作与异常支持。三个有直接 string operand 的目标只引用 production
`layer` 属性；Android arm64 由其具体代码生成方式隐藏该直接 operand ref。四端都没有
`matchedMotionPath` 等价调用、logger、format、`fprintf` 或 per-node trace loop。

## 三编码排除检索

按照宽字符串检索工作流，对每份 IDB 同时执行 IDA string search，以及 ASCII/UTF-8、
UTF-16LE、UTF-32LE byte search。以下六组文本在四端、三种编码中全部零命中：

- `PRTDIAG Player::playMotionLike`
- `PRTDIAG Player::initNonEmoteMotion`
- `PRTDIAG Player::buildNodeTree`
- `activePath`
- `after-ensure`
- `buildNodeTree.node`

这项排除与完整 call-set 相互独立：不仅没有相同 literal，也没有隐藏在无字符串 logger
wrapper 后的 path/format 调用。

2026-08-15 又对 RuntimeSupport 自身残留的旧单端说明做了 fresh 扩展排除：
`tracelogochain`、`traceLogoChain`、`-tracelogochain`、`snaplogo`、`logoChain` 在
四份当前 IDB 的 ASCII/UTF-8、UTF-16LE、UTF-32LE 中均为零命中；四端
`EmoteObject_init_guess` 与本节 `Player_playImpl_guess` 的完整 decompile/ref scan 也无
对应 query/trace/path-format 数据流。non-Emscripten 恒 false 与 Web 显式 opt-in 的边界
不变，旧 `libkrkr2.so` helper/地址注释已迁移。详见
`analysis/motionplayer_logo_trace_query_native_absence_four_binary_2026-08-15.md`。

## 原生生产数据流保持不变

本轮没有重新发明三个函数的业务语义；已有专项恢复仍是详细真值：

- `motionplayer_player_play_commit_state_four_binary_2026-08-14.md`：
  `playImpl` 的门控、按值 load owner、结果容器 retain、逐字段提交与异常边界；
- `motionplayer_init_non_emote_four_binary_2026-08-12.md`：
  property owner、parameter 容器、相邻状态字节提交和 Chain 尾部；
- node-tree/lifecycle 专项：旧树失效、layer-id 回收、root 保留/重建及子 Player owner。

本轮 fresh scan 再次确认其共同高层链：

```text
playImpl(borrowed motion, flags):
    duplicate-label / AsCan gates
    optional Join snapshot reset
    loadMotion(copy(live stealth chara), copy(requested motion))
    if Void:
        production "motion not found" log
        clear motion-content/context, clear playing
        return
    commit labels, result[0], result[1]
    retain committed motion Object
    read type
    dispatch to emote or ordinary initializer

initNonEmote(playFlags):
    acquire loop/tag/priority/root owners
    rebuild parameter containers and selected pointer
    commit syncWaiting=false, playing=true
    buildNodeTree()
    initVariables()
    apply non-Chain clock/queue tail

buildNodeTree():
    retain current motion Object
    reset/release old owned tree and layer IDs
    recursively build node deque
    enumerate layer data and publish label indexes
```

诊断隔离没有跨过这些生产语句，也没有为异常路径增加 rollback。

## 源码改动

### `PlayerTimeline.cpp`

- `playImpl` 只在全局 logo trace 已开启且 logger 可用时递增 sampled diag sequence；
- entry、load-before、load-after、call-init、exit 的 path/narrow/format/log 全部服从该 gate；
- load 前后的 path 比较仍只服务原有诊断，成功/失败业务判断仍只看 load result 的
  Variant type；
- duplicate-label、AsCan、Join、failure cleanup、label/result/property commit 和 initializer
  顺序未动。

### `PlayerCore.cpp`

- `initNonEmoteMotion` 的诊断序号与四组 PRTDIAG 输出进入同一 opt-in gate；
- production property getter、parameter vector/map、selected pointer、state bytes、建树、
  variable 初始化和 Chain tail 未改。

### `PlayerMotionLoad.cpp`

- `buildNodeTree` 缓存一次全局 trace gate；
- sampled sequence 与 `nodesBefore` 仅在 PRTDIAG 生效时读取；
- reset 后的 entry path、build 后的 path 与 per-node projection 不再在 trace 关闭时物化；
- trace 开启时，post-build PRTDIAG 与 path-specific node trace 复用同一个 post-build path；
- motion dispatch retain、旧树 teardown、递归 builder 与容器 publication 顺序未改。

## IDB 改进

四份 recovery IDB 的上述 12 个函数入口均追加诊断隔离注释，明确区分 production
call-set 与 portable Web sidecar。四库随后全部原位保存成功：

- Android arm64 recovery IDB；
- Android armv7 recovery IDB；
- iOS arm64 recovery IDB；
- iOS armv7 recovery IDB。

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten 翻译单元
  `-fsyntax-only` 成功；
- `Web Debug Build` 重新编译 `PlayerTimeline.cpp`、`PlayerMotionLoad.cpp`、
  `PlayerCore.cpp` 并完成 motionplayer 静态库、Wasm/`index.html` 最终链接；
- `git diff --check` 通过；输出仅含工作区既有 LF/CRLF 转换提示；
- 编译诊断只有仓库既有 `_tss` literal-operator 弃用、pthread/memory-growth、JSPI 与
  JS library warning。

这只是长期恢复目标中的一个 sidecar 边界闭合，不代表 motionplayer 已整体达到一比一。
