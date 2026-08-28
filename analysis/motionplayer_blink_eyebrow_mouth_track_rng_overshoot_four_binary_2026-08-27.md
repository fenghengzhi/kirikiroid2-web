# Blink / Eyebrow / Mouth track、RNG 与 overshoot 总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 闭合 `MP-R12`：三个 controller 虽然都使用12B primary keyframe，却不是一个可统一的
状态机：

- Eye/Blink拥有primary track、resolver-produced secondary track、mesh graph、可在同call重入的
  value-track machine，以及独立4阶段blink machine；
- Eyebrow拥有相同两层track与graph，但每call最多推进一个stage，没有blink fields、RNG或output
  remap；
- Mouth只有primary track与普通scalar easing，每call最多一个phase，另行输出固定beginFrame和
  current talk value。

四端overshoot condition-code、NaN/unordered compare、track pop顺序、blink RNG消费次数、large-dt
阶段丢弃和reset行为已经闭合。本地生产实现匹配，无生产语义修改。本轮只补Mouth large-dt完成
当前key但不启动下一key的回归断言。

## 2. fresh 四端证据

本轮对48个 distinct function ranges全部fresh decompile、完整disassembly、`xrefs_to`，合计
7,109条完整未截断指令与99个xrefs；全部decompile成功，全部cursor `done=true`。

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Blink ctor | `0x65FD48` / 792 | `0x551B34` / 375 | `0x1001A1C8C` / 280 | `0x1A0E50` / 476 |
| Eyebrow ctor | `0x661BEC` / 686 | `0x552CDC` / 299 | `0x1001A31F4` / 227 | `0x1A2560` / 369 |
| Mouth ctor | `0x663078` / 102 | `0x55369C` / 64 | `0x1001A3DE4` / 39 | `0x1A3200` / 85 |
| Blink step | `0x660FBC` / 250 | `0x552472` / 245 | `0x1001A27A0` / 223 | `0x1A19D8` / 262 |
| Eyebrow step | `0x6629E0` / 145 | `0x553280` / 127 | `0x1001A38C8` / 121 | `0x1A2C56` / 145 |
| Mouth step | `0x663448` / 80 | `0x553838` / 70 | `0x1001A3FC8` / 70 | `0x1A3402` / 84 |
| Blink target | `0x660C90` / 124 | `0x5522FC` / 50 | `0x1001A2568` / 34 | `0x1A1850` / 38 |
| Eyebrow target | `0x6626B4` / 124 | `0x553170` / 50 | `0x1001A3764` / 34 | `0x1A2B6C` / 38 |
| Mouth target | `0x663214` / 99 | `0x553788` / 46 | `0x1001A3EE0` / 30 | `0x1A3358` / 34 |
| global RNG get | `0x9F0308` / 42 | `0x7508D4` / 40 | `0x1002C24B0` / 36 | `0x2C7878` / 83 |
| canonical draw | `0x9F00D0` / 142 | `0x750838` / 51 | `0x1002C23E0` / 51 | `0x2C77DC` / 51 |
| resetControllers | `0x66BF6C` / 180 | `0x558888` / 64 | `0x1001AB03C` / 27 | `0x1AA714` / 25 |

step的四端调用都来自Engine progress对应category walk；target helper来自`setVariable` type
4/5/6 dispatch。每个ctor只有对应metadata builder一个caller。

## 3. 三个source对象不是继承族

### 3.1 Eye/Blink

```text
deque<12B key> primary
deque<pair<float,float>> secondary
MeshResolverState graph/candidates
track state/value/target/direction/span/accum/invDuration/power
beginFrame/endFrame/blinkPhase
intervalMin/intervalMax/frameCount/timer/blinkPos/enabled
```

### 3.2 Eyebrow

前半同样是primary、secondary、resolver，但curve scalar物理声明顺序不同，尾部只有beginFrame；
没有endFrame、interval、blinkPhase/timer/pos/enabled。它不是Eye的base/derived slice。

### 3.3 Mouth

只有 `deque<12B key>`、state/current/end、accum/invDuration/power/start和beginFrame；没有secondary、
resolver、RNG或blink state。

四端对象size/offset差异由pointer width与libstdc++/libc++ deque/vector header解释，source member
角色和顺序一致。

## 4. constructor数据流与RNG差异

Blink constructor：

1. empty primary/secondary/resolver containers；
2. state/target/direction/blinkPhase置0；
3. strict读取begin/end、interval min/max、frame count、enabled；
4. `trackValue=blinkPos=float(beginFrame)`；
5. 从共享global MT取一个canonical，timer=`min+(max-min)*random`；
6. strict读取edge/node arrays，构造edge vector与deque-of-row-vectors。

