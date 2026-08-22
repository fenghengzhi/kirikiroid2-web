# MotionPlayer Clamp builder `ncbPropAccessor`、共享 hint 与 partial commit（四参考，2026-08-16）

## 结论

四份当前参考把 Clamp builder 约束为以下共同源结构：

```text
copied clampControl Variant
  -> root clampControl ncbPropAccessor
     -> retained outer metadata source Variant
        -> metadata ncbPropAccessor
           -> enabled (shared Engine-wide hint)
           -> deque #7 default emplace / zero entry          [commit]
           -> type   GetValue<tjs_int>  (shared hint)
           -> var_lr GetValue<ttstr>     (Bust/Chain/Clamp shared hint)
           -> var_ud GetValue<ttstr>     (Bust/Chain/Clamp shared hint)
           -> min    GetValue<tjs_real>  (shared with EmotePlayer range setter)
           -> max    GetValue<tjs_real>  (shared with EmotePlayer range setter)
        -> release metadata accessor
     -> destroy retained outer source
  -> release root accessor after the whole loop
```

原有 Clamp entry ABI、默认 append 提交点、字段写入顺序和 post-emplace no-rollback 结论保持
不变。本轮修复的是此前仍由 plugin-local raw getter 表达的 accessor/source identity，以及被拆散或
遗漏的 process-wide mutable hint identity。

最重要的新交叉关系是：Clamp `min/max` 的 getter hints 与
`EmotePlayer::getVariableRange_guess` 在 HM5 hit 分支写新 Dictionary 的 `min/max` setter hints 是
完全相同的两个地址。它们不是“同名但不同 call-site cache”。

## 函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildClampControl_guess` | `0x66C23C` | `0x55892C` | `0x1001AB0A8` | `0x1AA760` |
| `EmotePlayer_getVariableRange_guess` | `0x670FCC` | `0x55AF8C` | `0x1001AE454` | `0x1ADC6C` |
| named integer getter | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |
| named string getter | caller 内联 | `0x492100` | `0x1000F18DC` | `0xEDCB0` |
| named real getter | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |

stripped 参考没有恢复原始 C++ 标识，因此这些语义名继续使用 `_guess`。

## Root 与 retained outer metadata

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input Variant copy | `0x66C270` | `0x558944` | `0x1001AB0D0` | `0x1AA782` |
| root accessor / input temp dtor | `0x66C288..0x66C2C4` | `0x55894E..0x558958` | `0x1001AB0E0..0x1001AB0F4` | `0x1AA7A6..0x1AA7D2` |
| root `GetArrayCount` | `0x66C2D0` | `0x558962` | `0x1001AB100` | `0x1AA7E0` |
| outer indexed result | `0x66C318..0x66C32C` | `0x55899C` | `0x1001AB144` | `0x1AA804` |
| second copy / metadata accessor | `0x66C338..0x66C380` | `0x5589A4..0x5589B6` | `0x1001AB150..0x1001AB168` | `0x1AA810..0x1AA826` |

root count与 outer indexed getter都使用 flags 0、`objthis == retained dispatch`，忽略 dispatch
HRESULT。outer indexed result先成为 iteration source Variant；第二份 copy只用于构造 metadata
accessor。这个 owner拓扑与 V133–V136 的其他 controller builders一致，但本轮是对 Clamp 四函数
重新取证所得，不是类推。

## enabled gate、append 提交点与字段顺序

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| enabled | `0x66C39C` | `0x5589D0` | `0x1001AB184` | `0x1AA850` |
| default append | inline `0x66C3B8` / boundary `0x66C414` | inline `0x5589E4` / boundary `0x5589F8` | helper `0x1001AB190` | helper `0x1AA85A` |
| type | `0x66C450` | `0x558A2A` | `0x1001AB1F4` | `0x1AA8C2` |
| var_lr | `0x66C4A0` | `0x558A42` | `0x1001AB218` | `0x1AA8EA` |
| var_ud | `0x66C524` | `0x558A8C` | `0x1001AB27C` | `0x1AA954` |
| min | `0x66C598` | `0x558ADA` | `0x1001AB2D8` | `0x1AA9BC` |
| max | `0x66C5BC` | `0x558AF8` | `0x1001AB304` | `0x1AA9EA` |

append 在任何字段 getter之前完成；之后没有 `pop_back`、catch-and-continue 或 entry rollback。
所以 typed accessor迁移必须保持：

- disabled metadata不 append；
- type getter失败/转换异常时保留完整零 entry；
- var_lr/var_ud/min/max依次失败时保留此前已发布的字段；
- entry声明布局仍为 `type,min,max,varLr,varUd`，但可观察的读取/赋值顺序严格为
  `type,var_lr,var_ud,min,max`。

fresh integer/string/real helper反编译在可见 out-of-line 目标上共同显示一次 named `PropGet`、同一
dispatch作为 receiver和 objthis、忽略 HRESULT、转换 written Variant、随后析构 temporary。Android
ARM64 var_lr/var_ud把 getter与 ttstr conversion内联在 builder中，语义相同。

## 六个 hint identity

### enabled

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F20` | `0x11114B8` | `0x101B69FD0` | `0x187D9F0` |

它复用 Eye/Eyebrow/Mouth/Transition/Selector/Loop/Bust/Chain 等 controller-family 的 Engine-wide
enabled slot。

### type

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F6C` | `0x1111504` | `0x101B6A01C` | computed `0x187DA03C`（base `[24]`） |

xref consumer集合在前三端一致为 Clamp builder、timeline initialization、shape-anchor resolution。
portable源码继续复用已有 `engineTypeHint_guess`。

