# MotionPlayer Selector builder nested `ncbPropAccessor` 与 owner 栈（四参考，2026-08-16）

## 结论

四份当前参考对 Selector builder 给出同一个五层数据/owner 图：

```text
copied selectorControl Variant
  -> root selectorControl ncbPropAccessor
     -> retained outer selector-element source Variant
        -> selector-element ncbPropAccessor
           -> local selector label ttstr（label 先于 enabled 读取）
           -> optionList GetValue<Variant> temporary
              -> optionList ncbPropAccessor
                 -> each indexed option GetValue<Variant> temporary
                    -> option ncbPropAccessor
                       -> local option label ttstr
                       -> typed float offValue / onValue
```

重要区别是：

- 外层 selector element 的 indexed return 被保存在独立 source Variant 中，直到本 selector
  iteration 公共尾部才析构；第二份 Variant copy 只负责构造 element accessor；
- `optionList` named Variant 与每个 indexed option Variant 都是直接构造 nested accessor 的
  temporary，accessor 获得 dispatch owner 后 temporary 立即析构；
- selector `label` 在 `enabled` 之前读取。disabled 时用该 label 删除 raw variable binding，随后
  仍按 label→element accessor→outer source 的顺序清理；
- enabled 时每个 option 的尾部先释放 option label，再释放 option accessor；selector 尾部依次
  释放 optionList accessor、moved-from option vector、selector label、element accessor、outer
  source；整个循环后才释放 root accessor。

这组源码身份在四端分别重新核对，没有从 Transition 或 Eye/Mouth builder 类推。既有
first-match transition borrow、flag clear、raw Selector controller owner、sparse type-8 publication
和异常 partial-commit 边界保持不变。

## 函数映射

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x66ACDC` | `0x557E04` | `0x1001AA030` | `0x1A96D8` |

`EmoteEngine_buildSelectorControl_guess` 继续带 `_guess`，因为 stripped 参考没有原始符号。

## Root 与 outer selector-element owner

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input Variant copy | `0x66AD14` | `0x557E1E` | `0x1001AA058` | `0x1A96FA` |
| root accessor vptr/AsObject | `0x66AD2C..0x66AD64` | `0x557E2A..0x557E30` | `0x1001AA068..0x1001AA074` | `0x1A971E..0x1A9746` |
| copied input temp dtor | `0x66AD68` | `0x557E34` | `0x1001AA07C` | `0x1A974A` |
| root `GetArrayCount` | `0x66AD70` | `0x557E3E` | `0x1001AA084` | `0x1A9758` |
| outer indexed source | `0x66B268` | `0x557EAC` | `0x1001AA0F0` | `0x1A978C` |
| second copy / element accessor | `0x66B288..0x66B2CC` | `0x557EC0..0x557ECE` | `0x1001AA0FC..0x1001AA110` | `0x1A9798..0x1A97AA` |
| accessor-copy temp dtor | `0x66B2D0` | `0x557ED4` | `0x1001AA118` | `0x1A97AE` |

root count 与 indexed getter 都对 accessor `_obj` 使用 flags 0、`objthis == _obj`，忽略 dispatch
HRESULT。外层 indexed getter的临时 result先 copy 成 retained source，再由 source copy 构造
element accessor；因此栈上同时存在 Variant storage 与 accessor vptr/dispatch storage。

## Selector label→enabled 顺序与共享 hints

| getter | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| selector label | `0x66B2FC` | `0x557EF4` | `0x1001AA138` | `0x1A97D6` |
| selector enabled | `0x66B33C` | `0x557F12` | `0x1001AA154` | `0x1A9800` |

两者使用 V133/V134 已恢复出的 Engine-wide identity：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| label | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |
| enabled | `0x1AB4F20` | `0x11114B8` | `0x101B69FD0` | `0x187D9F0` |

selector local label owner一直保留到本轮末尾：disabled branch先调用
`removeVariableLabel_guess(label)`，enabled branch把 label copy 到新 deque entry 并以该 entry key
发布 type 8；两条路径之后才释放 local label。

## `optionList` nested accessor

element accessor 的 named `GetValue<tTJSVariant>("optionList",flags=0,hint)` 直接构造 nested
`ncbPropAccessor`：

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| named Variant getter | `0x66B374` | `0x557F38` | `0x1001AA180` | `0x1A9830` |
| accessor vptr/AsObject | `0x66B394..0x66B3C0` | `0x557F3E..0x557F46` | `0x1001AA188..0x1001AA194` | `0x1A9836..0x1A9842` |
| result temporary dtor | `0x66ADC4` | `0x557F4A` | `0x1001AA19C` | `0x1A9846` |
| option count | `0x66ADD0` | `0x557F50` | `0x1001AA1A8` | `0x1A9854` |

Android ARM64 因基本块布局把 accessor tail 放到较低地址，不改变执行顺序。此处只有 nested
accessor继续持有 option-list dispatch；没有一个像 outer element 那样跨整个作用域保留的独立
`optionList` source Variant。

`optionList` 使用 Selector-only hint：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F58` | `0x11114F0` | `0x101B6A008` | computed `0x187DA028`（operand 显示 `dword_187D9DC[19]`） |

xref 复核中前三端的 code consumer都只属于 Selector builder；iOS ARMv7 的精确地址落在 IDA
未映射的 data-item 尾外表示中，因此把 computed slot 记录在 call-site 注释而不强行造 data。

