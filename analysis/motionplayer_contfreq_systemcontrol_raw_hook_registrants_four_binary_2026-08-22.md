# Motionplayer continuous-event 调度层与内建 raw-hook 注册者四端恢复

日期：2026-08-22  
纵切面：V288  
参考目标：

- `Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- `Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- `Kirikiroid2_1.3.9_iOS_arm64`
- `Kirikiroid2_1.3.9_iOS_armv7` 的 thin ARMv7 slice

本报告承接 V287 的 continuous-event vector、delivery 与 compressed software texture 生命周期，
继续向下闭合 `-contfreq` limit thread、platform `SystemControl` pump，并从四端
`TVPAddContinuousEventHook` 的全部 xref 穷举内建 raw-hook 注册者。旧 `libkrkr2.so` 地址和由它推导的
注释不作为本轮证据。

下文证据地址统一使用 binary-qualified 记法。为避免在宽表中重复完整文件名，四个稳定别名为：

- `AndroidA64` = `Kirikiroid2_1.3.9_Android_arm64-v8a.so`；
- `AndroidA32` = `Kirikiroid2_1.3.9_Android_armabi-v7a.so`；
- `iOSA64` = `Kirikiroid2_1.3.9_iOS_arm64`；
- `iOSA32` = `Kirikiroid2_1.3.9_iOS_armv7.thin-armv7`。

例如 `AndroidA64!TVPBeginContinuousEvent@0x906284` 同时绑定二进制、恢复语义与地址；`_guess`
仍保留在 IDB 名称中，表示名称来自四端结构对齐而非原始符号。

## 1. 结论

当前源码的核心执行语义与四个参考目标一致，本轮没有做“安全化”行为修复。新确认的高价值边界是：

1. `TVPBeginContinuousEvent` 并不是引用计数。每个 raw-hook append 都先调用 Begin；非零
   `-contfreq` 下，即使只是重复 hook，也会重写 interval、重置 next tick 并唤醒线程。
2. `-contfreq` 只在 command-line generation 改变时重读；新 generation 中若选项缺失，旧值继续保留。
   频率没有正数、上限或 interval-zero guard。
3. Begin 的零频率与非零频率分支不会停止另一种既存 scheduler。运行中切换 generation 可使
   SystemControl continuous pump 与 limit thread 同时处于 enabled/calling 状态；只有 End 同时停止两者。
4. limit thread 普通 End 只 disable、不销毁；对象由 priority `100` shutdown at-exit callback 最终析构。
5. SystemControl 的 continuous 状态是 bool gate；`-lowpri` 是独立的一次性缓存，只认精确的 `yes`，且
   真正的线程优先级 API 已被注释掉。continuous 只抑制 compact/rehash，不抑制 window `TickBeat`。
6. compressed texture 之外只有三类内建 raw-hook owner：Layer transition、`layerExMovie`、
   `MoviePlayerLayer/VideoPresentLayer`。四端 add xref 数为 Android 5/5、iOS 4/4；iOS 少一条只是
   `layerExMovie::start()` 被内联。
7. 三类 callback 都是对象内嵌的非 owning 次级子对象；vtable 首槽 thunk 把 callback `this` 调回 owner。
   raw registry 不 AddRef owner。
8. transition 与 layerExMovie 可以在同步 callback 中 remove+re-add；V287 已证明 delivery 读取 live
   size/base，因此新条目同轮可见，脚本可构造无界同轮链。
9. `MoviePlayerLayer::Play()` 无条件 append，`Stop()` 不 remove。重复 Play 会积累重复 callback；析构才
   remove-all。若 callback 路径重入 Play，同样可同轮增长。
10. `StartTransition` 在注册 hook 后才初始化 tick、调用 handler、写 `InTransition` 并 `Update(true)`；
    catch 只 Release 三个 local，不撤销 hook 或已发布成员。四端 remove xref 穷举也证明 Start 的 EH
    landing 不调用 remove。

