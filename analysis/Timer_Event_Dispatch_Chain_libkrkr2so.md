# TJS Timer → 主线程派发 → onTimer 脚本执行（libkrkr2.so 逆向对照）

权威来源：libkrkr2.so（Android kirikiroid2，ARM64）IDA 反编译。本地 cpp 仅用于对照差异。
分析日期 2026-06-11。所有函数已在 IDB 重命名 + set_comments + idb_save。

## 链路总览

```
[Timer 线程 pthread]                          [主线程 cocos update 每帧]
tTVPTimerThread::Execute @0xA36C70             TVPMainScene::update @0xAA0718
  curtick = TVPGetRoughTickCount32()<<16         └─ TVPMainScene_ProcessMessages @0x913FC0
  遍历 List<Timer*>, 累积 pending(timer+72)          ├─ NativeEventQueue_processQueue @0x915570
  PostEvent(0x10001) ───跨线程───┐                  │    排空 qword_1AF4388, 调 std::function
  WaitFor(sleeptime, clamp>=3)   │                  │    → trampoline @0xA379DC
                                 │                  │    → tTVPTimerThread_FireQueuedTimers @0xA36AD4
   NativeEventQueue_PostEvent ───┘                  │       → FireOnIdle @0xA2C2E4 (n 个 onTimer)
   @0x906DCC → postImpl @0x9156D8                   │          → TVPPostEvent @0x8DD324 入 TVP 队列 qword_1AE2EC0
   mutex+std::vector<tuple> 投递                     └─ TVPSystemWatchTimerTimer @0x927188
                                                          → TVPDeliverAllEvents @0x8DE7A0
                                                             排空 qword_1AE2EC0 → 脚本 onTimer 方法
```

两个独立队列：
- `qword_1AF4388` = NativeEventQueue（线程→主线程跨线程投递，0xA0 字节，sub_4300C0 创建）
- `qword_1AE2EC0..off_1AE2EC8` = TVP 事件队列（onTimer 事件，TVPDeliverAllEvents 派发）

---

## A. tTVPTimerThread::Execute @0xA36C70（线程主循环）

骨架：
```c
while (!bTerminate(+8)) {
  v2 = TVPGetRoughTickCount32(); tick = v2 << 16;          // 固定点 ms
  for (timer : List<Timer*>(+120..+128)) {
    if (!timer->enabled(+76)) continue;
    interval = timer+56; if(!interval) continue;
    next = timer+64;
    if (tick > next) {
      n = (tick - next)/interval + 1;
      if (n < 0x29) { next += interval*n; timer+72 += n; }   // n<41
      else          { next = interval + tick; timer+72 += 1; } // 防失控,丢弃
      hasFiring = 1; timer+64 = next;
      attachToPending(singleton, timer);                      // 放进 pendingQueue(+144)
    }
    minWait = min(minWait, next - tick);
  }
  if (hasFiring && !bEventEnqueued(+168)) {
    bEventEnqueued = 1; ev.tag = 0x10001;                     // 65537
    NativeEventQueue_PostEvent(this+264, &ev);                // 跨线程推主线程
  }
  sleep = (minWait>>16) 上取整到下个ms;                        // HIWORD + (LOWORD?+1)
  if (sleep <= 3) sleep = 3;                                  // TVP_LEAST_TIMER_INTERVAL
  WaitFor(this+172, sleep);
}
```

与本地 TimerImpl.cpp:110 一致性：
- `<<16`（TVP_SUBMILLI_FRAC_BITS=16）✔
- 补发 `n=(curtick-next)/interval+1` ✔（二进制 `+1` 在除法后）
- `n>40` 截断（`n<0x29`=`n<41`，即 `n>=41` 走截断）→ 截断分支 `next=tick+interval; count=1`，本地 `Trigger(1); SetNextTick(curtick+interval)` ✔
- sleeptime clamp 下限 3 ✔
- **差异（非偏离）**：本地用 `Event.WaitFor(sleeptime)`（Win32 event），二进制用 `pthread_cond_timedwait`（见 F），语义等价。
- **差异（平台）**：本地 `if(List.size()==0) sleeptime=INFINITE`；二进制用 `v16 = (v9==-1 || v4==v5)` → minWait=-1 → sleep=INFINITE，等价（v4==v5 即 List 空）。

