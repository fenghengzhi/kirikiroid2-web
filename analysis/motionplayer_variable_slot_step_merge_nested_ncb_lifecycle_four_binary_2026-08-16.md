# MotionPlayer VariableTrack slot step/merge nested NCB 生命周期四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新从 `reference/binaries/` 四份当前产品代码审计 VariableTrack 的两个
out-of-line slot helper：

- `VariableTrackSlot_step_guess` / portable `stepVariableTrackSlot_guess`；
- `VariableTrackSlot_merge_guess` / portable `mergeVariableTrackSlot_guess`。

符号已剥离，因此恢复名继续保留 `_guess`。旧实现已经大体还原字段写入次序，但用
`motionPropGet*` wrapper 加局部 Variant 表达数据流，漏掉了 native 的 accessor source
identity 与重入保活边界。四端 fresh 结果共同证明：

- step 的 owner tree 是 retained frame-source accessor → indexed frame accessor；
- merge 的非 type-0 owner tree再加一层 retained content accessor；
- merge 的 content accessor 会跨越后续 frame-level `easing` getter 与 assignment，随后才
  按 content → frame → root 顺序释放；
- type 0 只写 `typeZero=true` 后提前返回，保留 interp/interval/value/easing 的旧 payload；
- 所有 typed getter 都是 flags 0、null hint、receiver==objthis，ordinary post-write failure
  HRESULT 不形成 gate。

## 四端函数映射

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| step | `0x6B4C4C` (`0x204`) | `0x583518` (`0x130`) | `0x10010B604` (`0x168`) | `0x108EDC` (`0x1B4`) |
| merge | `0x6B4E50` (`0x3D4`) | `0x583648` (`0x234`) | `0x10010B76C` (`0x2B0`) | `0x109090` (`0x1DE`) |

step 四端都各有四个 call-site xref：advance 一处、absolute reseek 两处、rewind 一处。
merge 在 Android armv7、iOS arm64、iOS armv7 各有四个 xref；Android arm64 有六个，
因为 optimizer 在 advance/rewind 条件分支中复制了 call-site，而非产生另一份源级 helper。
共同 caller families 仍是：

- `Player_advanceTimelineStreams_guess`；
- `Player_reseekTimelineCursors_guess`；
- `Player_rewindTimelineStreams_guess`。

这两者因此既服务增量前后游标，也服务 absolute reseed，不是某一个 direction 私有的
inline 区域。

## 共同 ABI 与 slot 字段

四端 `VarTrackSlot` 的源级字段顺序共同为：

```text
+0   uint32 frameIndex
+8   double time
+16  uint32 interval
+20  byte typeZero
+21  byte interp
+22  byte merged
+24  double value
+32  Variant easing
```

64/32 位目标的 Variant 实体大小不同，但上述已使用字段偏移一致。step 的 index 参数与
slot.frameIndex 都是 32 位 raw word。用于 `PropGetByNum` 时同一 bit pattern 按 signed
`tjs_int` 解释；例如 `0x80000005` 会被 getter 看到为 `INT32_MIN + 5`，不是被扩大成正的
64 位 index。merge 同样从 slot 的 raw uint32 frameIndex 装入 numeric getter 参数。

## step：写前缀和 retained 两层 owner

四端共同数据流：

```text
slot.frameIndex = index

retained frameSource accessor(copy(frameSource))
└─ typed Variant frameSource[index]
   └─ retained frame accessor
      └─ typed Real frame["time"]

slot.time = converted time
slot.merged = false
destroy frame accessor
destroy frameSource accessor
```

关键点：

1. frameIndex 是函数第一项 slot mutation，早于 frameSource 的 Variant copy/object
   conversion。无论后续 conversion、indexed getter 或 real conversion 在哪里抛出，
   frameIndex 都已更新。
2. 构造 root accessor 的 conversion Variant 会立刻析构，但 accessor 自己持有 receiver。
3. numeric getter 的 typed Variant 结果直接用于构造 frame accessor；没有一个贯穿函数的
   frame Variant owner。indexed-result conversion Variant 在 `time` getter 前析构。
4. `time` getter 完成转换后才写 slot.time；随后才清 merged。time getter/real conversion
   抛出时 merged 保持旧值。
