# MotionPlayer `animating` 四参考二进制审计（2026-08-11）

## 结论

`Motion.EmotePlayer.animating` 与 `D3DEmotePlayer.animating` 都不是
`Player::_allplaying` / `Player::getAllplaying()` 的别名。四份当前参考共同给出一条
Engine 级查询：先检查三个直属变换控制器，再扫描活动 timeline 并建立“由 timeline
驱动的变量标签”集合，最后按固定顺序检查 selector、transition、eye、eyebrow、
mouth 控制器。D3D 表面只是从 shell 经 primary `EmoteObject` 取出同一个
`EmoteEngine` 后调用该查询。

旧本地实现的两处 `getAnimating()` 都直接返回 `player().getAllplaying()`，因此把
MotionNode 播放状态误当成控制器活动状态，也完全丢失 timeline 标签过滤、blend
控制器和一次性 timeline 的边界。本轮已把两个 facade 都改接
`EmoteEngine::getAnimating_guess()`。

## 四端映射

| 目标 | Emote 属性注册点 | Engine 查询体 | D3D 属性注册点 | D3D 包装体 | UTF-16 `animating` |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x67E248` | `0x671378` | `0x52FAD0` | `0x530E18` | 共享 `0x14BEE44` |
| Android armeabi-v7a | `0x5617CC` | `0x55B18C` | `0x494488` | `0x495006` | Emote `0xD84A16`；D3D `0x4946FC` |
| iOS arm64 | `0x1001B5880` | `0x1001AE5D8` | `0x100232908` | `0x10023344C` | Emote `0x1019606D4`；D3D `0x1019701C2` |
| iOS armv7 | `0x1B549C` | `0x1ADE54` | `0x231568` | `0x232196` | Emote `0x1752A38`；D3D `0x176256E` |

四个查询体均已命名并应用类型：

```cpp
bool __fastcall EmoteEngine_getAnimating_guess(const void *self);
```

四个 D3D 包装体均已命名并应用类型：

```cpp
bool __fastcall D3DEmotePlayer_getAnimating_guess(const void *self);
```

字符串分别命名为共享的 `aAnimating_utf16_guess`，或平台上彼此独立的
`aEmotePlayerAnimating_utf16_guess` / `aD3DEmotePlayerAnimating_utf16_guess`。
类型应用后的四个查询体和四个包装体均已重新反编译，四份 IDB 已保存。

Android arm64 D3D 注册器附近还存在相邻的 `fadeOutTimeline` 包装；顺序与反汇编
共同证明 `animating` 的正确入口是 `0x530E18`，而不是相邻的 `0x530DFC`。

## ABI 布局交叉验证

下表列出查询体实际读取的 Engine 字段。Android 使用 libstdc++ deque 的
`start.cur` 风格字段；iOS 使用 libc++ deque 的 map/start/size 风格字段，所以
“查询体触及的 deque 字段”不能误当作跨 ABI 固定容器起始偏移。

| 目标 | HM3 | active labels begin/end | position / scale / color / angle | selector / transition / eye / eyebrow / mouth 查询字段 |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `+936` | `+1040/+1048` | `+1072/+1080/+1088/+1096` | `+656/+576/+256/+336/+416` |
| Android armeabi-v7a | `+468` | `+520/+524` | `+536/+540/+544/+548` | `+328/+288/+128/+168/+208` |
| iOS arm64 | `+584` | `+672/+680` | `+704/+712/+720/+728` | deque map/header `+392/+344/+152/+200/+248` |
| iOS armv7 | `+292` | `+336/+340` | `+352/+356/+360/+364` | deque map/header `+196/+172/+76/+100/+124` |

`color` 的字段紧邻 scale 与 angle，四个查询体却都从 scale 直接跳到 angle。这不是
结构识别遗漏，而是可观察语义：color 控制器活动不令 `animating` 返回 true。

## 四端共同伪代码

```cpp
bool getAnimating(const EmoteEngine *engine) {
    auto varActive = [](const EmoteVarController *ctl) {
        return ctl->state != 0 || !ctl->queue.empty();
    };

    if (varActive(engine->position)) return true;
    if (varActive(engine->scale)) return true;
    if (engine->angle->state != 0 || !engine->angle->queue.empty())
        return true;

    unordered_set<ttstr> timelineDrivenLabels;
    for (const ttstr &timelineLabel : engine->activeTimelineLabels) {
        auto found = engine->timelineMap.find(timelineLabel);
        if (found == engine->timelineMap.end())
            continue;

        TimelineState &state = found->second;
        if (!state.timelineData)
            continue;

        for (const TimelineTrack &track : state.timelineData->variableList)
            timelineDrivenLabels.insert(track.label);

        if (varActive(state.blendController) || state.loopBegin < 0.0)
            return true;
    }

    for (const SelectorEntry &entry : engine->selectors)
        if ((entry.ctl->selState != 0 || !entry.ctl->queue.empty()) &&
            !timelineDrivenLabels.contains(entry.label))
            return true;

    for (const TransitionEntry &entry : engine->transitions)
        if (varActive(entry.ctl) &&
            !timelineDrivenLabels.contains(entry.label))
            return true;

    for (const EyeEntry &entry : engine->eyes)
        if ((entry.ctl->trackState != 0 || !entry.ctl->queue.empty()) &&
            !timelineDrivenLabels.contains(entry.label))
            return true;

    for (const EyebrowEntry &entry : engine->eyebrows)
        if ((entry.ctl->trackState != 0 || !entry.ctl->queue.empty()) &&
            !timelineDrivenLabels.contains(entry.label))
            return true;

    for (const MouthEntry &entry : engine->mouths)
        if ((entry.ctl->state != 0 || !entry.ctl->queue.empty()) &&
            !timelineDrivenLabels.contains(entry.label) &&
            !timelineDrivenLabels.contains(entry.talkLabel))
            return true;

    return false;
}
```

这里的 `unordered_set<ttstr>` 使用插件既有的缓存 ttstr hash/equality。Android
反编译呈现默认约 10 bucket 的 libstdc++ 初始化，iOS 呈现 libc++ 的空 map/start
初始化；这正是同一份默认构造源码在两套 STL ABI 下的差异，不应硬编码成两个算法。

## 顺序、数据流和边界

1. 直属控制器顺序严格为 position、scale、angle；color 被刻意排除。
2. 活动 timeline map 使用 `find`，缺失 key 只被跳过，不插入默认 HM3 节点。
3. 找到 HM3 节点后，`timelineData == nullptr` 会跳过整个活动项：不收集 track
   标签，也不检查 blend 控制器或 `loopBegin`。2026-08-15 的 fresh 四端指令审计
   修正了本页旧版伪代码在这里留下的错误。
4. 只有 `timelineData` 非空时，才先把其全部 56B track 的 `label` 插入临时集合，
   再检查 blend 控制器和 `loopBegin`。blend 控制器只要 `state != 0` 或队列非空
   即活动；`loopBegin < 0` 也立即活动，对应尚未收口的一次性 timeline。
5. `timelineData` 已存在的正常 HM3 timeline 节点被假定拥有非空 blend 控制器。
   四端都直接解引用，没有 null guard；但这个生命周期前置条件不能错误地扩大到
   `timelineData == nullptr` 的尚未解析节点，因为后者会在解引用之前被跳过。
6. timeline 扫描完成后，固定检查顺序为 selector、transition、eye、eyebrow、
   mouth。只有控制器本身活动、且其输出不由活动 timeline 驱动时才返回 true。
7. mouth 有两个输出键；只有 `label` 与 `talkLabel` 都存在于 timeline 集合时才被
   完全抑制。任意一个缺失仍返回 true。
8. 查询不检查 color、hair/bust 三个物理目标控制器、三组弹簧、loop/clamp deque、
   blink 的自动眨眼 phase、Player `_allplaying` 或 MotionNode 子树播放状态。
9. 临时集合在所有 true/false 返回路径上正常析构并释放持有的 ttstr 引用。

## D3D 一跳包装与对象链

四端包装体没有 null guard，也没有访问 Player：

```cpp
// 64-bit
return engineGetAnimating(*reinterpret_cast<EmoteEngine **>(
    *reinterpret_cast<uintptr_t *>(self + 24) + 8));

