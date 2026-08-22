# MotionPlayer Mirror builder nested `ncbPropAccessor` source identity（四参考，2026-08-16）

## 结论

四份当前参考把 `EmoteEngine_buildMirrorControl_guess` 约束为同一份源码级 owner 图：

```text
copied mirrorControl Variant
  -> root mirrorControl ncbPropAccessor
     -> GetValue<tTJSVariant>("variableMatchList", dedicated hint)
        -> direct temporary Variant
           -> variableMatchList ncbPropAccessor
              -> GetArrayCount() once
              -> for patternIndex in [0, snapshottedCount)
                   GetValue<ttstr>(patternIndex)
                     -> mirrorVariablePatterns.push_back()       [commit]
           -> release variableMatchList accessor
  -> release copied-input root accessor
```

旧 portable 实现虽然能从普通 Array 得到相同字符串，却用 plugin-local raw
`motionPropGet/motionPropGetCount/motionPropGetByNum` 展开了这条链。它没有表达两个 accessor 的
独立 dispatch owner、direct-temporary 转交、getter HRESULT 忽略规则和 nested-before-root 的逆序
析构。本轮把这些结构恢复为真实 ncbind 调用；既有 pattern vector、HM1/HM2 cache 与匹配语义不变。

## 函数与 helper 映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildMirrorControl_guess` | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| named Variant getter | caller 内联 virtual call | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| `ncbPropAccessor_GetArrayCount_guess` | `0x56CA74` | `0x4BEB84` | `0x1000F30F4` | `0xEF8B4` |
| indexed string conversion path | inline get + `0x683AD0` | `0x52E2C4` | `0x100108690` | `0x105DAC` |

stripped 参考没有恢复原始 C++ 标识，所以本地语义名继续使用 `_guess`。Android ARM64 把 indexed
`PropGetByNum` 与后续 Variant-to-`ttstr` 分成 caller 内联和一个转换 helper；另外三端可见一个
组合 helper。这是编译器/ABI 展开差异，不是源行为差异。

## Root 与 nested list source

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input Variant copy | `0x66C774` | `0x558C3C` | `0x1001AB514` | `0x1AABEE` |
| root accessor / input temp dtor | `0x66C780..0x66C7B4` | `0x558C46..0x558C50` | `0x1001AB524..0x1001AB538` | `0x1AAC10..0x1AAC3A` |
| `variableMatchList` named read | `0x66C7E4` | `0x558C66` | `0x1001AB55C` | `0x1AAC62` |
| nested accessor / result temp dtor | `0x66C7F0..0x66C84C` | `0x558C70..0x558C7C` | `0x1001AB564..0x1001AB578` | `0x1AAC68..0x1AAC78` |

输入先 copy-construct 一个 Variant，再通过 `AsObject`/等价内联序列把 dispatch 引用交给 root
accessor；输入 temporary 随即析构。named getter 的结果又先成为 Variant temporary，再交给第二个
accessor；该 temporary 也立即析构。因此函数中真正维持脚本对象生命期的是两个 accessor，而不是
调用者的 Variant，也不是一个贯穿全函数的 raw helper temporary。

两个 getter 层级都使用 flags 0，并把 accessor 持有的同一 dispatch 同时作为 receiver 与
`objthis`。named/indexed getter 在写出有效 Variant 后返回失败 HRESULT 时，四端都没有 success
predicate；written value 仍进入 Variant-to-object/Variant-to-string 转换。这与仓库的
`ncbPropAccessor::GetValue` 规则一致。

## Hint 与 UTF-16LE 字面量

`variableMatchList` 的专用 process-wide mutable hint 在四端分别是：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x1AB4F78` | `0x1111510` | `0x101B6A028` | `0x187DA48` |

它只属于 Mirror builder 的 sole named read，不与顶层 metadata `mirrorControl` hint、运行时
`mirror` flag 或 HM1/HM2 set hash 混用。本地继续使用既有
`mirrorVariableMatchListHint_guess`。

为了排除反编译器把宽串错误渲染成成对 16-bit 字符，本轮对完整 UTF-16LE
`variableMatchList\0` bytes 重新搜索；每库恰好一个命中：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x14D3A18` | `0x558D80` | `0x10195FE48` | `0x17521AC` |

## Count、indexed conversion 与 commit 边界

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `GetArrayCount` snapshot | `0x66C858` | `0x558C86` | `0x1001AB584` | `0x1AAC86` |
| indexed get/string conversion | `0x66C88C..0x66C8A8` | `0x558CAC` | `0x1001AB5AC` | `0x1AACA8` |
| vector append commit | inline `0x66C8AC..0x66C8E0` / growth `0x66C8F8` | `0x558CB4` | inline `0x1001AB5B0..0x1001AB5E0` / growth `0x1001AB5F0` | inline `0x1AACB0..0x1AACE2` / growth `0x1AACF0` |

