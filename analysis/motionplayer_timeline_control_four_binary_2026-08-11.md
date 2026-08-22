# MotionPlayer timeline 控制：四参考二进制联合复原（2026-08-11）

## 结论

本轮以 `reference/binaries/` 的 Android ARM64、Android ARMv7、iOS
ARM64、iOS ARMv7 四份产物为共同权威，重新核对 timeline 的播放、停止、
查询、blend 与 fade 路径。旧移植中有三处会改变脚本边界行为的问题：

1. `D3DEmotePlayer` 脚本成员 `setTimelineBlendRatio` 实际绑定到五参数
   `setTimeline(label, value, transition, easingWeight, autoStop)`。旧源码把
   `autoStop` 错放在第二项；AArch64 上 Boolean 和三个 float 分走 GP/FP
   寄存器，使错误顺序在只看调用指令时很有迷惑性。
2. `Motion.EmotePlayer.fadeInTimeline/fadeOutTimeline` 不是普通三参数
   ncbind 成员模板，而是直接注册的 raw callback；它们只强制要求 label，
   duration 和脚本 ease 都可省略并默认 0。
3. 脚本 ease 到控制器 power 的换算只存在于上述 `Motion.EmotePlayer`
   raw callback。`D3DEmotePlayer` 的普通 NCB 路径和 Engine 核心都直接传递
   `easingWeight`，不能重复换算。

2026-08-15 fresh 复核又纠正一项旧结论：`playTimeline` 的 HM3 miss 不抛
“label not found”异常，而是写普通单参数日志后正常返回；四端还都以内联
`std::count` 扫完整个 active-label vector，而不是 first-hit `std::find`。

## 四端地址映射

### D3DEmotePlayer 直接入口与 Engine 核心

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D `playTimeline` | `0x530C60` | `0x494F08` | `0x100233364` | `0x232058` |
| Engine `playTimeline` | `0x670350` | `0x55AA70` | `0x1001ADE0C` | `0x1AD53C` |
| D3D `isTimelinePlaying` | `0x530C6C` | `0x494F10` | `0x100233370` | `0x232060` |
| Engine `isTimelinePlaying` | `0x670938` | `0x55ACAC` | `0x1001AE100` | `0x1AD8CC` |
| D3D `stopTimeline` | `0x530C78` | `0x494F18` | `0x10023337C` | `0x232068` |
| Engine `stopTimeline` | `0x679680` | `0x55F6E4` | `0x1001B341C` | `0x1B2F70` |
| D3D `setTimeline` | `0x530C84` | `0x494F20` | `0x100233388` | `0x232070` |
| Engine blend 核心 | `0x67098C` | `0x55ACDC` | `0x1001AE178` | `0x1AD918` |
| D3D `getTimelineBlendRatio` | `0x530C94` | `0x494F48` | `0x100233394` | `0x232098` |
| D3D `fadeInTimeline` | `0x530DF0` | `0x494FE0` | `0x100233424` | `0x232170` |
| Engine `fadeInTimeline` | `0x670D24` | `0x55AE44` | `0x1001AE2E8` | `0x1ADABC` |
| D3D `fadeOutTimeline` | `0x530DFC` | `0x494FE8` | `0x100233430` | `0x232178` |

四端 D3D `setTimeline` 在 IDB 中统一命名为
`D3DEmotePlayer_setTimeline_guess`，Engine 核心统一命名为
`EmoteEngine_setTimelineBlendController_guess`。原始源码名无法仅凭产物证明，
所以保留 `_guess`。

### NCB 参数适配与 Motion.EmotePlayer raw callback

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D set NCB `FuncCall` | `0x5472FC` | `0x4A80A4` | `0x10024AF60` | `0x24C750` |
| D3D set NCB invoke | `0x547418` | `0x4A8168` | `0x10024B040` | `0x24C7E8` |
| D3D fade NCB `FuncCall` | `0x547898` | `0x4A84B4` | `0x10024B480` | `0x24CD2C` |
| D3D fade NCB invoke | `0x5479B4` | `0x4A8574` | `0x10024B560` | `0x24CDC0` |
| EmotePlayer fade-in raw callback | `0x670ACC` | `0x55AD64` | `0x1001AE200` | `0x1AD97C` |
| EmotePlayer fade-out raw callback | `0x670DD4` | `0x55AEA8` | `0x1001AE364` | `0x1ADB24` |

D3D set NCB 的共同转换顺序为：

```text
label         = ttstr(param[0])
value         = float(param[1].AsReal())
transition    = float(param[2].AsReal())
easingWeight  = float(param[3].AsReal())
autoStop      = bool(param[4])
setTimeline(native, label, value, transition, easingWeight, autoStop)
```

Android/iOS A64 的物理调用把 `autoStop` 放在 GP `W2`，三个 float 放在
`S0..S2`；Android ARMv7 则表现为 `R2=value`、`R3=transition`、栈上传
`easingWeight` 和 Boolean。四端的参数转换和最终 Hex-Rays 函数类型一致，
所以源码顺序不能按 A64 寄存器编号直觉重排。

D3D fade 的普通 NCB 模板严格要求三个脚本参数：

```text
fadeInTimeline(label, float(duration), float(easingWeight))
fadeOutTimeline(label, float(duration), float(easingWeight))
```

此路径没有脚本 ease 换算。

`Motion.EmotePlayer` 两个 raw callback 的共同前端为：

```text
if numparams < 1:
    return TJS_E_BADPARAMCOUNT
label      = ttstr(param[0])
duration   = numparams >= 2 ? param[1].AsReal() : 0
scriptEase = numparams >= 3 ? param[2].AsReal() : 0
power      = scriptEase == 0 ? 1
           : scriptEase > 0  ? scriptEase + 1
                             : 1 / (1 - scriptEase)
```

