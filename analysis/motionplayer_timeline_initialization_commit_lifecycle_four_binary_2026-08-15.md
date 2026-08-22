# motionplayer timeline 初始化提交/owner 生命周期四参考复核（2026-08-15）

## 结论

### 2026-08-16 V143 nested ncb/source-identity 补充

本轮重新 fresh decompile 四端后，已把本文先前只描述为“临时 TJS owner”的访问层
闭合为精确 source tree：copied rawElement root accessor、direct variableList accessor、
retained indexed variable source + nested accessor、direct frameList accessor、retained
rawFrame source + nested accessor、可选 direct content accessor。所有 typed getter 都
消费写出值而不以 HRESULT 为 gate。portable 原先还错误地把 Track append 放在 frame
Count 前；现已移到 Count 成功之后。完整 helper/codegen、可重入 owner probe、Count
异常 probe 与 IDB readback 见
`analysis/motionplayer_timeline_initialization_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

本轮重新以 `reference/binaries/` 四份当前参考产物对
`EmoteEngine_initializeTimelineState_guess` 与
`EmoteEngine_initializeTimelineControllers_guess` 取 decompile、disasm 和 code xref，
没有沿用旧 `libkrkr2.so` 注释。源码的主算法已经与四端一致；本轮新闭合的是提交
顺序、两个函数不同的 null-owner 门、跳过轨道的 owner 保留，以及新/旧 controller
两条生命周期路径。

`initializeTimelineState` 会立即替换 decoded-data owner，随后按字段/轨道/帧渐进写入；
任一步骤失败都不回滚先前提交。`initializeTimelineControllers` 则先提交 flags，bit 1
关闭时不读取 data owner，bit 1 打开时才要求 data 非空。空轨和 instant 轨不会被清理，
普通非空轨才会原地 reset 旧 controller 或新建 count=1 controller。

## 四端函数与调用拓扑

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize state | `0x66D03C` / `0xBE4` | `0x5590E8` / `0x4E6` | `0x1001ABD5C` / `0x5D0` | `0x1AB4B0` / `0x5B6` |
| initialize controllers | `0x66DC20` / `0x16C` | `0x559848` / `0xA2` | `0x1001AC5DC` / `0x128` | `0x1ABDA4` / `0x154` |
| playTimeline caller | `0x670350` | `0x55AA70` | `0x1001ADE0C` | `0x1AD53C` |
| setTimelineBlend caller | `0x67098C` | `0x55ACDC` | `0x1001AE178` | `0x1AD918` |

fresh xref 的共同拓扑是：

- state initializer 只有两个业务 caller：`playTimeline` 与
  `setTimelineBlendController`；
- controller initializer 只有 `playTimeline` 一个业务 caller；
- `playTimeline` 的顺序是：必要时 initialize state -> initialize controllers ->
  `seekTimeline(state, 0.0)`；
- blend setter 的 lazy materialization 只执行 initialize state，不创建 track controller，
  也不执行 seek。

因此不能把 play 的最终 `flags/currentTime/cursor` 状态归因于 state initializer 本身。

## initializeTimelineState 的提交顺序

四端共同顺序如下：

1. 完整分配并默认构造平台自然 ABI 的 `deque<Track>` header；构造成功后立刻安装到
   `state.timelineData`，再析构/释放旧 owner。
2. 从 retained `rawElement` 依次读取并分别提交 `loopBegin`、`loopEnd`、`lastTime`。
3. 写 `blendWeight = 1.0f`、`autoStop = 0.0`。
4. 分配 `EmoteVarController(1)`；构造成功后安装到 blend owner并销毁旧 owner，然后以
   `blendWeight` 地址执行 duration/power为0、append=false的直接 reset。
5. 获取 `variableList` 并快照一次有符号 Count。Count小于1时不进入外循环。
6. 每个 variable 先获取它的 `frameList` 并快照frame Count，然后才向 TimelineData
   deque追加一个全默认 Track。
7. 新 Track 先读取/提交label，再对 Engine instant-variable unordered_set做一次成员
   测试，把结果快照到 `instantVariable`。之后修改set不会反向更新Track。
8. 每个 raw frame 先从数组取出，再向Track vector追加全零24字节Frame，然后依次读取
   `time`、`type`。最大时间只在两次读取都成功后用ordered `time > max`更新。
9. `typeZero = (type == 0)`；type 0不读取content，value/easing保留默认0。
10. action frame先读取content.value并从double窄化到float，再读取content.easing并写：
    `0 -> 1`、正数 -> `easing + 1`、负数 -> `1 / (1 - easing)`。NaN走最后一支并传播。
11. 全部外/内循环正常完成后，只有ordered `lastTime < 0`才用最大frame time覆盖。

这个函数不会写 `flags`、`currentTime` 或 `frameCursors`。通常新map值令它们处于默认值；
若调用者人为保留这些字段但把data owner清空，再触发lazy rebuild，旧值仍保留。只有
play后续的controller initializer/seek会分别覆盖flags和清空cursor/提交time 0。

## 失败前缀与对象生命周期

该builder是渐进提交而非transaction：

- 新 TimelineData 安装后，后续metadata PropGet失败不会恢复旧data；
- `loopBegin`成功而`loopEnd`失败时只保留loopBegin前缀；
- blend owner只有新controller构造成功后才替换，new-expression构造失败由C++异常清理
  raw allocation，旧blend owner仍在；
- Track emplace后label读取失败，会在新TimelineData中留下一个默认Track；
- Frame emplace后time/type/content/easing任一读取失败，会留下默认或部分填写Frame；
- value读取成功、easing读取失败时，float value已经提交；
- `lastTime < 0`的fallback只在整个扫描正常完成后执行。

临时TJS Variant/Object owner在四端都按嵌套作用域释放；异常表/landing pad负责对应的
展开。TimelineData和Track本身继续采用平台STL自然析构：Track依次释放ttstr、frame
vector与独占controller，外层deque释放blocks/header。

## initializeTimelineControllers 的精确门与保留行为

共同伪代码：

```cpp
state.flags = flags;
if ((flags & 2) == 0)
    return;

