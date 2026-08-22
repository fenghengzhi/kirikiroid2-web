# MotionPlayer Player `progress` bridge 四参考二进制闭环（2026-08-12）

## 1. 范围与结论

本记录只以 `reference/binaries/` 中四个当前参考产物为依据，重新反编译
Motion.Player 的脚本包装体和其唯一 native progress bridge。旧
`libkrkr2.so` 地址不参与函数身份判断；更宽的 frame-progress、绝对 reseek、
Join 快照和 HM1 重建证据见
`analysis/motionplayer_progress_reseek_four_binary_2026-08-11.md`。

四端共同结论如下：

1. 脚本 `progress(deltaMilliseconds)` 先解析 native instance，再检查至少一个
   实参；缺参返回 `TJS_E_BADPARAMCOUNT`。
2. 第一个参数无条件执行 `AsReal()`，随后乘 `60.0 / 1000.0`。没有 Void
   特判，没有负值、超大值、无穷或 NaN 钳位，也没有 `ensureMotionLoaded()`。
3. wrapper 把当前 `objthis` 作为原始、仅本次调用有效的 event dispatch owner
   传给 bridge；bridge 不对这个字段做 AddRef/Release。
4. bridge 的固定顺序是：暂存 dispatch、`frameProgress`、无条件
   `updateLayers`、递归 bounds、pending-event dispatch、清空暂存 dispatch。
5. bridge 与 event helper 都不清空 pending-event vector。vector 非空而 dispatch
   为 null 时，event helper 会在无条件 AddRef 处解引用 null；这是共同 native
   崩溃边界，不应在移植层静默吞掉。
6. bridge 没有 try/finally 式 cleanup。中间阶段或事件回调若以异常/长跳转方式
   离开，末尾的 dispatch 清空不会执行；源码不能擅自补 RAII 修复。

## 2. 当前四端映射

| 阶段 | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player progress bridge | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| TJS `progress` wrapper | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |

四端 IDB 中已统一采用以下源码级原型：

```cpp
void Player_progressBridge_guess(
    void *player, void *currentDispatch, double frameDt);

int Player_progressWrapper_guess(
    void *result, int numparams, void **param, void *objthis);
```

这些类型是为了让 Hex-Rays 稳定呈现共同数据流，不声称已经恢复原作者的精确
typedef 或成员类名。函数名仍带 `_guess`，符合未知原始符号名的约束。

## 3. wrapper 的共同控制流

四个 fresh decompile 可归一为：

```text
self = ncbGetNativeInstance(objthis)
if self == null:
    return invalid-object/native-class error

if numparams < 1:
    return TJS_E_BADPARAMCOUNT

frameDt = param[0].AsReal() * 60.0 / 1000.0
Player_progressBridge_guess(self, objthis, frameDt)
return TJS_S_OK
```

可观察边界：

- native-instance 解析早于参数数目检查。因此“无效 receiver + 缺参”先走无效对象
  路径，而不是 `TJS_E_BADPARAMCOUNT`。
- `result` 不被写入；额外参数被忽略。
- `param[0]` 不存在 Void/null 兼容分支；参数转换错误沿 TJS `AsReal()` 的既有边界
  传播。
- wrapper 不检查 motion 是否已加载，不新建 node tree，也不修正 delta。
- 毫秒到帧的换算只属于脚本 wrapper；EmoteEngine 内部调用已经使用 frame unit，
  不再换算。

## 4. bridge 的共同数据流与对象生命周期

四端 bridge 可归一为：

```text
player.currentDispatch = currentDispatch
player.frameProgress(frameDt)
player.updateLayers()
player.calcBoundsRecursive()
player.dispatchPendingEvents(player.currentDispatch)
player.currentDispatch = null
```

所有目标都无条件进入 `updateLayers()`；空 node tree 的行为由 update-layers 自身
承担，不属于 bridge 的前置门控。`calcBoundsRecursive()` 的身份由其共同函数体确认：
它重置 bounds，递归 child/particle Player，并遍历节点累积边界，不是只凭调用位置
猜测。

`currentDispatch` 是 Player 对调用者的临时借用，不是持有引用：

- bridge 写字段时没有 AddRef；
- pending-event helper 仅在 vector 非空时对该指针 AddRef，一次遍历期间保持引用，
  然后 Release；
- bridge 在 helper 返回后把字段写回 null；
- event callback 期间字段仍指向当前脚本 receiver；
- EmoteEngine 的内部 frame-unit 路径和 metadata 初始化路径传 null。因此只要这些
  路径产生 pending event，四参考实现就进入 null-deref 边界。移植实现保留这一点。

pending-event helper 复用一个 callback-result Variant：type 0 调用 `onAction` 并
传两个参数，type 1 调用无参 `onSync`；其他 type 不调用脚本。helper 遍历结束后
不 erase、不 clear vector。事件何时由生产侧替换或释放必须从其生产调用链继续恢复，
不能在 bridge 尾部臆造消费语义。

2026-08-13 后续纵切已补完该生命周期：layer/node 生产点都向这个唯一 vector
push；type-3 与 type-4 child 更新后把 child 的同一 vector 插到 parent.begin()，随后
只 clear child 并保留 capacity；root dispatcher 从不消费 parent 队列。循环保存 raw
元素指针，但每轮条件重新读取 live `end`，因此 capacity 内重入追加会在同一轮继续
派发，发生 reallocation 则沿用原生的迭代器失效/未定义边界。完整四端映射见
`analysis/motionplayer_pending_event_lifecycle_four_binary_2026-08-13.md`。

## 5. 本地源码对齐

