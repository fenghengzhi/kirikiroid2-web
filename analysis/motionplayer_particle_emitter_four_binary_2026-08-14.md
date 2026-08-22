# MotionPlayer type-6 emitter 四参考二进制复原（2026-08-14）

## 1. 四体函数地图

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateParticleEmitters_guess` | `0x6BC1B0` | `0x588820` | `0x100111A6C` | `0x10F2CC` |
| `Player_updateEmitterCrossfadeDelta_guess` | `0x6BE920` | `0x58A978` | `0x100113ECC` | `0x1118B0` |
| `Player_updateParticleSystems_guess` | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |

`Player_updateLayers_guess` 在 camera constraint、vertex、visibility、camera node、shape、
child-motion 之后依次调用 emitter、particle-system、anchor-feedback。四体调用顺序一致；
emitter 与 particle-system 是两个相邻但独立的完整 pass。

## 2. type-6 emitter 的输入分工

旧 `libkrkr2.so` 注释把若干 node-expression 常数直接当成 slot-relative 偏移，进而把
type-6 emitter 误接到 `prt.trigger` 与 `motion.dtgt`。四体实际输入为：

| 语义 | A64/iOS A64 真 slot 偏移 | Android A32 | iOS A32 |
|---|---:|---:|---:|
| source identity | `src +36` | `+32` | `+28` |
| mode | `model.dt +388` | `+300` | `+288` |
| mode-4 target label | `model.dtgt +392` | `+304` | `+292` |
| timer offset | `model.timeOffset +408` | `+312` | `+300` |

A64/iOS A64 反编译常从 `node + 536*active` 计算地址，因此后三个字段会显示为表达式
常数 `+708/+712/+728`。真实 slot 从 `node+320+536*active` 开始，所以它们分别是
slot `+388/+392/+408`，并不落在 particle 或 motion block。

字段职责彼此独立：

- `src` 是 emitter 跨帧保留、比较和引用计数管理的 source identity；
- `model.dt` 是 2/3/4 模式分派；
- `model.dtgt` 只在模式 4 中作为 raw node-label lookup key；
- `model.timeOffset` 只参与首次绑定/源变化时的 timer 初始化；
- `prt.trigger` 属于 type-4 particle-system 的 frequency/count 分派；
- `motion.dt/motion.dtgt/motion.timeOffset` 属于 type-3 child-motion pass。

四端原始 UTF-16LE 字节与调用点装载共同确认 merger 的真实属性键是
`motion.timeOffset` 和 `model.timeOffset`。IDA 若只把起始字符定义成字符串项，伪代码会把
同一地址错误渲染成 `"t"`；不能据此截短属性名。回归同时提供冲突的 `t` 与
`timeOffset` 值，锁定长键优先的真实边界。

## 3. 对象生命周期与 timer

pass 在 preview 为真时整体不执行，且从非 root 节点开始遍历。仅 node type 6 进入主体。
若 accumulated inactive、active slot done 或 `src` 为 null-backed empty：

1. emitter-active byte 清零；
2. 释放并清空 emitter retained source；
3. timer 置零；
4. 继续下一个 node。

live emitter 的 source 更新分两条路径：

- node dirty/flags byte 为零时，不比较 retained source，直接 `timer += Player.deltaTime`；
- flags 非零时，若尚未 active 或 retained source 与 slot src 不同，则 retain 新 src、release
  旧 src，并重算 timer；若相同则仍只累加 deltaTime。

重算式为：

```text
parentTime = node.parameterEntry ? parameterEntry.value : player.frameTickCount
timer = parentTime - activeSlot.time + activeSlot.model.timeOffset
```

timer 分支合流后总是先把 emitter-offset-active 清零，再执行 mode 分派。因此未生成新 offset
时旧 xyz 数值可以残留，但 validity byte 为假，消费者不得读取。

## 4. model.dt 模式

- mode 4：以 `model.dtgt` 在 Player 的 UTF-16 raw-label ordered map 中查找节点；命中后写
  `target.accumulatedPos - emitter.accumulatedPos` 并置 offset-valid；缺失时保持 invalid。
- mode 3：无条件调用共享 crossfade derivative helper；helper 自己检查 active crossfading
  且 other 未 done。
- mode 2：若 Player 尚未完成第一次 update，或 timer 精确等于 0，同样调用 derivative
  helper；否则直接把节点的 `deltaPosX/Y/Z` 发布为 emitter offset。
- 其他 mode：不写新 offset。

共享 derivative helper 保留为四体独立函数。其算法是：

```text
if !active.crossfading || other.done: return
r = (player.clampedEvalTime - active.time) / (other.time - active.time)
r2 = r + 0.0001
if r2 >= 1: r = 0.9999
r2 = min(r2, 1)
p1 = evaluatePosition(active curves, other.xyz, active.xyz, r)
p2 = evaluatePosition(active curves, other.xyz, active.xyz, r2)
offset = p2 - p1
offsetValid = true
```

它没有 denominator 为零的保护，也不把差值除以 epsilon；结果是两个相邻采样点的直接差。
四体对 epsilon、末端 0.9999 特判、两次 interpolation 调用顺序和最终 xyz store 都一致。

### 2026-08-16 unordered 选择补充

重新读取四端原始 compare/select 后，上述伪代码需要把第一采样写得更严格：

```text
candidate = r + 0.0001
first  = candidate < 1.0 ? r : 0.9999
second = min(candidate, 1.0)
```

四端第一选择都以 ordered `candidate < 1` 选择 `r`，相等、大于和 unordered 全部选择
`0.9999`；旧源码的 `if (candidate >= 1)` 在 NaN 时错误保留 `r`。第二选择存在真实目标
分化：ARM64 使用 `FMIN`（不是 `FMINNM`）并传播 NaN，ARMv7 预置 `1.0`、只在 ordered
less 时移动 candidate，因此 NaN 得 `1.0`。详细指令和源码取舍见
`motionplayer_emitter_crossfade_sample_unordered_four_binary_2026-08-16.md`。

随后对 MotionSub angle-mode-3 的新鲜复核确认两套选择逐端同型；共享 selector 与旧文
`fmin` 更正见
`motionplayer_shared_position_derivative_sample_select_four_binary_2026-08-16.md`。

## 5. 与 type-4 particle-system 的边界

particle-system pass 从活动 slot 直接读 `prt.trigger`，从 evaluator 产出的 node 九-double
mirror 读 f/v/a/z/range。旧本地另有一个 node-level `prtTrigger`，但它只有诊断式 copy 和
手工测试写入，没有 native producer；四体都以 active-slot expression 直接读取 trigger。
该镜像现已删除，frequency/count 两处分派统一读取活动 slot。

## 6. 本轮闭环

- 四份 recovery IDB 统一命名 emitter pass、particle-system pass 与 derivative helper；
- 三个函数入口补充职责/边界注释并保存四份 IDB；
- 本地 emitter 恢复 `src + model.{dt,dtgt,timeOffset}` 数据流；
- merger 恢复 `motion.timeOffset`/`model.timeOffset` 原生键，并以宽字符串原始字节排除
  IDA 单字符边界误判；
- 删除 node-level `prtTrigger` 及 copy-assignment 残留；
- source-level 重新抽出与原生边界对应的 derivative helper，mode 2/3 共用；
- 回归覆盖冲突 src/motion/prt/model 输入和冲突 `t`/`timeOffset` 键；
- Web Debug 构建、完整 motionplayer 单测翻译单元语法检查、`git diff --check` 通过。
