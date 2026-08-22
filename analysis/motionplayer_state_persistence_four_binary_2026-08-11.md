# MotionPlayer 状态持久化四参考二进制复原（2026-08-11）

## 范围与证据标准

本轮只把下列四个当前参考二进制作为权威来源，不再沿用旧
`libkrkr2.so` 地址或仅凭旧注释推断：

- Android arm64-v8a
- Android armeabi-v7a
- iOS arm64
- iOS armv7

四端均重新从顶层 `serialize` / `unserialize` 的调用点向下追踪，随后逐个
反编译 Timeline、Eye、Eyebrow、Mouth、Transition、Selector、Base 与
OuterForce 的子保存/恢复函数。AArch64 的 `tTJSVariant` 返回值经隐藏的
X8/sret 槽传递，ARMv7 则使用显式返回槽；本文地址表保留原生 ABI，源码
只复原共同的 C++ 语义。

## 顶层与八个子对象的地址映射

### 保存侧

| 语义名 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `EmoteEngine_serializeState_guess` | `0x673220` | `0x55BB70` | `0x1001AF774` | `0x1AEF30` |
| `serializeTimelineState` | `0x673BC4` | `0x55C0E4` | `0x1001AFE68` | `0x1AF5BC` |
| `serializeEyeState` | `0x673EEC` | `0x55C290` | `0x1001B008C` | `0x1AF838` |
| `serializeEyebrowState` | `0x674328` | `0x55C500` | `0x1001B03A0` | `0x1AFBB4` |
| `serializeMouthState` | `0x674764` | `0x55C770` | `0x1001B06B4` | `0x1AFF30` |
| `serializeTransitionState` | `0x674A9C` | `0x55C9A4` | `0x1001B09A0` | `0x1B0294` |
| `serializeSelectorState` | `0x674CD0` | `0x55CAD0` | `0x1001B0B6C` | `0x1B04A0` |
| `serializeBaseState` | `0x674F88` | `0x55CC70` | `0x1001B0DC4` | `0x1B073C` |
| `serializeOuterForceState` | `0x675208` | `0x55CDF0` | `0x1001B0F98` | `0x1B0980` |

### 恢复侧

| 语义名 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `EmoteEngine_unserializeState_guess` | `0x675424` | `0x55CF3C` | `0x1001B1130` | `0x1B0B80` |
| `restoreTimelineState` | `0x675834` | `0x55D184` | `0x1001B1410` | `0x1B0EB0` |
| `restoreEyeState` | `0x675BE4` | `0x55D398` | `0x1001B16E0` | `0x1B11CC` |
| `restoreEyebrowState` | `0x6763D0` | `0x55D7D8` | `0x1001B19A4` | `0x1B1484` |
| `restoreMouthState` | `0x676BE4` | `0x55DBF4` | `0x1001B1C68` | `0x1B1734` |
| `restoreTransitionState` | `0x677400` | `0x55E13C` | `0x1001B1F2C` | `0x1B19F0` |
| `restoreSelectorState` | `0x677C48` | `0x55E578` | `0x1001B2218` | `0x1B1CD4` |
| `restoreBaseState` | `0x67846C` | `0x55EAC0` | `0x1001B24DC` | `0x1B1F8C` |
| `restoreOuterForceState` | `0x67872C` | `0x55EC4C` | `0x1001B26CC` | `0x1B21DC` |

### 共享控制器 helper

| helper | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| Var serialize | `0x664A5C` | `0x554384` | `0x1001A4DCC` | `0x1A42A0` |
| Var restore | `0x664EBC` | `0x554618` | `0x1001A50C0` | `0x1A45EC` |
| Angle serialize | `0x663C10` | `0x553D58` | `0x1001A45B8` | `0x1A3A24` |
| Angle restore | `0x663DF4` | `0x553EE8` | `0x1001A4770` | `0x1A3C70` |
| Eye controller restore | `0x6613A8` | `0x552820` | `0x1001A2BD8` | `0x1A1DB8` |
| Eyebrow controller restore | `0x662C24` | `0x55343C` | `0x1001A3AB8` | `0x1A2E38` |
| Mouth controller restore | `0x663588` | `0x553910` | `0x1001A40EC` | `0x1A3504` |
| Eye/Eyebrow request-queue serialize | `0x679474` | `0x55F560` | `0x1001B3250` | `0x1B2D88` |

