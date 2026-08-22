# MotionPlayer node frame parser nested NCB、共享 hint 与异常前缀四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新以 `reference/binaries/` 四份当前产品代码为联合权威，fresh 审计
`MotionNodeFrameSlot_parse_guess`。旧便携实现已经恢复 reset、frameIndex、time/type、
content.mask 与 action gate 的大体字段语义，但仍把动态读取写成彼此独立的
`motionPropGet*` wrapper，且没有传 native 的 process-wide member hint。这个表达漏掉了
三类可观察边界：

- raw frame-list、indexed frame、frame.content 分别由三只 retained `ncbPropAccessor`
  持有，普通释放顺序为 content → frame → frame-list root；
- `time/type/content/mask/act` 是一个连续的五 word hint 组，不是 parser 私有临时值；
- reset、逐字段写入和异常 unwind 形成精确的 mutation prefix，尤其 action/icon 是
  reset-exempt stale owner，只有相应 gate 命中才被覆盖。

四端 fresh decompile/disasm/xref 在这些语义上完全一致。符号已剥离，因此函数和新恢复
数据名继续保留 `_guess`。绝对地址只记录在本文和 recovery IDB，不写进编译源码注释。

## 四端函数与调用链

| target | parser | size | reset | mergeContent |
|---|---:|---:|---:|---:|
| Android arm64 | `0x68FA94` | `0x3FC` | `0x68F9EC` | `0x68FE90` |
| Android armv7 | `0x56EDE0` | `0x28C` | `0x56ED5A` | `0x56F06C` |
| iOS arm64 | `0x1000F1464` | `0x50C` | `0x1000F13A0` | `0x1000F1970` |
| iOS armv7 | `0xED638` | `0x23E` | `0xED558` | `0xEDD80` |

每份产品都恰有六个 code xref，caller family 和源级角色相同：

| caller role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize 两次 | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| advance 一次 | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| parameterized seek 两次 | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |
| rewind 一次 | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |

这排除了 parser 只是某个 forward/reverse phase 的 local inline block。它是初始化、增量前进、
参数化双 slot seek 和倒退共同调用的独立 helper。parser 只接收 slot、raw frame-list Variant
与 signed 32-bit frame index；不接收 MotionNode、Player、node type 或 active-slot selector。

## 连续五槽 member-hint 身份

四端都把五个可变 `tjs_uint32` 放在连续地址中：

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `timeMemberHint_guess` | `0x1AB5120` | `0x1111654` | `0x101B695E8` | `0x187D318` |
| `typeMemberHint_guess` | `0x1AB5124` | `0x1111658` | `0x101B695EC` | `0x187D31C` |
| `contentMemberHint_guess` | `0x1AB5128` | `0x111165C` | `0x101B695F0` | `0x187D320` |
| `maskMemberHint_guess` | `0x1AB512C` | `0x1111660` | `0x101B695F4` | `0x187D324` |
| `actMemberHint_guess` | `0x1AB5130` | `0x1111664` | `0x101B695F8` | `0x187D328` |

旧 IDB 只命名 `typeMemberHint_guess`，并错误把它建立为 16-byte data item；因此伪代码把
后面三个真实全局显示为 `typeMemberHint_guess+4/+8/+0xC`，time 则保持 `unk_*`。本轮把
五个边界都重建为独立 4-byte `unsigned int` 并命名，重新 decompile 后四端 parser 都直接
显示五个准确符号。

code-xref 的 unique function family 进一步证明这些槽是 process-wide identity：

- time：parser 与 `Player_skipToSync_guess`；
- type：parser、SeparateLayerAdaptor assignment、Player play、accurate render、
  calcViewParam、skipToSync、getCommandList；
- content：parser、`MotionNodeFrameSlot_mergeContent_guess`、skipToSync；
- mask：parser 与 mergeContent，其中 mergeContent 两次用于 nested motion/prt mask；
- act：目前只由 parser 使用，但仍是进程级可变槽，不是栈临时。

不同 ISA 为装载同一全局产生的 raw xref 数不同，例如 A32 的 literal-load/add/store 和 A64
的 page/add 常形成多条 data xref；判定共享身份时应按 resolved data address 与 unique caller
归并，不能把 instruction xref 数误当成源级 getter 次数。

便携端现在按同一组全局传递 hint。邻接 mergeContent 与 skipToSync 也只接入四端已经确认的
共享 pointer identity：mergeContent 的 frame.content 与两个 nested mask getter，skipToSync
的 type/time/content getter。它们更完整的 accessor owner tree 仍应在各自独立纵切面继续
复核；本轮没有用 hint 证据代替尚未完成的数据流恢复。

## 三层 retained source tree

非 type-0 路径的共同 source tree 是：

```text
retained frame-list/root ncbPropAccessor(copy(rawFrameListVariant))
└─ typed Variant root[frameIndex], flags 0
   └─ retained frame ncbPropAccessor
      ├─ typed Real    frame["time"],    &timeMemberHint_guess
      ├─ typed Integer frame["type"],    &typeMemberHint_guess
      └─ typed Variant frame["content"], &contentMemberHint_guess
         └─ retained content ncbPropAccessor
            ├─ typed Integer content["mask"], &maskMemberHint_guess
            └─ typed String  content["act"],  &actMemberHint_guess
```

