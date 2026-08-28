# motionplayer 全局 RNG、静态 cache、texture cache、registrar 与 guard 生命周期总审计（四参考二进制，2026-08-27）

## 1. 范围与结论

本报告逐项闭合：

- `MP-L15`：global RNG、static method cache、texture cache、registrar/static guard 的生命周期；
- `MP-B09`：static guard、全局 cache、RNG 和首次初始化并发。

本轮先从当前 motionplayer / emoteplayer 根可达闭包重新枚举所有有状态的进程级对象和
function-local static，再回到四份 IDB fresh 检查代表每个等价类的创建、读取、写入、异常
cleanup、xref 和退出析构。除了复用已经完成的细分报告，本轮新增的关键直接证据是此前没有
独立入账的 blink / wind 共享 MT19937 全局对象。

本轮 fresh decompile、完整 disassembly 和 xref 共覆盖 64 个函数实例、13691 条指令；全部
反汇编游标完整、没有截断，所有反编译均成功。四端联合结论：

1. blink controller 与 wind emitter 共享一个 clock-seeded MT19937 raw process pointer；它
   没有 guard、mutex、atomic 或 exit destructor。构造完成后才发布；失败重试；并发首建和
   并发 draw 都是原版 data race。
2. private `opengl` manager、默认 software-renderer 判定、accurate-SLA 选择、Private-GLL
   class dispatch、alpha-mask compiled methods、D3D clear method/parameter ID 和 StretchType ID
   使用 C++ function-static guard。成功后进程期不重读；初始化异常按 active stage abort/retry。
3. deep / Private-GLL render method selector 的 12 个 arm 不使用 `__cxa_guard`：method pointer
   本身是零初始化 sentinel，先发布 pointer，随后写 parameter ID。后半初始化失败不会清
   pointer，也不会重试已经越过的阶段。
4. Player shared D3DAdaptor 是无 guard 的 raw process pointer。成功创建后永久持有 Window、
   target texture 和 software-copy ordered map；没有 exit destructor。它使原本 per-adaptor 的
   texture map 在 shared draw 路径上成为事实上的 process-lifetime texture cache。
5. cubic Bezier basis map、default point vector、decrypt filter 和 module-loader set/map 是
   真正有析构的全局 owner，均由 static initializer 注册反向退出析构。basis cache 与 filter
   runtime replacement 没有业务锁；registrar chain node / ClassInfo tuple / method pointer /
   TJS hint word 则是 POD 或 borrowed raw state，不在退出时 Release pointee。
6. `motionplayer.dll`、`emoteplayer.dll` 和 DrawDeviceD3D dependency 的 registrar 是
   process-lived append-only / one-way publication；模块 loader 没有 unload caller 可以把这些
   native class/registrar 状态恢复为初值。

当前本地实现与四端联合证据一致，本轮没有语义 C++ 修改。

## 2. fresh 函数证据

### 2.1 global RNG 与直接消费者

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| global get / construct | `0x9F0308`，42 | `0x7508D4`，40 | `0x1002C24B0`，36 | `0x2C7878`，83 |
| next canonical | `0x9F00D0`，142 | `0x750838`，51 | `0x1002C23E0`，51 | `0x2C77DC`，51 |
| blink step | `0x660FBC`，250 | `0x552472`，245 | `0x1001A27A0`，223 | `0x1A19D8`，262 |
| wind step | `0x665BC8`，85 | `0x554E4C`，99 | `0x1001A5A24`，85 | `0x1A4FEC`，110 |

global getter 的完整 xref 集在每端都是 5 条：blink constructor、blink step、wind step 中的
两个坐标 draw，以及同一集成二进制中一个 motionplayer 范围外的独立 consumer。canonical
helper 的 motionplayer xref与上述四处一一对应；额外薄 wrapper / TJS callback 属于共享
库层，不改变本闭包的四个直接 consumer。没有第二个 blink/wind generator、per-Engine
generator 或 thread-local generator。