`Count` 是进入循环前的一次快照；getter 在迭代中修改 list 的 `count` 属性不会扩大或缩短本次循环。
每项直接走 indexed `GetValue<ttstr>`，没有先保留一个跨 append 生存的 `tTJSVariant value` 源变量。

每次 vector tail append 是该项的提交点：

- 字符串转换失败时，本项尚未 append；此前项保留；
- vector allocation/growth 失败时，标准容器负责当前 append 的内部强保证，但 builder 不回滚此前项；
- append 成功后没有 cache invalidation 或第二阶段 publication；
- count 小于等于零时不进入循环；
- duplicates、空字符串和原顺序逐项保留；
- builder 不清目标 vector，也不清 HM1/HM2 positive/negative cache；
- 没有 `enabled` gate、类型标签、dedup、empty filter 或事务化 rollback。

这些边界与此前 HM1/HM2 专题一致；本轮新增的是 owner/accessor 与 indexed conversion 的四端 fresh
证据，不是从旧文档反推。

## Cleanup 顺序

| cleanup | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| variableMatchList accessor | `0x66C914..0x66C92C` | `0x558CD0..0x558CD8` | `0x1001AB60C..0x1001AB620` | `0x1AAD08..0x1AAD16` |
| root mirrorControl accessor | `0x66C930..0x66C940` | `0x558CE6..0x558CEE` | `0x1001AB624..0x1001AB638` | `0x1AAD1A..0x1AAD28` |

正常空列表与非空列表都汇入同一尾部：nested list accessor 先 release，root accessor 后 release。
异常 unwind 也按 C++ 已构造局部的逆序规则恢复同一 owner 前缀；portable 代码用声明顺序
`controlObject` 后 `variableMatchList` 自然表达该边界。

## Portable 源码与生命周期探针

`EmoteEngine.cpp` 现使用：

```cpp
ncbPropAccessor controlObject{tTJSVariant(mirrorControl)};
ncbPropAccessor variableMatchList{controlObject.GetValue(
    TJS_W("variableMatchList"), ncbTypedefs::Tag<tTJSVariant>(), 0,
    &mirrorVariableMatchListHint_guess)};
const int count = static_cast<int>(variableMatchList.GetArrayCount());
for(int patternIndex = 0; patternIndex < count; ++patternIndex) {
    _mirrorVariablePatterns.push_back(variableMatchList.GetValue(
        patternIndex, ncbTypedefs::Tag<ttstr>()));
}
```

新增 probe 在 `variableMatchList` named getter 内可重入地清掉调用者 root Variant，并清掉 root
对象自己保存的 list Variant；两个 dispatch 仍分别由 accessor 保活。count 与 indexed getter 都先写
结果再返回 `TJS_E_FAIL`，锁定 HRESULT 忽略、flags 0 与 exact `objthis`。probe 还核对：

- Count 恰好读取一次；numeric indices 恰为 `{0,1}`；
- root 在两个 numeric read 期间都仍活着；
- list dispatch 析构时 root 仍活着；
- root dispatch 析构时 list 已死；
- 两个 dispatch 都恰好析构一次；
- 两个 written strings 都进入 `_mirrorVariablePatterns`。

Web presets 关闭 Catch2 tests，因此本轮不宣称该 probe 已在 Web runtime 执行；两套完整测试 TU
syntax-only 已确认它和生产代码均可实例化。

## Recovery IDB 与验证

四库均在 builder 入口、root source、sole named read、nested source、Count snapshot、indexed string
conversion、append commit、nested cleanup 和 root cleanup 写入 V138 注释；每库增加 source identity、
indexed conversion、cleanup 三项书签。四个 builder 全部 force-recompile 成功，function-comment
decompile readback 与八个 line-comment disasm readback 全部成功，随后四份 recovery IDB 原位保存。

验证包括：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 的完整 `motionplayer-dll.cpp` syntax-only 均通过，仅有既有
   `_tss` warning；
2. `Web Debug Build` 与 `Wasmtime Headless Debug Build` 均完成，后续增量复核为 no work；
3. `out/web/debug/index.wasm` 与 `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` 均可由
   `llvm-objdump -h` 解析；
4. 更正后的 Clamp 实现范围 raw getter/旧地址扫描为零；Mirror 实现范围的旧 raw getter扫描为零；
5. 本专题源码、测试、文档和计划的限定 `git diff --check` 通过。
