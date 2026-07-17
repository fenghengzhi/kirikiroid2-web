# DRACU START 主线程卡死分析（2026-07-12）

## 已确认并修复的独立架构偏差

- `EmoteEngine_progress @ 0x67D01C` 在 `0x67D060` 调用 `0x671764`。
- 反汇编参数：`X0=EmoteEngine*`（入口值未改写）、`W1=0`、`V0=original dt`。
- `0x671764` 已在 IDB 纠正为 `EmoteEngine_preProgress_guess`；其读取
  `EmoteEngine+936` HM3 与 `+1040/+1048` playing vector。
- Web 先前从普通 `Player::frameProgress` 调用该逻辑；`Player_progress_inner
  @ 0x6C106C` 无此调用边。现已删除错误调用并在 `EmoteEngine::progress`
  的 controller slice 之前恢复调用。
- `sub_687C80 @ 0x687C80` 证明 HM3 节点为 0x88B：16B 节点头 + 120B
  value；原本 104B opaque 记录已纠正。`sub_683E40 @ 0x683E40` 独立确认
  value 的 +0/+8/+16/+28/+96 所有权析构链。

## 运行验证

- Debug Web 全量构建通过。
- 修复前：点击 START 后进入 `main.psb` / `mono_loop.psb`，约一个短时间窗后
  Chromium 主线程完全不响应。
- 修复后：点击后 4 秒仍可响应并继续输出 title transition 日志；随后同一量级
  时间窗再次主线程硬卡死。卡死标签已立即关闭。
- 结论：`0x671764` owner/call topology 修复正确且应保留，但不是唯一卡死原因。

## 下一候选（尚未下结论）

1. `Player_progress_inner @ 0x6C14C4..0x6C14CC` 正向 loop-wrap：
   `do tick += loopTime-totalFrames; while(totalFrames<=tick)`。二进制与本地循环
   本身一致；若卡死在此，必须继续追 `_loopTime/_cachedTotalFrames` 的配对来源，
   不能给循环加 guard。
2. `PlayerUpdateParticles.cpp` emitter interval 循环与 child-motion 时间同步循环。
   必须通过卡死前最后 Player/数值探针或采样栈独立区分，禁止由键名推断。

## 第二项已实现、待运行验证的数据流修复

- `Player_initNonEmoteMotion @ 0x6B3708/0x6B3728` 只通过
  `Motion_propGetDouble @ 0x662668` 读取确切的 `loopTime` / `lastTime`；缺失或
  非数值时返回 0。
- 本地 `maybeRecordMotionClip` 原先会猜测 `frameCount/frames/length/end` 等
  `lastTime` 别名，还会猜测 `loop/repeat/is_loop` 并把缺失 `loopTime` 默认成 -1。
- 这些 fallback 已全部删除，恢复为两个确切键、默认 0，并继续在
  `Player::initNonEmoteMotionLike_0x6B365C` 成对写入。
- Web Debug 构建通过。

## 2026-07-13：完整包复测结论

- 标题后行为必须使用 `/Users/bytedance/Downloads/203.zip`；该 ZIP 约 3.5GB，
  含 `DRACU-RIOT!/` 下 58 个条目。`dracu_boot.zip` 只用于标题前验证，不能再用
  于 START 后结论。
- 当前精确 `lastTime` / `loopTime` 解析版本使用 `203.zip` 完成首次加载并到达标题。
- 点击 START 后约 4 秒黑屏过渡仍响应；冻结前已进入：
  - `main.psb`: `chara='STATUS'`, `motionKey='text'`；
  - `mono_loop.psb`: `鉄骨`、`窓` 两个 Player 持续走 same-motion 路径。
- 约 14 秒时截图调用超时、JS 执行环境被重置，确认主线程仍硬冻结；卡死页已立即
  finalize。故精确解析修复是有二进制证据的独立数据流纠正，但不是冻结根因。

## 下一轮诊断构建

已重新反编译并核对三个真实无界循环：

- `Player_progress_inner @0x6C106C`：正向 wrap 每轮加
  `loopTime-lastTime`；
- `Player_particleEmitterPass @0x6BF0DC`：timer 每轮加
  `lerp(60/prtFmin,60/prtF,random)`；
- child-motion 更新 `@0x6BE0C0`：仅父 delta<0 时每轮加
  `childLoopTime-childLastTime`。

本地循环条件/更新式与二进制一致。已在“更新量非正向收敛”入口增加临时
`HANGDIAG` 数值探针，不加 guard、不改变循环控制流；Web Debug 构建通过。下一次
`203.zip` 运行应以该探针定位上游错误 writer，定位后删除探针并修复真实数据流。

## 2026-07-13：精简复现包与候选排除

- 后续改用 `reference/xp3/dracu/dracu.zip`（约 706MB、52 个条目）；该包可进入
  同一标题并复现 START 后硬冻结。
