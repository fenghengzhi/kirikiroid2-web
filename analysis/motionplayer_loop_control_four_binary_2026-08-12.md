# MotionPlayer `loopControl` 构建、采样与生命周期四参考复原（2026-08-12）

> 2026-08-16 命名更新：本文的 `v0/v1` 是调查期伪代码字段标签，不是恢复出的原始
> C++ 拼写。fresh 四端 sampler/builder 数据流确认其角色后，编译源码改用
> `startValue_guess/endValue_guess`；12-byte 布局、公式与全部边界不变。地址与原因见
> `motionplayer_final_pseudocode_identifier_migration_four_binary_2026-08-16.md`。
>
> 2026-08-16 builder source-identity 更新：第 4 节的高层伪代码仍成立，但 getter 实现已由
> fresh 四体恢复为 copied-input root `ncbPropAccessor`、retained outer element source、
> direct-temporary `transitionList`/frame accessors、shared enabled hint、两个 Loop-only hints，
> 以及 `GetValue<tjs_real>` 后 caller-side float narrowing。完整 owner 栈、hint slot、清理顺序与
> 回归见 `motionplayer_loop_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
> 不再把本节伪代码中的 `getCount/getValue` 理解为 plugin-local raw getter。

## 1. 结论

本轮只使用 `reference/binaries/` 的四份当前 Android/iOS 参考及其 IDB，重新闭合
如下完整数据流：

```text
metadata.loopControl
  -> buildLoopControl_guess
  -> owning deque #10 {unique_ptr<EmoteLoopController>, var_loop ttstr}
  -> progress 每个 <=1.1 frame slice 的最后一个 metadata-controller family
  -> 12B float keyframe 环形采样
  -> HM7[var_loop] = (double)floatResult
  -> 普通 HM7 bind / timeline / mirror / Player parameter pipeline
  -> reset 或正常析构：HM6 key -> entry label -> key vector -> controller -> deque backing
```

现有采样数学本身与四体一致；在原有两项修正之外，2026-08-13 又补齐了
entry owner、raw emplace 与异常窗口：

1. Android arm64 把 walker 与 sampler 内联进 progress；其余三端都保留
   `EmoteEngine_stepLoopControls_guess -> EmoteLoopController_step_guess(out*,dt)`
   两层函数。旧源码注释把旧单库的“无独立 step”结论推广到全部目标，本地 helper
   也自造为直接返回 `float`。现在恢复为三端直接证明的 output-pointer 调用形状；
   arm64 仍由编译器自行决定是否内联。
2. 四端 reset/dtor 的 loop entry 清理顺序都是先释放 `ttstr label`，再释放 controller
   的 keyframe vector backing，最后 delete controller。本地原来先 delete controller，
   直到 deque clear 才析构 label，引用计数生命周期相反。现已统一为原生顺序。
3. entry 首字段并非需要 Engine 外层 delete helper 的普通 raw pointer，而是与 #4/#5/#6/#8/#9
   相同语义的单指针 owner。builder 则刻意不是局部 `unique_ptr`：它以 raw pointer 直接
   构造 deque destination；入队前的 resize、属性读取和 deque grow 失败都会泄漏，成功
   入队之后才由 entry owner 接管。现已恢复为 entry 内 `unique_ptr`，同时保留这些异常边界。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildLoopControl_guess` | `0x66B860` | `0x558440` | `0x1001AAA8C` | `0x1AA158` |
| keyframe vector resize | `0x6866F4` | `0x55885C` | `0x1001AAF50` | `0x1AA664` |
| raw-owner deque emplace | builder 内联 `0x66BBF4` | fast path 内联 `0x558606`；boundary `0x567EA8` | `0x1001AAFB4` | `0x1AA6B0` |
| builder EH cleanup | 内联 `0x66BDF8` | 无 controller cleanup landing | `0x1001AAE0C` | SjLj `0x1AA4A4` |
| `EmoteEngine_progressCore_guess` | `0x67A3F8` | `0x55FEF0` | `0x1001B4304` | `0x1B3E10` |
| loop deque walker | inline `0x67A680` | `0x55EDA0` | `0x1001B2870` | `0x1B23E0` |
| `EmoteLoopController_step_guess` | inline `0x67A6A4` | `0x554D48` | `0x1001A5984` | `0x1A4F38` |
| `EmoteEngine_resetMetadataState_guess` | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |
| loop deque clear | reset 内联/调用 range helper | `0x563718` | `0x1001B7B5C` | `0x1B7432` |
| loop entry range destroy | `0x6810A4` | `0x5637E4` | clear 内联 | clear 内联 |
| loop deque normal dtor | `0x681700` | `0x563C78` | `0x1001B88C4` | `0x1B7FE4` |

三份 out-of-line sampler 与 walker 已在 IDB 统一为：

```cpp
void __fastcall EmoteEngine_stepLoopControls_guess(void *self, float dt);
void __fastcall EmoteLoopController_step_guess(
    void *controller, float *outValue, float dt);
```

arm64 没有人为切割伪函数，只在真实内联块处写入语义注释。所有上表 helper 均已
应用类型/注释、强制重新反编译并保存四个 IDB。

