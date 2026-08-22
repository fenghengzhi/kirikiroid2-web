# MotionPlayer Loop builder nested `ncbPropAccessor`、hint 与 owner 栈（四参考，2026-08-16）

## 结论

四份当前参考把 Loop builder 约束为同一个四层访问器图：

```text
copied loopControl Variant
  -> root loopControl ncbPropAccessor
     -> retained outer loop-element source Variant
        -> loop-element ncbPropAccessor
           -> enabled, shared Engine-wide hint
           -> transitionList GetValue<Variant> temporary
              -> transitionList ncbPropAccessor
                 -> each frame GetValue<Variant> temporary
                    -> frame ncbPropAccessor
                       -> GetValue<tjs_real>(0/1/2), no hint
                       -> caller-side double-to-float narrowing
           -> deque raw-owner emplace
           -> var_loop GetValue<ttstr>, Loop-only hint
           -> HM6 publication {type=3, original metadata index}
```

这里有三个不能从旧 plugin-local raw getter 写法看出的 owner/ABI 边界：

- 外层 indexed element 的返回值被保存在独立 `tTJSVariant` source 中；第二次 copy 只用于建立
  element accessor。iteration 尾部先 Release accessor，再析构 source；
- `transitionList` 和逐帧 frame 都没有对应的长期 source local。它们由 `GetValue<tTJSVariant>`
  返回的 temporary 直接构造 nested accessor；accessor AddRef/AsObject 后 temporary 立即析构；
- frame 的三个 typed getter 返回 `tjs_real`，四端都在 builder caller 内再窄化为 `float`。这与
  Selector 的 `offValue/onValue` typed-float helper 不同，不能合并成一种伪 helper。

原有 raw controller 到 deque owner 的接管缺口、12-byte keyframe、disabled sparse metadata index、
`var_loop` publication 和异常 partial-commit 行为都保持不变。本轮只替换已由四体共同证明的访问器
结构、hint 身份、typed conversion 和 temporary 生命周期。

## 函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildLoopControl_guess` | `0x66B860` | `0x558440` | `0x1001AAA8C` | `0x1AA158` |
| indexed `GetValue<tjs_real>` helper | `0x66699C` | `0x4C7734` | `0x1000F2FF8` | `0xEF66C` |

参考均为 stripped binary；builder 和 helper 的未知原始标识继续保留 `_guess`。

## Root 与 retained outer element

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input Variant copy | `0x66B894` | `0x55845A` | `0x1001AAAB4` | `0x1AA17A` |
| root accessor 建立 / input temp dtor | `0x66B8AC..0x66B8E8` | `0x558466..0x558470` | `0x1001AAAC4..0x1001AAAD8` | `0x1AA19E..0x1AA1CA` |
| root `GetArrayCount` | `0x66B8F0` | `0x558476` | `0x1001AAAE4` | `0x1AA1D4` |
| outer indexed Variant result | `0x66B94C..0x66B960` | `0x5584D6` | `0x1001AAB30` | `0x1AA206` |
| second copy / element accessor | `0x66B96C..0x66B9B4` | `0x5584E2..0x5584F4` | `0x1001AAB3C..0x1001AAB54` | `0x1AA212..0x1AA228` |

root count 与 outer indexed getter 均使用 flags 0、`objthis == retained dispatch`，并忽略 dispatch
HRESULT。outer getter result 在四端都先成为 iteration source；element accessor 的构造另做一次
Variant copy/AsObject。公共尾部的 owner 逆序见后文。

## `enabled` 与 `transitionList`

