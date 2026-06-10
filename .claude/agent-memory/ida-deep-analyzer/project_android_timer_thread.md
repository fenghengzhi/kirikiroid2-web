---
name: android-timer-thread-architecture
description: libkrkr2.so 的 TJS Timer 由独立 pthread 驱动（tTVPTimerThread），非 cocos2d 调度器。线程在 +56 字段=固定点 ms*65536 下用 pthread_cond_timedwait 唤醒，按真实墙钟 TVPGetRoughTickCount32 计算应 fire 次数，通过 EventSystem PostEvent 0x10001 在主线程拉起。
metadata:
  type: project
---

# Android tTVPTimerThread 架构（libkrkr2.so）

## 全局对象
- `qword_1AFE830` = singleton `tTVPTimerThread*`（懒加载，首个 Timer.enabled=true 时创建）
- `qword_1AFA738` = singleton tick-count anchor（TVPGetRoughTickCount32 用）

## 类布局（tTVPTimerThread，对象大小 0x130 = 304 字节）
- +0   vtable (off_1A31FB0)
- +8   bTerminate 标志
- +16  pthread_t（来自 tTVPThread 基类）
- +24  pthread_mutex_t（基类用，启动互斥）
- +64  std::condition_variable + mutex（基类的 startup 等待）
- +112 bStarted（pthread 已离开 wait）
- +120/+128/+136 std::vector<tTJSNI_Timer*>（所有已注册 Timer 实例）
- +144/+152/+160 std::vector<tTJSNI_Timer*>（pendingQueue，已积累 tick 待 fire 的 timer）
- +168 bEventEnqueued（已 PostEvent 标志，去重）
- +172 pthread_cond_t（Timer 线程自己的 wakeup 条件变量）
- +220 mutex（Timer 线程 wakeup 用）
- +264 EventSystem reference（PostEvent 通过它推到主线程）

## tTJSNI_Timer 类布局（对象大小 0xA8 = 168 字节）
- +8   iTJSDispatch2* owner
- +32  uint16 fire serial（每 fire 一次 ++，用作 event tag 防重）
- +36  uint32 capacity（一帧最多 fire 次数，默认 0xFFFF）
- +48  int mode（0/1/2 影响事件优先级：0→16，1→48，2→80）
- +56  uint64 interval_fixed（毫秒×65536）
- +64  uint64 next_fire_tick_fixed（下一次应 fire 时刻，毫秒×65536）
- +72  uint32 pending_count（已累积待 fire 次数）
- +76  bool enabled

## 线程主循环（tTVPTimerThread::Execute @ 0xA36C70）
每次唤醒：
1. `tick = TVPGetRoughTickCount32() << 16`（毫秒，转固定点）
2. 遍历 vector<Timer*>，对 enabled && interval>0 的：
   - `if (tick > next_fire) {`
   - `   count = (tick - next_fire) / interval + 1;`
   - `   if (count < 0x29) next_fire += interval * count;`
   - `   else { next_fire = interval + tick; count = 1; }`  // 防失控
   - `   timer.pending_count += count;`
   - `   timer.attachIfNotYet();`  // 放进 pendingQueue
   - `   bHasFiring = true;`
   - `}`
   - 累计最小 next_fire-tick 到 minWait
3. 若 bHasFiring && !bEventEnqueued：`EventSystem.PostEvent(0x10001)` → 主线程 dispatch
4. `pthread_cond_timedwait(cv, minWait_ms)` — minWait 上取整到下个毫秒

## fire 主线程侧（FireQueuedTimers @ 0xA36AD4）
事件 0x10001 在主线程处理时：
- 遍历 pendingQueue，对每个 timer：
  - `tTJSNI_Timer_FireOnIdle(timer, timer.pending_count)`  // 一次性 fire 全部累积
  - `timer.pending_count = 0`
- 清空 pendingQueue，清 bEventEnqueued

## FireOnIdle @ 0xA2C2E4
对每个 timer 一次性发送 N 个 onTimer 事件：
- 通过 sub_8DD324（TJS PostEvent 系统）逐次入队
- 受 capacity 限制（默认 65535），超出会丢弃
- 不会被 Web 端那种 "一帧 fire 多次但只 update 一次" 的钳制吞掉

## 关键路径对比 Web
- **Android**: TJS Timer 独立 pthread + condvar_timedwait + 墙钟 tick，主循环掉帧不影响节奏
- **Web (libkrkr2.so 同等的 KrKr2/Win32)**: 5-level time-wheel ProgressAllTimer 每帧在 cocos update 中过 past 个 1ms 桶——掉帧时 past 暴涨，多个 timer 同帧 fire 但 actionmanager 的 lasttick 钳制让累积消失

## 重要：libkrkr2.so 中 NO `tTVPTimerImpl` / NO `ProgressAllTimer`
本地 cpp/core/utils/win32/TVPTimer.cpp 的时间轮实现 *不存在* 于 Android 版本。
Android 用的是这个独立 pthread + condvar 方案。

## 2026-06-11 全链路钉死（已 rename + set_comments + idb_save）
完整对照文档：analysis/Timer_Event_Dispatch_Chain_libkrkr2so.md
已重命名（命名权威=KiriKiri 类方法语义/onTimer 字面证据）：
- tTVPTimerThread_Execute @0xA36C70（n<0x29=41 截断, sleep clamp>=3）
- tTVPTimerThread_FireQueuedTimers @0xA36AD4（主线程侧, tag==0x10001 遍历 pendingQueue+144）
- tTJSNI_BaseTimer_FireOnIdle @0xA2C2E4（cap=LimitTimerCapacity?1:(t+36?:0xFFFF), flags 16/48/80, tag=(serial<<1)|1）
- TVPGetRoughTickCount32 @0xA2BF90（steady_clock::now()/1e6=CLOCK_MONOTONIC ms + 32→64 wrap）
- tTVPThreadEvent_WaitFor @0xA362DC（pthread_cond_timedwait, ms==0→无限 wait）
- tTVPTimerThread_ctor @0xA369BC（SetPriority(LimitTimerCapacity?3:5)）; tTVPThread_SetPriority @0xA361D0（case5=highest→sched_other prio2）
- TVPLimitTimerCapacity=byte_1AFA740, **.bss 无写入点→恒 0(false)** → 线程用 highest 优先级
- 跨线程: NativeEventQueue_PostEvent @0x906DCC → postImpl @0x9156D8（mutex+std::vector<tuple>, queue=qword_1AF4388）。非 ALooper/JNI。
- 消费泵每帧: TVPMainScene::update @0xAA0718 首行 → ProcessMessages @0x913FC0 → NativeEventQueue_processQueue @0x915570（swap 排空, trampoline @0xA379DC→FireQueuedTimers）→ TVPSystemWatchTimerTimer @0x927188 → TVPDeliverAllEvents @0x8DE7A0（排空 TVP 队列 qword_1AE2EC0, sub_8DF7AC(32) 按 priority 派发脚本 onTimer）。全部同帧。
- TVPPostEvent @0x8DD324 / TVPCountEventsInQueue @0x8DE46C（遍历 qword_1AE2EC0..off_1AE2EC8）
两个独立队列: qword_1AF4388=NativeEventQueue(跨线程), qword_1AE2EC0=TVP 事件队列(onTimer)。
本地 TimerImpl.cpp:110 Execute / TimerIntf.cpp:69 Fire 经对照逐行一致, 无架构偏离。