### 2.2 guard / manual-cache / registrar 代表闭包

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| private `opengl` manager | `0x6930E4`，51 | `0x570EA0`，46 | `0x1000F3D90`，28 | `0xF0834`，68 |
| normal method selector | `0x6D9898`，177 | `0x59B1FC`，218 | `0x100129134`，178 | `0x12827C`，214 |
| alpha-test method selector | `0x6D9470`，247 | `0x59AE64`，282 | `0x100128D00`，240 | `0x127F38`，261 |
| D3D clear cache | `0x6AB08C`，84 | `0x57D184`，100 | `0x100104130`，67 | `0x10149C`，130 |
| accurate-SLA guarded choice | `0x6D2A38`，184 | `0x597328`，118 | `0x1001233C8`，121 | `0x12257C`，207 |
| Private-GLL guarded class | `0x6D2D28`，142 | `0x5974D0`，110 | `0x100123670`，103 | `0x122884`，169 |
| cubic basis cache | `0x69DE30`，167 | `0x576C7C`，142 | `0x1000FB4A8`，107 | `0xF854C`，124 |
| motionplayer static root | `0x42F1F8`，80 | `0x3016E8`，85 | `0x10014FC74`，66 | `0x151C98`，98 |
| shared D3DAdaptor-containing `Player::draw` | `0x6D3398`，371 | `0x597864`，293 | `0x100123C84`，270 | `0x122F28`，423 |
| default software predicate | `0x848BDC`，29 | `0x65728C`，27 | `0x100323EB8`，22 | `0x32930C`，63 |
| six alpha-mask GPU caches | `0x6AC4E4`，1509 | `0x57E1E8`，1433 | `0x100104E68`，1197 | `0x10243C`，1654 |
| emoteplayer static module record | `0x42EEE0`，31 | `0x3013BC`，33 | `0x1001CAE20`，26 | `0x1C8EB2`，41 |

每个函数都重新调用 decompiler，并读取完整 disassembly / xref。表中 body 与以下既有细分
报告交叉复核，而不是只复述本地源码：private manager、D3DAdaptor lifecycle / clear、direct
SLA、Private-GLL class、deep renderer、alpha mask、Bezier basis、module roots 和 plugin loader。

## 3. process-state 总表