`enabled` 复用 V133–V135 已闭合的 Engine-wide hint；Loop 没有自己的 enabled slot：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F20` | `0x11114B8` | `0x101B69FD0` | `0x187D9F0` |

四库 xref 显示该 identity 也被 Bust/Chain/Eye/Eyebrow/Mouth/Transition/Selector/Clamp 等 builder
使用。Loop call site 分别是 `0x66B9CC`、`0x558508`、`0x1001AAB70`、`0x1AA24C`。

enabled 为真后，element accessor 执行 named `GetValue<tTJSVariant>("transitionList")`，getter
temporary 直接建立 nested accessor：

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| named getter | `0x66B9FC` | `0x558526` | `0x1001AAB94` | `0x1AA27A` |
| accessor 建立 / temporary dtor | `0x66BA1C..0x66BA5C` | `0x55852C..0x558538` | `0x1001AAB9C..0x1001AABB8` | `0x1AA280..0x1AA290` |
| nested `GetArrayCount` | `0x66BA80` | `0x558550` | `0x1001AABD8` | `0x1AA2B0` |

`transitionList` 使用 Loop-only hint：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F64` | `0x11114FC` | `0x101B6A014` | computed `0x187DA034`（operand 为 base `+0x58` / `[22]`） |

前三个已映射 slot 的 data xref 只有 Loop builder；Android ARM 上同一 slot 的 ADRP/ADD 或 literal
load 会产生两个/三个机械 xref，不代表多个语义 consumer。iOS ARMv7 的 computed address 落在 IDA
data item 的未映射尾外表示中，因此只在 call-site operand 与本文记录，不强制造 data。

## 每帧 direct-temporary accessor 与 triple real

每个 `transitionList[keyIndex]` 都直接构造 frame accessor：

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| indexed frame getter | `0x66BB08` | `0x55857E` | `0x1001AAC14` | `0x1AA2E0` |
| accessor 建立 / temporary dtor | `0x66BB28..0x66BB68` | `0x558584..0x558590` | `0x1001AAC1C..0x1001AAC30` | `0x1AA2E6..0x1AA2F6` |
| real index 0 | `0x66BB80` | `0x5585A6` | `0x1001AAC4C` | `0x1AA30C` |
| real index 1 | `0x66BB9C` | `0x5585C0` | `0x1001AAC68` | `0x1AA330` |
| real index 2 | `0x66BBBC` | `0x5585DA` | `0x1001AAC88` | `0x1AA34E` |
| frame accessor Release | `0x66BBD0..0x66BBE0` | `0x5585F0..0x5585F8` | `0x1001AAC98..0x1001AACAC` | `0x1AA362..0x1AA370` |

fresh 四端 helper 反编译一致证明：

```text
temporary Variant = void
retainedDispatch.PropGetByNum(flags, index, &temporary, retainedDispatch)
ignore HRESULT
result = temporary.AsReal()       // tjs_real / double
destroy temporary Variant
return result
```

call site 的 flags 都是 0，没有 hint 参数；indices 严格按 0、1、2。ARM64 使用 `FCVT S0,D0`，
两份 ARMv7 使用 `VCVT.F32.F64`，所以窄化发生在 caller。三个结果按 12-byte stride 写入
`{startValue_guess,endValue_guess,span}`，每项均为 float。

即使 `PropGetByNum` 返回普通失败 HRESULT，只要 dispatch 写出了 Variant，helper 仍转换该值；本地
测试用失败但写值的 frame dispatch 锁定了这一边界。

## raw controller 接管、`var_loop` 与清理顺序

controller 仍在 nested transition accessor 存活期间以 raw pointer 创建、resize、填充并直接入队。
成功 emplace 后才读取 `var_loop`；因此 `transitionList` accessor 跨过 controller publication，直到
HM6 `{3, metadataIndex}` 写入之后才释放。

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `var_loop` named getter | `0x66BCC0` | `0x55866E` | `0x1001AAD0C` | `0x1AA3CE` |
| Variant→ttstr conversion | `0x66BCEC` | `0x558676` | `0x1001AAD18` | `0x1AA3DA` |
| getter Variant dtor | `0x66BD38` | `0x5586AE` | `0x1001AAD60` | `0x1AA426` |
| transitionList accessor Release | `0x66BD58..0x66BD68` | `0x5586C2..0x5586D0` | `0x1001AAD7C..0x1001AAD98` | `0x1AA440..0x1AA44E` |
| element accessor Release | `0x66BD78..0x66BD90` | `0x5586D6..0x5586E4` | `0x1001AADA0..0x1001AADB4` | `0x1AA450..0x1AA460` |
| retained outer source dtor | `0x66BD98` | `0x5586E8` | `0x1001AADBC` | `0x1AA466` |
| root accessor Release | `0x66BDA8..0x66BDC0` | `0x5586F6..0x55870A` | `0x1001AADD0..0x1001AADE4` | `0x1AA476..0x1AA486` |

