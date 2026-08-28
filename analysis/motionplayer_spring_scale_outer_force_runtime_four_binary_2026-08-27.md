# Spring、scale 与 outer-force 运行时总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 闭合 `MP-R14`。四端共同恢复出的物理尾链是：

```text
Motion.EmotePlayer.progress(ms -> frames) ─┐
D3DEmotePlayer.progress(frames, zero gate) ├─> EmoteEngine.progress(originalDt)
                                           │
                                           └─ if originalDt != 0 && !directEdit:
                                              step bust outer-force controller
                                              step hair outer-force controller
                                              step parts outer-force controller
                                              simple deque + bustScale
                                              hair chain deque + hairScale
                                              parts chain deque + partsScale
```

simple spring、two-segment chain、post-bend、三套 metadata owner、三个 scale double、三个
two-channel outer-force controller、reset、public animating 排除和 outer-force state persistence
已经形成完整闭环。本地生产实现匹配四端共同伪代码，没有生产语义修改。

本轮新增单元测试，锁定 outer-force 的三键/共享 Var 七字段 schema、两通道数组、live queue
保留、scale 不参与 snapshot、public animating 排除，以及 reset 只提交 controller 尾项并重置
两个 first/init flags、不重置 scale 或 solver payload。

## 2. fresh 四端证据与分母

本轮覆盖 31 个 source-level roles。Android arm64 把 chain post-bend 内联到两条 wrapper 路径，
因此四端共有 123 个 distinct code ranges；其余 30/31/31/31 个地址均 fresh decompile，Android
arm64 的直接 Motion progress 毫秒换算包装器与 Engine progress 被 IDA 表示为同一 Hex-Rays
tail-merged function unit。D3D progress 仍是独立6指令包装器；Engine 的302指令 core range 和
D3D range 都另行逐指令扫描，cursor 均 done。

总计 14,378 条完整未截断指令、148 个完整未截断 `xrefs_to`：

| target | code ranges | instructions | xrefs_to |
|---|---:|---:|---:|
| Android arm64 | 30 | 4,762 | 57 |
| Android armv7 | 31 | 2,972 | 24 |
| iOS arm64 | 31 | 2,705 | 43 |
| iOS armv7 | 31 | 3,939 | 24 |

### 2.1 core runtime roots

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| simple ctor | `0x65F828` / 135 | `0x55176C` / 116 | `0x1001A18C4` / 82 | `0x1A099C` / 157 |
| chain ctor | `0x6662D8` / 430 | `0x5554F0` / 317 | `0x1001A6104` / 245 | `0x1A5710` / 413 |
| simple solver | `0x65FB48` / 128 | `0x551910` / 139 | `0x1001A1A8C` / 127 | `0x1A0BE0` / 159 |
| chain solver | `0x665D84` / 289 | `0x555010` / 267 | `0x1001A5BDC` / 277 | `0x1A51CC` / 302 |
| post-bend | inline | `0x555408` / 59 | `0x1001A6030` / 53 | `0x1A5634` / 58 |
| simple builder | `0x6683F8` / 520 | `0x55659C` / 328 | `0x1001A7DDC` / 250 | `0x1A730C` / 423 |
| chain builder | `0x668DB0` / 871 | `0x556B84` / 558 | `0x1001A87C0` / 416 | `0x1A7DCC` / 664 |
| simple wrapper | `0x678B28` / 138 | `0x55EE98` / 152 | `0x1001B29D0` / 163 | `0x1B24D8` / 195 |
| chain wrapper | `0x6790C8` / 235 | `0x55F2F4` / 191 | `0x1001B2F2C` / 198 | `0x1B2ABC` / 221 |
| Engine progress | `0x67A3F8` / 302 | `0x55FEF0` / 95 | `0x1001B4304` / 89 | `0x1B3E10` / 104 |
| D3D progress gate | `0x530E3C` / 6 | `0x49501E` / 8 | `0x100233470` / 6 | `0x2321AE` / 8 |
| reset | `0x66BF6C` / 180 | `0x558888` / 64 | `0x1001AB03C` / 27 | `0x1AA714` / 25 |
| public animating | `0x671378` / 852 | `0x55B18C` / 257 | `0x1001AE5D8` / 457 | `0x1ADE54` / 640 |
| serialize outer | `0x675208` / 135 | `0x55CDF0` / 94 | `0x1001B0F98` / 71 | `0x1B0980` / 140 |
| restore outer | `0x67872C` / 224 | `0x55EC4C` / 90 | `0x1001B26CC` / 70 | `0x1B21DC` / 135 |