for (Track &track : state.timelineData->variableList) {
    if (track.frameList.empty() || track.instantVariable)
        continue;
    if (track.controller) {
        float zero = 0;
        setTarget(track.controller.get(), &zero, 0, 0, false);
    } else {
        track.controller.reset(new EmoteVarController(1));
    }
}
```

指令级共同点：

- 四端先store flags再测试bit 1；bit关闭时不读取 `state.timelineData`，所以null data安全；
- bit打开时紧接着读取data/deque header，没有null guard；失败时flags已提交；
- 短路顺序先判 `frameList.begin == end`，再读instant byte；
- 空轨/instant轨连owner slot都不读取，已有queue/state/current值完整保留；
- 旧owner路径以一个栈上float零调用通用setter：清queue、state=0并按controller自身count
  复制current array。正常结构不变式是count=1；函数不验证被破坏的count；
- null owner路径构造count=1 controller，构造器已把current/start/target三个数组清零，
  但powCount/phase/invDuration仍未初始化；该分支不再调用setter；
- 新controller只在构造成功后安装。函数不销毁被skip轨道的旧controller，也不会在
  后续flags关闭时回收先前创建的controller。

Android arm64把旧owner setter内联展开，其余三端保留调用；队列blocks释放、state清零、
current复制的效果一致。deque步长仍为64位56字节、32位28字节；Android block 504字节，
iOS block 4088字节。

## 源码/回归

源码的两个实现无需语义修改。新增回归
`EmoteEngine timeline controller initialization preserves skipped owners` 固定：

- flags=1在null data上只提交flags并安全返回；
- 空轨和instant轨保留原owner与queued command；
- 普通非空旧owner保持指针身份，但queue清空、state/current归零；
- 普通非空null owner获得count=1、空queue、idle、current=0的新controller。

绝对地址只保留在本文档和四份recovery IDB中；编译源码继续只使用语义名。