## A-补：线程优先级 / TVPLimitTimerCapacity

tTVPTimerThread_ctor @0xA369BC：
```c
SetPriority(this, TVPLimitTimerCapacity ? 3 : 5);   // tTVPThread_SetPriority @0xA361D0
```
- `TVPLimitTimerCapacity` = 全局 `byte_1AFA740`（已重命名）。**位于 .bss**（地址 0x1AFA73F/40/41 全 0xFF=IDA 未初始化填充），**无任何代码写入点**（xref 全是 read）→ 运行时初值 = **0 (false)**，Android 二进制中恒为 false。
- 因此默认：优先级 enum 5 → SetPriority case 5 → `pthread_setschedparam(SCHED_OTHER, prio=2)`（highest）。对应本地 `SetPriority(TVPLimitTimerCapacity ? ttpNormal : ttpHighest)` 的 ttpHighest 分支 ✔。

---

## B. 跨线程通知机制（NativeEventQueue）

NativeEventQueue_PostEvent @0x906DCC → NativeEventQueue_postImpl @0x9156D8：
```c
ev = new(0x20); ev->queue=a1; ev->tag=0x10001;  // + std::function manager(sub_906F98)
// postImpl:
pthread_mutex_lock(queue+48);
push tuple<void*, int, std::function> 到 vector(queue+88..104);  // _M_emplace_back_aux
pthread_mutex_unlock(queue+48);
```
- **不走 ALooper / JNI Handler**。就是一个 **mutex 保护的 std::vector 事件队列**（生产者=Timer 线程，消费者=主线程），与本地 Application user-message 队列模型一致。
- 消费：主线程每帧 `TVPMainScene_ProcessMessages @0x913FC0` → `NativeEventQueue_processQueue @0x915570`：mutex 下 swap 出整个 pending vector 清空，逐个调 `std::function`（v6[3](v6)）→ trampoline @0xA379DC → `tTVPTimerThread_FireQueuedTimers @0xA36AD4`。**每帧排空全部**。
- 节奏 = cocos2d 帧率（见 D）。

FireQueuedTimers @0xA36AD4：
```c
if (ev.tag==0x10001 && !bTerminate) {
  for (timer : pendingQueue(+144..+152)) {
    if (timer+72 /*pending_count*/) { FireOnIdle(timer, timer+72); timer+72=0; }
  }
  pendingQueue.clear(); bEventEnqueued(+168)=0;
}
```
本地对应 `tTVPTimerThread::Proc` 遍历 Pending → `FirePendingEventsAndClear`（@0xA36F40 是单 timer 版本 `tTJSNI_Timer_FirePendingEventsAndClear`）✔。

---

## C. tTJSNI_BaseTimer::Fire / FireOnIdle @0xA2C2E4

```c
FireOnIdle(timer, n):
  eventname = L"onTimer";                                    // UTF-16, ttstr_createFromWide
  count = TVPCountEventsInQueue(owner, owner, &onTimer, 0);  // @0x8DE46C
  cap = TVPLimitTimerCapacity ? 1 : (timer+36 ? timer+36 : 0xFFFF);
  more = cap - count;
  if (more >= 1) {
    if (n > more) n = more;                                   // clamp
    mode = timer+48; flags = (mode==1?48 : mode==2?80 : 16);  // 16/48/80
    tag = (2 * (uint16)timer+32) | 1;                         // serial<<1 | 1
    重复 n 次: TVPPostEvent(owner, owner, &onTimer, tag, flags, 0, 0);  // @0x8DD324
    ++(uint16)timer+32;                                       // serial++
  }
```
与本地 TimerIntf.cpp:69 `Fire(n)` 一致：
- `static ttstr eventname(TJS_W("onTimer"))` ✔（二进制 L"onTimer"，__cxa_guard 一次性初始化）
- `cap = TVPLimitTimerCapacity ? 1 : (Capacity==0 ? 65535 : Capacity)` ✔（0xFFFF=65535）
- `more = cap - count`, `if(n>more) n=more` ✔
- `tag = 1 + (Counter<<1)` ✔
- flags = `TVP_EPT_POST(0x10) | TVP_EPT_DISCARDABLE | (mode: Normal/Exclusive/Continuous→额外位)`：mode 0→16, 1→48(0x30), 2→80(0x50) ✔（本地 atmNormal/atmExclusive/atmAtIdle 映射 TVP_EPT_NORMAL/EXCLUSIVE/CONTINUOUS）
- `++Counter` ✔
- TVPPostEvent @0x8DD324 确认：`(a5&0x10)`=POST 位检测、`(a5&0xF)==2` immediate 分支、普通入队 qword_1AE2EC0、`(a5&0xE0)==0x20` 设 has-events 标志。

