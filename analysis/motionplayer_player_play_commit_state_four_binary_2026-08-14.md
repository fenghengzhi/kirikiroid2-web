# MotionPlayer `Player::play` / `playImpl` 四端提交状态机（2026-08-14）

## 结论

本轮只把 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四个参考二进制的共同实现当作真值；旧 `libkrkr2.so` 单端地址、旧
`playMotionLike_0x6B2284` 名称和本地先前的 `bool` 返回抽象均不再作为证据。

四端共同恢复出的源级接口为：

```cpp
void Player::play(tjs_int flags, const ttstr &motion);
void Player::playImpl(const ttstr &motion, tjs_int flags);
```

需要特别注意两层参数顺序不同。`Player::play` 是 `(flags, motion)`，
`Player::playImpl` 是 `(motion, flags)`；所有机器码 caller 都按该顺序重排。
两者都返回 `void`。反编译器在部分目标上显示出来的整数、指针、`__n128` 返回值只是
未定返回寄存器残值，不是源码级 `bool` 或对象结果。

`motion` 在这两层都是借用的 `ttstr` 槽。正常入口不会为 `play` 或 `playImpl` 的形参
制造 owner；只有以下位置发生 CopyRef：

1. stealth 请求暂存到 pending 字段；
2. `playImpl` 调用按值 `loadMotion` helper 前，分别复制 live stealth-chara 与请求标签；
3. 成功提交到 live stealth/primary motion 字段。

## 函数与 caller 映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player::play` | `0x6AF5C8` | `0x5800EC` | `0x1001074A4` | `0x104A7C` |
| `Player::playImpl` | `0x6AF664` | `0x580158` | `0x100107540` | `0x104AE8` |
| Player NCB `play` wrapper | `0x6CFFE8` | `0x59565C` | `0x1001212C0` | `0x120050` |
| EmotePlayer `play` thunk | `0x67C7EC` | `0x5610B0` | `0x1001B5040` | `0x1B4CBC` |
| EmotePlayer motion setter thunk | `0x67F08C` | `0x561EAC` | `0x1001B5F84` | `0x1B5C34` |

四端的直接 caller 集合共同包含：

- Player 脚本 `play` NCB wrapper；
- EmotePlayer 的 C++ `play` 转发；
- EmotePlayer `motion` setter；
- EmoteObject 初始化；
- type-3 motion-subnode child 播放；
- 粒子 child 播放（Android arm64 该小路径被优化/合并，其他三端保留独立 xref）。

没有 caller 消费 `Player::play` 的返回寄存器。EmotePlayer thunk 同样是 `void`，只是从
owned Player 取指针、重排参数并转发。

## `Player::play`：pending owner 与直接借用

共同伪代码：

```text
play(flags, borrowedMotion):
    if (flags & Stealth) != 0 and liveStealthChara owner is null:
        pendingStealthMotion = CopyRef(borrowedMotion)
        return

    playImpl(borrowedMotion, flags)

    if pendingStealthMotion owner is non-null:
        playImpl(reference-to-pending-field, Stealth)
        release(pendingStealthMotion)
        pendingStealthMotion = null
```

这里有三个可观察边界：

1. pending 判定是底层 `ttstr` owner 是否为 null，不是一次新字符串转换；
2. flush 不先复制 pending，也不在 nested call 前清字段，而是把持久成员槽地址直接传给
   `playImpl`；nested call 整段期间该 owner 仍在字段中；
3. nested call 抛异常时，调用后的 release/null 不执行，因此 pending owner 保持原值。

旧端口曾把 pending 复制到局部值、先清字段再调用。这会改变重入可见状态、异常后的字段
状态以及最后一个 owner 的析构时点，现已移除。

## `playImpl` 的门控顺序

四端顺序一致：

```text
selected = Stealth ? liveStealthMotion : livePrimaryMotion

if no (Force | AsCan) and selected == requestedMotion:
    return

if no Force and AsCan and player.playing:
    return

if Join:
    resetMotionState()

loadResult = loadMotion(copy(liveStealthChara), copy(requestedMotion))
```