上述 24 个语义名已写回四个 IDB；13 个普通双参数恢复函数也已统一补成
`void (self, value)` 类型。保存函数没有强行套普通返回原型，以免破坏
AArch64 隐藏返回寄存器与 ARMv7 返回槽的真实 ABI。

## 顶层保存数据流

四端共同伪代码为：

```text
preProgress(force=true, dt=0)

for eye:        Eye.step(0)       -> HM7[eye.label]
for eyebrow:    Eyebrow.step(0)   -> HM7[eyebrow.label]
for mouth:      Mouth.step(0)     -> HM7[label], HM7[talkLabel]
for selector:   Selector.step(0)  -> HM7[selector.label]
for transition: Var.step(0)       -> HM7[transition.label]

apply position/color/scale/angle controllers at dt=0

result = Dictionary()
result.timeline   = serializeTimelineState()
result.eye        = serializeEyeState()
result.eyebrow    = serializeEyebrowState()
result.mouth      = serializeMouthState()
result.transition = serializeTransitionState()
result.selector   = serializeSelectorState()
result.base       = serializeBaseState()
result.outerforce = serializeOuterForceState()
return result
```

因此保存并非纯粹的只读快照：它先在零步长下刷新 timeline/controller 输出、
更新 HM7，并把四个基础控制器的当前值重新应用给 Player。八个根属性的写入
顺序在四端完全一致。

## 精确保存 schema

### Timeline

每个活动 label 生成：

```text
label               = active label
flags               = state.flags | 1
curTime             = state.currentTime
blendRatioCtrl      = VarController state dictionary
stopWhenBlendDone   = state.autoStop
```

两个容易被“合理化”而改错的边界：

1. 四端都对 timeline map 使用 `operator[]`，不是 `find`。活动向量中出现陈旧
   label 时会先物化默认 map entry；随后仍会无条件序列化其 blend controller。
   默认 entry 的 controller 为空时会继续落入原生无效解引用，而不是静默跳过。
2. 保存的 `flags` 永远强制 `| 1`。本地旧实现曾原样保存 flags，已纠正。

### VarController

```text
phase      = state
tick       = phase/tick accumulator
speed      = inverse duration
exponent   = easing exponent
frame[]    = currentValue[]
prev[]     = startValue[]
target[]   = targetValue[]
```

`prev`/`target` 分别对应插值起点快照和目标数组。2026-08-15 的 fresh step
data-flow 复核确认旧本地成员名恰好互换；本地字段已按物理 `+96/+104` 槽恢复为
`startValue/targetValue`，序列化槽位本身没有改变。

### AngleController

```text
phase, tick, speed, exponent
frame  = currentRad
prev   = startRad
target = targetRad
```

恢复时存在四端一致的出厂 quirk：`prev` 和随后读取的 `target` 都写入
`startRad`，`targetRad` 根本不写。若两者都存在，`target` 的值覆盖 `prev`；
恢复前的 `targetRad` 保持不变。

### Eye 与 Eyebrow

二者属性集合相同：

```text
label, phase, frame, v, target,
length, lengthDone, exponent, speed, rq
```

`rq` 是数组，每项为 `{ p0, p1 }`。若 `rq` 缺失或不是 Array，原队列保持
不变；若是 Array，先清空队列，再按原顺序重建。队列元素会被直接对象化并
读取 `p0`/`p1`，没有额外的逐项类型过滤。

Eye 与 Eyebrow 的逻辑字段相同，但原生对象内布局不同：Eyebrow 把
`length/lengthDone` 以及 `exponent/speed` 的物理槽位分别与 Eye 对调。
Web 源码通过各自结构体的语义字段表达这一点，不能把两类控制器当成同一
裸布局 memcpy。

### Mouth

```text
label, phase, mouth, frame, prev, target, tick, exponent, speed
```