---

## D. 主循环泵 / 帧率

`TVPMainScene::update(float dt) @0xAA0718`（cocos2d Scene::update，每帧调用）首行：
```c
TVPMainScene_ProcessMessages(qword_1AF4388, dt);  // @0x913FC0
DrawDevice_FlushAllPending(...);
... (FPS/draw 统计调试覆盖层)
```
ProcessMessages @0x913FC0：
```c
NativeEventQueue_processQueue(qword_1AF4388);      // 排空跨线程消息(含 Timer 0x10001)
if (qword_1AF4558) TVPSystemWatchTimerTimer(qword_1AF4558);  // @0x927188
```
TVPSystemWatchTimerTimer @0x927188：
```c
... 多个 sub_A2A96C(watch...) ...
if (v1+2 标志) TVPDeliverAllEvents();              // @0x8DE7A0  同帧派发
...
```
- **ProcessMessages 每帧排空 user message 队列** ✔（processQueue swap 出全部）
- **TVPDeliverAllEvents 同帧执行** ✔（在 ProcessMessages → SystemWatchTimerTimer 内）
- 因此：Timer 线程在第 K 帧投递的 0x10001，在 **第 K+1 帧（最早同帧若投递发生在 processQueue 之前）的 ProcessMessages** 被 FireQueuedTimers 消费 → FireOnIdle 把 onTimer 入 TVP 队列 → **同一次 SystemWatchTimerTimer 的 TVPDeliverAllEvents** 派发到脚本。即跨线程投递与脚本派发同帧完成。

帧率/vsync：
- `cocos2d::Application::setAnimationInterval(float)` 符号存在 @ 字符串 0xd9a0c / 函数符号在二进制。具体调用站点（是否 1/60）需在 TVPMainScene::initialize/CreateInstance 内确认；本次未深挖到字面 1/60 常量站点。cocos2d Android 后端帧率由 Choreographer/EGL swap 驱动，setAnimationInterval 设目标间隔。**Web 移植对照点**：本地以 cocos update 每帧 ProcessMessages，节奏由浏览器 rAF/cocos scheduler 决定，结构等价。
- 注：onTimer 节奏**不依赖**帧率精度——Timer 线程用墙钟 tick 独立计算 fire 次数（A 节），掉帧只影响"何时被派发"，不影响"应 fire 几次"（补发逻辑补齐）。

---

## E. 时钟源 TVPGetRoughTickCount32 @0xA2BF90

```c
v4 = std::chrono::steady_clock::now() / 1000000;   // 纳秒→毫秒 (CLOCK_MONOTONIC)
if (dword_1AFA728 > v4) qword_1AFA730 += 0x100000000;  // 32bit wrap 计数
dword_1AFA728 = v4;
return qword_1AFA730 + (uint32)v4;                  // 64bit tick
```
- 时钟源 = **steady_clock = CLOCK_MONOTONIC 毫秒** ✔（对应任务描述 E）。
- 额外：维护 32→64bit wrap，使返回值单调 64bit。本地 TVPGetTickCount 同语义。

## F. tTVPThreadEvent::WaitFor @0xA362DC