本轮实施了以下语义修正：

- `progressFramesLike_0x6D2A54(double)` 改为语义名
  `progressFrames_guess(iTJSDispatch2 *, double)`，显式接收临时 dispatch owner。
- bridge 按四端共同顺序写 `_currentDispatch`、执行四阶段、派发事件并清空字段。
- EmoteEngine progress 与 metadata 初始化分别以 `(nullptr, originalDt)` 和
  `(nullptr, 0.0)` 进入 native-shaped frame bridge。
- `progressCompatMethod` 删除 `ensureMotionLoaded()`、Void/null 参数特判以及
  `[0, 60000]` 范围钳位，改为直接 `param[0]->AsReal()`。
- wrapper 保存 `objthis` 作为 dispatch owner，并保持 pending-event vector 不消费。
- 源码中与这一调用链相关的旧单库地址和“wrapper 会钳位”说明已改为四端语义描述；
  当前地址只保留在本分析文件。

2026-08-16 的 fresh wrapper/bridge/xref 源结构复核又确认：四端不存在第二个
`progressMillisecondsCompat_guess(double)` C++ member。脚本 raw wrapper 自己无条件执行
`AsReal() * 60 / 1000` 后直达 bridge（Android arm64 把短 bridge 内联），而 Engine 的
两个 caller 直接传 frame unit。本地这个未注册、零 production caller、仅两处测试使用的
convenience 还额外实现了参考中不存在的负值/60000ms 钳位，现已删除；测试改为显式传入
换算后的 frame unit 调用真实 `progressFrames_guess` bridge。删除证据与四端 xref 表见
`analysis/motionplayer_player_progress_dead_convenience_four_binary_2026-08-16.md`。

诊断构建仍可在 wrapper 的 native 阶段之间输出 gated trace/snapshot；这些诊断默认
关闭，不参与四参考运行语义。2026-08-14 的后续检查发现旧 sidecar 虽然在 helper 内
门控，调用前仍无条件物化 motion path，并 eager 求值两个 `fmt::format` 实参；现已把
argument materialization 一并移入 opt-in gate。完整证据见
`analysis/motionplayer_player_progress_diagnostic_isolation_four_binary_2026-08-14.md`。
日志位置标签仍使用 `Player.progress`，避免把旧目标地址传播成当前身份。

## 6. IDB 改进

四端 bridge 与 wrapper 均已：

- 重命名为统一的 `_guess` 语义名；
- 写入统一的 32/64 位无关源码级原型；
- 强制失效 Hex-Rays 缓存并重新反编译；
- 添加共同调用顺序、dispatch 借用关系、无 event clear 和无 cleanup 的注释。

保存后的 IDB 可直接从 wrapper 追到 bridge，再追到 frame core、update-layers、
recursive bounds 与 event helper，不再依赖旧 `libkrkr2.so` 地址注释。

## 7. 验证

本轮完成：

- Web motionplayer 静态库重编译：通过；
- Wasmtime motionplayer 静态库重编译：通过；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 的 Emscripten
  `-fsyntax-only`：通过，仅有仓库既有 `_tss` 弃用警告；
- Web `index.html` 完整链接：通过；
- Wasmtime guest 完整链接及 exnref 转换：通过；
- 相关文件 `git diff --check`：通过，仅有工作树既有 LF/CRLF 提示。

当前 CMake 配置没有可直接运行的 Catch2 motionplayer 测试目标，因此这里只声称
测试翻译单元通过编译，不虚构 runtime test 结果。

## 8. 后续边界

本纵切已经关闭 2026-08-11 文档中的“frame-unit bridge 缺少 current-dispatch owner”
继续项。2026-08-13 的 pending-event 生命周期专项又关闭了生产点、转移/释放时机、
重复派发以及 callback 异常展开问题：dispatcher 自身会释放局部 Variant 与 retained
dispatch，但更外层 bridge 没有 cleanup，异常时 `_currentDispatch` 保持原指针。当前
MotionSubNode 与 particle-child 两个后续四端专项又证明：child worker 直接调用
`child.frameProgress(parent.deltaTime)` 和 `child.updateLayers()`，不经过 child 的
progress bridge，也不读写 child `_currentDispatch`；随后 child event vector 被前插到
parent 并清空。故 child dispatch owner 与 parent 隔离，事件最终统一由最外层 parent
bridge 的 owner 派发。

`play`/load/emote-init 后续四端专项也已关闭最后一项：play wrapper 在 native play
调用前写 raw `_currentDispatch`，primary load、同一调用栈内的 emote 二次选片均复用
它；ordinary init 不再次调用 load helper。wrapper 尾部才清零且没有 cleanup，因此
callback/property/find/init 任一步异常都会跳过清零并留下原 raw 指针。这个危险状态
与 progress callback 异常后的字段残留同构，不得用 RAII 修复。至此本节早期列出的
四个后续项均已有四端闭环。

已关闭部分的完整映射见
`analysis/motionplayer_pending_event_lifecycle_four_binary_2026-08-13.md`；不得继续把
本节早期的四项列表当成当前开放状态。
child 路径的直接调用证据见
`analysis/motionplayer_motion_subnode_four_binary_2026-08-12.md` 与
`analysis/motionplayer_particle_child_lifecycle_four_binary_2026-08-12.md`。
load 接力与仅两个直接 caller 的证据见
`analysis/motionplayer_progress_reseek_four_binary_2026-08-11.md`、
`analysis/motionplayer_emote_init_four_binary_2026-08-12.md` 和
`analysis/motionplayer_load_callers_four_binary_2026-08-12.md`。