### var_lr / var_ud

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| var_lr | `0x1AB4F3C` | `0x11114D4` | `0x101B69FEC` | computed `0x187DA00C`（base `[12]`） |
| var_ud | `0x1AB4F40` | `0x11114D8` | `0x101B69FF0` | computed `0x187DA010`（base `[13]`） |

两组映射 slot 的 consumer集合都是 Bust builder、Chain builder、Clamp builder。portable源码引入
`engineVarLrHint_guess/engineVarUdHint_guess`；本轮 Clamp 已使用，后续 Bust/Chain source-identity
纵切面必须复用同一变量，不能另造局部 hint。

### min / max：Clamp getter 与 EmotePlayer setter 共享

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| min | `0x1AB4F70` | `0x1111508` | `0x101B6A020` | computed `0x187DA040` |
| max | `0x1AB4F74` | `0x111150C` | `0x101B6A024` | computed `0x187DA044` |

前三端 xref consumer严格是 Clamp builder和 `EmotePlayer_getVariableRange_guess`；iOS ARMv7 builder与
EmotePlayer call-site operands也使用同一 computed addresses。EmotePlayer hit分支在 fresh 四端
反编译中均以 `TJS_MEMBERENSURE (0x200)` 设置新 Dictionary 的 real `min/max`，忽略 setter bool。

portable源码因此删除 `EmotePlayer.cpp` 原先两个 translation-unit-local hint，改在公共 dispatch-hint
层声明/定义：

```text
motion::detail::emoteVariableRangeMinHint_guess
motion::detail::emoteVariableRangeMaxHint_guess
```

Clamp named real getter与 EmotePlayer named real setter都引用同一 pair；Player 自身的 range dictionary
仍使用其参考中不同的 `playerRangeMin/Max` slot，不合并。

## UTF-16LE 字面量复核

反编译器在部分目标把宽串渲染成 `"var_"`、`"amin"` 或相邻 literal tail。按 UTF-16LE bytes搜索：

- `var_lr` 与 `var_ud` 在每个目标都各只有一个命中；
- `min/max` 因其他模块也使用而有多个命中，但 Clamp call-site refs分别落到下表 effective literal。

| literal | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| var_lr | `0x14D3958` | `0xD84428` | `0x10195FD68` | `0x17520CC` |
| var_ud | `0x14D3966` | `0xD84436` | `0x10195FD76` | `0x17520DA` |
| min | `0x14D5216` | `0xD84DC6` | `0x10195FE38` | `0x175219C` |
| max | `0x14C6956` | `0xD84DD0` | `0x10195FE40` | `0x17521A4` |

portable C++ 的完整属性名因此保持 `var_lr/var_ud/min/max`。

## iteration cleanup 与 owner 边界

| cleanup | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| metadata accessor | `0x66C5C4..0x66C5DC` | `0x558B06..0x558B0E` | `0x1001AB318..0x1001AB32C` | `0x1AA9F0..0x1AA9FE` |
| retained outer source | `0x66C5E4` | `0x558B12` | `0x1001AB334` | `0x1AAA04` |
| root accessor | `0x66C5F4..0x66C60C` | `0x558B2C..0x558B34` | `0x1001AB348..0x1001AB35C` | `0x1AAA16..0x1AAA24` |

disabled与 enabled路径都进入 accessor→source公共尾部；enabled只是在此前增加 default append与六个
named reads。entry是 Engine deque成员，不在这个栈上清理；其逆成员析构 `varUd -> varLr`、各 ABI
entry size/block公式仍见原 Clamp container文档。

## Portable 源码与探针

本轮修改：

- `EmoteEngine.cpp`：Clamp root/outer metadata恢复为真实 `ncbPropAccessor`；六个 named getter全部
  使用 typed `GetValue`与真实 hints；append和字段顺序不变；
- `MotionDispatch.h` / `RuntimeSupport.cpp`：增加跨文件共享的 variable-range min/max slots；
- `EmotePlayer.cpp`：HM5 hit dictionary setter改用同一 exported pair，删除错误的本地 pair；
- controller-builder probe加入 Clamp kind。

probe仍令 root count/index和所有 named getter先写值再返回 `TJS_E_FAIL`，从而锁定 ncb忽略 HRESULT
的边界；outer root array在 indexed getter内放弃 metadata owner，证明 retained source Variant保活到
iteration尾。测试精确核对：

- read order `{enabled,type,var_lr,var_ud,min,max}`；
- 所有 flags为 0、所有 named `objthis == metadata dispatch`；
- entry值 `{1,"builder-lr","builder-ud",-2.25,4.5}`；
- enabled与此前 builders共享；六个 hint均非空且关键 pair互异；
- Clamp min/max读到的 pointer恰为公共
  `emoteVariableRangeMinHint_guess/emoteVariableRangeMaxHint_guess`。

## Recovery IDB 与验证

四库已对 Clamp入口、outer source、enabled、type、var_lr、var_ud、min、max、iteration cleanup，以及
EmotePlayer min/max setter写入 V137注释；每库增加 Clamp source、var pair、range pair三项书签。
Clamp builder与 EmotePlayer range函数共八个函数全部 force-recompile成功；builder decompile readback、
十个 builder/EmotePlayer disasm点的 V137注释 readback均成功，随后四库原位保存。

验证包括：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer test TU syntax-only通过，仅有既有
   `_tss` warning；Web presets关闭 Catch2 tests，因此不宣称探针已运行；
2. Web Debug重编译 `34/34`通过；
3. Wasmtime Headless Debug重编译 `66/66`通过；
4. 两份最终 `index.wasm`均可由 `llvm-objdump -h`解析；
5. Clamp builder定向旧 raw getter/旧单库地址扫描为零；
6. 本专题源码、测试、文档和计划的限定 `git diff --check`通过。