## 2. `-contfreq` limit thread 函数映射

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| thread ctor | `AndroidA64!LimitThread::ctor@0x905F08` | `AndroidA32!LimitThread::ctor@0x6C6B88` | `iOSA64!LimitThread::ctor@0x1001D72D8` | `iOSA32!LimitThread::ctor@0x1D54F4` |
| complete dtor | `AndroidA64!LimitThread::complete_dtor@0x905FDC` | `AndroidA32!LimitThread::complete_dtor@0x6C6C1C` | `iOSA64!LimitThread::complete_dtor@0x1001D73EC` | `iOSA32!LimitThread::complete_dtor@0x1D5680` |
| deleting dtor | `AndroidA64!LimitThread::deleting_dtor@0x906060` | `AndroidA32!LimitThread::deleting_dtor@0x6C6C80` | `iOSA64!LimitThread::deleting_dtor@0x1001D747C` | `iOSA32!LimitThread::deleting_dtor@0x1D577C` |
| `Execute` | `AndroidA64!LimitThread::Execute@0x906084` | `AndroidA32!LimitThread::Execute@0x6C6C90` | `iOSA64!LimitThread::Execute@0x1001D7490` | `iOSA32!LimitThread::Execute@0x1D578C` |
| `SetEnabled` | `AndroidA64!LimitThread::SetEnabled@0x9061E0` | `AndroidA32!LimitThread::SetEnabled@0x6C6DB0` | `iOSA64!LimitThread::SetEnabled@0x1001D75BC` | `iOSA32!LimitThread::SetEnabled@0x1D5938` |
| Begin | `AndroidA64!TVPBeginContinuousEvent@0x906284` | `AndroidA32!TVPBeginContinuousEvent@0x6C6E48` | `iOSA64!TVPBeginContinuousEvent@0x1001D764C` | `iOSA32!TVPBeginContinuousEvent@0x1D5A24` |
| End | `AndroidA64!TVPEndContinuousEvent@0x906400` | `AndroidA32!TVPEndContinuousEvent@0x6C6F4C` | `iOSA64!TVPEndContinuousEvent@0x1001D7748` | `iOSA32!TVPEndContinuousEvent@0x1D5BAC` |
| shutdown release | `AndroidA64!ReleaseLimitThread@0x906460` | `AndroidA32!ReleaseLimitThread@0x6C6FB8` | `iOSA64!ReleaseLimitThread@0x1001D77AC` | `iOSA32!ReleaseLimitThread@0x1D5BF6` |
| priority-100 init | `AndroidA64!RegisterLimitThreadAtExit@0x430328` | `AndroidA32!RegisterLimitThreadAtExit@0x302690` | `iOSA64!RegisterLimitThreadAtExit@0x1001D77FC` | `iOSA32!RegisterLimitThreadAtExit@0x1D5C2A` |

### 2.1 对象布局

| 字段 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| complete size | `0x110` | `0x48` | `0x140` | `0xC8` |
| `NextEventTick` | `+120` | `+24` | `+144` | `+88` |
| `Interval` | `+128` | `+32` | `+152` | `+96` |
| event/condition | `+136` | `+40` | `+160` | `+104` |
| critical section/mutex | `+224` | `+48` | `+272` | `+176` |
| `Enabled` | `+232` | `+52` | `+280` | `+180` |
| `NativeEventQueue` closure | `+240` | `+56` | `+288` | `+184` |

Android 64 与 iOS 64 的 condition/mutex 实现和大小不同，32 位两端也采用各自 STL/thread ABI；字段角色、
构造顺序与状态机相同。构造从 suspended `tTVPThread(true)` 开始，初始化 event、CS、EventQueue，写
`Next=0`、默认 interval `65536000/60 == 1092266`、`Enabled=false`，然后 Allocate queue 并 Resume。

析构严格执行：