```c
pthread_mutex_lock(this+48);
if (ms != 0) {
  ts = system_clock::now() + ms*1e6 ns;            // 绝对超时
  pthread_cond_timedwait(this /*cond*/, this+48 /*mutex*/, &ts);
} else {
  condition_variable::wait(this, &mutex);          // 无限等待
}
pthread_mutex_unlock;
```
- Android 实现 = **pthread_cond_timedwait**（非 sem_timedwait）✔。
- 超时基准是 `system_clock::now()`（注意：与 tick 源 steady_clock 不同时钟，但仅用于 timedwait 超时，不影响 fire 计数）。
- 本地 Win32 `Event.WaitFor(sleeptime)` 的等价物。被唤醒条件：超时 或 被 Set/notify（terminate/新 timer 注册时）。

---

## 结论：一致性总评

| 环节 | 本地 cpp | libkrkr2.so | 结论 |
|------|----------|-------------|------|
| A Execute 补发/n>40/clamp3 | TimerImpl.cpp:110 | @0xA36C70 | **逐行一致** |
| A 优先级 TVPLimitTimerCapacity | ttpNormal:ttpHighest | @0xA369BC, .bss 默认 false→highest | 一致 |
| B 跨线程通知 | Application user msg 队列 | mutex+std::vector(NativeEventQueue) | 模型一致(非 ALooper) |
| B 消费节奏 | 主循环每帧 ProcessMessages | @0x915570 每帧排空 | 一致 |
| C Fire capacity/Count/flags/tag | TimerIntf.cpp:69 | @0xA2C2E4 | **逐行一致** |
| D 主循环泵同帧 | ProcessMessages+DeliverAllEvents | update@0xAA0718→@0x913FC0→@0x927188→@0x8DE7A0 | 一致(同帧) |
| E 时钟源 | CLOCK_MONOTONIC ms | steady_clock/1e6 @0xA2BF90 | 一致 |
| F WaitFor | Win32 Event | pthread_cond_timedwait @0xA362DC | 平台等价 |

无发现架构性偏离。Android 与本地这条链在六维（结构/数据流/调用链/对象生命周期/容器选型/边界行为）上对齐。唯一平台差异是同步原语（Win32 Event ↔ pthread_cond_timedwait）和帧驱动后端（Choreographer ↔ rAF），均为不可避免的平台边界，语义等价。

---

## 补充（2026-06-11）：60Hz 假设的二进制证据

**Q: libkrkr2.so 的设计是否假设屏幕刷新率 60Hz？**

1. **引擎核心层（timer/事件/动画采样）：无 60Hz 假设。** Execute@0xA36C70 全程墙钟驱动（补发 n=(tick-next)/interval+1），Fire/TVPPostEvent/DeliverAllEvents 无任何帧率常量。刷新率无关。
2. **帧泵层：硬编码 1/60。** `TVPAppDelegate::applicationDidFinishLaunching @0xA97BAC` 在 0xa97d18 处以字面浮点 **0.016667** 调 Director vtbl+24（setAnimationInterval）。`cocos2d::Application::setAnimationInterval @0xB075C4` 经 JniHelper::callStaticVoidMethod 转发 Java `org/cocos2dx/lib/Cocos2dxRenderer.setAnimationInterval(F)`。
3. **Java 渲染器（随包 Kirikiroid2_1.3.9.apk classes.dex，dexdump 实证）：**
   - `setAnimationInterval`: `sAnimationInterval = (long)(f * 1e9)` ns。
   - `onDrawFrame`（GLSurfaceView 每 vsync 回调）: `if (sAnimationInterval <= 1.66667e7 /*ns=1/60s*/) { nativeRender(); return; } else { 不足则 Thread.sleep 补齐再渲染 }`。
   - 1/60 设置恰好 ≤ 阈值 → 走**无节流分支**：每个 vsync 回调渲染一帧。该阈值 1.66667e7ns 本身就是上游 cocos2d-x 内嵌的"系统默认每秒回调 60 次"假设。
4. **结论**：60Hz 假设不在引擎核心，而在帧泵配置（1/60 字面量）+ Java 渲染器阈值 + 2018 年代设备生态（全 60Hz vsync）。若 kirikiroid2 跑在 120Hz Android 上，onDrawFrame 同样会跑 120fps（阈值判定不节流），与 Web 120Hz RAF 处境相同。法娘 ActionManager 的 interval=16(ms) 是游戏脚本层最强的 60Hz 整拍假设（每帧恰好一拍）。