### 2.2 outer-force 与 scale surfaces

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine outer router | `0x670138` / 56 | `0x55A928` / 63 | `0x1001ADC9C` / 51 | `0x1AD37C` / 72 |
| Motion raw setOuterForce | `0x66FE58` / 180 | `0x55A828` / 79 | `0x1001ADB98` / 60 | `0x1AD218` / 108 |
| D3D typed setOuterForce | `0x530E6C` / 39 | `0x495048` / 41 | `0x1002334BC` / 21 | `0x2321F8` / 61 |
| D3D getOuterForce TODO | `0x530F08` / 6 | `0x4950D0` / 6 | `0x100233524` / 6 | `0x2322CC` / 6 |
| direct scale set H/P/B | `0x67F300/08/10` / 2 each | `0x5620AC/B6/C0` / 3 each | `0x1001B619C/A4/AC` / 2 each | `0x1B5F84/8E/98` / 3 each |
| direct scale get H/B/P | `0x67F318/20/28` / 2 each | `0x5620CA/D4/DE` / 3 each | `0x1001B61B4/BC/C4` / 2 each | `0x1B5FA2/AC/B6` / 3 each |
| D3D scale get/set H | `0x5304D0/E0` / 4 each | `0x494A6E/7C` / 5 each | `0x100232EA8/EB8` / 4 each | `0x231AFA/1B08` / 5 each |
| D3D scale get/set P | `0x5304F0/500` / 4 each | `0x494A8A/98` / 5 each | `0x100232EC8/ED8` / 4 each | `0x231B16/1B24` / 5 each |
| D3D scale get/set B | `0x530510/20` / 4 each | `0x494AA6/B4` / 5 each | `0x100232EE8/EF8` / 4 each | `0x231B32/1B40` / 5 each |

Android arm64 的 `B` tailcall 使 IDA 把直接 Motion progress 入口 `0x67EC94` 和远端 Engine
body 识别为一个 function with tail chunk；这不是 source inline，也不是重复 body。D3D 的
`0x530E3C` 仍是独立 zero-gate/tail-jump function。报告和 coverage 按实际 code ranges 计数。

## 3. 物理对象与 owner topology

```text
Engine
├─ owns VarController(2) bust outer force
├─ owns VarController(2) hair outer force
├─ owns VarController(2) parts outer force
├─ deque #1 owns simple node -> EmoteSpringState (fixed 72B)
├─ deque #2 owns hair chain node -> EmoteBustChainSpring (176/168B)
└─ deque #3 owns parts chain node -> EmoteBustChainSpring (176/168B)
```

simple/chain state 内无动态容器。chain 尾部 `collisionCurve` 只借用 Engine 的 wind emitter，
wrapper 在每个 node、每次 solver gate 以前刷新；销毁时不读取该借用指针。

simple builder 从 `bustControl` 构造 spring，随后用 `param.op/p/pv/ofs` 覆盖动态 snapshot，
再 emplace owner、写 baseLayer/var_lr/var_ud 并发布 type0 resolver。shared chain builder 分别为
`hairControl`/`partsControl` 写 type1/type2，恢复 `op/ofs/bendR/bendS` 和 `bp -> p -> pv`
三组两元素 vec3 Array，再发布三个 variable keys。

constructor 成功到 deque emplace 之间均是 raw pointer；任何后续 property/Array/grow 异常都
保留原版 leak window。emplace 后 label 或 resolver map 抛出时，owner 和此前写入的 entry/map
prefix 保留。disabled row 不插 placeholder，但 resolver index 使用原 metadata index；duplicate
key 后写覆盖 locator，不销毁旧 spring owner。

## 4. constructors 与 fixed state

### 4.1 simple 72B state

ctor 写 `firstFlag=1`，清 stored/pos/vel 三个 vec3，再按顺序读取：

```text
gravity -> k_a
spring -> k_b
friction -> drag
scale_x -> leverX
scale_y -> leverY
```

`biasY` 由 builder 的 param.ofs 后写；`prevDeltaX/Y` 直到 first solver call 才写。构造任一 TJS
读取/转换失败时，new-expression 释放 pending storage；已读 scalar 只存在于未完成对象，不发布。

### 4.2 two-segment chain

ctor 写 firstFlag、bendR/S、op、p/pv/bp 与 null wind borrow，读取 gravity、friction_x/y、b_rate、
v_bound、ud_eft、bend_spd/vol、两元素 length/scale_x/scale_y，并从 rest unit `(0,1,0)` 派生
两段初始 p/pv。builder 随后用 snapshot 覆盖其中的 live fields。