```text
Terminate -> Resume -> Event.Set -> WaitFor -> EventQueue.Deallocate
          -> CS/event/base reverse member destruction
```

shutdown release 仅在全局指针非空时通过 deleting dtor 销毁，返回后才把全局写零。四端注册优先级均为
`TVP_ATEXIT_PRI_SHUTDOWN == 100`。

### 2.2 `Execute` 共同伪代码

```cpp
while (!GetTerminated()) {
    uint64_t cur = TVPGetTickCount() << 16;
    uint32_t sleep;
    lock(CS);
    if (Enabled) {
        if (NextEventTick <= cur) {
            TVPProcessContinuousHandlerEventFlag = true;
            EventQueue.PostEvent(NativeEvent(65540));
            while (NextEventTick <= cur)
                NextEventTick += Interval;
        }
        uint64_t delta = NextEventTick - cur;
        sleep = (delta >> 16) + ((delta & 0xffff) ? 1 : 0);
    } else {
        sleep = 10000;
    }
    unlock(CS);
    if (sleep == 0) sleep = 1;
    Event.WaitFor(sleep);
}
```

pending flag、PostEvent 与 catch-up loop 都在 CS 内。锁 holder 的 EH 会在普通 C++ exception 时解锁；
平台 lock failure/terminate encoding 依 ABI 不同。线程本身没有包住整个循环的通用 catch。

### 2.3 `SetEnabled` 与数值边界

```cpp
lock(CS);
Enabled = enabled;
if (enabled) {
    cur = TVPGetTickCount() << 16;
    NextEventTick = ((cur + 1) / Interval) * Interval;
    Event.Set();
}
```

- 这是 `(cur+1)` 后向下取 interval 网格，不是常规 `ceil(cur/interval)*interval`；
- disable 不 signal event，worker 最迟在已排定 deadline 或 disabled 10 秒 wait 后观察到；
- frequency 为负时做 signed division，再把负商符号扩展/转换到 unsigned interval；
- `abs(frequency) > 65536000` 时商可以为零，随后 `SetEnabled` 除零；
- 没有 guard、clamp 或 rollback；`Enabled=true` 已在除法前写入；
- 新线程的 raw allocation 只有 ctor 成功后才发布全局，ctor 异常 landing 会 delete 未发布 allocation。

## 3. Begin/End 与 scheduler 交接

共同控制流为：

```cpp
if (ArgumentGeneration != TVPGetCommandLineArgumentGeneration()) {
    ArgumentGeneration = TVPGetCommandLineArgumentGeneration();
    tTJSVariant val;
    if (TVPGetCommandLine(L"-contfreq", &val))
        TVPContinousHandlerLimitFrequency = (tjs_int)val;
}
if (frequency == 0) {
    if (TVPSystemControl) TVPSystemControl->BeginContinuousEvent();
} else {
    if (!thread) thread = new LimitThread();
    thread->SetInterval(65536000 / frequency);
    thread->SetEnabled(true);
}
```

Option-absent 保留旧频率是静态变量自然结果，不是“回到默认”。duplicate Begin 的零频率路径由
SystemControl bool gate 吸收；非零路径仍重置 next tick。运行时 generation 从 nonzero 切到 zero 会启动
SystemControl、但旧 thread 保持 enabled；从 zero 切到 nonzero 会启动 thread、但旧 Continuous flag 保持
true。End 的源码顺序先 disable thread，再 End SystemControl，因此最终同时关停。

