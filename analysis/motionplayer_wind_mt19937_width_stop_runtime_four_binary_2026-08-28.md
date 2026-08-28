# Wind emitter、共享 MT19937 与宽度停止条件总审计（四参考二进制，2026-08-28）

## 1. 任务结论

本 slice 闭合 MP-R15。Wind 不是独立随机系统，也不是动态 particle container：它是 Engine
raw single owner 指向的固定 0x61C 对象，内嵌128个12B slot；Blink constructor、Blink wait
phase 与 Wind chance/y spawn 共同消费一个 clock-seeded、无锁、永不释放的 process-global
MT19937。

四端唯一真正的业务分叉是 setWind stop predicate：两个64位目标与两个32位目标各自一致，
不能用单一 target-independent 条件“整理”。固定池构造、restart reuse、emission、strict kill、
chain first-hit lookup、direct/D3D stop、owner replacement、RNG state width和失败/并发边界已经闭环。

本地生产实现匹配。本轮没有生产语义修改；单元测试新增同端点 restart 保留 live slot，以及
stop 返回前不更新五个 cache 的断言。

## 2. fresh 四端证据

本轮 fresh decompile、完整 disassembly 与 xrefs_to 覆盖60个 distinct code ranges，共7,022条
完整未截断指令和397个未截断 xrefs。四端各15个 range：Android arm64 把 MT regenerate
内联到 canonical helper、保留独立 Wind ctor；其余三端保留独立 regenerate、把 Wind ctor
内联到 setWind。

Android arm64 的 Engine progress core 是直接 Motion progress 尾包装器的 IDA tail chunk；
本轮对 0x67A3F8..0x67A8B0 单独扫描得到302条，不把7指令毫秒换算 wrapper 重复计入。

| target | ranges | instructions | xrefs_to |
|---|---:|---:|---:|
| Android arm64 | 15 | 2,693 | 116 |
| Android armv7 | 15 | 1,418 | 101 |
| iOS arm64 | 15 | 1,263 | 92 |
| iOS armv7 | 15 | 1,648 | 88 |

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| rough-ms seed helper | 0xA2A308 / 10 | 0x770E54 / 24 | 0x1002964AC / 10 | 0x29AAAC / 11 |
| global get + inline seed ctor | 0x9F0308 / 42 | 0x7508D4 / 40 | 0x1002C24B0 / 36 | 0x2C7878 / 83 |
| MT regenerate | inline | 0x750578 / 56 | 0x1002C223C / 48 | 0x2C7692 / 54 |
| canonical draw | 0x9F00D0 / 142 | 0x750838 / 51 | 0x1002C23E0 / 51 | 0x2C77DC / 51 |
| Blink ctor consumer | 0x65FD48 / 792 | 0x551B34 / 375 | 0x1001A1C8C / 280 | 0x1A0E50 / 476 |
| Blink step consumer | 0x660FBC / 250 | 0x552472 / 245 | 0x1001A27A0 / 223 | 0x1A19D8 / 262 |
| Engine setWind | 0x66DD8C / 84 | 0x559900 / 114 | 0x1001AC718 / 99 | 0x1ABF24 / 120 |
| Wind ctor | 0x66DEDC / 136 | inline | inline | inline |
| Wind step | 0x665BC8 / 85 | 0x554E4C / 99 | 0x1001A5A24 / 85 | 0x1A4FEC / 110 |
| chain lookup | chain solver 0x665D84 / 289 | 0x554FA0 / 32 | 0x1001A5B78 / 25 | 0x1A5160 / 32 |
| chain borrow wrapper | 0x6790C8 / 235 | 0x55F2F4 / 191 | 0x1001B2F2C / 198 | 0x1B2ABC / 221 |
| Engine progress core | 0x67A3F8 / 302 | 0x55FEF0 / 95 | 0x1001B4304 / 89 | 0x1B3E10 / 104 |
| direct Motion stopWind | 0x67EE18 / 11 | 0x561D90 / 9 | 0x1001B5CD8 / 11 | 0x1B5944 / 9 |
| D3D startWind | 0x530A60 / 3 | 0x494D94 / 3 | 0x1002331D4 / 3 | 0x231E38 / 3 |
| D3D stopWind | 0x530A6C / 8 | 0x494D9C / 13 | 0x1002331E0 / 8 | 0x231E40 / 13 |
| Engine ordinary dtor | 0x67C898 / 304 | 0x5610E8 / 71 | 0x1001B8B4C / 97 | 0x1B814E / 99 |

rough-ms helper 有大量 motionplayer 外部 caller，因此 xref 总数较高；本轮没有把它错误命名为
Blink 私有函数，而是恢复为 generic TVPGetRoughTickCount32_guess。

## 3. shared MT19937 object 与初始化

