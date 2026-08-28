# Player 播放加载、Join、Emote/普通初始化状态机（四参考二进制，2026-08-27）

## 1. 范围与结论

本切片闭合四条公开路径共同汇入的播放状态机：

- `Player::playMotionImpl_guess`：flags gate、load、失败、提交与 type dispatch；
- `Player::loadMotionResult_guess`：可重入 `onFindMotion` 与 ResourceManager
  `findMotion`；
- `Player::resetMotionState_guess`：Join 的 HM3/HM4 snapshot 重建；
- `Player::initEmoteMotion_guess`：type-1 division 选择和二次 motion load；
- `Player::initNonEmoteMotion_guess`：type-0 owner、参数表、node tree、变量和时钟初始化。

这条闭包由 Motion/Stealth motion property、legacy raw `play`、EmotePlayer `play`、子
Player 自动播放、Emote 角度切换和 type-1 secondary motion 共同复用。四端源级状态机一致；
STL/deque 物理实现、ARM32 SjLj cleanup 和非 chain 初始 clamp 的 signed-zero 机器行为是
目标差异。

本地核心逻辑已经吻合，但 `playMotionImpl_guess` 与 `initNonEmoteMotion_guess` 仍夹带四端
不存在的 `PRTDIAG` sequence/path/logger sidecar。它们会增加字符串窄化、路径扫描、全局
计数写、logger 格式化和额外异常点。本轮已从两个函数删除这些 sidecar 与两个只为它们
服务的采样/布尔 helper，没有改变原生播放状态机。

## 2. 四端等价类与完整证据

20 个目标均 fresh decompile 成功；完整 disassembly 的 `cursor.done` 全部为 true：

| 语义函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `playMotionImpl` | `0x6AF664`，459 | `0x580158`，281 | `0x100107540`，236 | `0x104AE8`，386 |
| `loadMotionResult` | `0x6AE2F0`，504 | `0x57F654`，253 | `0x1001067BC`，225 | `0x103BBC`，356 |
| `resetMotionState` | `0x6AFF5C`，193 | `0x580668`，122 | `0x100107B90`，98 | `0x1051AC`，126 |
| `initEmoteMotion` | `0x6B0270`，494 | `0x5807E0`，281 | `0x100107D38`，230 | `0x105350`，348 |
| `initNonEmoteMotion` | `0x6B0A3C`，384 | `0x580C28`，225 | `0x100108258`，189 | `0x1058F8`，310 |
| 每端合计 | 2034 | 1162 | 978 | 1526 |

fresh xref 也确认：

- `loadMotionResult` 只有 `playMotionImpl` 和 `initEmoteMotion` 两类 caller；
- `resetMotionState` 只有 `playMotionImpl` 的 Join edge；
- `initNonEmoteMotion` 只有 `playMotionImpl` 的 type-0 edge和 `initEmoteMotion` 成功 edge；
- `initEmoteMotion` 还由角度 setter、update/particle/child motion 路径复用；
- Android arm64 把若干 outer play/property 协调器内联成直接 `playMotionImpl` call edge，
  另外三端更多保留共享 outer helper。这是编译 call-shape 差异，不是两套状态机。

## 3. flag gate 与入口别名

联合四端恢复出的 bit 值：

```text
Force   = 1
Chain   = 2
AsCan   = 4
Join    = 8
Stealth = 16
```

`playMotionImpl(label, flags)` 最前面的 gate 精确为：

```cpp
const ttstr &selected = flags & Stealth ? stealthMotion : motionKey;

if (!(flags & (Force | AsCan)) && selected == label)
    return;

if (!(flags & Force) && (flags & AsCan) && allplaying)
    return;

if (flags & Join)
    resetMotionState();
```

因此：

- Force 和 AsCan 都绕过“当前 label 相同”短路；
- Force 同时绕过 AsCan+playing gate；
- AsCan 只有在 `allplaying` 为 true 时抑制 reload；
- Stealth 只选择比较/提交槽，不改变 load helper 的 chara 参数；
- Join snapshot 发生在 load 之前，即使随后 load 失败也已提交 snapshot 副作用。

public `playMotion_guess` 的 pending-stealth owner 协调器已由
`analysis/motionplayer_player_chara_motion_properties_four_binary_2026-08-26.md`
闭合；本报告从它调用的 inner 状态机继续向下。

## 4. `loadMotionResult`：一个复用 result 槽，两段 dispatch