## 4. SystemControl 四端映射与数据流

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| ctor | `AndroidA64!SystemControl::ctor@0x92686C` | startup 内同构 ctor | `iOSA64!SystemControl::ctor@0x100342184` | `iOSA32!SystemControl::ctor@0x344A8E` |
| Begin | `AndroidA64!SystemControl::Begin@0x9268B4` | `AndroidA32!SystemControl::Begin@0x6D6FEC` | `iOSA64!SystemControl::Begin@0x1003421D0` | `iOSA32!SystemControl::Begin@0x344AC4` |
| End | `AndroidA64!SystemControl::End@0x9268CC` / merged tail | `AndroidA32!SystemControl::End@0x6D70B0` | `iOSA64!SystemControl::End@0x100342298` | `iOSA32!SystemControl::End@0x344BE8` |
| `ApplicationIdle` | `AndroidA64!SystemControl::ApplicationIdle@0x926A00` | `AndroidA32!SystemControl::ApplicationIdle@0x6D70D4` | dead-stripped | dead-stripped |
| `DeliverEvents` | `AndroidA64!SystemControl::DeliverEvents@0x926A60` | `AndroidA32!SystemControl::DeliverEvents@0x6D710C` | dead-stripped | dead-stripped |
| timer pump | `AndroidA64!SystemControl::PumpEvents@0x926A88` | `AndroidA32!SystemControl::PumpEvents@0x6D712C` | `iOSA64!SystemControl::PumpEvents@0x1003422B4` | `iOSA32!SystemControl::PumpEvents@0x344C00` |

64 位对象 size 为 `0x30`，iOS 32 为 `0x28`，Android 32 同构。constructor 在 body 前完成 timer member
构造；body 清 continuous、auto-console、四个 tick 与 MixedIdleTick，最后把 `TVPSystemControlAlive=true`。
参考中没有 destructor 把 Alive 改回 false。

Begin 首次把 continuous 写 true，调用空的 `CallDeliverAllEventsOnIdle`，并求值一次性 `-lowpri` getter；
End 只在 true 时写 false，并再次读取缓存 getter。被注释的 platform priority API 没有机器码。

`DeliverEvents` 的顺序是：continuous 时先写 pending flag；再由 `EventEnable` 决定是否调用
`TVPDeliverAllEvents`。`ApplicationIdle` 调 Deliver、返回 `!ContinuousEventCalling`，并把 rough tick 加入
MixedIdleTick。iOS 把这两个小函数内联/死裁，application loop 直接调 timer pump；源码级数据流相同。

timer pump 始终：termination housekeeping、entropy、DeliverEvents、遍历全部 window `TickBeat`。只有
compact level 5 与 rehash 受 `!ContinuousEventCalling` gate；比较保持 `>4000`、`>Last+1500`、modal
`>Last+4100` 的严格条件和原生 unsigned tick 算术。

## 5. raw-hook add xref 穷举

V287 的 compressed texture add 之外，没有隐藏的第四类 owner：

| 注册者 | Android A64 add site | Android A32 add site | iOS A64 add site | iOS A32 add site |
|---|---:|---:|---:|---:|
| layerExMovie `startMovie` | `AndroidA64!startMovie/add@0x5E4034/0x5E4074` | `AndroidA32!startMovie/add@0x50C6B4/0x50C6E0` | `iOSA64!startMovie/add@0x10029DD28/0x10029DD64` | `iOSA32!startMovie/add@0x2A2258/0x2A2280` |
| layerExMovie `start` | `AndroidA64!start/add@0x5E40C4/0x5E40E8` | `AndroidA32!start/add@0x50C70C/0x50C724` | inlined above | inlined above |
| Layer `StartTransition` | `AndroidA64!StartTransition/add@0x8152A0/0x815988` | `AndroidA32!StartTransition/add@0x639ABC/0x639F3E` | `iOSA64!StartTransition/add@0x100084824/0x100084DD0` | `iOSA32!StartTransition/add@0x829D4/0x83014` |
| compressed `GetPixelData` | `AndroidA64!GetPixelData/add@0x84E6B8/0x84E750` | `AndroidA32!GetPixelData/add@0x65BB84/0x65BBDC` | `iOSA64!GetPixelData/add@0x100329FD0/0x10032A068` | `iOSA32!GetPixelData/add@0x32F4B0/0x32F508` |
| `MoviePlayerLayer::Play` | `AndroidA64!MoviePlayerLayer::Play/add@0x934E18/0x934E3C` | `AndroidA32!MoviePlayerLayer::Play/add@0x6DFEF8/0x6DFF0E` | `iOSA64!MoviePlayerLayer::Play/add@0x100341BC8/0x100341BEC` | `iOSA32!MoviePlayerLayer::Play/add@0x3444B8/0x3444CE` |

