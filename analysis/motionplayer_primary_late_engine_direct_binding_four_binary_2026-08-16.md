# MotionPlayer Primary #50–52/#62–69：后段 Engine 直绑四参考复原

日期：2026-08-16

本文只使用 `reference/binaries/` 中四个当前参考二进制，审计 Primary
`Motion.EmotePlayer` 70 项表面的后段。结果纠正了本地剩余的一组源结构偏差：#50–52
和 #62–69 共 11 项都直接保存 `EmoteEngine` 成员指针与零 adjustment，不存在
`EmotePlayer` forwarding member。紧随其后的 #70 `getCommandList` 则确实保存 Primary
wrapper，后者再取 embedded Player；它是本轮刻意保留的负对照。

## 1. registrar descriptor 位置

| member | 脚本名 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 50 | `animating` getter | `0x67E21C` | `0x5617DE` | `0x1001B58A4` | `0x1B54BA` |
| 51 | `setMirror` | `0x67E28C` | `0x5617EE` | `0x1001B58C4` | `0x1B54D8` |
| 52 | `skip` | `0x67E2D0` | `0x561800` | `0x1001B58E4` | `0x1B54F6` |
| 62 | `getMainTimelineLabelList` | `0x67E68C` | `0x5618A2` | `0x1001B5A0C` | `0x1B5610` |
| 63 | `getDiffTimelineLabelList` | `0x67E6A8` | `0x5618B4` | `0x1001B5A2C` | `0x1B562E` |
| 64 | `getLoopTimeline` | `0x67E6E4` | `0x5618C2` | `0x1001B5A4C` | `0x1B564C` |
| 65 | `getTimelineTotalFrameCount` | `0x67E75C` | `0x5618D6` | `0x1001B5A6C` | `0x1B566A` |
| 66 | `getPlayingTimelineInfoList` | `0x67E7B4` | `0x5618E8` | `0x1001B5A8C` | `0x1B5688` |
| 67 | `isSelectorTarget` | `0x67E7F0` | `0x5618FA` | `0x1001B5AAC` | `0x1B56A6` |
| 68 | `activateSelectorTarget` | `0x67E868` | `0x56190C` | `0x1001B5ACC` | `0x1B56C4` |
| 69 | `deactivateSelectorTarget` | `0x67E8D4` | `0x56191E` | `0x1001B5AEC` | `0x1B56E2` |

32 位 registrar 在每个 target materialization 旁显式写 `R3=0`；iOS arm64 factory
调用传 `a4=0`。Android arm64 的 ordinary Function descriptor 先把 code/adjustment
slot 整体清零，再插入 target code pointer；property #50 还把 getter adjustment、setter
code 和 setter adjustment 全部清零，只写 getter code。四端因此不仅调用结果相同，
连 Itanium member-pointer 对象形状也一致。

## 2. 直接 Engine target

| member | recovered target | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 50 | `EmoteEngine_getAnimating_guess` | `0x671378` | `0x55B18C` | `0x1001AE5D8` | `0x1ADE54` |
| 51 | `EmoteEngine_setMirror_guess` | `0x66F190` | `0x55A336` | `0x1001AD644` | `0x1ACCEA` |
| 52 | `EmoteEngine_resetControllers_guess` | `0x66BF6C` | `0x558888` | `0x1001AB03C` | `0x1AA714` |
| 62 | `EmoteEngine_getMainTimelineLabelList_guess` | `0x672334` | `0x55B5C8` | `0x1001AEF14` | `0x1AE6F4` |
| 63 | `EmoteEngine_getDiffTimelineLabelList_guess` | `0x6724A0` | `0x55B63C` | `0x1001AEFA0` | `0x1AE7C8` |
| 64 | `EmoteEngine_getLoopTimeline_guess` | `0x67260C` | `0x55B6B0` | `0x1001AF02C` | `0x1AE89C` |
| 65 | `EmoteEngine_getTimelineTotalFrameCount_guess` | `0x6727D0` | `0x55B750` | `0x1001AF0D4` | `0x1AE9A4` |
| 66 | `EmoteEngine_getPlayingTimelineInfoList_guess` | `0x6728A4` | `0x55B788` | `0x1001AF104` | `0x1AE9D0` |
| 67 | `EmoteEngine_isSelectorTarget_guess` | `0x67F7DC` | `0x562378` | `0x1001B64D0` | `0x1B6394` |
| 68 | `EmoteEngine_activateSelectorTarget_guess` | `0x672BFC` | `0x55B908` | `0x1001AF2F0` | `0x1AEBE4` |
| 69 | `EmoteEngine_deactivateSelectorTarget_guess` | `0x672FD4` | `0x55BAD4` | `0x1001AF628` | `0x1AEE48` |

每一行的 target 都与之前按功能恢复的 Engine core 相同；本轮新增结论是 registrar
直接保存这些 core，而不是保存返回/参数完全相同的 facade thunk。

## 3. typed family 与 owner 边界