### 4.1 可选 `onFindMotion`

helper 的两个 ttstr 参数都是独立按值 owner。进入时先把 hidden-sret result 初始化为
Void。仅当 Player 的 raw `_currentDispatch` 非空时执行 callback phase：

```cpp
request = new Dictionary;
request.chara  = chara;
request.motion = motion;

snapshot = AddRef(currentDispatch);
snapshot.onFindMotion(request, result);  // status ignored
destroy request Variant;

response = strict owning Object(result);
chara  = PropGet(MUSTEXIST, "chara")  failed ? empty : strict ttstr(value);
motion = PropGet(MUSTEXIST, "motion") failed ? empty : strict ttstr(value);
```

边界与 owner 顺序：

- Dictionary 的原始返回 ref、request Object 和 ObjThis Variant refs、currentDispatch
  snapshot ref 相互独立；
- currentDispatch 在 Dictionary 两个 PropSet 后才取快照并 AddRef；
- callback status 不决定后续流程，result 必须严格转 Object；
- callback 可以替换 argv[0]，但原始 Dictionary raw owner仍活到 response 两个字段读完；
- response 字段 PropGet 失败产生空 ttstr，不转换失败输出；成功输出严格转换；
- body 使用 currentDispatch snapshot，re-entrant 改写 Player raw 字段不会改 receiver；
- 所有 dispatch/Variant/ttstr owner 在异常边由 ABI cleanup 逆序释放。

### 4.2 ResourceManager `findMotion`

callback phase 后复制 Player 的 persistent ResourceManager Variant，严格转 Object并独立
AddRef；构造：

```text
path = "motion/" + adjustedChara + "/" + adjustedMotion
arguments[0] = CopyRef(findMotionContextVariant)
arguments[1] = independent copy of path Variant
resourceManager.findMotion(arguments, result)   # status ignored
```

关键原始边界是同一个 hidden-sret result 槽贯穿两次调用。若 `onFindMotion` 已写 Object，
随后 `findMotion` 返回失败且不写 result，helper 会把前一个 callback Object 原样返回；它不
在两次调用之间 Clear。无 currentDispatch 时 result 仍从初始 Void 开始。

## 5. load 成败与提交顺序

`playMotionImpl` 总是把 live `_stealthChara` 和 caller label 传给 load helper。成功定义只有
`result.Type() != Void`，不是“result 是合法 Array/Object”。

失败路径：

```text
log "motion not found " + liveStealthChara + "/" + callerLabel
Clear motionContentVariant
Clear findMotionContextVariant
allplaying = false
return
```

失败不写 `_stealthMotion` 或 `_motionKey`；之前的 Join snapshot、日志和 load callback 副作用
保留。

非 Void 路径先提交 label，再验证 result：

```cpp
stealthMotion = label;                 // 所有成功请求都写
if (!(flags & Stealth)) motionKey = label;

resultObject = strict owning Object(result);
motionContentVariant    = resultObject[0];   // status ignored, CopyAssign
findMotionContextVariant = resultObject[1];  // status ignored, CopyAssign
```

因此非 Object result 会在两个 label 已提交后抛；index 0 成功、index 1 抛时，motion owner 已
部分提交而 context 保留旧值。完整 result Object owner贯穿两个索引读取，re-entrant getter
不能通过改写 caller result 改变第二次读取。

随后再从已提交的 `_motionContentVariant` 建立第二个 strict owning Object snapshot，读取
`type`。这份 snapshot贯穿所有 type-specific 读取和 initializer；re-entrant property getter
即使替换 Player canonical field，也不会重定向剩余分支。

## 6. type 1 / type 0 direct-edit 转换

### 6.1 type 1

```cpp
if (!directEdit) {
    emoteAngle = root.delta.angle;
    root.delta.angle = 0;
}
directEdit = true;
emoteDivisionVariant   = motionObject.division;
emoteMotionListVariant = motionObject.motionList;
emoteMotionIndex = -1;
initEmoteMotion(flags);
```

root 由 constructor invariant 保证存在，没有 empty-deque guard。重复 type-1 play 不再搬运
root angle，但会替换 division/list owners、重置 index 并运行 initializer。

### 6.2 type 0

```cpp
if (directEdit) {
    root.delta.angle = emoteAngle;
    emoteAngle = 0;
}
directEdit = false;
initNonEmoteMotion(flags);
```