length/scale accessor 强制对象后直接读取 index 0/1，没有 count guard；短 Array、非对象、getter
异常按已构造 scalar prefix 失败。`ud_eft` 是原始 signed int32，不 clamp 到0/1。

## 5. simple spring solver

first call 使用 stored anchor 并写 `prevDelta=stored-input`、清 firstFlag；后续 call 用
`prevDelta+input` 重建并回写 stored XY。solver 随后：

1. 以 `sin(-angle)`/`cos(angle)` 旋转外力与 rest vector；
2. 使用 `k_b*dt`、`k_a*dt` 与 `drag*dt` 更新三维速度和位置；
3. 用原版 float 常量 `0.0451603944` 与 `0.0392699082` 做两个 atan angle 输出。

没有 dt、scale、coefficient、pointer 或 finite guard。NaN/Inf、负 dt、负 damping、除零相关
结果原样传播。X output store 发生在读取 Y 路径的 biasY/leverY 以前，因此 outX 若 alias 这些
state fields 会改变随后 Y 结果；已有 alias test 精确锁定该顺序。

## 6. chain solver 与 post-bend

chain solver first/non-first anchor delta 与 simple 同构，但固定处理两段。每段先构造 parent
target/rest position，再处理过长 constraint：

- 仅 ordered `distanceSquared > restLength²` 才 sqrt；
- distance 还必须 ordered `> 1/64`；
- segment0 只把 `(extension*bRate)*dt` 加到 velocity；
- segment1 先把 position 直接投影回 constraint surface，即使 dt==0，再按 `vBound*dot*dt`
  去除速度分量。

旋转外力、gravity、friction 与 integration 全用 float。`dt*0.0` 保留，因此 NaN dt 能污染 Z
velocity/position。wind borrow 非空时按 segment old X 做 first-hit lookup。X angle 每段都写；Y
只在 raw `segment==udEft` 时写，畸形 udEft 让 wrapper 的 Y scratch 保持未初始化。

post-bend：ordered `fabs(lastY)<=28` 时 depth 减 `dt/32` 并下限 clamp0，否则（包括 NaN）增加
并上限 clamp1；phase 用 `fmod(phase + depth*speed*dt, 2π)`。输出先 `seg1 += bend`，后
`seg0 -= bend`，两个 output alias 时该顺序可观察。Android arm64 把同一逻辑内联到 wrapper，
其余三端保留 helper。

## 7. wrappers、scale mapping 与时间边界

### 7.1 精确 family mapping

| pass | owner deque | force controller | scale double | published outputs |
|---|---|---|---|---|
| simple | #1 from bustControl | bust | bustScale | keyX, keyY |
| hair chain | #2 from hairControl | hair | hairScale | keyA, keyB, keyC |
| parts chain | #3 from partsControl | parts | partsScale | keyA, keyB, keyC |

三个 scale setter/getter 在直接 Motion facade 与 D3D shell 上都只是 raw double load/store；D3D
多三层 pointer hop。它们不置 dirty、不 reset controller、不 clamp/abs/finite-normalize，也不读取
metadataScale、inverseCombinedScale 或 meshDivisionRatio。NaN、±Inf、subnormal、`-0.0` bit
pattern 保留；进入 solver 时才窄化为 float。

scale 初值都是1.0。reset、metadata replacement 与 outer-force restore 均不修改它们；state
serialization 也不包含 scale。因此基于 serialize/unserialize 的 EmoteObject clone 得到 fresh
constructor 的1.0 scales，而不是 source facade 的运行时 scale triplet。

### 7.2 wrapper dt 与 publication

两个 wrapper 都从 controller 的 unchecked `count` 向2-float stack scratch memcpy；正常 owner
count=2，畸形 count0留下未初始化 force，count>2可越界写栈。每个 node 先解析 shape anchor。

- entry.initFlag 非零：先清 flag，使用完整 dt step 一次；
- 否则仅当 ordered `dt-0.0001 > 0` 时，以不大于1.1的 substep 向新 anchor 插值；
- small/negative/NaN dt 的 non-init node 跳过 solver，但仍把未初始化 output 写 variable map；
- anchor 在 output map upsert 以前提交；map/property 异常不回滚 spring/anchor/earlier key。

chain 每 node 还在 gate 以前刷新 wind borrow，并在 solver 后 post-bend。simple 按 keyX→keyY，
chain 按 keyA→keyB→keyC 顺序发布。

## 8. outer-force API、queue 与 public state