Eyebrow只读取beginFrame与edge/node，seed trackValue；**不调用RNG**。Mouth只读取beginFrame，
state/current/end置0，四个interpolation fields保持未初始化到首次setup。

构造任一property/conversion/container allocation失败按已构造prefix unwind；Blink在RNG draw之后
graph失败时，global stream consumption不回滚。raw controller完成后到Engine deque publication前的
异常/leak window由controller element owner report闭合。

## 5. target/replace行为

Eye、Eyebrow、Mouth都使用：

```text
if !(duration > 0): immediate snap
else optional replace then push 12B {value,duration,power}
```

所以NaN duration与0/negative一样snap，这与VarController的NaN-queues family split不同。

- Eye/Eyebrow snap或`append=false`同时clear primary和resolver secondary并置state0；
- Eye/Eyebrow `append=true`保留两条已有tracks；
- Mouth只有primary可clear/preserve；
- replace clear发生在new deque block allocation之前，失败留下已clear idle state；
- append allocation失败保留旧prefix/state；
- power是raw float word copy，不做integer conversion。

## 6. Eye value-track machine

Eye track使用外层无限loop，完成segment后可以在同一次step继续：

1. state0 pop primary，然后调用mesh resolver生成secondary、发布span/invDuration/power，进入state1；
2. state1 pop一个secondary `{start,end}`：
   - equal endpoint：写trackValue、保留state1，立即退出track部分；不看下一segment；
   - non-equal：写target/direction，进入state2；
   - empty：回state0，并可同call继续下一个primary；
3. state2按power curve推进trackAccum/value；未overshoot则保存accum并退出；overshoot则clamp target、
   state1，并立刻回loop继续secondary/primary。

Eye在state0先从primary `pop_front`，随后才调用resolver。因此resolver allocation/search抛出时当前
primary已经丢失；earlier secondary clear/candidate publication按resolver direct report保留。

大dt没有被拆成residual：若overshoot后同call启动下一nonzero segment，下一段仍看到本次原始dt。
这不是物理时间守恒器，而是原版离散状态机。

## 7. Eye overshoot condition codes

共同predicate不能简单写成一个对称`direction>0 ? next>=target : next<=target`：

```text
if direction > 0:
    overshoot = ordered(target <= next)
else if !(direction >= 0):
    overshoot = !(target < next)
else:
    overshoot = false
```

结果：

- positive direction + NaN target：ordered LS false，不overshoot；
- negative direction + NaN target：`!(NaN < next)` true，overshoot；
- NaN direction走第二分支；finite target是否overshoot由complemented target compare决定；
- `direction==±0`不overshoot。

这些来自ARM FCMP/VCMPE condition-code组合，现有三section test逐项锁定。

## 8. Blink phase machine与large-dt丢弃

value-track后只执行一个switch case：

| phase | 行为 | transition |
|---:|---|---|
| 0 | enabled且`sat_i32(blinkPos)==beginFrame`时timer-=dt | timer<=0只写phase10 |
| 10 | 以`dt*2.5/frameCount`向end推进 | clamp end、phase11、timer=frameCount/5 |
| 11 | timer-=dt | timer<=0抽一次RNG、写新interval、phase12 |
| 12 | 以`dt*-2.5/frameCount`向begin推进 | clamp begin、phase0 |

没有case fallthrough或while catch-up。无论dt多大，每call最多跨一个blink phase；超出threshold的
时间不结转到下一phase。phase0触发不会同call closing，phase11到期不会同call opening。

wait中的float→int是target饱和语义：NaN→0，`+Inf/+2^31`→INT_MAX，`-Inf/-2^31`→INT_MIN，
finite in-range向0截断。final output在trackValue位于inclusive `[begin,end]`时做blinkPos remap；
`end==begin`保留divide-by-zero/NaN行为。

## 9. Eyebrow one-stage machine

Eyebrow每call只执行入口state对应的一段：

- state0：resolver成功后才pop primary，发布curve fields并变state1；不像Eye，resolver抛出保留
  primary；
- state1：最多pop一个secondary；equal endpoint保留state1，non-equal变state2，empty变state0；
- state2：只做一次curve update；overshoot时clamp target、变state1，但不在本call继续。

overshoot时它把`delta`改成`(target-next)*direction`，然后发布
`trackAccum=previousAccum+delta`。这只是correction term，不是clamp到span；现有test的正向大dt
会得到accum `-1`，精确匹配四端。

Eyebrow unordered predicate又不同：

```text
if !(direction <= 0): overshoot = !(target > next)
else if direction >= 0: overshoot = false
else: overshoot = !(target < next)
```

因此positive/NaN direction与NaN target的结果和Eye不同；不能抽取共享overshoot helper。