type 既非 0 也非 1 时，labels、motion/context owners 已提交，但 directEdit、初始化状态和
playing byte不再改变。type PropGet status 被忽略，随后 Integer转换决定异常。

## 7. Join snapshot 状态机

`resetMotionState` 在 `_queuing == true` 时完全 no-op。否则：

1. clear HM3 per-node map 与 HM4 variable snapshot map；
2. 插值当前 variable track；
3. 从 node index 1 开始，把每个节点 flags 设为 1，并以 live clamped cursor、join=true
   evaluate timeline；Android arm64 内联这段，其余三端保留 aggregate helper；
4. 物理遍历 variable-label scope deque：`cursor = activeSlotCursor & 1`，仅当 active slot
   `typeZeroFlag == false` 时执行 `HM4[cascadeKey] = value`；
5. 再从 node index 1 物理遍历 live node deque：先检查 `joinTarget`，再接受 node type
   `{0,2,3,4,7,8}`，即 bitmask `0x19D`；
6. 以完整 ancestor path 构造 key，用 HM3 `operator[]` 物化 value，然后调用节点 snapshot
   初始化器。

容器没有事务回滚。timeline evaluation、ttstr path构造、unordered-map插入或 MotionNode
snapshot 中途抛出时，两个 map 保留截至抛点的 partial rebuild；child Player/particle
Variant 的 ownership transfer 也不回滚。

Android 使用 libstdc++ segmented deque/unordered-map，iOS 使用 libc++ block map。stride、
block 容量、hash node形状不同，但物理遍历顺序、root排除、gate 和 partial commit相同。

## 8. type-1 division 与 secondary motion

`initEmoteMotion` 先计算 `cameraAngle + emoteAngle`：

```cpp
while (angle < 0)   angle += 360;
while (angle >= 360) angle -= 360;
```

没有 finite shortcut：+Inf/-Inf 分别在第二/第一循环永久不终止，NaN 跳过两个循环。

随后严格取得 division count，从 index 1 扫描第一个 `(division[i-1], division[i]]` 覆盖
angle 的相邻区间；未命中则让 index 走到 count。最终执行 `selected = index % count`，没有
count==0 guard，空 division 保留整数除零边界。若 selected 与 `_emoteMotionIndex` 相同，
立即返回；否则先写新 index，再做后续所有可能失败操作。

`motionList[selected]` 转 ttstr，以 `/` split 后无范围检查读取 `parts[2]`。因此短 path 是
原始 vector 越界边界。secondary load 使用 live stealthChara 和 parts[2]：

- 非 Void：读取 element 0/1 到 motion/context persistent owner，然后调用普通 initializer；
- Void：记录相同 missing log，Clear 两个 owner，但不清 allplaying；
- selected index 在失败时也保留，下一次相同 angle 不重试，直到 division 选择改变或 outer
  type-1 play把 index重置为 -1。

## 9. type-0 owner、参数表和初始化提交

`initNonEmoteMotion` 首先从 `_motionContentVariant` 建立一个贯穿函数的 owning accessor，
严格按以下顺序提交：

```text
loopTime
lastTime -> cachedTotalFrames
tag Variant owner
priority Variant owner
priority[0] owning accessor
priority[0].content -> rootContent Variant owner
```

只有上述五个 persistent owner/标量全部提交后，才 clear node-label map 和 parameter-entry
vector；旧 node deque及其 layer IDs 继续活到 `buildNodeTree` 自己的 reset。若随后读取
parameterize 或构建参数抛出，旧 node tree仍在、两个索引已清、前述 owners已换新。

参数分流：

- `parameterize.Type()==Object`：append 单项并 finalize ramp map；只有 vector非空才把
  selected pointer设为 front，空 append 保留旧 pointer（可能已因 vector clear悬空）；
- 其他类型：先读取/parse `parameter`；若 parameterize 是 Integer，则以 signed负值也失败
  的 unsigned-size边界检查索引，越界抛原始 `"parameter id out of range."`；否则 selected
  pointer写 null；
- parameter-entry vector clear 不同时 clear ramp map，异常前沿可能留下指向已销毁 entry 的
 旧 map指针，这是原生结构边界，不能主动修复。

随后以相邻 byte/halfword store提交：

```text
syncWaiting = false
allplaying = true
buildNodeTree()
initVariables()
```

因此 build/init异常会留下“playing=true、syncWaiting=false”以及 partial node/variable状态。

