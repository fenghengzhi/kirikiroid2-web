# EmotePlayer 主流程、状态序列化与 raw setter（四参考二进制，2026-08-27）

## 1. 范围与结论

本切片闭合 `Motion.EmotePlayer` 注册表序号 4..22，也就是本地成员编号 #1..19：

- 主时间流：`progress`、`frameProgress`；
- facade / Player 路由：`draw`、`play`、`clear`、`contains`；
- Engine 直绑：`initPhysics`、`startWind`、`getVariable`、`serialize`、
  `unserialize`；
- 独立薄体：`stopWind`、`pass`；
- 六个 native-instance raw callback：`setVariable`、`setCoord`、
  `setScale`、`setRotate`、`setColor`、`setOuterForce`。

四端 source-level 结构、调用顺序、owner、容器访问和边界行为一致；唯一需要在源层显式
保留的目标差异是 `startWind` 的 64 位 / 32 位停止谓词。当前本地 C++ 已与联合证据吻合，
本轮不需要运行时语义修改。完成后，73 行 EmotePlayer 注册面中除 Factory/常量外的 70
个 callback body 已全部闭合；全局 NCB 台账不再有
`BODY_PENDING_SEPARATE_SLICE`。

这里的“全部闭合”只指 EmotePlayer 公开 callback 分母，不等于整个 motionplayer 的所有
根可达 helper、vtable、函数指针、静态析构和 renderer/resource 内部函数已经全部闭合。

## 2. callback 地址与完整指令证据

表中数字为 `mcp__idalib__disasm(include_total=true)` 返回的完整函数指令数；76 次读取的
`cursor.done` 全部为 true。每个入口也都 fresh 尝试反编译。

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `progress` | `0x67EC94`，7 | `0x561D08`，7 | `0x1001B5C68`，7 | `0x1B586C`，7 |
| `frameProgress` | `0x67A3F8`，302 | `0x55FEF0`，95 | `0x1001B4304`，89 | `0x1B3E10`，104 |
| `draw` | `0x67ECB0`，29 | `0x561D38`，27 | `0x1001B5C84`，16 | `0x1B5898`，45 |
| `initPhysics` | `0x67A8B0`，435 | `0x560020`，307 | `0x1001B4468`，246 | `0x1B3F58`，431 |
| `startWind` | `0x66DD8C`，84 | `0x559900`，114 | `0x1001AC718`，99 | `0x1ABF24`，120 |
| `stopWind` | `0x67EE18`，11 | `0x561D90`，9 | `0x1001B5CD8`，11 | `0x1B5944`，9 |
| `play` | `0x67C7EC`，5 | `0x5610B0`，5 | `0x1001B5040`，5 | `0x1B4CBC`，5 |
| `clear` | `0x67EE44`，42 | `0x561DA8`，33 | `0x1001B5D04`，23 | `0x1B595C`，54 |
| `getVariable` | `0x5341FC`，90 | `0x4979BC`，77 | `0x1001B5D84`，53 | `0x1B5A2C`，95 |
| `contains` | `0x67EEEC`，18 | `0x497BFE`，15 | `0x1001B5E84`，18 | `0x1B5B74`，21 |
| `serialize` | `0x673220`，398 | `0x55BB70`，188 | `0x1001AF774`，147 | `0x1AEF30`，255 |
| `unserialize` | `0x675424`，260 | `0x55CF3C`，159 | `0x1001B1130`，122 | `0x1B0B80`，228 |
| `pass` | `0x67F028`，1 | `0x561E18`，1 | `0x1001B5ECC`，1 | `0x1B5BAA`，1 |
| `setVariable` | `0x66F1D0`，153 | `0x55A368`，70 | `0x1001AD684`，56 | `0x1ACD18`，99 |
| `setCoord` | `0x66F440`，171 | `0x55A450`，77 | `0x1001AD778`，71 | `0x1ACE5C`，79 |
| `setScale` | `0x66F6FC`，144 | `0x55A548`，70 | `0x1001AD894`，55 | `0x1ACF52`，60 |
| `setRotate` | `0x66F948`，130 | `0x55A628`，56 | `0x1001AD970`，51 | `0x1AD010`，59 |
| `setColor` | `0x66FB5C`，151 | `0x55A6E8`，49 | `0x1001ADA3C`，46 | `0x1AD0CC`，52 |
| `setOuterForce` | `0x66FE58`，180 | `0x55A828`，79 | `0x1001ADB98`，60 | `0x1AD218`，108 |