## 3. UTF-16 属性名复核

Hex-Rays 在 Android armv7 和两个 iOS 目标中把宽字符串参数错误渲染为窄串
`"t"`；Android arm64 则显示完整 `L"transitionList"`。为避免把反编译器类型误判
当成跨平台源差异，本轮直接搜索 UTF-16LE 字节：

```text
74 00 72 00 61 00 6E 00 73 00 69 00 74 00 69 00
6F 00 6E 00 4C 00 69 00 73 00 74 00
```

| 目标 | `transitionList` UTF-16 literal | builder 内 xref |
| --- | ---: | ---: |
| Android arm64 | `0x14D39E8` | `0x66B908` 等，共 4 个 |
| Android armv7 | `0xD84472` | `0x558516`、`0x558520`；另有相邻 tail xref |
| iOS arm64 | `0x10195FDFE` | `0x1001AAB80` |
| iOS armv7 | `0x1752162` | `0x1AA26E`、`0x1AA278` |

因此四端属性名都是完整的 `transitionList`，本地无需改成 `t`。普通 IDA string
搜索在四体均返回空，UTF-16LE byte search 各返回唯一命中，这也再次说明宽字面量
不能只依赖默认 string list。

## 4. builder 的共同源级结构

四端可归一为：

```text
count = getCount(loopControl)
for metadataIndex in [0, count):
  element = loopControl[metadataIndex]
  if !element.enabled:
    continue

  transitionList = element.transitionList
  keyCount = getCount(transitionList)

  controller = new zero-initialized EmoteLoopController
  controller.keys.resize(keyCount)
  for keyIndex in [0, keyCount):
    frame = transitionList[keyIndex]
    controller.keys[keyIndex] = {
      (float)frame[0],
      (float)frame[1],
      (float)frame[2]
    }

  deque10.emplace_back(controller)       // raw pointer copied into entry owner
  deque10.back.label = element.var_loop

  ref = HM6.findOrInsert(deque10.back.label)
  ref.type = 3
  ref.index = metadataIndex
```

关键边界：

- builder 本身不清空 deque 或 HM6；正常入口 `applyMetadata_guess` 先运行完整 metadata
  reset。
- `enabled == false` 只跳过构造，外层 metadata index 仍递增；HM6 的 index 因此可能
  与 compact deque 下标不相等。
- 这里的 type 3 并不在 `EmoteEngine::setVariable` 的 controller 路由 switch 中拥有
  case；命中 type 3 时 Engine 只会先置 dirty，然后走 default return。也就是说该
  index 是原版保存的数据，不应被“修正”为 deque 下标，也不能凭空新增 case 3。
- `var_loop` 同时是 deque label、HM6 key 和后续 HM7 output key。
- 重复 `var_loop` 会复用 HM6 节点并把 type/index 写成较后的 metadata 项；deque 中
  的所有 enabled controller 仍保留。progress 按 deque 顺序写 HM7，因此同 label 的
  后项覆盖前项。
- keyframe 三个数都先从 TJS double 窄化为 float；不是整数位解释。
- 没有空 key list、span 符号或 currentIndex 合法性检查。

### 4.1 entry 接管与异常窗口

四端都把 controller allocation 存在 builder 的 raw slot/register 中，然后依次执行
`keys.resize` 和逐 keyframe TJS 读取。入队使用的是该 raw slot 的地址或等价内联 store：

```text
raw = new EmoteLoopController()          // value-init: scalar/vector header all zero
raw->keys.resize(keyCount)
fill raw->keys
deque10.emplace_back(raw)                // destination {owner=raw,label=null}
deque10.back().label = var_loop
HM6[deque10.back().label] = {3, metadataIndex}
```

关键点是 emplace helper 只复制 raw pointer 并把 destination label 清零，绝不把 source raw
slot 清零。四端 builder cleanup 都只释放 TJS Variant、对象 wrapper 和临时 ttstr；没有
`raw->keys` destructor 或 `operator delete(raw)`。所以各窗口严格为：

- `new` 自身失败：没有 allocation 可清理；
- `keys.resize` 或任意 transitionList/frame 数值读取失败：raw controller 连同已经分配的
  keyframe backing 泄漏；
- deque map/block grow 失败：raw controller 同样仍未被 entry 接管并泄漏；
- destination pointer/empty-label store 成功后：entry owner 已接管；
- 后续 `var_loop` 读取/label assignment/HM6 upsert 失败：builder 不回滚，已入队 entry 保留，
  后续 reset 或 Engine 析构再按 entry 生命周期回收。

这也是本地 builder 继续使用 `auto *ctl = new ...`、直到 `emplace_back(ctl)` 才接管的原因；
若改为 builder-local `unique_ptr` 再 move，会错误修复前三类原版泄漏。

## 5. 原生容器与数据布局

### 5.1 controller 与 keyframe

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| controller size | `0x20` | `0x14` | `0x20` | `0x14` |
| `currentIndex` | `+0` int32 | `+0` | `+0` | `+0` |
| `accum` | `+4` float | `+4` | `+4` | `+4` |
| key vector header | `+8`, 24B | `+8`, 12B | `+8`, 24B | `+8`, 12B |