非 Chain 时：

```text
frameTickCount = +0
clampedEvalTime = min(cachedTotalFrames, +0)
queuing = true
firstFrame = true
```

Chain 保留 frameTickCount、clampedEvalTime 和 queuing，只写 firstFrame=true。

## 10. non-chain clamp 的机器边界

两端 64 位使用 `FMINNM/FMINNM` 形式，操作数为 cachedTotalFrames 和 +0；两端 32 位用
ordered compare：`total < 0 ? total : +0`。有限非零值一致，NaN 都得到 +0；等值 signed zero
时 64 位 numeric-min 选择 -0，而 32 位 ordered false分支选择 +0。

本地保留 `std::min(0.0, totalFrames)` 的共同源形状：32 位自然生成 ordered compare，64 位
优化器可生成 numeric-min。若未来要求跨宿主模拟目标 ISA 的 signed-zero bit pattern，应在
显式 platform-boundary 层处理，不能把两端机器差异伪装成统一数学规则。

## 11. owner 与异常清理总览

五个函数的主要 owner 边：

| 阶段 | owner | 正常释放/提交 | 异常边 |
|---|---|---|---|
| load callback | request Dictionary raw ref + Object/ObjThis refs | response读取后逆序 Release | 每个已建立 ref独立 cleanup |
| callback receiver | currentDispatch snapshot AddRef | callback/response后 Release | re-entrant改字段不影响 snapshot |
| load result | hidden-sret Variant贯穿 onFindMotion/findMotion | 交给 caller owner | 第二次失败可保留第一次 Object |
| play result Object | strict AddRef dispatch + complete result Variant | 两个 indexed owner提交后 Release | label/第0项可先行 partial commit |
| motion Object | canonical field CopyRef + strict dispatch | initializer后 Release | re-entrant替换字段不重定向 |
| Join maps | owning ttstr/hash nodes/PerNodeLayerState | 后续 seek消费/销毁 | partial rebuild，不回滚 transfer |
| Emote split | vector<ttstr> + selected ttstr | nested load后逆序析构 | parts[2]无检查；index已先提交 |
| ordinary init | motion/root accessors + parameterize/parameter Variants | function尾逆序 Release | persistent owners/flags按阶段 partial commit |

Android armv7/iOS armv7 的完整指令流包含 stack canary / SjLj call-site cleanup；AArch64 包含
对应 landing pads。报告不把编译器 cleanup 展开误建模成 portable字段或显式 padding。

## 12. 本地修改与逐行映射

本地语义对应：

- `cpp/plugins/motionplayer/PlayerTimeline.cpp:95`：playImpl gate、load、commit、type dispatch；
- `cpp/plugins/motionplayer/PlayerCore.cpp:442`：loadMotionResult callback/result owner；
- `cpp/plugins/motionplayer/PlayerCore.cpp:588`：Emote division/secondary load；
- `cpp/plugins/motionplayer/PlayerCore.cpp:647`：普通 motion owner/parameter/node/clock 初始化；
- `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:735`：Join snapshot；
- `cpp/plugins/motionplayer/Player.h:72`：五个 flag bit。

本轮语义修改仅删除四端不存在的诊断 sidecar：

- `PlayerTimeline.cpp`：删除 playImpl 的 static sequence、路径扫描、narrow、bool formatting 和
  四组 logger 分支，以及只被它们使用的两个 helper；
- `PlayerCore.cpp`：删除 initNonEmote 的 static sequence 和 enter/before-build/after-build/
  exit logger 分支，以及只被它们使用的两个 helper。

原生错误日志 `TVPAddLog("motion not found ...")` 保留；它在四端五个函数中均有直接证据，
与被删除的 Web `PRTDIAG` sidecar不是同一机制。

## 13. IDB 与验证状态

- 四端 20 个函数完成 fresh decompile、完整 disassembly 和 xref；
- 四份 IDB 各完成五个语义命名、函数注释、bookmark 并原位保存；
- 现有测试覆盖完整 result owner、label-before-conversion、index0 partial commit、raw dispatch
  throw残留、ordinary owner re-entry、Join path/snapshot和 playback state；
- `git diff --check` 通过，受控源码中两组播放诊断字符串已不存在；
- 当前环境仍缺少 CMake、Ninja 和 Emscripten，standalone syntax check受缺失第三方 headers
  阻塞，因此不声称正式 native/Web build 或运行测试通过。