`var_loop` 使用另一个 Loop-only hint：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F68` | `0x1111500` | `0x101B6A018` | computed `0x187DA038`（operand 为 base `+0x5C` / `[23]`） |

前三端映射 slot 的 xref 也只属于 Loop builder；它与 `transitionList`、shared enabled 以及 Selector
三个 dedicated hint 都是不同 identity。`var_loop` getter仍是 ncbind typed `GetValue<ttstr>`：
compiler 把 named Variant fetch 和 Variant→ttstr conversion 分成两个 helper/call site，不应误写回
plugin-local string getter。

disabled 路径跳过 transitionList/controller/var_loop，只执行 element accessor→outer source 公共尾部。
enabled 路径则按 frame accessor（每帧）→var_loop temporary→transitionList accessor→element accessor→
outer source 清理；root accessor 在整个循环之后释放。

## Portable 源码与回归探针

`cpp/plugins/motionplayer/EmoteEngine.cpp` 已恢复：

- copied-input root accessor、retained outer element source 和 element accessor；
- shared `engineEnabledHint_guess`；
- dedicated `loopTransitionListHint_guess` / `loopVarLoopHint_guess`；
- direct-temporary transitionList/frame accessors；
- indexed `GetValue<tjs_real>` 后 caller-side `static_cast<float>`；
- 原有 raw controller→deque owner gap、label publication 和 HM6 type/index 顺序不变。

测试把 controller-builder dispatch 组扩展到 Loop：

- root count/index、element named、transitionList count/index、frame index 全部先写值再返回 `TJS_E_FAIL`；
- outer root array在 indexed getter内清掉 element owner，证明 retained outer source保活；
- element dispatch在 `transitionList` getter内清掉 nested list owner，证明 getter temporary建立的 accessor
  保活，并证明该 accessor在稍后的 `var_loop` 读取时仍存活；
- transitionList dispatch在 frame getter内清掉 frame owner，证明 direct-temporary frame accessor保活到
  triple read尾部；两层对象均恰好析构一次；
- 精确核对 element read order `{enabled,transitionList,var_loop}`、frame indices `{0,1,2}`、所有 flags
  为 0、所有 `objthis == receiver`、三个 float结果，以及 shared/dedicated hint pointer身份。

## Recovery IDB 与验证

四个 recovery IDB 已在 builder入口、root/outer source、transitionList、frame triple、`var_loop`、
cleanup 与 hint call site写入 V136 语义注释和书签。四个 builder均强制重新反编译；decompile
readback确认 root count、transitionList、real helper、var_loop 与入口 V136 注释，disasm readback
逐点确认七个 call-site/cleanup 注释，随后四库原位保存。

验证包括：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer test TU syntax-only 通过，仅有仓库
   既有 `_tss` warning；当前 Web presets 明确关闭 Catch2 tests，因此这里不把探针误报为已运行；
2. Web Debug 增量构建 `3/3` 通过；
3. Wasmtime Headless Debug 增量构建 `4/4` 通过；
4. `out/web/debug/index.wasm` 与 `out/wasmtime/debug/index.wasm` 均可由 `llvm-objdump -h` 解析；
5. Loop builder 定向区域的旧 `detail::motionPropGet*` raw getter扫描为零，旧单库 provenance/地址扫描
   为零；
6. 本专题源码、测试、文档和 `plan.md` 的限定 `git diff --check` 通过。