Engine router 精确、区分大小写地识别 `bust`、`hair`、`parts`。其他、empty 或 null label
成功 no-op。x/y/duration/power 先从 double 窄化 float，再进入共享 Var setter；append/replace 使用
Engine `_queuing` byte。router 不写 dirty。

两个 façade 的最后一个参数语义不同：

- Motion raw callback：至少3参；transition/ease 默认0；先在 double 中把 script ease 映射为
  power（0→1、正值→1+ease、负值→1/(1-ease)），然后交给 router；多余参数忽略；
- D3D typed method：五个 typed 参数中的 power 直接透传，不做 script-ease remap；临时 label
  Variant/ttstr owner 覆盖完整调用并在返回后释放。

D3D `getOuterForce` 四端都不是 getter，而是固定抛出
`TODO: implement D3DEmotePlayer::getOuterForce()`；直接 Motion 表面没有对应 getter。

outer controllers 使用 R11 已闭合的 Var queue/state machine。关键的聚合边界是：即使三者 active
或 queue 非空，public `animating` 也故意忽略它们；physics 只在 progress 尾阶段消费。

## 9. progress 与 reset

Engine 保留原始 double dt，controller slice 使用独立 working remainder；Player progress 后，
physics gate 检查原始 `dt != 0 && !directEdit`。NaN 与零不相等，因而进入；随后仅窄化一次为
float。outer controllers 按 bust→hair→parts 用完整原始 dt step，不使用1.1 slice remainder；接着
simple→hair chain→parts chain。

D3D facade 在进入 Engine 前有 exact `dt==0` gate；直接 Motion facade 把毫秒乘 `60/1000` 后
即使结果是0也进入 Engine core。两者的 nonzero physics 语义相同。

reset 顺序：

1. reset bust、hair、parts Var controllers，queue 非空时提交 back target；
2. 对 simple deque 每项写 `spring.firstFlag=1; entry.initFlag=1`；
3. 对 hair chain、parts chain 做相同两 flag reset；
4. 继续重置其他 controller families。

它不清 spring deque、不重置 anchors/pos/vel/bend/wind borrow、不重置 scale。下一次 wrapper 因
entry initFlag 会以完整 dt step，而 solver firstFlag 会重新建立 anchor delta。

## 10. outer-force persistence 与 partial commit

outer state 是按 bust→hair→parts 顺序写入的三键 Dictionary。每个 child 使用 shared two-channel
Var schema：

```text
phase, tick, speed, exponent,
frame[2], prev[2], target[2]
```

不保存 queue、scale、spring state、entry flags 或 variable-map outputs。serialize 创建临时
Dictionary/Array owners，不改 controller。

restore 先要求 outer value 为 Object，随后按 bust→hair→parts 取 child 并原地 restore。缺 child
产生 Void，使该 controller no-op；属性缺失保留原字段。conversion、callback、短 channel Array
或强制 Object 失败时，已恢复的前 controller、scalar 与 channel prefix 不回滚，后续 child 不执行。
live queue 与 scale 始终保留。

## 11. 本地映射与新增测试

生产逐行比较：

- `EmoteSpring.h/.cpp`：两个 fixed objects、constructors、simple/chain/post-bend；
- `EmoteEngine.h/.cpp`：三个 owner controllers/deques、builders、wrappers、router、reset、
  serialize/restore、progress；
- `EmotePlayer.h/.cpp`：直接/D3D scale surfaces、两种 setOuterForce、D3D TODO getter；
- `main.cpp` 与 `DrawDeviceD3D.cpp`：相应 NCB method/property 发布。

既有测试已覆盖 constructor hints、simple alias/NaN、chain constraint/udEft/NaN/post-bend alias、
builder publication、shape anchor、direct/D3D scale 与 outer setter。新增
`outer-force state excludes scales and preserves live controller queues` 补齐 persistence、public
animating 排除和 reset flags/scale 边界。

## 12. IDB 改进与 disposition

本轮为此前未命名的 D3D progress、setOuterForce 和六个 scale leaf 共写回32个确定性函数名；
四份 IDB 共追加123条 `MP-R14` root 语义注释，各加1个 task bookmark并原位保存。Android
arm64 的直接 Motion wrapper/Engine tail ownership 与独立 D3D progress 名称均已明确，避免把
远端共享 core 误算为 D3D 私有实现。

因此 `MP-R14` 可标为 `CLOSED_STATIC`。正式 native unit、Web runtime 与 motion trace
differential 执行仍统一由 `MP-V` 任务跟踪。