Android arm64 的 `frameProgress` 是 `0x530E3C` 所属函数的 distant chunk
内部入口。这里没有创建重叠函数；302 条数字是 IDA 从该内部入口解析到的完整 containing
function / chunk 组合。`progress` 的 7 条包装器在乘除后直接落入同一 Engine chunk，
因此 Hex-Rays 会把 wrapper 和 Engine 主体合并显示。

Android arm64 的 `clear` 是唯一 fresh decompile 失败项：Hex-Rays 报告
`Decompilation failed at 0x67ee44 (address: 0x67ee8c)`。这不是未取证：42 条完整反汇编清楚
显示 target/fill 两次 Variant CopyRef、`Player_clearRecursive_guess` 调用、fill 后 target
逆序析构，以及两条异常清理路径；另外三端的完整伪代码提供同构交叉证据。不能把这个
工具失败记作未闭合，也不能据此改写函数边界。

## 3. 下一层语义 helper 闭包

为避免只审 wrapper 不审 sink，本轮同时 fresh 读取了以下下一层 helper；所有反汇编也
都是完整游标：

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine::passTimelines` | `0x67A100`，183 | `0x55FCC4`，151 | `0x1001B3FE4`，153 | `0x1B3BBC`，165 |
| `EmoteEngine::setVariable` | `0x66E608`，323 | `0x559D84`，156 | `0x1001ACDBC`，188 | `0x1AC5F4`，176 |
| `EmoteVarController::setTarget` | `0x6646E0`，100 | `0x5542B0`，55 | `0x1001A4C44`，38 | callback 内联 |
| `EmoteAngleController::setTarget` | `0x663870`，105 | `0x553AD4`，62 | `0x1001A4308`，46 | callback 内联 |
| `setColor` apply tail | callback 内联 | `0x55A788`，48 | `0x1001ADAF4`，41 | `0x1AD16A`，50 |
| `setOuterForceTarget` | `0x670138`，56 | `0x55A928`，63 | `0x1001ADC9C`，51 | `0x1AD37C`，72 |
| wind emitter constructor | `0x66DEDC`，136 | `setWind` 内联 | `setWind` 内联 | `setWind` 内联 |

`draw`、`play`、`clear`、raw-label lookup、shape `contains` 和 Player variable getter 的深层
主体已经由以下独立四端切片闭合，本报告重新核对了本组 call edge，而不重复篡改它们的
覆盖所有权：

- `analysis/motionplayer_player_draw_router_four_binary_2026-08-27.md`；
- `analysis/motionplayer_player_chara_motion_properties_four_binary_2026-08-26.md`；
- `analysis/motionplayer_player_clear_four_binary_2026-08-26.md`；
- `analysis/motionplayer_player_variable_root_layernames_four_binary_2026-08-26.md`；
- `analysis/motionplayer_geometry_contains_boundary_four_binary_2026-08-26.md`。

## 4. NCB 边界与源代码分层

### 4.1 typed method

前 13 个成员除 `pass` 的薄 thunk 形状外，均由 typed descriptor 发布；其中
`frameProgress/initPhysics/startWind/getVariable/serialize/unserialize` 直接保存 Engine
成员函数目标，其余保存 EmotePlayer facade 成员。typed adapter 的共同外层顺序是：

```text
if memberName != null: MEMBERNOTFOUND
if receiver == null: NATIVECLASSCRASH          # result 保持原值
clear result when result != null
if argc < required: BADPARAMCOUNT
resolve EmotePlayer native payload
convert/copy exactly required argv values
invoke member
publish non-void return if result != null
destroy converted owners in reverse order
```

多余参数不转换。所需参数数依次为：`1,1,1,1,5,0,2,2,1,3,0,1,0`。因此 C++ 声明中
`play` 的 `flags=0` 只是普通 C++ 调用默认值；成员函数指针的 typed NCB 形状仍要求
label 和 flags 两项。`startWind` 先把五个 Variant 转 Real，再逐项缩窄成 float。

### 4.2 native-instance raw callback

六个 setter 使用 NCBind native-instance raw specialization。外层先按 EmotePlayer class ID
解析 receiver；错误 receiver 不会进入 callback 自己的 argc gate。进入 body 后 result
参数完全不读写，最小 argc 检查先于 argv 解引用：

| callback | 最小 argc | 可选默认值 | 转换顺序 |
|---|---:|---|---|
| `setVariable` | 2 | transition=0, ease=0 | label, value, transition, ease |
| `setCoord` | 2 | transition=0, ease=0 | x, y, transition, ease |
| `setScale` | 1 | transition=0, ease=0 | scale, transition, ease |
| `setRotate` | 1 | transition=0, ease=0 | angle, transition, ease |
| `setColor` | 1 | transition=0, ease=0 | Integer color, transition, ease |
| `setOuterForce` | 3 | transition=0, ease=0 | label, x, y, transition, ease |

argc 覆盖到的 argv/元素指针没有额外 null 防护；畸形 native 调用保持原始解引用崩溃边界。
异常时已经构造的 ttstr owner 由 ABI unwind 释放；多余参数完全不访问。

## 5. 时间流与 Engine 数据流

### 5.1 `progress` 与 `frameProgress`

共同伪代码：

```cpp
void EmotePlayer::progress(double milliseconds) {
    engine.progress((milliseconds * 60.0) / 1000.0);
}