5. ordinary HRESULT 即使为 failure，只要 getter 已写结果，typed conversion 和后续写入仍
   继续。
6. 正常与异常 cleanup 都要求 frame accessor 先于 root accessor 释放。

代表性 raw anchors：

| target | frameIndex | indexed Variant | time getter | time / merged writes | frame / root release |
|---|---|---|---|---|---|
| Android arm64 | `0x6B4C78` | `0x6B4CE0` | `0x6B4D64` | `0x6B4D68` / `0x6B4D6C` | `0x6B4D88` / `0x6B4D9C` |
| Android armv7 | `0x58352E` | `0x583554` | `0x58357C` | `0x583586` / `0x58358C` | `0x58359A` / `0x5835B0` |
| iOS arm64 | `0x10010B620` | `0x10010B664` | `0x10010B69C` | `0x10010B6A0` / `0x10010B6A4` | `0x10010B6C0` / `0x10010B6D8` |
| iOS armv7 | `0x108EFE` | `0x108F62` | `0x108F94` | `0x108FA0` / `0x108FA4` | `0x108FB6` / `0x108FC8` |

## merge：retained root/frame/content source tree

非 type-0 路径的共同 owner tree 是：

```text
retained frameSource accessor(copy(frameSource))
└─ typed Variant frameSource[slot.frameIndex]
   └─ retained frame accessor
      ├─ typed Integer "type"
      ├─ typed Variant "content"
      │  └─ retained content accessor
      │     ├─ typed Integer "interval"
      │     └─ typed Real "value"
      └─ typed Variant "easing"
```

准确执行顺序：

```text
slot.merged = true
construct root accessor
construct frame accessor from root[index]
type = integer(frame["type"])

if type == 0:
    slot.typeZero = true
    destroy frame accessor
    destroy root accessor
    return

slot.typeZero = false
if type == 2: slot.interp = 0
else if type == 3: slot.interp = 1

construct content accessor from frame["content"]
slot.interval = uint32(integer(content["interval"]))
slot.value = real(content["value"])
slot.easing = variant(frame["easing"])

destroy content accessor
destroy frame accessor
destroy root accessor
```

content accessor 虽然在 value 后已经没有 content getter，仍持续到 frame-level easing
assignment 完成。脚本 getter 若在这期间重入清空 caller 的 root、frame 或 content storage，
三层 source 仍必须分别存活。把 content 写成只包围 interval/value 的短 scope 会提前释放；
把 easing 错从 content 读取则改变 receiver、objthis、副作用与异常行为。

## type state matrix 与 stale payload

| recovered type | writes | preserved stale fields | later dynamic reads |
|---:|---|---|---|
| `0` | `merged=true`, `typeZero=true` | interp, interval, value, easing | none after type |
| `2` | `merged=true`, `typeZero=false`, `interp=0`, interval/value/easing | none of refreshed payload | content then easing |
| `3` | `merged=true`, `typeZero=false`, `interp=1`, interval/value/easing | none of refreshed payload | content then easing |
| other nonzero | `merged=true`, `typeZero=false`, interval/value/easing | interp only | content then easing |

特别是 type 0 没有清 easing Variant，也没有把 interval/value 归零。它通过 typeZero byte
阻断消费端，而旧 payload 仍作为 slot 的生命周期成员保留。其他非零未知 type 也不会把
interp 改成默认值。

`interval` getter 先转换成 signed `tjs_int`，再把低 32 位写入 uint32 slot。因此脚本值
`-7` 写成 `0xFFFFFFF9`；这里不是负数 clamp、unsigned typed getter 或额外 saturation。

## Variant assignment 和 teardown

四端 easing 都从 frame accessor 读取 typed Variant。getter 结果 temporary 随后进入
`tTJSVariant` copy assignment：先 retain source owner，再 release 旧 slot.easing owner，
最后复制 payload/type。这个顺序支持 incoming 与 old destination 共享同一 dispatch，不能
用 clear-then-copy 近似。

普通 nonzero return 的 release 顺序在四端完全一致：

```text
content accessor
frame accessor
frameSource/root accessor
```

type-0 路径没有 content accessor，只有 frame → root。异常 unwind 则只析构抛出点之前已
完成构造的 accessor，并保留此前 slot mutation。例如：