其中 `mouth` 是整数 begin-frame 字段，其余连续值为控制器当前值、起点、
终点、累计 tick、指数与逆时长。Mouth 不保存自己的命令队列。

### Transition 与 Selector

Transition 先生成完整 VarController dictionary，再追加 `label`。

Selector 精确保存五项：

```text
label, value, phase, speed, tick
```

Selector 的命令 deque、option vector、entry gate 与 dormant targets vector
均不保存；详见
`analysis/motionplayer_selector_transition_four_binary_2026-08-11.md`。

### Base 与 OuterForce

```text
base = {
  coord:  VarController(position),
  scale:  VarController(scale),
  color:  VarController(color),
  rotate: AngleController(angle)
}

outerforce = {
  bust:  VarController(bust outer force),
  hair:  VarController(hair outer force),
  parts: VarController(parts outer force)
}
```

键顺序与控制器角色在四端一致。

## 顶层恢复数据流与对象生命周期

四端共同流程为：

```text
copy input variant
ToObject()
obtain/addref dispatch
clear temporary variant

for key in timeline, eye, eyebrow, mouth,
             transition, selector, base, outerforce:
    child = dispatch.PropGet(key)   // 普通 flags；缺失时 child 保持 void
    restoreChild(child)

release dispatch
```

原生函数在中途抛出时仍释放持有的 dispatch；本地用 `try/catch` 复原这一
生命周期。Base/OuterForce 先要求子值为对象，随后按固定键取值；缺失键得到
void，再由控制器 restore 的对象检查忽略。

Timeline restore 在验证输入是否为 Array **之前** 就执行空 label 的
`stopTimeline`，即无效/缺失的 timeline 子值同样先停止全部活动 timeline。
随后：

- 非对象数组项跳过；
- 缺少 `label` 跳过；
- timeline map 中不存在的 label 跳过；
- `flags` 与 `curTime` 缺失时分别默认为 0 与 0.0；
- 对已知 label 依次 `playTimeline`、应用 `curTime`、恢复
  `stopWhenBlendDone` 与可选 `blendRatioCtrl`。

## 未知 label 的非对称边界

四端不是统一的“找不到就跳过”。精确矩阵如下：

| 分类 | 非对象项 | 缺 label | 未知 label | 重复 label |
|---|---|---|---|---|
| Timeline | 跳过 | 跳过 | 跳过 | 按输入顺序重复恢复同一 state |
| Eye | 跳过 | 跳过 | **安全跳过** | 只命中 deque 中第一个 |
| Eyebrow | 跳过 | 跳过 | **解引用 end，原生 UB** | 只命中第一个 |
| Mouth | 跳过 | 跳过 | **解引用 end，原生 UB** | 只命中第一个 |
| Transition | 跳过 | 跳过 | **解引用 end，原生 UB** | 只命中第一个 |
| Selector | 跳过 | 跳过 | **解引用 end，原生 UB** | 只命中第一个 |

这一非对称性在四端都成立，并不是单一编译器优化造成的假象：

- Eye 在恢复 helper 调用前显式比较搜索结果与 deque end；
- 其余四类在搜索结束后直接读取 iterator 指向的 controller；
- 所有搜索都是首个匹配即停止，因此合法重复 label 只恢复 deque 中第一项。

本地旧代码曾给 Eyebrow、Mouth、Transition 增加 `found != end` 保护，连同
先前发现的 Selector 保护一起构成了与原版不一致的“安全化”。这些保护现已
按四端证据移除；Eye 的原生保护保留。

## 本轮源码调整

- 把持久化家族的旧 `Like_0x...`/旧 Android 地址名改为语义化 `_guess`
  名称；地址只保留在本文。
- Timeline 保存改为 map `operator[]`，并保存 `flags | 1`。
- 保留 Eye 未知 label 安全跳过；移除 Eyebrow、Mouth、Transition 的额外
  end guard。Selector 的额外 guard 已在上一轮移除。
- 共享 Var/Angle/controller/request-queue helper 改为语义名，并保留
  Angle 双写 `startRad` 的原生 quirk。