// frameProgress 直接绑定：
engine.progress(frameDelta);
```

乘法和除法分别以 binary64 执行，不能折叠成 `milliseconds * 0.06`。两条入口都允许 0、
负数、NaN 和无穷进入 Engine；只有 D3D shell 的另一套 API 有 dt==0 gate。

Engine 主体保留 originalDt，再执行：

1. `preProgress(false, originalDt)`；
2. 当 `remaining > 0 || dirty` 时循环，以 `std::min(remaining, 1.1)` 且 remaining 为第一
   操作数取得 double slice，缩窄一次为 float step，清 dirty；
3. 按 eye、eyebrow、mouth、selector、transition、loop 六个 deque 顺序 step，并写入
   Engine variable-value unordered-map；
4. step position/color/scale/angle 四个 root controller，随后 step live wind emitter；
5. 每轮从 remaining 减去同一个 double slice；
6. 循环后遍历 variable-value map，加入 timeline contribution，执行 mirror 分类，并绑定到
   Player 两张查询 map；
7. 应用 clamp control，随后只把 originalDt 一次性交给 Player frame bridge；
8. `originalDt != 0 && !directEdit` 时，再用 originalDt 的一次 float 缩窄 step 三个 outer
   force controller、hair/parts 和 bust spring。

容器物理实现随 STL 变化：Android 使用 libstdc++ deque/unordered-map，iOS 使用 libc++；
逐标签写入互不依赖，因此 unordered-map 枚举顺序不是源语义。deque block/iterator 算术在
四端机器码不同，但 category 顺序和 live owner 相同。

`std::min` 的第一操作数顺序必须保留：dirty 强制且 remaining 为 NaN 时，NaN 会进入
controller step，不会被 1.1 替换。负数且 dirty=false 不跑 slice，但仍执行 bind/clamp/
Player bridge；零且 dirty=true 至少跑一个零 slice。

## 6. metadata、wind 与 facade owner

### 6.1 `draw`

typed adapter 先拥有一份 target Variant；EmotePlayer facade 又 CopyRef 一份按值局部给
Player draw。正常和异常退出都按内层再外层逆序释放，caller 的 argv Variant 不变。
Player 的 D3D/Separate/Layer 分派和 shared adaptor 生命周期由独立 draw 报告闭合。

### 6.2 `initPhysics` / `applyMetadata`

四端顺序严格相同：

```text
resetMetadataState()                         # 先清旧 metadata 状态
copy input Variant again -> ToObject
retain dispatch in accessor
clear second local Variant before first PropGet

mirror -> flipX -> resetControllers -> Player progress(0)
scale -> step scale controller at 0 -> inverseCombinedScale
optional variableList
bustControl, hairControl, partsControl
eyeControl, eyebrowControl, mouthControl, transitionControl
optional selectorControl
loopControl, clampControl, mirrorControl
optional instantVariableList
timelineControl
syncSelectorControls
```

`variableList/selectorControl/instantVariableList` 用同一个可复用 Variant 槽和
`TJS_MEMBERMUSTEXIST` 探测；缺失时不调用 builder。其余控制项按普通 PropGet 取值，status
不单独检查，转换/builder 继续决定异常。每个 required control Variant 在对应 builder
返回后立即析构，不统一拖到函数尾。metadata accessor 在正常与异常出口都 Release。

最重要的别名边界是 reset 发生在第二份 Variant CopyRef 之前：typed adapter 的第一份按值
owner 保证输入即使别名到旧 metadata 对象，也不会被 reset 提前销毁。

### 6.3 `startWind` 与 `stopWind`

`startWind` 先把负 amplitude 变正，并在负值时交换 min/max。停止谓词是联合证据中唯一
目标宽度差异：

```cpp
// Android/iOS 64-bit
stop = absAmp == 0 || normalizedMin == normalizedMax ||
       (freqX == 0 && freqY == 0);