Android 各有 5 个 code xref；iOS 各有 4 个。数量差只来自编译器是否保留 `layerExMovie::start` 独立函数。

## 6. Layer transition callback 与生命周期

| 项目 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| BaseLayer ctor | `AndroidA64!BaseLayer::ctor@0x7FC2D0` | `AndroidA32!BaseLayer::ctor@0x62BD8C` | `iOSA64!BaseLayer::ctor@0x100074544` | `iOSA32!BaseLayer::ctor@0x716E0` |
| callback offset | `+800` | `+584` | `+800` | `+580` |
| callback vtable | `AndroidA64!TransitionCallback::vtable@0x1A23998` | `AndroidA32!TransitionCallback::vtable@0x10BF6EC` | `iOSA64!TransitionCallback::vtable@0x101ADE7B0` | `iOSA32!TransitionCallback::vtable@0x18310FC` |
| callback thunk | `AndroidA64!TransitionCallback::thunk@0x8337AC` | `AndroidA32!TransitionCallback::thunk@0x64A538` | `iOSA64!TransitionCallback::thunk@0x100097524` | `iOSA32!TransitionCallback::thunk@0x95AB0` |
| InternalStop | `AndroidA64!BaseLayer::InternalStopTransition@0x815BD0` | `AndroidA32!BaseLayer::InternalStopTransition@0x63A0F0` | `iOSA64!BaseLayer::InternalStopTransition@0x100084F78` | `iOSA32!BaseLayer::InternalStopTransition@0x8321C` |
| InvokeTransition | `AndroidA64!BaseLayer::InvokeTransitionCallback@0x815FE0` | `AndroidA32!BaseLayer::InvokeTransitionCallback@0x63A3B8` | `iOSA64!BaseLayer::InvokeTransitionCallback@0x10008534C` | `iOSA32!BaseLayer::InvokeTransitionCallback@0x83610` |

thunk 不从 callback 地址做固定负偏移；它读取 callback `+8/+4` 的 Owner，再 tail-call InvokeTransition。
Start 在 callback 后一槽写 owner，然后仅当 `!TransSelfUpdate` append。

InternalStop 先写 `InTransition=false`、`TransCompEventPrevented=false`，再按 self-update gate remove，随后清
cache/source/handler，并同步发 `onTransitionCompleted`。因为状态和 hook 已先清，事件处理脚本可立即对同一
layer StartTransition；新 callback 被 append，V287 live iteration 可在同一 delivery 中继续调用它。

Invoke 即使 `TVPEventDisabled` 非零也会运行。若 handler 请求 stop，disabled 时只写 prevented；后续 enabled
callback 才进入 InternalStop/remove/event。该状态机四端完全一致。

## 7. layerExMovie callback 与生命周期