| family | 物理 owner / sentinel | 首次发布 | 初始化失败 | 正常退出 | 并发 / 重入边界 |
|---|---|---|---|---|---|
| blink/wind RNG | raw global `EmoteBlinkMt19937*` | 完整 clock-seeded ctor 返回后 store | slot 保持 null，下一次重试 | 永不 delete | 首建、两-word draw、regenerate 全部无锁 data race |
| default software predicate | guarded `bool` | 默认 manager virtual query 返回后 guard_release | active guard abort，后续重试；部分 ABI cleanup 外置 | 无析构 | guard 使首次初始化线程安全；成功后不再查询 manager |
| private OpenGL manager | guarded borrowed raw pointer | named `opengl` lookup 和临时 ttstr cleanup后 | guard_abort并重试 | 不 Release | 首次初始化由 ABI guard串行；成功后不跟随 manager replacement |
| alpha-mask methods | 每 method 独立 guard + raw method；3个 threshold ID另有guard | 完整 compile / blend expression后；threshold随后单独发布 | 只 abort active stage；已完成 guard保留 | 不 Release method | 每个guard串行；不同method guards可并行，renderer内部共享状态仍可竞态 |
| D3D clear | method guard + colorId guard | method先完成，colorId后完成 | 后一阶段失败不撤销前一阶段 | 不 Release method | 两个独立guard；disabled clear不触碰任何guard |
| accurate SLA | guarded bool | prepare/projection之后第一次配置读取成功 | guard_abort重试 | 无析构 | 成功后renderer/config变化不可见 |
| Private-GLL class | guarded raw class dispatch pointer | native class ctor/registrar完成后 | pending allocation cleanup + guard_abort | 永不 delete class | guard串行首建；ClassID/lookup slot随后为process state |
| StretchType ID | guarded int | 第一次mesh submit选定manager后Enum成功 | guard_abort重试 | 无析构 | 首次manager identity固定此后ID；不同manager仍复用 |
| selector methods | 12组 BSS method sentinel + IDs | method pointer先store，随后写color/threshold IDs | pointer非null时不再补做失败阶段 | 不 Release | 完全无guard；并发可观察pointer配零/旧ID |
| shared D3DAdaptor | raw global pointer | ctor完成后store | slot null，重试；ctor自身保留原版Window失败泄漏边 | 永不delete | 无guard；并发可多建/覆盖并泄漏，map并发访问是data race |
| shared software texture map | shared adaptor内ordered map；borrowed source key + intrusive copy holder | map unique emplace后 | raw factory ref可在node失败时泄漏 | 因shared adaptor不析构而不clear | lookup/emplace/clear无锁；source地址复用保留原版风险 |
| cubic basis cache | global `map<int, vector<vector<double>>>` owner | static ctor先建空map；runtime miss先插node再fill | 留空/半成品node，后续hit不修复 | `__cxa_atexit`递归释放 | 无guard/mutex；同key/不同key并发均是container data race |
| default patch points | global vector owner | Motion registrar尾部reserve/push 16项 | 已push前缀保留；empty gate允许下次从当前非空前缀直接跳过 | vector dtor | no lock；并发首次填充非法 |
| decrypt filter | global `std::function` owner | replacement copy成功后swap/commit | 旧target保留 | std::function dtor | install/load无锁data race |
| plugin loader registries | global set/map owner；list借用registrar pointer | static ctor后，AllRegist/LoadModule append/commit | 已append/已load前缀保留 | internal map先析构，registered set后析构 | startup once不存在；重复AllRegist可追加重复borrow |
| NCB registrar / ClassInfo | static chain nodes、POD tuple和borrowed class pointer | static root/Setup按阶段发布 | member前缀和早期tuple字段不回滚 | registrar node不unlink；class pointer通常不Release | runtime loader路径本身无plugin级锁；依赖宿主单线程加载约定 |
| TJS hint words | 零初始化process words，dispatch按地址更新 | 第一次对应Prop/Func调用 | status/异常不重置统一全局 | 无析构 | plugin不加锁；并发调用共享同一hint word |
| stencil state / overflow | process bytes / trivial local static | begin/apply或第一次overflow前store | 已写byte不回滚 | 无析构 | 无guard；并发frame可交叉覆盖或重复/跳过message |

本地 `MotionTraceWeb.cpp`、logo trace/session mutex/map和相关 enable guards属于测试/诊断
sidecar，不是四参考 motionplayer 根可达语义；它们不进入上表。`static` 成员函数、
`constexpr`常量和函数内无状态helper也不是静态存储 owner。

## 4. blink / wind MT19937 的生命周期和共享序列

### 4.1 object layout 与初始化

四端 getter 先检查一个零初始化 raw global slot。miss路径：

```text
storage = operator new(pointer64 ? 0x1398 : 0x9CC)
seed64 = steady_clock::now().time_since_epoch().count()
seed32 = low32(seed64 / 1_000_000)
construct polymorphic MT object:
    left = 1
    mt[0] = seed32
    for i = 1..623:
        word = 1812433253 * (word XOR (word >> 30)) + i
        mt[i] = pointer_width_slot(word)
    cursor = mt
    left = 1
global = completed object
return global
```

LP64 的 624 state slots 是 64-bit，但生成、temper和canonical拼接都只读取/写入低32位；
ILP32直接使用32-bit slots。虚析构表只解释对象前缀，不代表 global owner 会在退出时调用它。
四端都没有 `__cxa_guard`、`__cxa_atexit`、atomic compare-exchange 或 mutex。