四端均检查一个 zero-initialized raw global pointer。miss path：

    storage = operator new(pointer64 ? 0x1398 : 0x9CC)
    seed64 = steady_clock::now().time_since_epoch().count()
    seed32 = low32(seed64 / 1_000_000)
    vptr = MT virtual table
    left = 1
    mt[0] = seed32
    for i = 1..623:
        word = 1812433253 * (word xor (word >> 30)) + i
        mt[i] = pointer_width_slot(low32(word))
    cursor = mt
    left = 1
    global = completed object

LP64 的 vptr/left/pad/cursor 后是624个64-bit slots，ILP32 是624个32-bit slots；算法只使用每个
slot 的低32位，所以序列一致。对象有虚析构 prefix，但 global 没有 dtor registration、
__cxa_atexit、guard、mutex、atomic 或 thread-local storage。

new/clock/inline ctor 失败时 global 仍为 null，后续调用重试。两个线程同时首次调用可各自构造，
last store wins，另一个完整对象泄漏；同时 draw 会在 left/cursor/mt 上发生无锁 data race。

## 4. regenerate、temper 与 canonical double

每个 word 先 pre-decrement left；旧 left 为1时 regenerate，并设 cursor=mt,left=624。原版
regenerate 以227 + 396 + final 三段处理 MT19937 wrap：offset397、upper mask 0x80000000、
lower-without-bit0 0x7FFFFFFE、matrix 0x9908B0DF。后半段依赖本轮已经写回的低 index slots；
不能改成从独立旧 state snapshot 读取。

temper 固定顺序：

    y ^= y >> 11
    y ^= (y << 7)  & 0x9D2C5680
    y ^= (y << 15) & 0xEFC60000
    y ^= y >> 18

canonical draw 连续调用两次 next-word；第一次 draw 后可能恰好触发 regenerate，第二次从新 state
首词取值。第一个 tempered word 放 mantissa bits 0..31，第二个仅取低20位放 bits 32..51，
加 0x3FF0000000000000 构造 [1,2) double 后减1。因此它不同于常见的
std::generate_canonical<double,53> 高低拼接顺序。

## 5. Blink 与 Wind 的共享消费顺序

global getter 的 motionplayer consumer closure 是：

- Blink ctor 读取 interval metadata 后消耗一个 canonical draw，初始化 wait timer；
- Blink phase11 到期消耗一个 draw，选择下一 wait interval；
- Wind 每个 emission unit 先消耗 chance draw；
- chance < 1/16 且找到 free slot 时再消耗 y draw。

这四处都取得同一个 raw pointer。Blink 构造后续 graph 失败、Blink/Wind 后续状态异常或 full
pool 都不会回滚已经消费的序列。full pool 仍消费 chance，但跳过 y；chance 未命中也只消费
chance。没有 per-Engine seed、per-controller stream 或 deterministic replay state。

固定 seed 的局部 MT 可精确测试；clock-seeded global 的跨进程首序列不可稳定 oracle。测试通过
直接重写 global state 来验证 Wind/后续 canonical 的共享消费，而没有把生产 seed 改成固定值。

## 6. setWind normalization 与真实宽度分叉

共同 normalization：

    amp = abs(inputAmplitude)
    if inputAmplitude >= 0:
        normalizedMin = inputMin
        normalizedMax = inputMax
    else:
        normalizedMin = inputMax
        normalizedMax = inputMin

NaN amplitude 的 ordered >=0 为 false，所以交换 endpoints，amp 仍为 NaN。-0.0 走
nonnegative branch，abs 后等于0并停止。

stop predicate：

| pointer width | exact condition |
|---|---|
| 64-bit | amp==0 或 normalizedMax==normalizedMin 或 freqX==0且freqY==0 |
| 32-bit | amp==0 或 freqX==0 |

所以64位允许 freqX=0,freqY!=0，但拒绝 collapsed interval；32位相反地只看 freqX，既不看
collapsed interval，也不让 freqY 单独维持 emitter。NaN equality 按 ordered compare 处理。

stop path delete/null raw emitter 后立即 return，五个 cache 完全保留：windMin/max/amp/freqX/freqY
仍描述最后一次成功 start，不改成零。

## 7. raw owner reuse、replacement 与失败窗口

非 stop path 先读取 metadataScale。若 emitter 为 null 或 normalized endpoints 与两个 cache 不同，
就 replacement：

1. 有旧 owner 时先 delete；member 暂不置 null；
2. new EmoteWindEmitter(normalizedMin/scale, normalizedMax/scale)；
3. 完整 ctor 后才写 member。

所以旧 owner delete 后 allocation failure 会让 member 保留 freed address；cache 也仍是旧值。下一次
set、progress、chain lookup 或 Engine dtor 可按原版触发 double-delete/UAF。若无旧 owner，失败后
member 保持 null。division by zero/NaN 是普通 FP 传播，不抛异常。

normalized endpoints 相等时复用原 fixed pool：所有 active slots、lifePos/yPos/padding 保留。成功
start 随后更新五个 caches，并写：

    yHi=freqX; yLo=freqY; gate=1
    velocity=sign(endPos-startPos) * amp/metadataScale
    emitAccumulator=0