## 每个 option 的 direct-temporary accessor

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| indexed option getter | `0x66AE1C` | `0x557F78` | `0x1001AA1CC` | `0x1A9870` |
| option accessor建立 | `0x66AE3C..0x66AE78` | `0x557F7E..0x557F86` | `0x1001AA1D4..0x1001AA1E0` | `0x1A9876..0x1A9882` |
| getter result temp dtor | `0x66AE7C` | `0x557F8A` | `0x1001AA1E8` | `0x1A9886` |
| option label | `0x66AEAC` | `0x557FA2` | `0x1001AA208` | `0x1A98AE` |
| offValue | `0x66AF9C` | `0x557FEC` | `0x1001AA2BC` | `0x1A9966` |
| onValue | `0x66AFBC` | `0x558022` | `0x1001AA2FC` | `0x1A99BA` |

option label复用 Engine-wide label hint。`offValue`/`onValue` 使用两个 Selector-only hint：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| offValue | `0x1AB4F5C` | `0x11114F4` | `0x101B6A00C` | computed `0x187DA02C`（`[20]`） |
| onValue | `0x1AB4F60` | `0x11114F8` | `0x101B6A010` | computed `0x187DA030`（`[21]`） |

四端 `ncbPropAccessor_GetValueNamedFloat_guess` 分别位于
`0x661AEC / 0x552C68 / 0x1001A316C / 0x1A2490`。fresh helper 反编译共同证明：只执行一次
named `PropGet`，HRESULT 被忽略，temporary Variant 按 TJS real 转换并在模板内收窄 float，随后
析构；这里不是 `double` helper返回后由 caller再收窄。

每个 option的本地 label在 option vector append 后先释放，随后 accessor Release：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| label `0x66B09C..0x66B0A8`，accessor `0x66B0B4..0x66B0C4` | label `0x558034`，accessor `0x558042..0x55804A` | label `0x1001AA344`，accessor `0x1001AA34C..0x1001AA360` | label `0x1A99F6`，accessor `0x1A99FC..0x1A9A0A` |

## Selector iteration 与 root 清理

| cleanup | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| optionList accessor | `0x66B21C..0x66B22C` | `0x5580D6..0x5580DE` | `0x1001AA60C..0x1001AA620` | `0x1A9C96..0x1A9CA6` |
| option vector storage | `0x66B234..0x66B238` | `0x5580E2..0x5580E4` | `0x1001AA644..0x1001AA66C` | `0x1A9CAA..0x1A9CD2` |
| selector label | `0x66B3CC..0x66B3D4` | `0x5580FE` | `0x1001AA674` | `0x1A9CD6` |
| element accessor | `0x66B3D8..0x66B3F0` | `0x558108..0x558110` | `0x1001AA67C..0x1001AA690` | `0x1A9CDE..0x1A9CEC` |
| retained outer source | `0x66B3F8` | `0x558116` | `0x1001AA698` | `0x1A9CF2` |
| root accessor | `0x66B408..0x66B420` | `0x558130..0x558138` | `0x1001AA6AC..0x1001AA6C0` | `0x1A9D02..0x1A9D12` |

disabled 路径没有 optionList/vector两项，但保留 selector label→element accessor→source 的公共尾部。
enabled 路径在 controller/map publication 后先离开 nested accessor/vector 作用域，再进入同一公共
尾部。portable declaration/block order据此恢复。

## Portable 源码与回归

`cpp/plugins/motionplayer/EmoteEngine.cpp` 已完成：

- root selector control、outer element、optionList 与 option 全部恢复为真实 `ncbPropAccessor`；
- outer source element单独保留，optionList/option则用 typed getter temporary直接构造 accessor；
- selector label→enabled 顺序不变，label/enabled复用 Engine-wide hints；
- 增加 optionList/offValue/onValue 三个 Selector-only hint；
- off/on 从 plugin-local double getter+caller cast 改为真实 `GetValue<float>`；
- transition first-match borrow、option vector顺序、raw controller→deque owner与 type-8 map发布不变。

测试把 V133/V134 的 source-lifetime probe扩展到 Selector，并增加 option-array/option-element 两层
dispatch：

- root、option-list count、outer indexed、option indexed和所有 named getter都先写值再返回
  `TJS_E_FAIL`；
- outer array与 option array都在 indexed getter内部放弃自身 child owner；
- outer element必须靠 retained source活到 selector iteration尾；option则由 direct temporary建立的
  accessor保活到 option尾，两者都恰好析构一次；
- 精确核对 selector element read order `{label,enabled,optionList}` 与 option read order
  `{label,offValue,onValue}`；
- 核对 flags/index/objthis、option float结果、共享 label/enabled hint pointer，以及三个
  Selector-only hint互异。

## Recovery IDB 与验证

四库已对 builder入口、outer source、label/enabled、optionList、option direct temporary、option
label/off/on、三层 cleanup 和三个 hint slot写入 V135 注释与入口书签；四函数 force-recompile后
逐点 readback成功。Android ARM64/iOS ARMv7 因 disasm分页限制使用完整 instruction-offset分页
readback，所有目标点均确认；四库已原位保存。

验证包括：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer test TU syntax-only 通过，仅有
   仓库既有 `_tss` warning；
2. Web Debug 与 Wasmtime Headless Debug 增量构建通过；
3. 两份最终 wasm可由 `llvm-objdump -h` 解析；
4. Selector builder旧 raw getter定向扫描为零；
5. 本专题源码、测试、文档和 `plan.md` 限定 `git diff --check` 通过。