operator new / clock / constructor失败时global slot仍为null。并发两个首次调用可各自看到null、
各自构造并最后覆盖global；被覆盖对象泄漏。更严重的是非首次调用同时draw会并发修改
`left/cursor/mt[]`，其结果不是一个有定义的interleaving。

### 4.2 canonical draw

一次输出严格连续消耗两个 tempered word。每个word先执行 `left--`；旧left为1时原地
regenerate 624 slots并把 `cursor=mt,left=624`。regenerate遵循MT19937的397偏移、
`0x9908B0DF` matrix和三个wrap区间语义。temper顺序为：

```text
y ^= y >> 11
y ^= (y << 7)  & 0x9D2C5680
y ^= (y << 15) & 0xEFC60000
y ^= y >> 18
```

随后：

```text
bits = lowWord | ((highWord & 0xFFFFF) << 32) | 0x3FF0000000000000
result = bit_cast<double>(bits) - 1.0
```

这不是 `std::uniform_real_distribution`，也不是项目的 TJS `Math.RandomGenerator`。blink
constructor、blink step和wind step中的两个spawn draw共享同一sequence；调用顺序本身可观察。

## 5. method cache：guarded 与 manual sentinel 必须区分

### 5.1 manual selector caches

normal和AlphaTest selector各有六个arm：Add、Sub(2/5)、Mul、Screen、AlphaBlend `_a`、
AlphaBlend普通版。normal每arm有method+colorId；AlphaTest再有thresholdId。它们都是BSS零值，
没有guard：

```text
if method == null:
    method = privateManager.GetRenderMethod(name)   // pointer先发布
    colorId = method.EnumParameterID("color")
    [thresholdId = method.EnumParameterID("alpha_threshold")]
set color every call
[set threshold=64 every call]
```

如果GetRenderMethod返回null，紧随其后的dereference保持原版崩溃边界。如果GetRenderMethod成功、
Enum抛出，method已非null，下次不会重试Enum，ID保留零/旧值。两个线程还可同时lookup或让一个
线程在另一个只发布method后继续使用零ID。

### 5.2 guarded alpha-mask methods

`AddAlphaMask`、`AlphaMaskRev`、`AlphaMask`、`AlphaMaskThresholdFill/Crop/Threshold` 的
method动态初始化包含`GetOrCompileRenderMethod(...)->SetBlendFuncSeparate(...)`完整表达式；
只有表达式成功返回才guard_release并发布最终method值。异常会abort active method guard。
三条threshold method的`EnumParameterID("threshold")`是第二个独立guard，因此method成功、
threshold失败时后续只重试threshold阶段。method/hint/ID都不注册退出Release。

### 5.3 D3D clear、StretchType 与 other POD caches

- D3D clear的`FillARGB` method和`color` ID使用两个顺序guard；`clearEnabled=false`不读取guard。
- mesh submit的`StretchType` ID使用第一次实际submitManager进行初始化；成功后不因后续manager
  改变而重算。
- stencil enabled byte、overflow-once flag和约281个本地TJS/member hint声明都是trivial POD
  cache，不触发C++ guard。四端代表body都把相同零word地址重复传给native/TJS API；这些word
  不拥有被查询对象，也没有退出析构。

## 6. renderer、shared adaptor 与 texture cache

### 6.1 两个受guard的renderer结论

`TVPIsSoftwareRenderManager`把默认render manager的一次virtual predicate缓存为bool；
`getPrivateOpenGLRenderManager`则按宽字符串`opengl`做一次named lookup并缓存borrowed raw
pointer。两者有独立guard、独立slot，不能合并：

- 默认predicate控制software/canvas/capture分流；
- private manager提供Motion D3D target、texture upload、stencil和triangle提交；
- 首次成功后renderer replacement或配置变化均不可见；
- cached pointer无AddRef/Release/null fallback。