构造 root accessor 时，函数先 copy-construct 一个 conversion Variant，再从中取 Object；
conversion Variant 随即析构，但 accessor 自己保留 receiver。numeric typed Variant getter 的
返回值同样直接用于构造 frame accessor，indexed-result conversion Variant 在 time getter
之前就析构。frame.content 又重复这一形状：typed Variant 结果直接构造 content accessor，
conversion Variant 随即析构。

因此源码中三个长期 `const tTJSVariant` 局部变量并不准确：它们会改变临时析构点、owner
数量与重入 getter 清空外部 storage 后的存活状态。portable parser 现改为三只明确的
`ncbPropAccessor`，与 native source identity 一致。

所有 typed getter 的共同 ABI：

- flags 为 0；
- named getter 使用上述准确 hint pointer，numeric getter 的 hint 为 null；
- holder dispatch 同时作为 receiver 和 objthis；
- ordinary HRESULT 被忽略；getter 已写 result 后返回 `TJS_E_FAIL` 仍继续 conversion 与写入；
- Object/Real/Integer/String conversion 或脚本 getter 抛出的异常自然传播；
- parser 没有 frameIndex bounds guard、负数钳制或 malformed Variant fallback。

## 精确写入顺序与 type/mask gate

四端共同伪代码：

```text
reset(slot)
slot.frameIndex = frameIndex

root  = retained accessor(copy(frameList))
frame = retained accessor(typed Variant root[frameIndex])

slot.clipStartTime = typed Real frame["time"]
type = typed Integer frame["type"]

if type == 0:
    slot.done = true
    destroy frame
    destroy root
    return

slot.done = false
if type == 2: slot.crossfading = false
if type == 3: slot.crossfading = true

content = retained accessor(typed Variant frame["content"])
slot.contentMask = typed Integer content["mask"]
if slot.contentMask & 0x40000:
    slot.actionValue = typed String content["act"]

destroy content
destroy frame
destroy root
```

reset 在 parser 第一项执行；随后 frameIndex 在任何 dynamic access 之前提交。time 完成
typed conversion 后才写 clipStartTime，type getter 再执行。type 0 只把 done 写 true，完全
跳过 content/mask/action。非零 type 先写 done=false：type 2 写 crossfading=false，type 3 写
true；其他非零 type 没有 crossfading store，但 reset 已把它清成 false，因此它不同于
VariableTrack merge 中“未知 type 保留旧 interp”的 stale 行为。

reset 仍遵守早先四端已闭合的 selective 边界：清 frame/time/ti/contentMask、
done/crossfading/merged、src、transform 标量、七个 Variant、mesh vector size 与 motion
flags/dt；保留 mesh allocation/capacity、icon、action 和后续 motion-tail 字段。于是：

| path | contentMask | actionValue | iconValue |
|---|---|---|---|
| type 0 | reset 为 0 | 保留旧 owner | 保留旧 owner |
| nonzero, mask 无 `0x40000` | 写新 mask | 保留旧 owner，由 mask gate 禁止消费 | 保留旧 owner |
| nonzero, mask 有 `0x40000` | 写新 mask | 由 `content.act` 覆盖 | 保留旧 owner |

action 的 typed String temporary 先拥有新 `ttstr`。随后 assignment 先 retain incoming owner，
再 release/替换旧 action owner，最后 temporary 析构；不能用 `Clear(); assign` 近似。普通返回
时 content accessor 先释放，frame 第二，frame-list root 最后。type-0 没有 content
accessor，因此只有 frame → root。

## 异常 mutation prefix

parser 不提供 transactional rollback。各抛出点保留此前已经完成的 reset/store：

| throw point | guaranteed committed state |
|---|---|
| frame-list copy/Object conversion | reset，frameIndex |
| numeric frame getter/conversion | reset，frameIndex |
| time getter/conversion | reset，frameIndex；clipStartTime 尚未写 |
| type getter/conversion | reset，frameIndex，新 clipStartTime |
| content getter/conversion | reset，frameIndex，time，done/crossfading 的 type 前缀 |
| mask getter/conversion | 上述前缀；contentMask 仍是 reset 的 0 |
| act getter/conversion | 上述前缀；新 contentMask 已写，旧 action 仍保留 |
| action assignment | mask 已写；assignment 自己遵守 retain-before-release |

异常 unwind 只析构已经完成构造的 accessor，顺序始终逆构造。例如 mask/act 抛出时为
content → frame → root；time/type 抛出时为 frame → root。getter 若先把 result owner 写入
其 temporary、再抛出，该 temporary 也会在 helper 栈展开中释放，不泄漏半构造 owner。