11 项复用已经分别闭合的 generated NCBind family：

- #50：property getter，返回 Boolean；没有 setter Function；
- #51：一 Boolean、void 返回；
- #52：无参数、void 返回；
- #62/#63/#66：无参数、Variant 返回；
- #64/#67：一项按值 `ttstr`、Boolean 返回；
- #65：一项 `ttstr`、double 返回；
- #68/#69：一项按值 `ttstr`、void 返回。

这些 ordinary Function 共同遵守 membername/receiver gate、result clear、minimum argc、
native unwrap 和 surplus ignore 顺序。无参 family 接受任意非负 argc；一参 family 只转换
`param[0]`。#62/#63/#66 的 hidden-return Variant 和 Array/Dictionary owner 链已在各自
功能纵切面闭合；#65 的 `double -> tvtReal` handoff 与 #9 `getVariable` 共享同一 typed
family。

selector 三项尤其需要区分源码签名与 ABI 的隐藏地址传递：wrapper 从 `param[0]` 建立一份
owned `ttstr`，再按值传入 #67–69 的 Engine member；Itanium ABI 下非平凡按值对象仍以
不可见地址交给 core，因此反编译中看起来像借用指针。core 只在调用期比较 targets，
不 retain、不 release，也不把地址存入 deque；生成的 caller/wrapper 在 member 返回后
释放其按值参数 owner。这正好与四端 core 中“只解引用 label owner 并遍历，不出现
refcount 操作”一致，也解释了 NCBind 只接受按值 `ttstr` member pointer 的原因。

## 4. 11 条直接调用链

```text
Primary adaptor payload == Engine base at adjustment 0

animating getter       -> Engine controller/filter scan
setMirror(bool)         -> Engine mirror XOR/cache -> full controller reset
skip()                  -> Engine full controller reset

main/diff label lists  -> Engine vector -> fresh TJS Array
loop/total query       -> Engine HM3 non-inserting lookup
playing info list      -> Engine active-label walk -> fresh Array/Dictionary values

selector predicate     -> Engine selector deque/targets scan
selector activate      -> Engine selection + zero-step publication, flag=0
selector deactivate    -> Engine selection + zero-step publication, flag=1
```

这些调用不需要也不经过 embedded Player owner hop。Engine core 自己决定何时访问 Player，
例如 mirror/reset 路径；把一层无状态 facade 留在源码里虽然通常不改变正常返回值，却会
伪造函数边界、异常展开帧、成员指针身份和 NCB descriptor 类型。

## 5. #70 是必须保留的负对照

四端 #70 target 分别是：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x67F900` | `0x5623DE` | `0x1001B65D4` | `0x1B644C` |

恢复名都是 `EmotePlayer_getCommandList_guess`。64 位 body 只有一个 tail branch，32 位
body 也只有取 owner 后的短 thunk，但它们确实先从 Primary/Engine 取得 embedded Player，
再进入 `Player::getCommandList`。registrar 没有保存 Player member pointer，也没有保存
某个 Engine 同名 core。因此源码继续保留 `EmotePlayer::getCommandList()`，不能为了表面
整齐把 #70 与 #62–69 一并删除。

## 6. 源码和回归落地

源码已：

- 删除 #50 inline getter、#51/#52 bodies，以及 #62–69 declaration/body；
- 用显式 `Property`/`Method` 把 11 个脚本名直接绑定到对应 Engine member；
- 保留 #53–61 尚有真实 Primary thunk/raw callback 的表面；
- 保留 #70 Primary→Player wrapper。

回归以 strict member-pointer type 锁定全部 11 个 Engine 签名，并通过真实 Primary adaptor
覆盖：

- `animating` property 返回 Boolean；
- `setMirror`/`skip` 成功后 result 为 Void 且 surplus 忽略；
- 三个 no-arg Variant 方法对空集合仍返回 fresh empty Array；
- loop/total miss 分别返回 false/0.0；
- selector predicate miss 返回 false，activate/deactivate miss 为成功 no-op；
- 一参 family 的 surplus 字符串不被转换。

## 7. recovery IDB 与验证

四份 recovery IDB 已在 11 个 registrar descriptor 点写入 direct Engine target、零
adjustment、typed family 和 #70 负对照注释，并添加四组 bookmark；selector descriptor
另补“按值 `ttstr` 经 ABI 隐藏地址传递”的更正，避免把反编译指针形态误写成源码
`const ttstr &`。四库 registrar 已强制反编译回读。源码测试翻译单元使用 Web Debug 的
真实 Emscripten 参数执行 `-fsyntax-only` 已通过，只有仓库既有 `_tss` warning；完整
10-step Web Debug 构建及最终 `index.html` 链接通过。定向检查确认 #50–52/#62–69 不再有
Primary façade/旧 `NCB_METHOD` 注册，#70 `EmotePlayer::getCommandList` 负对照仍存在，且
相关文件 `git diff --check` 通过。四份 recovery IDB 随后原位保存。