| 项目 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| ctor | object cluster | `AndroidA32!layerExMovie::ctor@0x50C20C` | `iOSA64!layerExMovie::ctor@0x10029D724` | `iOSA32!layerExMovie::ctor@0x2A1BBC` |
| callback offset | `+64` | `+44` | `+64` | `+44` |
| callback vtable | `AndroidA64!layerExMovie::callback_vtable@0x1A11038` | `AndroidA32!layerExMovie::callback_vtable@0x10B6228` | `iOSA64!layerExMovie::callback_vtable@0x1019B1978` | `iOSA32!layerExMovie::callback_vtable@0x177965C` |
| callback thunk | `AndroidA64!layerExMovie::callback_thunk@0x5E4574` | `AndroidA32!layerExMovie::callback_thunk@0x50CAB0` | `iOSA64!layerExMovie::callback_thunk@0x10029E220` | `iOSA32!layerExMovie::callback_thunk@0x2A27D0` |
| stopMovie body | `AndroidA64!layerExMovie::stopMovie@0x5E3B08` | `AndroidA32!layerExMovie::stopMovie@0x50C3AC` | `iOSA64!layerExMovie::stopMovie@0x10029D930` | `iOSA32!layerExMovie::stopMovie@0x2A1E90` |
| startMovie | `AndroidA64!layerExMovie::startMovie@0x5E4034` | `AndroidA32!layerExMovie::startMovie@0x50C6B4` | `iOSA64!layerExMovie::startMovie@0x10029DD28` | `iOSA32!layerExMovie::startMovie@0x2A2258` |
| callback body | `AndroidA64!layerExMovie::OnContinuousCallback@0x5E4478` | `AndroidA32!layerExMovie::OnContinuousCallback@0x50C9DC` | `iOSA64!layerExMovie::OnContinuousCallback@0x10029E144` | `iOSA32!layerExMovie::OnContinuousCallback@0x2A2688` |

thunk 从 embedded callback 固定减 owner offset。start 先 remove-all、写 `playing=false`，再 add、写 true；
startMovie 在 overlay Play 后执行这一步，再同步调用 onStart。stopMovie 保存旧 playing，Stop overlay、remove、
clear overlay，最后按旧值同步 onStop。

callback 先调 overlay `OnContinuousCallback(tick)`，锁 mutex 后把 `PostEvents` 三指针 range 整体 swap 到
local，解锁，再按 snapshot 处理 update/ended，最后 delete snapshot backing。callback 执行时 producer 新增
event 留在 live vector，下一轮处理。若 overlay 已清，callback 自 remove 并清 playing。

registry 不拥有 layerExMovie；其 destructor 依靠 stopMovie/remove 先 tombstone raw pointer。同步 onStart、
onStop、onUpdate、onEnded 仍能触发对象销毁或 restart，这些边界不能用 registry ownership 推断为安全。

## 8. MoviePlayerLayer / VideoPresentLayer callback

| 项目 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| callback offset | `+528` | `+376` | `+552` | `+416` |
| callback vtable | `AndroidA64!VideoPresentCallback::vtable@0x1A29668` | `AndroidA32!VideoPresentCallback::vtable@0x10C257C` | `iOSA64!VideoPresentCallback::vtable@0x1019B4938` | `iOSA32!VideoPresentCallback::vtable@0x177B474` |
| callback thunk | `AndroidA64!VideoPresentCallback::thunk@0x934A08` | `AndroidA32!VideoPresentCallback::thunk@0x6DFC48` | `iOSA64!VideoPresentCallback::thunk@0x100341838` | `iOSA32!VideoPresentCallback::thunk@0x344104` |
| callback body | `AndroidA64!VideoPresentLayer::OnContinuousCallback@0x934944` | `AndroidA32!VideoPresentLayer::OnContinuousCallback@0x6DFBA0` | `iOSA64!VideoPresentLayer::OnContinuousCallback@0x1003417A0` | `iOSA32!VideoPresentLayer::OnContinuousCallback@0x34407C` |
| VideoPresent dtor | `AndroidA64!VideoPresentLayer::dtor@0x93476C` | `AndroidA32!VideoPresentLayer::dtor@0x6DFF14` | `iOSA64!VideoPresentLayer::dtor@0x1003415CC` | `iOSA32!VideoPresentLayer::dtor@0x343DE8` |
| Movie Play | `AndroidA64!MoviePlayerLayer::Play@0x934E18` | `AndroidA32!MoviePlayerLayer::Play@0x6DFEF8` | `iOSA64!MoviePlayerLayer::Play@0x100341BC8` | `iOSA32!MoviePlayerLayer::Play@0x3444B8` |