// Android/iOS 32-bit
stop = absAmp == 0 || freqX == 0;
```

停止只 delete/null emitter，缓存 min/max/amp/frequencies 不清。继续运行时，如果 emitter
不存在或两个端点任一变化，则先 delete 旧 emitter，再以 `endpoint / metadataScale`
构造 1564-byte emitter，最后发布新指针。成员不会在 `new` 前先清零；分配异常会保留已经
被 delete 的旧地址，这是原始窄异常边界。随后更新五个 cache、频率、gate、带方向速度和
发射 accumulator。没有零 scale、finite 或 NaN 防护。

独立 `stopWind` 不走五个零参数的 setWind 路径：它只 delete/null emitter，同样保留全部
缓存。delete null 是成功 no-op。

## 7. Player facade、查询与状态快照

### 7.1 `play` / `clear`

`play(label, flags)` 只把嵌入 Player 和参数按 `flags, label` 顺序交给 Player 播放状态机；
本 facade 不维护第二份 motion owner。

`clear(target, fill)` 不是对象销毁或卸载，而是两 Variant 的 gated recursive
draw-to-layer。空 motion 在 target 转 Object 前成功 no-op；D3D target 优先，Separate
target 会替换为 backing Layer；普通 Layer/对象走 callable 或 `Layer.fillRect`，最后对 live
child deque 递归。详细 malformed child、Layer global ref leak 和异常 owner 见 Player
clear 报告。

### 7.2 `getVariable` / `contains`

`getVariable(label)` 先查询 Player 的 variable-label scope deque：命中则走 Player
HM1/HM2 getter；未命中走 short-lived snapshot 或 bound-parameter lookup。label 按值 owner
贯穿选择和最终查询；缺失最终返回正 0.0，不访问 Engine variable-value map 作为第三路。

`contains(label,x,y)` 递归查 raw label，命中后直接调用节点内 shape `contains`；缺失返回
false。它不做 visibility、空 label、更新或 motion-loaded gate，坐标也不预变换。

### 7.3 `serialize` / `unserialize`

`serialize` 先 `preProgress(force=true, dt=0)`，再以 0 step 更新 eye、eyebrow、mouth、
selector、transition 和四个 root controller；这里不 step loop deque。随后创建新
Dictionary，按固定顺序发布：

```text
timeline, eye, eyebrow, mouth, transition, selector, base, outerforce
```

每个子对象是新 Variant owner，SetValue status 被忽略；最终返回对 Dictionary dispatch 的
独立 Object owner。active timeline 的 stale label 仍可在内部 `operator[]`/直接控制器访问
路径物化或解引用原始状态，不能替换成“跳过坏项”的健壮化行为。

`unserialize` 对输入做严格 Object 转换，AddRef dispatch 后清掉按值 data owner，再按完全
相同的八键顺序逐项 PropGet 和 restore。PropGet status 不检查；缺失键以 Void 继续传给
restore helper。dispatch 在正常返回和 catch/rethrow 两条路径都恰好 Release 一次。

## 8. `pass` timeline 容器状态机

`pass` 是单指令 thunk；Engine helper 不推进 elapsed time。共同伪代码：

```cpp
for (size_t i = 0; i < activeLabels.size(); ) {
    State &s = timelineStates.at(activeLabels[i]);
    if (s.loopBegin >= 0 || ((s.flags & 2) && (s.flags & 4))) {
        ++i;
        continue;
    }
    if (s.flags & 2) {
        setBlend(label, 0, 20, 0, true);
        s.flags |= 4;
    }
    for each track with trackIndex {
        if (!(s.flags & 2) || track.instantVariable) {
            int32 frame = uint32(s.cursor[trackIndex]) + 1;
            while (size_t(frame) < track.frames.size()) {
                if (!track.frames[frame].typeZero)
                    setVariable(track.label, frame.value,
                                frame.time, frame.easingWeight);
                frame = uint32(frame) + 1;
            }
        }
    }
    if (s.flags & 4) ++i;
    else activeLabels.erase(activeLabels.begin() + i);
}
```

active labels 是 vector-like 连续 ttstr owner；erase 左移并 Release 尾 owner，索引不递增。
state lookup 是 bounds-checked `at`，stale label 抛出；`timelineData`、cursor 长度和 track
container 不做补救检查。frame cursor 必须先按 uint32 加一再回到 int32，随后转 size_t
比较；负值因此成为巨大无符号数并跳过循环，不能先提升成 64 位有符号算术。

## 9. 六个 raw setter 的共同变换与分流

ease 变换为：

```cpp
g(e) = e > 0 ? e + 1 : e < 0 ? 1 / (1 - e) : 1;
```

NaN、+0 和 -0 都进入最后一支得到 1。除 `setVariable` 外，raw callback 只做一次
`g(ease)`，再把 value/transition/power 缩窄为 float 后投递 controller。

`setVariable` 是原始实现中的双变换窄边界：raw body 先把 ease 变为 `g(ease)`，随后共享
Engine router 对第五参数再执行同一 piecewise 变换，controller 最终得到
`g(g(ease))`。Engine router：

- 先查 label -> `{type,index}` unordered-map；miss 直接写 variable-value map，不置 dirty；
- hit 先置 dirty；type 0/1/2 仅在 directEdit 时落 variable-value map，否则返回；
- type 4/5 投递 eye/eyebrow controller；
- type 6 在主 label 命中时把 value 饱和 toward-zero 到 int32 beginFrame，在 talk label
  命中时投递 mouth ramp；
- type 7/8 先检查各自 direct-write/enqueue gate，再投递 transition/selector controller；
- deque index 完全信任 metadata 建表结果，不做范围检查。

`setCoord/setScale/setRotate/setColor` 都在调用 controller helper 前无条件 `_dirty=true`，
并把当前 `_queuing` byte 作为 append/replace 选择。duration=0 的立即应用、队列替换和异常
行为由共用 controller helper 决定。

`setColor` 先按 TJS Integer 转换，再只取低 32 位，按低到高字节生成四个 float：R、G、B、
A；负整数也按其低 32 位补码字节解释。

`setOuterForce` 精确、区分大小写地只识别 `bust`、`hair`、`parts`，分别选择三个拥有型
controller pointer；其他 label 成功 no-op。它不写 dirty，这一点与另外四个直接 root
setter 不同。x/y、duration、power 仍按 double-to-float 原始窄化，且使用 `_queuing`。

## 10. 本地逐行对照

联合四端证据与当前源的对应关系：

- `cpp/plugins/motionplayer/EmotePlayer.cpp:515`：毫秒转换、draw facade、stopWind、play、
  clear、contains、pass；
- `cpp/plugins/motionplayer/EmotePlayer.cpp:560`：六个 raw callback 的 argc、转换顺序、
  默认值、ease 预变换、dirty 和 controller 路由；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1317`：outer-force exact label router；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1425`：metadata reset、owner 和 builder 顺序；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:2572`：getVariable 与共享 setVariable router；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:3144`：passTimelines live vector/state/deque 逻辑；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:3448`：serialize 顺序和零 step；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:3782`：unserialize owner/restore 顺序；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:3817`：完整 Engine progress 数据流；
