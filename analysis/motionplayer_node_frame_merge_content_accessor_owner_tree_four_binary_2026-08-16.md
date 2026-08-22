# MotionPlayer node-frame mergeContent accessor 所有权树与提交边界四参考复原（2026-08-16）

## 范围与结论

本纵切面重新以 `reference/binaries/` 四份当前产品代码为联合权威，fresh 审计
`MotionNodeFrameSlot_mergeContent_guess`。已有便携实现已经覆盖绝大部分 mask 与字段值，
但把 root、frame、content 及十类 nested object 主要表达成短命 `tTJSVariant` 加独立
`motionPropGet*` 调用。这会丢失脚本 getter 重入清空上游 storage 时仍可观察的 receiver
生命周期，也会改变正常返回和异常展开的析构次序。

四端共同证明 native source tree 是：

- `slot.merged=true` 在任何动态访问之前提交；`slot.done` 立即返回时只保留这一项写入；
- raw frame-list、indexed frame 与 `frame.content` 分别由三只长期 retained accessor 持有；
- coord、object-color、mesh、object-points、motion、model、prt、camera、anchor、feedback
  是按 mask 进入的 block-local accessor；
- 普通 getter HRESULT 仍被忽略，已写 result 后返回失败照常转换和逐字段提交；脚本 getter、
  Object/Integer/Real/String conversion 或 vector allocation 抛异常时不回滚已完成前缀；
- nested accessor 在本 block 尾释放，函数普通尾严格是 feedback（若存在）→ content →
  frame → frame-list root；AArch64 的后部大块 CFG 是 EH cleanup，不是另一条普通析构尾。

符号已剥离，因此 helper 名继续保留 `_guess`。绝对地址仅记录在本文与 recovery IDB，
不写进编译源码注释。

## 四端函数与调用面

| target | function | size | blocks | instructions | raw call xrefs |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x68FE90` | `0x1E38` | 459 | 1924 | 8 |
| Android armv7 | `0x56F06C` | `0xE4C` | 158 | 1304 | 5 |
| iOS arm64 | `0x1000F1970` | `0x1154` | 149 | 983 | 5 |
| iOS armv7 | `0xEDD80` | `0x1276` | 162 | 1706 | 5 |

四端 prototype 均恢复为等价的：

```cpp
void mergeContent(ClipSlot *slot, int nodeType,
                  const tTJSVariant *frameListVariant);
```

unique caller family 完全相同：

| caller role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize slots | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| advance streams | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| parameterized seek | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |
| rewind streams | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |

initialize 在四端都有两个 raw callsite，对应两只 slot。其余 caller 在 A32/iOS 上各有一个
合并后的 callsite；Android A64 对两 slot 路径做了 unroll/duplication，因而 advance、seek、
rewind 各有两个 raw callsite。这里的 8 对 5 是优化形状，不是源级调用族差异。

## 完整 accessor source tree

共同 source tree 可表示为：

```text
retained frame-list/root accessor(copy(raw frame-list Variant))
└─ flags=0 typed Variant root[slot.frameIndex]
   └─ retained frame accessor
      └─ flags=0 typed Variant frame["content"], shared content hint
         └─ retained content accessor                       (whole merge)
            ├─ content["coord"] Variant
            │  └─ coord accessor                            (numeric 0,1,2)
            ├─ content["color"] Variant
            │  └─ accessor only when Type == Object         (numeric 0..3)
            ├─ content["mesh"] or fallback content["obj"]
            │  └─ mesh accessor                             (cc/mcc,bp/bezierPatch)
            │     └─ points accessor only when Object       (count,numeric 0..31)
            ├─ content["motion"] → motion accessor
            ├─ content["model"]  → model accessor
            ├─ content["prt"]    → particle accessor
            ├─ content["camera"] → camera accessor
            ├─ content["anchor"] → anchor accessor
            └─ content["feedback"] → feedback accessor