callback 忽略 tick。仅在 `m_usedPicture != 0` 时读取 player clock/1e6，锁 picture mutex，按 current index 读取
picture pts，解锁；若 `pts <= clock`，虚调用 `OnPlayEvent(Update,null)`。四端 picture element stride 都是
56/40/56/36 字节的 ABI-specific layout，但控制语义相同。

Play 调 base Play 后直接 add，没有 guard、remove 或 dedup；Stop 不 remove。VideoPresentLayer destructor 先把
derived vptr 写回，再以 embedded callback 地址 remove-all，然后进入 base destructor。因此：

- 重复 Play 产生多个 live slot，每轮重复调用；
- Stop 后 callback 仍在，只是 player/picture 状态决定实际工作；
- re-entrant Play 可在当前 delivery 末端不断 append；
- 只有 destructor remove-all 收束所有重复项。

## 9. EH 与 ABI 差异

- Android A64/A32 与 iOS A64 采用普通 unwind landing；iOS A32 使用 SJLJ call-site switch；
- limit-thread constructor failure 都只 delete 未发布 allocation；
- raw hook add growth failure仍保持 V287 的 Begin side effect，无 scheduler rollback；
- StartTransition 的 catch 只处理 `pro/sop/handler` local。由于 add 之后的 remove xref 在四端都不存在，
  tick callback、Update 或脚本异常可留下已注册但 `InTransition` 尚未 commit 的半配置 callback；
- mutex holder/unlock 的具体符号和 failure path 随 libstdc++/libc++ 改变，不构成源码语义差异。

## 10. 本轮源码处理

没有改变控制流或容器实现，仅把四端已证实且容易被旧注释/常规重构误改的边界写回：

- `cpp/core/base/impl/EventImpl.cpp`：interval setter/数值边界、pending/post/catch-up lock、floor grid、disable
  不唤醒、option-absent sticky、duplicate Begin reset、双 scheduler overlap、shutdown-only destruction；
- `cpp/core/environ/win32/SystemControl.cpp`：`-lowpri` one-shot exact-yes、Begin bool gate、EventEnable 前 pending、
  continuous 只 gate maintenance；
- `cpp/core/visual/LayerIntf.cpp`：Start hook commit/EH 非回滚、InternalStop 同步重入 re-add、event-disabled
  prevented state；
- `cpp/plugins/layerExMovie.cpp`：remove-all/add、raw non-owning、PostEvents snapshot 与 callback restart；
- `cpp/core/movie/ffmpeg/KRMovieLayer.cpp`：destructor remove-all、Play duplicate/Stop-retain/reentrant append。

这些注释全部以四个 `reference/binaries` 为依据，不引用旧 `libkrkr2.so` 地址。

## 11. IDB 写回与冷读

四端均从 V287 packed canonical 开始，在 headless IDA 中写入 `_guess` 名称与四端证实的边界注释，先保存到
`out/idb-recovery/v288-contfreq-raw-hooks/candidate/`，关闭 writer 后再从 candidate 冷开。candidate 冷读至少
覆盖 limit-thread `Execute`、SystemControl Begin、Layer `StartTransition`、layerExMovie callback 和 Movie Play；
名称、函数注释、Hex-Rays 输出与 `auto_analysis_ready=true` 均通过后才发布。

| canonical packed IDB | size | SHA-256 |
|---|---:|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` | 368,556,262 | `680605B8598175F81380640C9B5B0BD9EE7329661FB2DDF0ED267C050FA4BF04` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` | 347,599,459 | `28E3A338EF69287878901DFCF969BA3EB79D1B699CC9E074D85041848BD8DDF5` |
| `Kirikiroid2_1.3.9_iOS_arm64.i64` | 337,114,373 | `62015513813A642AE7AB2778615AD61DEE2093B3F438A250A6B010B6F7D9CB96` |
| `Kirikiroid2_1.3.9_iOS_armv7.i64` | 378,836,784 | `ADD89109623213B07AAF9BDCF315B97F08D7D762DB8895258615B93D31381F10` |