- Force 与 AsCan 参与第一个同标签门控，因此任一置位都会绕过同标签 early return；
- 只有 `!Force && AsCan && playing` 才进入第二个 early return；
- Join reset 在 load 前执行，load 失败或后续异常不会恢复 snapshot；
- load 始终使用 live stealth-chara，不因本次是否 Stealth 而切换到 primary chara；
- load helper 的两参数是按值 owner，但 `playImpl` 自身的请求仍是借用引用。

## 成功提交顺序

四端都只以 `loadResult.Type() != tvtVoid` 判成功，不预检 Object 类型。精确顺序为：

```text
if loadResult is non-Void:
    liveStealthMotion = CopyRef(originalRequestedMotion)
    if not Stealth:
        livePrimaryMotion = CopyRef(originalRequestedMotion)

    resultCopy = CopyRef(loadResult)
    resultDispatch = resultCopy.AsObject()       // owning conversion
    clear(resultCopy)

    temp0 = Void
    resultDispatch.PropGetByNum(flags=0, index=0, hint=null, out=temp0)
    ignore status
    motionContent = CopyRef(temp0)
    clear(temp0)

    temp1 = Void
    resultDispatch.PropGetByNum(flags=0, index=1, hint=null, out=temp1)
    ignore status
    findMotionContext = CopyRef(temp1)
    clear(temp1)

    motionCopy = CopyRef(motionContent)
    motionDispatch = motionCopy.AsObject()       // independent owning retain
    clear(motionCopy)

    type = motionDispatch.PropGet(
        flags=0, name="type", staticHint, out=temp).AsInteger()
    ignore property status

    if type == 1:
        transition ordinary -> direct-edit if necessary
        division   = motionDispatch.PropGet(flags=0, staticHint)
        motionList = motionDispatch.PropGet(flags=0, staticHint)
        emoteMotionIndex = -1
        initEmoteMotion(flags)
    else if type == 0:
        transition direct-edit -> ordinary if necessary
        initNonEmoteMotion(flags)
    else:
        no initializer

    release(motionDispatch)
    release(resultDispatch)
    destroy(loadResult)
```

结果容器与 motion-content dispatch 是两层独立 retain。后者一直存活到 type 分支和
initializer 完成；即使属性 getter 重入并替换 Player 字段，当前分支仍在原 dispatch 上
继续。析构顺序是 motion dispatch、result dispatch、原始 `loadResult` Variant。

### 逐步提交而非事务

成功路径没有 rollback：

- 非 Void、非 Object 的 `loadResult` 在 `AsObject()` 抛出前，两个 live motion 标签已经
  提交；
- index 0 成功提交，而 index 1 抛异常时，新 motion-content 与两个新标签保留，旧 context
  保留；
- index getter 返回失败但不抛异常时，状态码被忽略，初始化为 Void 的输出仍会覆盖对应
  Player 字段；
- motion-content 非 Object 时，两个标签、元素 0、元素 1 都已经提交，随后才抛转换异常；
- type/division/motionList getter 或 initializer 抛异常也不回滚此前任何提交；
- 未识别的 type 不清 loaded 字段，也不调用任一 initializer。

## load 失败分支

`loadResult.Type() == tvtVoid` 的共同顺序为：

```text
log("motion not found " + liveStealthChara + "/" + originalRequestedMotion)
destroy log string temporaries
motionContent.clear()
findMotionContext.clear()
playing = false
destroy loadResult
return void
```

失败分支不写 primary/stealth motion 标签。因此旧标签是否为空完全取决于进入本次
`playImpl` 前的状态；本分支只清两个 Variant owner 与 playing。日志使用原始请求标签，
不是 `onFindMotion` 调整后的 lookup 名称，并且日志发生在字段 clear 之前。

## direct-edit 类型分流

`type == 1` 与 `type == 0` 的状态搬运保持原版顺序：

- ordinary -> emote：若尚未 direct-edit，把 root delta angle 搬到 emote-angle 并把 root
  angle 置零；随后设置 direct-edit，读取 division/motionList，索引置 `-1`，调用 emote
  initializer；
- emote -> ordinary：若已经 direct-edit，把 emote-angle 搬回 root delta angle 并把
  emote-angle 置零；随后清 direct-edit，调用 ordinary initializer；
- type 不是 0/1：不改变 direct-edit，也不触发 initializer。

## NCB wrapper 的 raw dispatch 生命周期

四端 Player NCB wrapper 的共同顺序为：