2026-08-15 又从四端 registrar stored callback pointer 与八个 fresh callback
反编译体闭合了更细的 ABI/生命周期边界：这两项与 Primary member 14–19 一样
使用 NCBind 的 native-instance raw-callback specialization。第四参数已经是解包后的
`EmotePlayer *`（其 Engine 基址可直接使用），回调体内不存在 `objthis` native lookup；
因此错误接收者由外层 descriptor 拒绝，永远到不了回调自己的 argc gate。`result`
参数在正常、少参以及回调体内均不读不写。

共同转换和 owner 顺序不是只在最终数值上等价，而是：

```text
argc < 1 -> BADPARAMCOUNT（尚未构造 label）
ttstr label(param[0])                         // owning 临时量
float duration = argc >= 2 ? AsReal(param[1]) : 0
double scriptEase = argc >= 3 ? AsReal(param[2]) : 0
double mapped = piecewise-map(scriptEase)
float easingWeight = mapped
Engine call while label is alive
destroy label
return TJS_S_OK
```

也就是说 duration 在读取 ease 之前就收窄；ease 输入保持 double，分段加法/倒数也在
double 域完成，只有映射结果在进入控制器前收窄。Android A64 把 fade-in Engine
组合体内联进 callback，另外三端保留独立 Engine helper，但上述可观察前缀、owner
生存期和最终调用完全一致。源码现以两个显式 `float` 局部量固定该顺序，避免把 cast
留在参数求值顺序不够直观的调用表达式里。

随后 fade-in 调 Engine fade-in；fade-out 直接调 blend 核心并传
`value=0, autoStop=true`。raw callback 正常返回 `TJS_S_OK`，不会写 result。

## Engine 共同伪代码

### 播放、查询与停止

```text
playTimeline(label, flags):
    if flags & 1:
        stopTimeline("")
    state = HM3.find(label)
    if state missing:
        log "timeline label not found '<label>'."
        return
    if count(activeLabels.begin, activeLabels.end, label) == 0:
        activeLabels.push_back(label)
    if state.timelineData == null:
        initializeTimelineState(state)
    initializeTimelineControllers(state, flags)
    seekTimeline(state, 0)

isTimelinePlaying(label):
    if label is empty:
        return !activeLabels.empty()
    return label is in activeLabels

stopTimeline(label):
    if label is empty:
        activeLabels.clear()      // 保留 vector capacity
    else if first matching label exists:
        erase that first item
```

空标签的 `isTimelinePlaying` 是“是否有任何 timeline 播放中”，而不是在
vector 中查找一个空字符串。命名 stop 只擦除第一个匹配项；正常 play 会去重，
但 play 的去重测试会扫描完整个 vector，不在首个命中处提前停止。另一个可见提交
边界是 `flags & 1` 的全停发生在 HM3 lookup 之前；若随后 label miss，active vector
已经被清空，日志返回不会回滚它。HM3 miss 本身不插入 state。

### Blend 与 fade

```text
setBlend(label, value, duration, easingWeight, autoStop):
    state = HM3.find(label)
    if state missing:
        return                  // 不插入
    if state.timelineData == null:
        initializeTimelineState(state)
    state.blendController.setTarget(
        value, duration, easingWeight, engine.appendFlag)
    state.autoStop = double(bool(autoStop))

getBlend(label):
    state = HM3.find(label)
    if state exists and state.timelineData != null:
        return double(state.blendWeight)
    return 0

fadeIn(label, duration, easingWeight):
    if !isTimelinePlaying(label):
        playTimeline(label, 3)
        setBlend(label, 0, 0, 1, false)
    setBlend(label, 1, duration, easingWeight, false)

D3D fadeOut(label, duration, easingWeight):
    setBlend(label, 0, duration, easingWeight, true)
```

本轮移除了本地伪造的独立 Engine `fadeOutTimeline`；四端只有 facade/raw
callback 直接组合 blend 核心的路径。

## ABI 字段偏移差异

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Engine HM3 | `+936` | `+468` | `+584` | `+292` |
| HM3 value `timelineData` | `+16` | `+16` | `+24` | `+12` |
| HM3 value blend controller | `+24` | `+20` | `+32` | `+16` |
| HM3 value blend weight | `+88` | `+72` | `+96` | `+68` |
| HM3 value auto-stop double | `+96` | `+80` | `+104` | `+76` |
| Engine append/queuing byte | `+1161` | `+593` | `+793` | `+409` |

偏移差异来自 STL/ABI 布局，逻辑和字段次序在四端一致。本地源码继续用
语义字段与容器表达，不把任一平台的硬编码偏移当作跨平台结构定义。

## 本地落地与回归边界

- Engine API 改为语义 `_guess` 名称，删除旧 Android 单地址式函数名。
- D3D 五参数绑定恢复真实顺序，并删除未注册、未被调用的两参数 C++
  `setTimelineBlendRatio` 移植发明。
- `Motion.EmotePlayer` 两个 fade 改成 native-instance raw callback，恢复可选
  duration/ease 与只在此处发生的 ease-to-power 换算。
- 定向单测覆盖：空标签查询、first-only erase、空标签 clear、HM3 miss
  不插入、blend 队列字段、auto-stop、blend 读取门槛，以及两个 raw
  callback 的参数数目、native receiver gate、duration 即时收窄和 double-domain
  ease 映射边界。
- 2026-08-15 回归追加 play miss 的普通返回、replace-before-miss 提交顺序，
  并将本地去重实现恢复为四端共同的 full-range `std::count`。
- 四份 IDB 已保存本轮函数命名、函数类型和反编译改进。