- 三个 `HANGDIAG` 收敛探针在冻结前均未触发，随后截图调用超时并重置 JS 执行
  环境。因此已排除此前三个候选：
  1. `Player_progress_inner @0x6C106C` 正向 loop-wrap；
  2. `Player_particleEmitterPass @0x6BF0DC` emitter timer；
  3. child-motion `@0x6BE0C0` 负向 time-wrap。
- 卡死页已立即关闭。下一诊断版本按 freshly-decompiled
  `Player_progress_inner @0x6C106C` 与 `Player_updateLayers @0x6BB33C` 增加
  `HANGPHASE` entry/exit 边界探针，覆盖 progress 与 updateLayers 的 Phase1/2、
  十个 Phase3 调用。未移动调用、未改变参数或控制流；Web Debug 构建通过。
- 2026-07-17 清理：上述 `HANGPHASE` 探针的 enter/exit 已证明这些调用均正常
  返回，且后续性能轨迹已把现象收敛为长任务而非永久挂死；因此从运行时代码删除，
  不保留默认关闭的移植层开关。同期删除每帧命中的 `Player::onFindMotion PRTDIAG`
  日志，以及 progress-wrap、particle-timer、child-wrap 三个未触发的 `HANGDIAG`
  收敛探针；原有分支、调用顺序、循环表达式和字段写入不变。

## 标题菜单颜色旁路发现

- `mtndump` 从 `motion/title.psb` 成功导出 24 张源图，0 skipped，全部
  `decoded_bgra=0`。
- 五个按钮各自确有独立 `*_nomal` 与 `*_over` 源图；当前截图中 START 使用白色细线
  `start_over`，LOAD/EXTRA/CONFIG/EXIT 使用红边浅色填充的 `*_nomal`，选择状态与
  资产集合一致。
- 因源图解码完整且通道路径为 RGBA，若与 Android 仍有色差，下一步应比较
  MotionNode color/opacity → SourceCache color bake → blend/premultiply 合成链；不能
  通过替换贴图或手调常量修复。

## 仍存在的容器归属缺口

本地 EmoteEngine 已声明 HM3@+936 与 variant vector@+1040，但 population 尚未接通；
现有 timeline 运行态仍存于内嵌 Player 模型。此次恢复了真实 owner/call boundary，
未把该差异标为平台边界。后续需继续迁移真实 HM3 value 与 playing-vector 生命周期。

## 2026-07-13：Chrome Performance Trace 纠正“硬冻结”结论

用户提供的 `/Users/bytedance/Downloads/Trace-20260713T013832.json.gz`
包含 322,197 个事件。renderer main thread 在点击 START 后出现一段
10,654.667ms 的 `RunTask/requestAnimationFrame` 长任务，但该任务最终返回，后续帧
继续执行。因此上文由 10 秒自动化超时得出的“永久硬冻结 / 无限循环”结论已被证伪；
正确现象是一次超过自动化超时阈值约 0.65 秒的主线程同步长任务。

该长任务的 V8 CPU profile 热点集中在：

- `PSB::PSBList::toTJSVal() const`
- `PSB::PSBDictionary::toTJSVal() const`
- `TJSCreateArrayObject` / `TJSCreateDictionaryObject`
- `TJSAddObjectHashRecord` / `tTJSHashTable::AddWithHash`

调用尾链明确经过 `PSBFile.root` 的 NCB property getter `getRoot(...)`。本地旧实现读取
`root` 时创建 TJS Dictionary，然后递归把完整 PSB 树物化成 TJS Dictionary/Array。

libkrkr2.so 的对应实现与本地旧链不同：

- `PSBFile` NCB 注册：`sub_597F38 @ 0x597F38`，`root` property wrapper 为
  `sub_5981F8`；
- `sub_5981F8 @ 0x5981F8` 返回持有 PSB owner 与 root node 指针的自定义
  `iTJSDispatch2`；
- `sub_597854 @ 0x597854` 按字典 key 惰性读取 child；list 只特殊处理
  `count`；
- `sub_5976C4 @ 0x5976C4` 按 list index 惰性读取，负下标先加一次 count；
- `sub_59673C @ 0x59673C` 只立即转换标量；type `0x20/0x21` 的 list/dictionary
  child 继续包装成同类 dispatch。

本地 `cpp/plugins/psbfile/main.cpp` 已恢复上述 TJS 可见数据流：`getRoot` 不再递归
创建整棵 TJS 对象树，改为 `PSBValueDispatch` 的逐 key / index 惰性访问。Web Debug
构建通过；使用 `reference/xp3/dracu/dracu.zip` 的完整运行资源复测，点击 START 后
3 秒内进入列车场景并显示第一句文本，原 10.65 秒同步物化阻塞已消失。

标题按钮 hover 黑块仍可独立复现：移动到 START 后出现大块黑色矩形，移开可恢复。
它与 `PSBFile.root` 长任务不是同一问题。当前应继续对照 `sub_6C2334 @ 0x6C2334`
的 clip AABB 复制/条件仿射变换以及 `sub_6C4E28 @ 0x6C4E28` 的 alpha-mask 合成
消费链，不能把黑块归因于错误按钮贴图。