```text
unwrap Player; require numparams >= 2
player.currentDispatch = objthis       // raw, no AddRef
flags = param[1].AsInteger()
label = ttstr(param[0])                // local owner
player.play(flags, label)              // void
destroy(label)
player.currentDispatch = null
return TJS_S_OK
```

wrapper 不写 `result`，所以调用者提供的旧 result Variant 保持不变。更重要的是，raw
`currentDispatch` 在两次参数转换之前就已写入，且只在正常返回尾部清空：

- flags 转换、label 构造或 `play` 内部任何调用抛异常时，字段保持 `objthis`；
- Android arm64 与 iOS armv7 的显式异常 cleanup 只析构已构造的 label，然后 resume；
- Android armv7 与 iOS arm64 没有清该字段的异常 cleanup；
- 字段本身从不 AddRef，因此异常后它可能成为悬挂 raw 指针。

这是四端共同边界，不应以 RAII guard 自动“修复”。本地 `playCompat` 已保留正常返回清理
与异常不清理的差异。

## 本地实现修正

- `Player::playMotion_guess` 与 `playMotionImpl_guess` 从错误的 `bool` 改为 `void`；
- `Player::playMotion_guess` 恢复 `(flags, const ttstr&)`，`playMotionImpl_guess` 恢复
  `(const ttstr&, flags)`；
- EmotePlayer / D3DEmotePlayer 的 `play` 及 motion setter 转发同步改为 `void`；
- pending flush 改成直接借用持久成员槽，并在 nested call 返回后才 release/null；
- success 分支恢复“标签 -> result Object -> index 0 -> index 1 -> motion Object -> type”的
  增量提交链；
- 两个 numeric getter 均显式使用 flags 0、忽略 status；type/division/motionList 均从
  retained dispatch 以 flags 0 和静态 hint 读取；
- failure 分支恢复先日志、后清两个 Variant、最后清 playing；
- `playCompat` 不再清 result，且在参数转换前写 raw current-dispatch，异常时不清该字段；
- 删除旧 `setMotionCompat`：四端 `motion` 属性是普通 typed property，setter 只执行
  `setMotion(by-value label) -> play(0,label)`，不会保存 `objthis`、手工调用
  `onFindMotion`、改写 chara 或提交 callback 改写后的标签；
- 删除该纵向触及位置中的旧单端地址/偏移说明，源码注释只保留四端共同语义。

## 回归覆盖

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增/加强：

- load-result container 在 motion `type` getter 期间仍存活，返回后才析构；
- type getter flags 为 0、hint 非空；
- 非对象 load result 抛异常后两个 live motion 标签已经提交，content/context 边界正确；
- index 1 抛异常后 index 0 字段已提交、context 仍是旧值，且 numeric flags/index 顺序为
  `{0,0}` / `{0,1}`；
- `playCompat` 异常后 raw current-dispatch 仍指向 `objthis`；
- `playCompat` 正常返回不改 result，并把 raw current-dispatch 清空。
- typed `motion` property 设置时，即使实例上覆盖了 `onFindMotion`，callback 调用计数仍为
  0；ResourceManager 收到原始 `motion/<live chara>/<setter label>`，live chara/label 也保留
  原始 setter 语义。

`chara` / `stealthChara` 的 live/pending 槽、值相等 no-op、标签无效化范围与 pending
direct-member flush 已独立闭合，见
`motionplayer_player_chara_pending_four_binary_2026-08-14.md`。尤其是角色变化只清两个
motion label 与 playing，不清本节成功路径提交的 content/context owner。

## IDB 改进

四份 recovery IDB 均已：

- 把 `Player_play_guess`、`Player_playImpl_guess` 的返回类型纠正为 `void`；
- 把两个 EmotePlayer thunk 命名并纠正为 `void`；
- 标注 borrowed label、pending direct-member call、成功提交点、两次数值读取、两层 dispatch
  retain、释放顺序和失败清理；
- 标注 Player NCB wrapper 的 result 不写、raw dispatch 正常/异常不对称生命周期；
- 保存到各自 recovery IDB。

## 验证

- Web Debug 完整目标重新编译并成功链接 `index.html` / Wasm；
- 聚合 `motionplayer-dll.cpp` 使用 Web Debug 真实 Emscripten defines/includes/ABI 参数执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check` 通过；仅有工作树既有 LF/CRLF 提示。