## 10. Mouth setup、step与overshoot

Mouth state0 + nonempty queue只setup：start=current、end=front.value、inv=1/duration、accum=0、
pow=raw copy、pop、state1；**同call不插值**。

state1：

```text
phase = accum + invDuration*dt
if phase >= 1:
    state=0; accum=1; current=end
else:
    current=powf(phase,pow)*(end-start)+start
```

无论state如何，始终输出`float(beginFrame)`和current，并返回beginFrame。large dt只complete当前
key，overshoot丢弃，下一queued key留到后续call。新增test使用dt=100验证：setup call不推进、
completion call不pop下一key、第三call才setup下一key。

NaN phase ordered `>=1` false并传播pow结果；unknown nonzero state只输出，不自动修复。

## 11. reset与track completion

Eye/Eyebrow reset：

- primary非空：current=primary.back.end、state0、clear primary+secondary；
- primary空但state非0：secondary空则current=trackTarget，否则current=secondary.back.first；clear
  secondary、state0；
- blinkPhase/timer/blinkPos/enabled完全不reset。

Mouth reset由Engine helper链内联：primary非空取back.end并clear；否则active时取endVal；state0。

public animating对Eye/Eyebrow只看`trackState!=0 || !primary.empty()`，对Mouth同理；malformed
`state==0`但secondary非空不单独贡献animating。timeline-driven label过滤由R11/animating report
闭合。

## 12. shared MT19937 lifecycle与bit-exact output

四端只有一个raw process-global pointer，Blink和Wind共享：

- miss时`operator new`、steady clock count除1,000,000后low32 seed、完整construct后store；
- 没有`__cxa_guard`、mutex、atomic、TLS或normal exit destructor；
- constructor失败保持null、下次重试；
- concurrent first calls可双建/覆盖泄漏，concurrent draws对left/cursor/state构成data race。

MT state是624个pointer-width slots，但算法只用low32。seed recurrence、397-offset twist、
`0x9908B0DF`和temper顺序与MT19937一致。

canonical output连续取两个tempered words：第一个填mantissa低32位，第二个low20填高20位，OR
`0x3FF0000000000000`形成`[1,2)` double，再减1。每个word先`left--`，旧left==1时regenerate，
所以一次canonical draw可以在两个word之间twist。现有seed5489与mid-draw-regeneration tests精确
覆盖。

RNG消费边界：Blink constructor一次；blink phase11到期一次；Wind emission chance/coordinate按
自己的分支继续消费同一stream。Eyebrow/Mouth不消费。

## 13. 本地映射与tests

生产：

- `cpp/plugins/motionplayer/EmoteBlinkController.cpp:48` / `:125` / `:151` / `:311`；
- `cpp/plugins/motionplayer/EmoteEyebrowController.cpp:43` / `:98` / `:124` / `:202`；
- `cpp/plugins/motionplayer/EmoteMouthController.cpp:10` / `:18` / `:45`；
- `cpp/plugins/motionplayer/EmoteBlinkRng.cpp:31` / `:62` / `:92` / `:101`；
- `cpp/plugins/motionplayer/EmoteMeshResolver.cpp`：secondary path producer；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1266`：reset order；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1339`：animating；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1771`：三个metadata builders。

tests：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:11760`：MT word/canonical/mid-draw twist；
- `:28050`：Eye replacement、track reentry、blink phases、saturated wait；
- `:28200`：Eyebrow one-stage与Eye/Eyebrow unordered overshoot矩阵；
- `:28390`：Mouth ctor/setup/step，本轮追加large-dt next-key deferral；
- Wind共享stream消费测试位于`:11540`附近。

生产无需修改；新增Mouth测试不会改变ABI。`git diff --check`通过，正式运行留给MP-V。

## 14. IDB固化与disposition

四库新增20项确定性名称：四端Eyebrow/Mouth step与三族target helpers。Blink step/三个ctors和RNG
此前已有名称。48个R12 roots写comments、4个bookmarks，四库保存。

| 要求 | disposition |
|---|---|
| track topology | Eye/Eyebrow双track+resolver，Mouth单track闭合 |
| stage progression | Eye reentry、Eyebrow/Mouth one-stage闭合 |
| overshoot | ordered/unordered condition codes、correction/丢弃策略闭合 |
| blink phases | one-case-per-slice、remap、saturated int闭合 |
| RNG | shared raw global、seed/twist/temper/canonical/消费次数闭合 |
| reset/completion | primary/secondary terminal commit、blink state保留、animating predicate闭合 |
| local | 生产已匹配；补Mouth large-dt test；无task-local静态缺口 |

因此 `MP-R12` 可标为 `CLOSED_STATIC`；动态验证继续由MP-V跟踪。