- `EmotePlayer` 的 clone、脚本 `serialize` 与 `unserialize` 调用点同步改名。
- 单元测试不再称该格式为“Android schema”，并新增 Timeline flags 与
  Eye-only unknown-label 边界覆盖。

## ABI 差异与共同结论

- AArch64 保存函数普遍使用隐藏返回地址；ARMv7 使用显式返回槽。不能给
  AArch64 serializer 强套普通 `void *return` 原型。
- 32/64 位 `std::deque` map/node stride、`ttstr` 引用计数代码和对象字段
  偏移不同，但迭代顺序、属性集合、默认值和边界分支一致。
- iOS/Android 的宽字符串在 IDA 中有时只渲染成首字符；通过 UTF-16LE
  字面量与属性 hint/global 的对应关系可还原完整键名。四端调用顺序与 hint
  序列相同。

## 2026-08-13 独立复核补记

为排除 IDA 将宽字符串只显示为首字符所造成的误判，本轮又直接按
UTF-16LE 含终止零搜索了八个顶层键。每个键在每个参考二进制中都只有一处
完整命中，且连续排列：

| 键 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `timeline` | `0x14D3B02` | `0xD84534` | `0x10195FF4C` | `0x17522B0` |
| `eye` | `0x14D3B14` | `0xD84546` | `0x10195FF5E` | `0x17522C2` |
| `eyebrow` | `0x14D3B1C` | `0xD8454E` | `0x10195FF66` | `0x17522CA` |
| `mouth` | `0x14D387A` | `0xD8435A` | `0x10195FC58` | `0x1751FBC` |
| `transition` | `0x14D3B2C` | `0xD8455E` | `0x10195FF76` | `0x17522DA` |
| `selector` | `0x14D3B42` | `0xD84574` | `0x10195FF8C` | `0x17522F0` |
| `base` | `0x14D3B54` | `0xD84586` | `0x10195FF9E` | `0x1752302` |
| `outerforce` | `0x14D3B5E` | `0xD84590` | `0x10195FFA8` | `0x175230C` |

同时重新反编译了四套 Var/Angle serialize/restore helper，确认：

- Var 的三条指针槽按物理顺序稳定映射为 `frame=currentValue`、
  `prev=startValue`、`target=targetValue`。旧本地字段名曾反向命名这两个物理槽；
- Angle 保存分别读取 `currentRad/startRad/targetRad`，但恢复的 `prev` 和
  `target` 两次写入同一 `startRad` 地址；四端都没有写 `targetRad`；
- 控制器内部可选字段 helper 的 `PropGet` flags 为 `1024`
  (`TJS_MEMBERMUSTEXIST`)；属性不存在时返回 false 并保留字段；
- 顶层八个子对象以及 Base/OuterForce 的固定子键使用 flags `0`。缺失键得到
  void Variant，再交给子 restore 的对象类型门处理；
- Timeline 是唯一在 Array 类型门之前有副作用的分类：先以空 label 调用
  `stopTimeline`，再判断输入是不是 Array。因此顶层 `timeline` 缺失、void、
  数值或普通非 Array 对象时，活动 timeline 仍会先全部清空。

新增单元测试用空顶层字典锁定最后一条：普通 `PropGet` 取得缺失的
`timeline` 后，`unserialize` 必须清空已有活动 timeline，随后其余七个缺失
子对象均由各自类型门忽略。

## 验证

- `cmake --build --preset "Web Debug Build"`：通过。
- `cmake --build --preset "Wasmtime Headless Debug Build"`：通过；普通与 guest
  两条构建路径均重新编译了本轮改动。
- 从 Web Debug 的 `EmoteEngine.cpp` 完整编译命令派生
  `motionplayer-dll.cpp -fsyntax-only`：通过；仅有仓库既有的 `_tss` 字面量
  操作符空白弃用警告。
- `git diff --check`：通过；输出只有工作树现有的 LF/CRLF 转换提示，无
  whitespace error。
- `out/windows/debug` 当前只有不完整的 CMake cache/vcpkg 安装，没有生成
  `build.ninja`，因此本轮无法执行 Windows 原生单元测试目标；这不是源码
  编译失败。