---

## 补充（2026-06-11 第二轮）：FrameScan 修后残留抖动的根因与 vsync 锁相 tick

FrameScan shim（commit 3d06d30）修后仍残留 ~3-8% 的双帧步 + 偶发 in-fade 停顿。
用 carousel3kag fixture（reference/xp3/）+ readPixels 帧哈希 + EM_ASM 探针
（SetInterval/FrameScan fire/Proc fire 时间线推 globalThis._timerDiag）钉死：

1. **归因（120Hz 屏实测，150s）**：106 个晚步异常中 96 个 = ActionManager
   `tick - lasttick >= interval(16)` 整数毫秒门跳拍（探针直击 `timer.interval=1`
   分支被执行，onTimer 到达时刻距 lasttick 仅 14/15 整数毫秒）；8 个 = 13ms
   重锚到期错过下帧扫描；渲染滑帧仅 2。
2. **根因是时钟采样抖动，不是派发链**：ActionManager 的门依赖
   "逐帧采样 System.getTickCount 得到均匀时间步"。Android 上 Choreographer
   回调执行时刻对 vsync 栅格抖动 ±0.2ms → delta 恒 ≥16.47 → 永不跳拍。
   浏览器 RAF 时间戳本身对理想栅格残差 p95=1.17ms（实测），回调内脚本执行
   时刻再叠 ±0.6ms 派发抖动，而门的裕量只有 16.67-16=0.67ms → 4.2% 跳拍。
   135/347 个跳拍直接发生在"帧间距收缩到 15ms"的抖动帧上。
3. **修复（平台边界 shim，web 分支）**：主线程 vsync 锁相 tick——
   `TVPGetRoughTickCount32`（cpp/core/environ/web/Platform.cpp）主线程路径
   返回软件锁相环输出的均匀帧栅格 tick；PLL 每帧由 `TVPWebFrameTickUpdate`
   （TVPMainScene::update 帧首调用）用 RAF 时间戳驱动（period EWMA + 相位
   10% 慢跟踪 + 失锁重同步）。Worker 线程（计时线程）不受影响仍读
   CLOCK_MONOTONIC。RAF 停摆（模态自旋循环/标签页隐藏）>50ms 自动回退
   原始时钟。配套：TickCount.cpp 的 32bit wrap 误判防护（两时钟域 ±数 ms
   偏差不得触发 bias +2^32，阈值 2^31）。
4. **实现陷阱（已踩）**：本机 emscripten 的 CLOCK_MONOTONIC/emscripten_get_now
   是 timeOrigin 绝对毫秒（~1.78e12）而 RAF 时间戳是页面相对毫秒；
   double→uint32 在 wasm 是 trunc_sat（超范围饱和成 0xFFFFFFFF）而非
   mod 2^32 回卷——首版直接 (uint32)double 把主线程 tick 永久钉死在
   0xFFFFFFFF，全部 timer 不再到期（白屏）。修正：PLL 统一在绝对域运行
   （rafT + performance.timeOrigin），double→uint32 一律经 uint64 中转。
5. **效果（120Hz，164s 稳态）**：标称步 93.3%→98.5%（晚步/3帧+ 步=0，余
   1.5% 为 1 帧多余变化非停顿）；步长 std 3.04→0.80ms；gate 跳拍
   347→5/150s；update 间距 99.86% 恰好 2 帧（mean 16.67ms）；in-fade
   停顿（≥3帧）51 次/211s→**0**。
6. **可观察行为对齐论证**：Android 的 tick 时钟连续（@0xA2BF90），但引擎
   对它的每帧采样时刻被 Choreographer 锁在 vsync 栅格上——"逐帧采样值 =
   均匀栅格"才是脚本可观察的行为。Web 锁相 tick 复刻的正是这一可观察
   性质；牺牲的是帧内多次采样的亚毫秒分辨率（Android 上脚本帧内连读
   会看到 +0.x ms 推进，Web 锁相后帧内恒值），KAG/ActionManager 无此依赖。
