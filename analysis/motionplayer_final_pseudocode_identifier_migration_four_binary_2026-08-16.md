# MotionPlayer 最后一批伪代码标识迁移与死函数复扫（四参考，2026-08-16）

## 结论

生产源码已经没有旧 `libkrkr2.so` 名称、`sub_xxx` 身份或绝对函数地址注释；本轮继续
扫描仍发现一小组不是源结构、而是 Hex-Rays 参数/局部占位符的名称：

- `buildMirrorControl_guess` 的循环变量 `v6`；
- loop keyframe 的 `v0` / `v1` 字段名；
- setVariable 注释中的 `{a3,a4,a5}`；
- alpha-mask wrapper 注释中的 `a9/a10/a11`；
- `EvalCascadeState` 注释中的 `a4`。

四份当前 recovery IDB 的 fresh decompile/disassembly 证明，这些名称都能由稳定的数据流
角色替代：`patternIndex`、segment start/end、`{value,easing,factor}`、
`{threshold,maskMode,op}` 和 binder 的 `value` 参数。源码只迁移标识与注释；字段顺序、
参数 ABI、计算、容器行为和异常边界均不变。

同一轮还对全部 44 个 `cpp/plugins/motionplayer/*.cpp` 翻译单元重放 Web Debug 编译命令，
附加 `-Wunused-function -fsyntax-only`。除公共头中的既有静态 inline/literal warning 外，
没有任何 warning 指向对应 motionplayer `.cpp` 的本地未使用函数。因此此前删除的
`ensureAccurateSlaStateLayer_guess` 是孤立旧端口伪影；当前没有第二个可仅凭 caller census
删除的同类 helper。`main.cpp` 在并行重放时曾无输出返回非零，单独以同一最终命令重放
后返回 0；这是 Emscripten batch 并发重放问题，不是源码诊断。

## 四参考映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildMirrorControl_guess` | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| loop sampler | progress 内联 `0x67A6A4..0x67A734` | `0x554D48` | `0x1001A5984` | `0x1A4F38` |
| `Player_bindParameterValue_guess` | `0x6C1A48` | `0x58C4D8` | `0x100116410` | `0x113D54` |
| `Motion_doAlphaMaskOperation_guess` | `0x6AC4E4` | `0x57E1E8` | `0x100104E68` | `0x10243C` |

## `patternIndex` 与镜像模式向量

四个 builder 都先 snapshot `variableMatchList.Count`，随后从零递增索引，对每项执行
numeric get、Variant-to-`ttstr` 转换和 vector tail append。A64 的 `v6`、A32 的 `i`、
iOS A64 的 `v4` 和 iOS A32 的 `v10` 是同一个 loop counter；它们不代表字段、状态或
不透明 token。四个 IDB 都已把该 local 统一改为 `patternIndex`。

这个改名不改变既有重要边界：builder 没有 enabled gate、过滤、去重、预清空或 cache
失效；每次追加只提交已成功转换的前缀。

## loop keyframe 的 start/end 角色

四端都以 12-byte stride 读取当前 key：

```text
+0  first endpoint
+4  second endpoint
+8  span
out = (accum/span) * second + (1 - accum/span) * first
```

Android arm64 内联块的 `LDP S3,S2,[key]` 后依次执行 `t*S2` 与 `(1-t)*S3`；其余三份
out-of-line sampler 逐项显示同一公式。builder 又严格把 frame `[0]`、`[1]`、`[2]`
窄化为 float 后写入 `+0/+4/+8`。因此旧 `v0/v1` 并非可恢复的原始字段名，只是早期
伪代码标签。源码现在使用 `startValue_guess` / `endValue_guess`，用 `_guess` 明确不声称
恢复了原始拼写；12-byte POD 顺序和 float 运算完全不变。

## 参数角色

### setVariable controller tuple

五类 enqueue 路线接受的源级 tuple 是 `{value, easing, factor}`，并把后两项分别保存为
keyframe duration/powCount。旧注释写 `{a3,a4,a5}` 会把某一份反编译器参数编号误当成
共同源码；现改为语义 tuple。raw-float 存储和 step 的 float load 不变。

### parameter binder

四份 `Player_bindParameterValue_guess(Player*, key, mode, value)` 都在 HM1 hit/insert 后把
第四参数写入 `EvalCascadeState::writeVal`；只有首次插入才把相邻 `weight` 设为 1.0。
随后相同 `value` 继续写 HM2 并进入 ramp。`value_structs.h` 因此移除 `a4`，不改变布局：
64-bit 的 `writeVal/weight` 仍在 value payload `+32/+40`。

### alpha-mask tail

四份 11 参数 compositor 的最后三项稳定为 `threshold`、`maskMode`、`op`。threshold 原样
进入阈值公式；maskMode 选择 stencil branch；op 选择 1/2/5/6 operation 分支。源码 wrapper
本来已经有正确命名和 ABI，本轮只删除旁边重复的 `a9/a10/a11` 伪代码别名。

## 源码与 IDB 落地

- `EmoteLoopController.h/.cpp`、`EmoteEngine.cpp`：loop endpoint 字段和 mirror index 迁移为
  语义名；
- `EmoteEngine.cpp`、`main.cpp`、`internal/value_structs.h`：参数注释不再使用 `aN`；
- 四份 IDB 的 mirror builder local 已统一为 `patternIndex`；loop sampler、binder 和 alpha
  compositor 入口/内联块已添加语义注释；
- 四份 recovery IDB 均保存成功。

本轮没有改变脚本 surface 或新增 test hook；验证使用现有完整 motionplayer unit translation
unit 与 Web/headless archive/link 门禁。

## 验证

- ordinary Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten `-fsyntax-only` 均通过；只有既有
  `_tss` literal warning；
- `Web Debug Build` 完整重编 motionplayer、归档并成功链接 `index.html/index.wasm`；
- `Wasmtime Headless Debug Build --target motionplayer` 成功重编并生成 archive；
- 44 个 translation-unit `-Wunused-function` 普查无 motionplayer 本地 hit；
- residual scan 不再在编译源码发现 `.v0/.v1`、`int v6`、`{a3,a4,a5}`、尾参数
  `a9/a10/a11` 或 `writeVal := a4`；
- scoped `git diff --check` 通过。