`EmoteLoopKeyframe12B` 在所有 ABI 都是相同的内部 POD：`v0@+0`、`v1@+4`、
`span@+8`，stride 12。该元素布局是算法直接按字节步进的数据契约；controller 与
Engine 的外围 ABI 偏移则不是 Web 可复制的对象大小契约。

### 5.2 deque entry 与 block

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Engine deque header | `+720` | `+360` | `+432` | `+216` |
| entry size | 16B | 8B | 16B | 8B |
| entry layout | one-pointer owner `+0`, ttstr `+8` | owner `+0`, ttstr `+4` | 同 A64 | 同 A32 |
| native block bytes | 512 | 512 | 4096 | 4096 |
| entries/block | 32 | 64 | 256 | 512 |

Android 的 libstdc++ walker 通过 cur/last/node 迭代；iOS libc++ 通过 start index、
size 和 map block 计算首尾。实现形态不同，但四端都是声明顺序稳定的 deque 扫描。

## 6. sampler 的共同伪代码

```text
idx = controller.currentIndex
accum = controller.accum + dt       // float add
controller.accum = accum            // 在任何 key 读取前写回

count = controller.keys.size()
span = controller.keys[idx].span

if span <= accum:
  do:
    idx = (idx + 1) % count
    accum = accum - span
    span = controller.keys[idx].span
  while span <= accum

  controller.accum = accum
  controller.currentIndex = idx

t = accum / span
*outValue = t * keys[idx].v1 + (1.0f - t) * keys[idx].v0
```

sampler 输出 `float` 到调用者提供的 slot；walker 随后把该 float 扩为 double 并执行
`HM7[entry.label] = value`。Android arm64 把这两层完整内联，另外三体保留两个
out-of-line helper；共同源结构仍由三体的显式调用形状约束为 output pointer。

### 6.1 边界行为

- exact equality 使用 `span <= accum`，因此 `accum == span` 会前进到下一 segment，
  而不是在当前段返回 `v1`。
- 负 `dt` 只令 accum 变小；不倒退 currentIndex，也不钳位 `t`，所以会向当前段
  `v0` 之前外插。
- `dt` 或当前 span 为 NaN 时比较为 false；currentIndex 不变，accum/output 传播
  NaN。
- 当前 span 为零且 accum 为负时不进循环，随后执行 float 除零；accum 非负时会
  进入 span 消耗循环。若整个环的 span 都不为正，可能永不终止。
- 正无穷 accum 对有限正 span 的反复减法仍为正无穷，同样可能永不终止。
- 空 keys、越界 currentIndex、null controller/outValue 都没有保护；空 keys 会在
  计算 count 的保护机会出现之前就读取 `keys[idx].span`。
- 所有中间量保持 float；只有写 HM7 时扩为 double。不得把 controller 状态和
  keyframe 数值“提升”为 double。

## 7. reset 与正常析构生命周期

metadata reset 先清 HM6，随后按 deque #1 到 #10 清理 controller/spring 容器。
loop deque 的每个 entry 在四体共同按以下次序销毁：

```text
release entry.label ttstr
delete controller.keys backing
delete controller object
entry.controller = null
```

然后 deque clear 调整 size 并按各自 STL 实现保留最小 backing。正常 Engine 析构
则在 Player 与较晚声明的容器之后，以 #10 -> #1 的逆声明顺序销毁十个 deque；
loop deque 使用同一 entry 次序，随后释放全部 deque blocks/map。

本地现在由 `EmoteLoopControlEntry_Deque10` 的声明顺序直接表达该生命周期：
`unique_ptr<EmoteLoopController> ctl` 在前、`ttstr label` 在后，自动逆成员析构自然得到
`label -> controller.keys -> controller`。reset 可直接 `clear()`；正常析构仍在 #10 的
原生阶段调用 `releaseContainerStorageAtNativePhase()`，同时释放元素与 deque backing。

## 8. 本地落地与验证范围

- `EmoteLoopController_step` 更名为未知原名形式
  `EmoteLoopController_step_guess`，并改为 `(controller, outValue, dt)`。
- Engine progress 恢复局部 float output slot，再写 HM7。
- reset 与正常析构恢复 label-before-controller 的 entry 内生命周期。
- entry 首字段恢复为 `unique_ptr<EmoteLoopController>`，builder 改为 raw-pointer
  `emplace_back(ctl)`；删除原先人为外置的 `destroyLoopControlEntries_guess`。
- 新增 owner move 测试，约束 entry 为可移动的单指针 owner；builder-local raw pointer
  则刻意保留四端 pre-emplace/grow-failure 泄漏边界。
- 清除了 loopControl 相关编译源码中的旧 `libkrkr2.so` 地址式说明；地址、ABI 与
  反编译证据集中保存在本文。
- 新增单元测试覆盖普通插值、跨段精确回绕、负 dt 外插与 NaN 传播。故意不执行
  空 keys 或全零 span 的崩溃/死循环边界。