- step indexed getter 抛出：只保证 frameIndex 已写，time/merged 不变；
- merge root/index/type getter 抛出：merged 已为 true，typeZero 及后续 payload 仍旧；
- merge content getter 或 conversion 抛出：merged/typeZero/interp 的前缀可能已提交；
- interval conversion 抛出：value/easing 未写；
- value conversion 抛出：interval 已写；
- easing getter/assignment 抛出：interval 与 value 已写，既有已构造 owner 按逆序 unwind。

## dispatch ABI 与关键 merge anchors

indexed/named typed getter 共同使用：

- flags 0；
- numeric/named null member hint；
- holder dispatch 同时作为 receiver 与 objthis；
- ordinary post-write failure HRESULT 被忽略；
- object/integer/real conversion exception 自然传播。

| target | merged | indexed frame | type / state | content | interval / value | easing / assign | teardown |
|---|---|---|---|---|---|---|---|
| Android arm64 | `0x6B4E7C` | `0x6B4EE4` | `0x6B4F68`; `0x6B4F74/90/9C` | `0x6B4FC8` | `0x6B5044/64` | `0x6B5094/B4` | `0x6B50D8/F4`, `0x6B5108` |
| Android armv7 | `0x58365C` | `0x583684` | `0x5836AC`; `0x5836B8/756/6C2` | `0x5836D4` | `0x5836FC/70C` | `0x583728/732` | `0x583750/76C/782` |
| iOS arm64 | `0x10010B784` | `0x10010B7C8` | `0x10010B800`; `0x10010B808/828/81C` | `0x10010B854` | `0x10010B88C/8AC` | `0x10010B8D0/8DC` | `0x10010B900/91C/934` |
| iOS armv7 | `0x1090B4` | `0x109118` | `0x10914A`; `0x109156/166/16E` | `0x10918A` | `0x1091BC/1DC` | `0x109204/212` | `0x10922C/23E/250` |

## portable 源与探针

本轮将两个 helper 从匿名 wrapper 风格迁到 `motion::internal` 的真实 out-of-line 定义，
并在 `Player.h` 声明，以便所有生产 caller 与 test TU 使用同一实现。对齐内容包括：

- root、frame、content 三角色 `ncbPropAccessor`；
- indexed typed Variant 与 named typed Integer/Real/Variant getter；
- raw uint32 index bit pattern 到 signed tjs_int 的显式转换；
- exact write prefix；
- type-0 与未知非零 type 的 stale 字段；
- easing 的 frame receiver 与 copy-assignment；
- content accessor 跨 easing 的生命周期。

新增 probe 覆盖：

1. step indexed getter 写结果后返回 failure，并在 getter 内清 root external owner 与 frame
   storage，验证两层 accessor 保活、flags/index/objthis、time-before-merged 和 frame→root
   teardown。
2. step indexed getter 抛出，验证只提交 frameIndex，frame/root 正确 unwind。
3. merge 在 root/frame/content getter 内逐层清 storage且返回 post-write failure，验证
   type/content/interval/value/easing 次序、content 跨 easing 保活、旧 easing owner 在三层
   accessor 都存活时释放，以及 content→frame→root teardown。
4. type 0 验证只写 typeZero 并保留 interp/interval/value/easing。
5. type getter 抛出验证 merged-only prefix 与 frame/root unwind。

## IDB 回写

四份 recovery IDB 均完成：

- step 与 merge 各一条 V150 function comment；
- step 11 条、merge 18 条关键逐地址注释，即每库 29 条 line comments；
- bookmark `V150 var-slot retained root/frame/content + stale type0`；
- 两函数强制 recompile/decompile；
- 每库 scoped comment 搜索读回 31 个 V150 heads（两个函数入口加 29 个行地址）；
- 四份数据库最终保存。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅有既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅有同一既有 warning。
- `Web Debug Build` 完整构建通过；最终 `index.wasm` 为 85,640,538 bytes。
- `Wasmtime Headless Debug Build` 完整构建通过；最终 `index.wasm` 为
  84,987,679 bytes。
- Node `WebAssembly.Module` 解析两份 Wasm 成功：Web 539 imports / 69 exports，
  headless 538 imports / 69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；因此这里只报告探针成功编译，
  不虚报 runtime CTest。
- `git diff --check` 通过。