- `cpp/plugins/motionplayer/PlayerCore.cpp:922`：64/32 位 wind predicate 和 emitter 生命周期；
- `cpp/plugins/motionplayer/PlayerTimeline.cpp:311`、
  `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:679`：clear / raw-label sink。

逐项比较未发现需要修改的 C++ 语义。地址只记录在本分析文件和 TSV 中，没有写入编译
源码注释。

## 11. IDB 与验证状态

- 四份 IDB 本轮 73 次函数语义命名全部成功；六个 raw callback 家族此前已经命名，
  Android arm64 的 internal `frameProgress` chunk 保持非重叠，只写 line comment；
- 76 个 callback 入口全部写入本切片注释和 bookmark；helper 语义名也已写回；
- 四份 IDB 已原位保存；
- 76 个 callback 和 22 个显式 helper 函数/continuation 均 fresh decompile + 完整 disasm；
  唯一 decompile 工具失败是已由完整指令流和三端同构解决的 Android arm64 `clear`；
- 当前源已有 raw callback、wind、typed owner、clear、variable、controller 和 timeline
  边界测试，本轮没有新增或修改运行时 C++；
- 当前环境缺少 CMake、Ninja 和 Emscripten，且 standalone syntax check 会被缺失第三方
  headers 阻塞，因此本切片不声称正式 native/Web 构建或测试通过。