```

root numeric lookup、frame.content 和所有后续 named/numeric getter 都用 holder dispatch 同时
作为 receiver 与 objthis。flags 为 0；frame.content 使用上一纵切面已经四端闭合的
`contentMemberHint_guess`。nested motion.mask 与 prt.mask 同样继续使用已经闭合的共享
`maskMemberHint_guess`。其余 merge 专属 hint 属于更大的连续全局组，本纵切面没有根据
字段名字猜地址，也没有把当前 null placeholder 固化为 native 结论。

## 十三次 object materialization

A32 和 iOS 两端每份函数都能直接看到 13 次 `tTJSVariant_AsObject_guess`：三只长期 owner 加
十只 nested owner。Android A64 语义相同，但优化器把 root、frame 与 mesh wrapper
scalar-replace 成 retained dispatch pointer，并内联部分 AsObject；不能因 call 数较少而删掉
source-level owner。

| owner | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root | inline/X25 by `0x68FF18` | `0x56F0A8` | `0x1000F19C0` | `0xEDE08` |
| frame | inline/X26 by `0x68FF90` | `0x56F0D2` | `0x1000F19F4` | `0xEDE34` |
| content | `0x68FFEC` | `0x56F106` | `0x1000F1A34` | `0xEDE72` |
| coord | `0x690240` | `0x56F264` | `0x1000F1BCC` | `0xEE018` |
| object color | `0x690784` | `0x56F346` | `0x1000F1CE8` | `0xEE126` |
| mesh | inline/X22 by `0x690934` | `0x56F6A6` | `0x1000F210C` | `0xEE57C` |
| object points | `0x690ADC` | `0x56F772` | `0x1000F2220` | `0xEE68A` |
| motion | `0x690CF8` | `0x56F85A` | `0x1000F2348` | `0xEE7B6` |
| model | `0x690F14` | `0x56F9D4` | `0x1000F2518` | `0xEE9C2` |
| prt | `0x691090` | `0x56FACC` | `0x1000F2640` | `0xEEAFE` |
| camera | `0x69131C` | `0x56FCB6` | `0x1000F286C` | `0xEEDBA` |
| anchor | `0x69144C` | `0x56FD74` | `0x1000F294C` | `0xEEEA0` |
| feedback | `0x69155C` | `0x56FE14` | `0x1000F2A0C` | `0xEEF58` |

object-color accessor 只在 color Variant 的 type 为 Object 时构造；String/Octet/Integer/Real
走标量 Integer conversion 并广播四角，其他 type 广播 0。points 同样只在 Object 时构造；
Void 经过 `bp → bezierPatch` fallback 后仍为非 Object 就完全跳过 count/point reads。

mesh 的 owner 与 points 的 owner 生命周期嵌套：mesh 跨 `cc → mcc` 与 `bp → bezierPatch`
fallback，一旦 points 为 Object，再在其内部建立 points accessor。A64 的 points accessor 在
`0x690C68` 释放，随后 scalar-replaced mesh X22 在 `0x690C8C` 释放。这个次序与 A32/iOS
显式 wrapper 完全一致。

## mask、默认值与逐字段提交

三只长期 accessor 构造成功后，函数才写合并默认值：四角 `0xFF808080`、scale `(1,1)`、
opacity `255`、blend mode `16`。因此 root lookup、frame.content getter 或三层 Object
conversion 在此之前抛异常时，只有 `merged=true` 保证已经提交；默认值仍保留旧 slot 字节。

已恢复的 nested owner gate：

| block | content mask | owner/behavior |
|---|---:|---|
| coord | `0x00000002` | accessor，顺序提交 x、y、z |
| color | `0x00000200` | Object 才建立 accessor；否则标量广播 |
| mesh | `0x02000000` | mesh/obj fallback 后无条件 Object conversion/accessor |
| motion | `0x00080000` | accessor；内部 mask 决定 flags/dt/docmpl/dofst/dtgt |
| prt | `0x00100000` | accessor；内部 mask 决定 trigger/min/max/range |
| camera | `0x00200000` | accessor；fov 后 target |
| anchor | `0x00800000` | accessor；target |
| model | `0x01000000` | accessor；timeOffset/loop/dt/dtgt |
| feedback | `0x08000000` | accessor；timespan |

每个 typed conversion 完成后才写对应 slot 字段，没有 transaction snapshot。例如 coord index 0
完成后 x 已写；index 1 getter/conversion 抛异常时，y/z 保留旧值，但 merged 与四项默认值以及
x 都保留。异常展开顺序为 coord → content → frame → root。普通 `TJS_E_FAIL` 不触发该路径：
只要 getter 已写 result，helper 仍 conversion、提交字段并析构 result Variant。

`slot.done` 是更早的边界：函数先写 merged，再检查 done；命中后不构造 root accessor，
不读取 frameIndex/source，不写默认值，也不触碰任何 payload owner。

## 普通析构尾与 ABI 差异

| owner released | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| feedback | `0x6915CC` | `0x56FE48` | `0x1000F2A40` | `0xEEF92` |
| content | `0x6915E4` | `0x56FE5E` | `0x1000F2A5C` | `0xEEFA4` |
| frame | `0x691600` | `0x56FE76` | `0x1000F2A78` | `0xEEFB6` |
| root | `0x691614` | `0x56FE8E` | `0x1000F2A90` | `0xEEFC8` |

feedback 行只在 mask 命中并完成构造时执行；表中地址是该函数最后一个 nested block 的普通尾。
其他 nested block 均在各自 block 尾先释放。iOS A32 另有 SJLJ unregister；它发生在 root
release 之后，只是异常 ABI bookkeeping。Android A64 普通返回在 `0x691658`；后面的
`0x6916FC..0x691CAC` 是 landing pads 与统一异常清理，不应再被解释成第二条 ordinary tail。

## portable 源码改动

- `mergeNodeFrameContent_guess` 迁出匿名 namespace，成为 `motion::internal` 的单一 out-of-line
  helper，并在 `Player.h` 声明，生产 caller 与 test TU 使用同一实现；
- root、frame、content 改为明确的 retained `ncbPropAccessor`；
- coord、object-color、mesh、object-points、motion、model、prt、camera、anchor、feedback
  均恢复 block-local accessor，析构自然落在 native block 边界；
- `MotionDispatch.h` 补齐 caller-owned dispatch 的 strict probe、named Integer、numeric Real
  与 numeric Integer overload；这些 overload 继续保持 flags/hint、receiver==objthis、
  ordinary HRESULT ignore 与 temporary Variant conversion 语义；
- `copyRawCurveVariant` 改为接收 content accessor 的 dispatch，不再从短命 holder Variant
  重取 Object。

本轮没有提前声明整个 merge hint group。源码只传递已经由四端地址/xref 闭合的 content、
mask 与既有 angle hint；其余字段暂保留 placeholder，下一纵切面逐槽恢复。

## 回归探针

新增 `NodeFrameMerge*` owner chain 覆盖：

1. root numeric getter写 frame 后清 root-held frame 与 external frame-list；frame.content getter
   写 content 后清 frame-held content；content.coord 写 coord 后清 content-held coord；
2. 所有 getter返回 `TJS_E_FAIL`，验证 ordinary failure 不阻止 Object/Real conversion 和提交；
3. 验证 numeric/name flags 都为 0、frame.content 使用准确共享 hint、每层 receiver==objthis；
4. 验证坐标读取期间 root/frame/content/coord 全部存活，普通析构严格为
   coord → content → frame → root；
5. `done=true` 验证只写 merged，source 和旧 payload 完全不动；
6. coord index 1 抛异常验证 defaults 与 x 已提交、y/z 仍旧，以及相同逆序 unwind。

测试只记录 coord hint vector 的存在，不断言 pointer 为 null；这样不会把下一纵切面将修复的
native non-null hint 错误钉死。

## IDB 回写

四份 recovery IDB 均完成：

- function comment 写入完整 source tree、ordinary HRESULT、field prefix 和 cleanup 结论；
- Android A64 回写 25 个稳定 code heads，其余三库各 18 个，覆盖长期/nested owner 的
  materialization 与普通 release；
- A64 单独标注 root/frame/mesh scalar replacement、points→mesh release，以及 ordinary tail
  与 EH landing-pad 分界；
- bookmark `V152 mergeContent retained accessor owner tree + field commit prefixes`；
- 四端强制 fresh decompile，function comment 与既有 content/mask hint 名称已读回；
- 四份数据库保存。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 完整链接通过；`index.wasm` 为 85,643,682 bytes。
- `Wasmtime Headless Debug Build` 完整链接通过；`index.wasm` 为 84,990,823 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；因此这里只报告探针编译通过，
  不虚报 runtime CTest 执行。
- 四个本轮源码文件 `git diff --check` 通过且无 trailing whitespace；仅有工作树既有
  LF→CRLF 提示。

## 下一纵切面

`mergeContent` 仍有一大段连续 process-wide member-hint family 未按字段拆分命名。V153 应从
四端每一个 named getter 的 hint address 建立完整序列表，核对共享 xref 身份，再一次性接入
源码、probe 与 IDB；在这之前不能根据 A64 的 `dword+offset` 或旧单目标注释猜槽名。