restart 不清 slots。NaN endpoint cache 永远不等于自身，因此重复 start 每次 replacement。

## 8. fixed emitter layout 与 constructor

对象固定1564B：

    slots[128] @ 0, stride 12
      byte active; 3B padding; float lifePos; float yPos
    startPos @1536; endPos @1540
    gate @1544; 3B padding
    yHi @1548; yLo @1552; velocity @1556; accumulator @1560

ctor 只清128个 active byte；slot padding、inactive life/y payload 和 gate padding 保留 allocator
内容。tail 初始化 endpoints、gate0、yHi=1、yLo=0、velocity=0、acc=0。没有 dynamic container、
slot constructor/destructor或 separate occupancy count。

## 9. Wind step 与停止条件

gate byte 由 Engine progress 在每个 controller slice 前检查；owner null 或 gate0 时不 step。
step 先：

    accumulator += abs_by_ordered_branch(velocity) * dt
    while ordered(accumulator >= 0):
        chance = RNG()
        if ordered(chance < 1/16):
            free = first slot index 0..127 with active==0
            if free:
                active=1; lifePos=startPos
                yPos=yLo + (yHi-yLo)*RNG()
        accumulator -= 1

inclusive >=0 使 accumulator 精确0也 roll 一次。full pool/failed chance 都仍减1。spawn 完成后
遍历全部128 slots，因此新 slot 在同 call 立即移动，甚至立即越过终点并死亡。

kill 只使用 strict direction comparisons：positive velocity 在 nextPos>end、negative 在
nextPos<end 时清 active；到达 equality 仍存活。velocity==±0 不选任何方向，永不 kill；
NaN velocity/dt 使 acc/position NaN，ordered emission/kill 均 false，active slot 带 NaN 存活。
inactive slot payload不清。

## 10. chain lookup 与 borrowed lifetime

chain wrapper 在每个 node 的 solver gate 以前写 spring.collisionCurve=engine.windEmitter。lookup
按 index 0→127 扫 active slot：

    halfWidth = yPos/2 + 4
    if lifePos-halfWidth < segmentX < lifePos+halfWidth:
        return yPos * velocity
    return 0

区间两端 strict；多个 overlap 只取第一条；NaN operand 不命中。Android arm64 内联该扫描，
其余三端保留 helper。borrow 不 AddRef/delete；stop 后旧 chain field 暂时 dangling，但下一次 wrapper
在 solver 前刷新。若 setWind replacement allocation 失败使 Engine member 自身 dangling，刷新只会
复制坏地址，随后 lookup UAF。

## 11. façade stop 与 teardown

直接 Motion.EmotePlayer.stopWind 是独立 delete/null body，不调用 setWind，因此显式保留 cache。
D3D stop 则用五个0调用 setWind；两种 pointer-width stop predicate 都立即进入 stop path，最终也
保留 cache。D3D start/stop 仅多 shell→EmoteObject→Engine pointer hops。

Engine ordinary destructor 首先 delete raw wind owner，然后才销毁 Player、controller owners 与
后部 spring deques。chain collisionCurve 在短暂窗口内 dangling，但 fixed spring/node destructor
不读取。global MT 与 Engine 无 owner 边，Engine teardown 不影响 RNG，进程退出也不 delete 它。

Wind emitter、slots、caches、RNG state 都不在 Engine serialize schema；serialize/unserialize clone
不会复制 wind。该跨任务 persistence disposition由 MP-R21 的八组 state schema独立闭合。

## 12. 本地映射、测试与 IDB disposition

生产映射：

- EmoteBlinkRng.h/.cpp：global owner、seed、regenerate、temper、canonical；
- EmoteWindEmitter.h/.cpp：fixed layout、ctor、step；
- PlayerCore.cpp：pointer-width setWind、cache/raw-owner flow；
- EmoteSpring.cpp / EmoteEngine.cpp：first-hit lookup、borrow refresh、progress gate；
- EmotePlayer.cpp：direct delete/null 与 D3D five-zero stop。

既有 unit tests 覆盖 ctor byte preservation、strict direction kill、NaN/zero、spawn/full-pool RNG、
lookup strict interval、MT seed/twist/canonical和between-word regenerate、pointer-width stop。新增断言
覆盖 same-endpoint pool reuse 与 stop cache retention。

四份 IDB 共追加60条 MP-R15 root 注释、4个 bookmarks 并保存；新增3个 generic rough-ms helper
名称、3个 MT regenerate 名称和8个 D3D start/stop 名称，共14个确定性 task helper renames；另将
前一 slice 的 Android arm64 D3D progress 名称按最新 tail ownership纠正为独立 wrapper。

因此 MP-R15 可标为 CLOSED_STATIC。正式 unit、Web runtime 与跨目标 motion trace
differential 仍由 MP-V 跟踪。