// 32-bit
return engineGetAnimating(*reinterpret_cast<EmoteEngine **>(
    *reinterpret_cast<uint32_t *>(self + 16) + 4));
```

也就是 `D3DEmotePlayer shell -> primary EmoteObject -> EmoteEngine`。这与前序生命周期
审计得到的对象拓扑一致，并再次排除“D3D shell 自己缓存 animating”或“直接读
Player”两种旧猜测。

## 本地修正与回归覆盖

- `EmoteEngine.h/.cpp` 新增 `getAnimating_guess() const`，保持四端检查顺序、
  timeline `find`、空 `timelineData` 整项跳过、标签集合过滤、mouth 双键，以及
  timeline 数据存在之后的非法空 blend 生命周期边界。
- `EmotePlayer::getAnimating()` 与 `D3DEmotePlayer::getAnimating()` 都改为调用
  Engine 查询，不再调用 `Player::getAllplaying()`。
- 新增单元测试覆盖：初始 idle、color 排除、position/scale/angle、五类后置控制器、
  timeline 标签过滤、mouth 双键、blend 活动、负 `loopBegin`，以及空
  `timelineData` 即使同时拥有活动 blend 与负 loop 标记也整项跳过。
- 既有 D3D timeline 测试在停止全部 timeline 后改为期望 `animating == false`；
  原来的 true 只是旧 `_allplaying` 接线残留。

本轮源码通过 `Web Debug Build` 与 `Wasmtime Headless Debug Build` 的完整增量编译
和链接；紧接着再次调用两个 preset 均返回 `ninja: no work to do`。`git diff
--check` 无空白错误。原生 Catch2 结果将在仍在进行的首次 Windows vcpkg 测试配置
完成后补记。

2026-08-15 对空 `timelineData` 前置门的重新取证、源码修正和本轮验证记录见
`analysis/motionplayer_animating_filter_set_owner_short_circuit_four_binary_2026-08-15.md`。