Android A32、iOS A64、iOS A32 candidate 继续保留，三者 size/hash 与 canonical 完全相等。Android A64
candidate 在 cold-read、发布和 canonical 双哈希确认后，因当时磁盘空间不足被删除；V287 candidate 在整个
替换窗口中保留，canonical 最终 hash 如上，因此删除的不是唯一副本。

发布后又直接从四个 `G:\My Drive\reference\binaries` canonical 路径逐一冷开：四端 module、imagebase、
input mapping、Hex-Rays 与 auto analysis 均正确；每端重新读取 6 个调度/owner 关键点。最后关闭全部 worker，
`idb_list` 返回 `0`。

一个明确记录的 IDA 边界伪影是
`AndroidA64!layerExMovie::OnContinuousCallback@0x5E4478`：自动分析因前一 `clang_call_terminate` 的
noreturn/exception tail 表示，把 body 并入 `AndroidA64!sub_5E4364@0x5E4364`；但是
`AndroidA64!layerExMovie::callback_thunk@0x5E4574` 明确跳回 `0x5E4478`，入口序言、完整 body 与入口行注释
均可冷读。IDB 没有强拆重叠 function，以免把分析器伪影伪装成原始符号边界。

## 12. 构建与审计

- 依 `krkr2-build` 流程实测 GNU Bison `3.8.2`、CMake `4.4.2`；
- 第一遍 `cmake --build out/web/debug` 在 CMake regenerate 阶段失败，因为新 PowerShell 没有加载 emsdk，
  cache 中 toolchain 被解析为无盘符的 `/upstream/.../Emscripten.cmake`。这一步未进入源码编译；
- 设置 `VCPKG_ROOT=C:\Users\fengxuexin\Developer\vcpkg`、dot-source
  `C:\Users\fengxuexin\Developer\emsdk\emsdk_env.ps1`，并确认 `EMSDK_PYTHON` 指向 emsdk Python 后，执行
  `cmake --preset "Web Debug Config"` 和 `cmake --build out/web/debug`；configure/generate 成功，构建
  `31/31`、最终 `index.html` 链接成功；
- 固定产物存在：`index.html` 87,111 bytes、`index.js` 634,997 bytes、`index.wasm` 85,612,628 bytes、
  `vlfs.js` 42,548 bytes、`assets.zip` 7,858,873 bytes；
- warning 仅为项目既有 `_tss` literal、null-reference stub、enum switch、cocos2dx config 及 Emscripten
  pthread/JSPI/JS-library 警告，本轮五个注释翻译单元均成功编译；
- 所有 IDA worker 关闭后，审计四个明确目录中的 `.id0/.id1/.id2/.nam/.til`：删除 60 个仅由冷开生成的
  loose sidecar，共 4,296,317,742 bytes；复核 remaining count 为 0。它们不是 packed canonical，且可由上述
  已校验 `.i64` 再生成；
- 早期空间恢复还删除了一个 cold-open 失败、未发布的损坏 Android A32 candidate，以及 V286 的四个冗余
  `prepublish` packed copy（共约 1.43 GiB）。V286/V287 candidate 与四个 canonical 均有保留链；未删除参考
  binary、用户源码或唯一 IDB。

## 13. 后续边界

V287/V288 已闭合 continuous-event 从 registry storage、delivery live mutation、platform scheduler 到全部
内建 raw owner 的主链。下一步不再需要继续猜 hook 容器；更高价值方向是沿本轮确定的同步重入点，选取
transition provider、layerExMovie 脚本 event或 movie callback 的具体 owner teardown，验证 callback 内
销毁造成的 UAF 是否被更上层 TJS/native owner 规则阻断，并把可达链与单纯理论风险分开。