accurate SLA又缓存第三个独立选择：只有prepare成功并完成projection后才首次读取；software
renderer强制accurate，否则读取一次`ogl_accurate_render`。Private-GLL class则只在首次进入
legacy支路时惰性构造。

### 6.2 shared D3DAdaptor

`Player::draw`中的global slot不是C++ function static owner：

```text
if shared == null:
    width/height = MainWindow live dimensions
    shell = new D3DAdaptor(MainWindow.OwnerNoAddRef,
                           width, height, width/2, height/2)
    shared = completed shell
return shared
```

成功后它永久持有Window ref、target texture creation ref和
`map<borrowed source texture*, intrusive software copy holder>`。因此shared路径上的software
copy永不因进程退出的plugin destructor而clear。map node失败仍可能泄漏raw factory ref；
并发lookup/emplace/clear是标准容器data race。普通脚本创建的D3DAdaptor仍是每对象owner，
会在其析构中clear map；两种lifetime不能混为一谈。

## 7. 全局容器与异常 poisoning

cubic basis cache由translation-unit static root构造成空map，runtime miss先执行
`cache[division]`发布default mapped vector，再resize/逐行push四个double。任何后续异常会留下
空或半成品entry；hit不验证、不重建。它在process退出时仍会被map destructor完整回收。

default patch point vector同样是全局owner，但填充发生在Motion registrar尾部。empty check只
保护普通重复调用；reserve/push中途失败后vector已经非空，下一次会跳过余下填充，永久保留
不完整16点前缀。unit quad与selected evaluator function pointer是POD，不需要析构。

process-global decrypt filter用copy/swap替换：copy失败保留旧target，成功后旧target在返回前
析构；安装与load没有锁。plugin loader的registered set/internal map则由独立static initializer
构造并注册析构，退出时先析构internal map、后析构registered set；list中的registrar pointer
只借用，不删除registrar。

## 8. registrar、ClassInfo 与退出顺序

`motionplayer.dll` static root的共同顺序：

```text
POD unit quad
construct cubic basis map + register dtor
construct default point vector + register dtor
construct/append BezierPatch attached registrar record
construct/append Motion registrar record
register callable/binding cleanup as required by ABI
```

退出时`__cxa_atexit`逆序运行：后续binding state先清，default point vector次之，basis map最后。
静态registrar head-chain node本身没有shutdown unlink；loader中的module set/map dtor也不会调用
`Unregist`。`emoteplayer.dll` module record同样process-lived，运行时pre-reg只单向LoadModule、
Setup EmotePlayer并把两个decrypt setter发布到ResourceManager。DrawDeviceD3D dependency root
又单向发布两个native class identity并LoadModule(emoteplayer)。

ClassInfo tuple的name/id/classObject/initialized字段按Setup顺序发布；member registrar中途失败
保留已发布descriptor前缀。四端集成loader没有真正module erase/unload caller，因此成功状态
不会在正常运行中回到zero tuple。给本地实现增加plugin级总锁、自动unlink或退出Release
borrowed class/method pointer都会改变参考生命周期。

## 9. 并发、重入和失败矩阵

| 情形 | 参考结果 |
|---|---|
| 两线程首次取得global RNG | 两次new/ctor均可能发生，最后store获胜，另一个对象泄漏；data race |
| 两线程同时RNG draw | left/cursor/mt原地竞争；无定义sequence |
| guarded static同一时刻首次进入 | ABI guard串行；成功后其他线程读完成值；异常active stage abort后可重试 |
| manual method sentinel并发 | pointer和ID可被交叉观察；无补偿或atomic publication |
| selector Enum异常 | method已发布，ID未发布；下次不再Enum |
| alpha-mask method expression异常 | method guard未完成；下次重做完整method expression |
| alpha-mask threshold Enum异常 | method guard保持完成，只重试threshold guard |
| basis cache miss异常 | node/outer rows/当前row前缀按抛点保留；hit不修复 |
| shared adaptor ctor异常 | global保持null；D3DAdaptor自己的已AddRef Window失败边仍可能泄漏 |
| shared adaptor并发首建 | 可创建多个Window/target/map owner并覆盖raw slot；被覆盖对象永久泄漏 |
| overflow message构造/显示异常 | once byte已先写true；以后不再显示 |
| registrar/member中途异常 | 已append record、ClassInfo字段或descriptor前缀不回滚 |
| TJS hint并发更新 | plugin无锁共享word；遵循原生TJS/cache data-race边界 |