## 关键 raw anchors

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| reset | `0x68FAC0` | `0x56EDFC` | `0x1000F1484` | `0xED65A` |
| frameIndex | `0x68FACC` | `0x56EE04` | `0x1000F1488` | `0xED664` |
| root copy / temp dtor | `0x68FAD0` / `0x68FB10` | `0x56EE06` / `0x56EE1A` | `0x1000F1494` / `0x1000F14B8` | `0xED66A` / `0xED6B6` |
| indexed Variant / temp dtor | `0x68FB34` / `0x68FB9C` | `0x56EE2A` / `0x56EE40` | `0x1000F14D0` / `0x1000F14EC` | `0xED6CA` / `0xED6E0` |
| time getter / store | `0x68FBBC` / `0x68FBC0` | `0x56EE54` / `0x56EE62` | `0x1000F150C` / `0x1000F1510` | `0xED704` / `0xED722` |
| type getter | `0x68FBE0` | `0x56EE70` | `0x1000F1530` | `0xED732` |
| done false / type0 true | `0x68FBEC` / `0x68FC08` | `0x56EE7C` / `0x56EF32` | `0x1000F1538` / `0x1000F1558` | `0xED73E` / `0xED74E` |
| content getter / temp dtor | `0x68FC44` / `0x68FCA4` | `0x56EE9C` / `0x56EEB2` | `0x1000F1588` / `0x1000F15A4` | `0xED77C` / `0xED792` |
| mask getter / store | `0x68FCC4` / `0x68FCC8` | `0x56EEC6` / `0x56EECE` | `0x1000F15C4` / `0x1000F15C8` | `0xED7B6` / `0xED7C0` |
| act getter / assignment | `0x68FCF0` / `0x68FD0C` | `0x56EEE4` / `0x56EEE8` | `0x1000F15F0` / `0x1000F15F4` | `0xED7E8` / `0xED7EC` |
| content/frame/root release | `0x68FD40/5C/70` | `0x56EF2C/48/5E` | `0x1000F1640/5C/74` | `0xED834/46/58` |

A32 mask 路径在 `0x56EECA` 先 `TST`，`0x56EECE` 再 store；iOS A32 同样在
`0xED7BC` test、`0xED7C0` store。branch flags 在 store 之间不被破坏，源级语义仍是先得到
同一个 mask 结果、写 slot 并按 `0x40000` gate；不能据反汇编局部次序误造第二次 getter。

## portable 源与回归探针

portable `parseNodeFrame_guess` 迁出匿名 namespace，成为 `motion::internal` 的单一 out-of-line
定义并在 `Player.h` 声明；生产 caller 和 test TU 因而共享同一函数。实现使用 root/frame/
content 三只 accessor、五个共享 hint、typed Real/Integer/Variant/String getter 和原生写前缀。

新增 probe 覆盖：

1. root numeric getter 内清 persistent/external frame-list owner，frame.content getter 内清
   frame-held content storage，所有 getter 写 result 后返回 `TJS_E_FAIL`；验证三层仍存活、
   flags/hint/objthis 与 content→frame→root teardown。
2. 成功 action 路径验证 reset 保留 icon、清 src/vector/motion flags/dt、覆盖旧 action，且
   typed String temporary 析构后 slot 仍持有新字符串。
3. type 0 验证只访问 time/type，done=true，contentMask=0，icon/action 保留。
4. 未知 nonzero type 加无 action mask 验证 crossfading 取 reset false，旧 action 保留但由
   mask=0 gate 禁止消费。
5. index/time/type/content/mask/act 六个异常 section 分别验证 mutation prefix、已构造 owner
   的逆序 unwind，以及 stale icon/action。
6. 既有 skipToSync reentrant owner probe 扩展为检查 type/time/content 使用同一五槽组中的
  准确 pointer、flags 0 和 receiver==objthis。

## IDB 回写

四份 recovery IDB 均完成：

- 五个 hint 重新建立为相邻但独立的 4-byte data item，并命名；
- parser 一条 V151 function comment、21 个关键 code heads 的逐地址注释；
- 五个 hint data heads 的身份/共享 caller 注释；
- bookmark `V151 node-frame retained root/frame/content + five shared hints`；
- parser、mergeContent、skipToSync 强制 recompile，parser fresh decompile 已直接显示五个名称；
- scoped readback 每库得到 22 个 parser comment heads 与 5 个 data comment heads；
- 四份数据库最终保存。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅有既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅有同一既有 warning。
- `Web Debug Build` 完整构建通过；最终 `index.wasm` 为 85,641,158 bytes。
- `Wasmtime Headless Debug Build` 完整构建通过；最终 `index.wasm` 为 84,988,299 bytes。
- Node `WebAssembly.Module` 解析两份 Wasm 成功：Web 539 imports / 69 exports，
  headless 538 imports / 69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；这里报告探针编译通过，不虚报
  runtime CTest 执行。
- `git diff --check` 通过。

## 仍未由本纵切面闭合

- `MotionNodeFrameSlot_mergeContent_guess` 的完整 retained accessor tree、全部 nested content
  object owner 与每一字段异常 prefix；本轮只使用 xref/decompile 已确认的 content/mask hint
  identity。
- `Player_skipToSync_guess` 自己的 frame/content accessor 临时析构边界；既有纵切面已恢复
  overall state mutation，本轮只补 type/time/content 的共享 hint。
- parser 的原始未剥离 C++ 名称；因此 `_guess` 必须保留。