## 10. 本地映射与验证物料

| family | 本地位置 | 结论 |
|---|---|---|
| global blink/wind MT19937 | `cpp/plugins/motionplayer/EmoteBlinkRng.cpp:9` | 匹配 |
| blink / wind consumers | `cpp/plugins/motionplayer/EmoteBlinkController.cpp:82`; `cpp/plugins/motionplayer/EmoteWindEmitter.cpp:27` | 匹配 |
| private manager、basis、method selectors | `cpp/plugins/motionplayer/MotionRenderBackend.cpp:21`; `:194`; `:272` | 匹配 |
| D3D clear guards与software texture map | `cpp/plugins/motionplayer/D3DAdaptor.cpp:139`; `:199` | 匹配 |
| shared D3DAdaptor raw process slot | `cpp/plugins/motionplayer/PlayerRenderInternal.cpp:37`; `:779` | 匹配 |
| alpha-mask six guarded methods | `cpp/plugins/motionplayer/PlayerRenderInternal.cpp:970` | 匹配 |
| accurate SLA guard | `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:1137` | 匹配 |
| Private-GLL guarded class | `cpp/plugins/motionplayer/PrivateMotionGLL.cpp:493` | 匹配 |
| StretchType guarded ID | `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:664` | 匹配 |
| stencil process bytes / overflow once | `cpp/plugins/motionplayer/MotionRenderBackend.cpp:27`; `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:246` | 匹配 |
| geometry/cache/registrar static root | `cpp/plugins/motionplayer/main.cpp:24` | 匹配 |
| decrypt filter | `cpp/plugins/motionplayer/ResourceManager.cpp:112` | 匹配 |
| TJS hint family | `cpp/plugins/motionplayer/RuntimeSupport.cpp:37`及各独立translation unit | 匹配 |
| module loader global set/map | `cpp/core/plugin/ncbind.cpp:18`; `cpp/core/plugin/PluginImpl.cpp:38` | 匹配 |

现有unit material覆盖basis cache poisoning可观察结果、shared adaptor单例复用、D3D map holder、
clear method gate、SLA一次选择、Private-GLL复用、stencil overflow once和decrypt filter replacement。
clock-derived global RNG没有稳定跨运行oracle；算法的固定seed局部状态可由后续MP-R12/R15
controller/wind测试继续验证，但这不影响本任务的生命周期静态闭环。正式native/Web build与
运行unit仍由`MP-V06..V08`跟踪。

## 11. IDB 改善与 disposition

- 四端global RNG getter、canonical helper、blink step和wind step共16个函数已统一确定性命名；
- 64个本轮函数实例全部写入本任务函数注释；
- 20个关键根写入bookmark；
- 四份IDB均已原位保存。

disposition：

- 原始任务：`MP-L15`；静态状态：`CLOSED_STATIC`；
- 原始任务：`MP-B09`；静态状态：`CLOSED_STATIC`；
- 覆盖切面：`MP-L15-GLOBAL-STATIC-CACHE-RNG-GUARD-LIFECYCLE`；
- task-local剩余静态差异：无；
- controller数值状态机、global owner总数/AddRef-Release总审计和正式验证仍由
  `MP-R11..R15`、`MP-L16/MP-V13`、`MP-V06..V08`分别跟踪。
