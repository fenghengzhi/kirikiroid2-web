# MotionPlayer `motionKey` / `project` 四端对照（2026-08-12）

## 结论

本轮以 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7 四个当前参考二进制为共同真值，重新复原了 `Motion.Player` 与 `Motion.EmotePlayer` 的 `motionKey` / `project` 属性、持久 Variant owner、运动查找数据流、成功/失败提交边界、子 Player 传播和析构释放顺序。旧 `libkrkr2.so` 单端地址注释不再作为证据。

四端共同语义如下：

- `motionKey` 和 `project` 是两个脚本成员名，但它们安装同一个 getter/setter 语义，并共享 Player 内唯一一个持久 `tTJSVariant` 字段；没有第二个 `project` 字段。
- getter CopyRef 持久字段，返回一个独立 owner；它不把 Variant 转成字符串、不查找 motion，也不克隆对象内容。
- setter 对持久字段执行 `tTJSVariant` copy-assignment。新 owner 在旧 owner 释放前被保留，所以以 getter 结果重新赋回同一字段是安全的。
- setter 本身没有 motion 查找、播放、标签更新或 playing 状态副作用。
- `loadMotion` CopyRef 当前字段作为 `ResourceManager.findMotion` 的参数 0；参数 1 是 `"motion/" + chara + "/" + motion`。
- 成功播放先提交原始请求标签，再强制把非 Void 返回值转换成 Object；随后依次把返回容器元素 0 提交给 motion-content owner、元素 1 提交给该 context owner。每一步都不回滚：转换、第二次索引、type 读取或 initializer 抛异常时会保留此前已经完成的标签/字段写入。
- 加载失败清空 motion-content 与 context 两个 owner，并清 playing 字节；两个公开属性因此同时读到 Void。
- type-3 与粒子子 Player 都得到独立 CopyRef。之后父子任一方成功覆盖、失败清空或析构，都不直接改变另一方字段。
- Player 析构在成员逆序析构阶段释放字段；getter 先前返回的独立 Variant owner 仍保持对象存活。

## 字符串、注册和字段映射

### Player

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `motionKey` 字符串 | `0x14D3E56` | `0xD8480C` | `0x10195CA56` | `0x174EDBA` |
| `project` 字符串 | `0x14D3E6A` | `0xD84820` | `0x10195CA6A` | `0x174EDCE` |
| `motionKey` 注册 | `0x6D4390` | `0x598024` | `0x1001246E0` | `0x123A06` |
| `project` 注册 | `0x6D43F8` | `0x598040` | `0x100124714` | `0x123A34` |
| getter | `0x692FC0` | `0x570E6C` | `0x1000F3D84` | `0xF06F8` |
| setter | `0x6B1D58` | `0x5817A4` | `0x100109168` | `0x106994` |
| Player 字段偏移 | `+1012` | `+696` | `+900` | `+632` |

iOS 两端的两次属性注册明确复用同一 getter/setter 函数地址。Android armv7 同样把已装载的一对函数指针连续用于两个名称。Android arm64 的模板展开更复杂，但 getter/setter 本体、两个注册点和最终字段均唯一。

四端 Player getter/setter 的共同源级形态为：

```cpp
tTJSVariant getMotionKeyProject_guess() const {
    return _findMotionContextVariant; // CopyRef
}

void setMotionKeyProject_guess(const tTJSVariant &value) {
    _findMotionContextVariant = value; // copy-assignment
}
```

Android arm64、iOS arm64 的 getter 是 sret CopyRef；两个 32 位目标用显式返回对象地址。setter 都直接调用同一 ABI 的 Variant copy-assignment helper。

### EmotePlayer 转发

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `motionKey` 注册 | `0x67D848` 前后的属性块 | `0x56151E` | `0x1001B544C` | `0x1B50D4` |
| `project` 注册 | 同一连续属性块 | `0x56153A` | `0x1001B5480` | `0x1B5102` |
| getter 转发 | `0x67F128` | `0x561F18` | `0x1001B5FEC` | `0x1B5CF4` |
| setter 转发 | `0x67C6C0` tail | `0x560FD4` | `0x1001B4F68` | `0x1B4B38` |

EmotePlayer 不保存第二份 context。getter 先从 owned Player CopyRef 字段，再把临时 Variant 作为返回值传出；setter 先由 NCB 把脚本参数物化成按值 Variant，再 copy-assign 到 owned Player 字段并清理临时值。Android arm64 的 setter tail 被 IDA 错误合并进前一个引用计数 vector assignment 函数；原始指令从 `0x67C6C0` 开始仍完整显示独立 prologue、临时 Variant、owned Player 指针和字段 copy-assignment。本文不把错误的合并函数边界伪装成可靠源函数名。

## `loadMotion`：context 是参数 0

| 目标 | `Player_loadMotion_guess` | RM Variant 字段 | context 字段 |
|---|---:|---:|---:|
| Android arm64 | `0x6AE2F0` | `+992` | `+1012` |
| Android armv7 | `0x57F654` | `+684` | `+696` |
| iOS arm64 | `0x1001067BC` | `+880` | `+900` |
| iOS armv7 | `0x103BBC` | `+620` | `+632` |

四端共同伪代码：

```text
loadMotion(player, requestedChara, requestedMotion):
    result = Void
    chara  = copy(requestedChara)   // helper 按值 owner
    motion = copy(requestedMotion)

    if current script dispatch exists:
        request = { chara, motion }
        currentDispatch.onFindMotion(request, result=&result)
        ignore callback status
        response = CopyRef(result).AsObject() // owning conversion
        chara  = required response.chara with empty-string-on-failure
        motion = required response.motion with empty-string-on-failure

    rm = CopyRef(player.resourceManager).AsObject()
    path = "motion/" + chara + "/" + motion
    contextArg = CopyRef(player.motionContext)
    pathArg = CopyRef(path)
    rm.findMotion(contextArg, pathArg, result=&result)
    ignore findMotion status
    return result
```

这里有五个容易混淆的边界：

1. `onFindMotion` 改写的是 chara/motion 查找名，不替换 context 参数。
2. context 与 path 都先构造成独立临时 Variant；调用结束按逆序清理。
3. property setter 只改下一次查找的参数 0，不会立即调用 `findMotion`。
4. callback 与 RM dispatch 都被独立持有；非对象值会在 owning conversion 处抛出。
5. 两个调用复用同一 result 槽且都忽略状态码；若 `findMotion` 失败且不写 output，
   callback response Object 会原样成为 helper 返回值。完整证据见
   `motionplayer_player_load_motion_dispatch_four_binary_2026-08-14.md`。

## `playImpl` 的提交与失败边界

| 目标 | `Player_playImpl_guess` | success motion-content | success context | failure clear pair |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AF664` | `0x6AF8E0` | `0x6AF92C` | `0x6AFBC8`, `0x6AFBD0` |
| Android armv7 | `0x580158` | `0x5802A4` | `0x5802C4` | `0x5803D0`, `0x5803D8` |
| iOS arm64 | `0x100107540` | `0x1001076BC` | `0x1001076E8` | `0x100107868`, `0x100107870` |
| iOS armv7 | `0x104AE8` | `0x104CAE` | `0x104CD8` | `0x104E70`, `0x104E7E` |

共同顺序：

```text
result = loadMotion(...)
if result is non-Void:
    stealthMotion = originalRequestedMotion
    if not Stealth:
        primaryMotion = originalRequestedMotion
    resultObject = CopyRef(result).AsObject()
    motionLocal  = resultObject[0]       // flags 0, ignore status
    player.motionContent = motionLocal
    clear(motionLocal)
    contextLocal = resultObject[1]       // flags 0, ignore status
    player.motionContext = contextLocal
    clear(contextLocal)
    motionObject = CopyRef(player.motionContent).AsObject()
    inspect motionObject.type            // flags 0, static hint
    dispatch emote/non-emote initializer
else:
    log not found
    clear(player.motionContent)
    clear(player.motionContext)
    player.playing = false
```

success 分支的两个标签写入甚至早于返回容器的 Object 转换；元素 0 与元素 1 又是逐个提交，而不是事务式解包。因而非对象 result 会留下新标签，元素 1 抛异常会留下新标签和新 motion-content、但保留旧 context。type getter、节点构建或任何 initializer 抛异常时，异常清理同样不恢复旧字段。失败分支则明确清两个 owner；不是“保留旧 project 以供重试”。完整的 `play`/`playImpl` 顺序和 owner 释放链见 `motionplayer_player_play_commit_state_four_binary_2026-08-14.md`。

## 子 Player 传播

节点树四端复核已经确认 type-3 初始化顺序包含：

```text
child.parentPlayer = parent
child.rootPlayer = parent.rootPlayer ? parent.rootPlayer : parent
...
child.motionContext = parent.motionContext  // Variant CopyRef
copy coordinate / zFactor / transform order
create NCB adaptor
```

粒子 child 创建路径同样在第一次 `playMotion` 之前复制父 context。复制的是 Variant owner，不是指向父字段的引用。因此：

- 父字段随后被新 motion 成功覆盖时，child 仍持有旧 owner，直到自己加载并由 result[1] 覆盖；
- child 加载失败只清 child 字段；
- parent/child 析构各自只释放自己的 owner；
- 对象型 context 的底层 dispatch 会在最后一个父/子/外部 getter owner 释放时销毁。

## 析构与外部 getter owner

| 目标 | Player 析构 | context release |
|---|---:|---:|
| Android arm64 | `0x6CCEBC` | `0x6CD034` (`+1012`) |
| Android armv7 | `0x593C24` | `0x593CB0` (`+696`) |
| iOS arm64 | `0x10011F2A0` | `0x10011F344` (`+900`) |
| iOS armv7 | `0x11DCC4` | `0x11DDB4` (`+632`) |

四端均在 Player 析构主体完成自定义 tree/resource teardown 后，按成员逆序销毁 Variant/字符串/容器成员。tag owner 比 context 更晚声明，因而先于 context 释放；context 又明显早于 motion-content owner 释放。该次字段释放不会触碰 getter 已 CopyRef 出去的 Variant。外部保存的对象型 `motionKey/project` 值在 Player 析构后仍可调用其 dispatch。

## 本地实现修正

本轮修正/加强：

- `Player.h`
  - 明确把 `motionKey` / `project` 建模为同一持久 Variant；
  - 保留 NCB 按值物化层；新增 `setMotionContextVariant_guess(const tTJSVariant&)` 作为原生形态的 copy-assignment 内核；
  - 移除相关旧 `libkrkr2.so` 地址/偏移注释。
- `main.cpp`
  - 两个公开名仍分别注册，但注释改成四端共同语义，不再引用过时单端地址。
- `PlayerMotionLoad.cpp` / `PlayerUpdateParticles.cpp`
  - 标明 type-3/粒子 child 获得独立 owner，而非父字段引用。
- `PlayerRender.cpp` / `PlayerRenderItems.cpp` / `PlayerResource.cpp`
  - 清理相关过时地址/偏移注释，保留 context 消费的源级所有权描述。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 记录 `findMotion` 实际收到的 context 参数；
  - 覆盖两个属性名共享同一字段、两向脚本读写、getter alias 自赋值、setter 无即时 lookup、成功 result[1] 覆盖、getter owner 跨 Player 析构存活，以及失败清空两个名称。

本地原先直接把按值参数 `std::move` 到字段。对象净引用数在普通路径通常相同，但它没有表达四端 Player setter 的 copy-assignment 调用结构。当前实现显式分为 NCB 按值 wrapper 与 `const&` copy-assignment 内核，既满足现有 ncb convertor 只能转换按值 Variant 的约束，也恢复了参考二进制中的所有权顺序。

## IDB 改进

四份 IDB 均新增并 fresh-decompile 验证以下 `_guess` 名称：

- `Player_getMotionKeyProject_guess`
- `Player_setMotionKeyProject_guess`
- `EmotePlayer_getMotionKeyProject_guess`
- `EmotePlayer_setMotionKeyProject_guess`（Android arm64 除外；该目标因 IDA 错误函数合并，仅给 setter tail 保留行级语义注释）

Android arm64 被错误覆盖的前一函数已恢复为 `ObjectRefVector_copyAssign_guess`，避免把一个真实 vector assignment 误命名成 EmotePlayer setter。getter/setter、loadMotion、playImpl 和析构在改名/注释后均使用当前 IDB fresh-decompile 复核。

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten 定义和头路径执行 `-fsyntax-only`：通过；唯一诊断为仓库既有 `_tss` literal-operator 弃用警告。
- Web Debug `motionplayer` 静态库：编译成功，立即复跑为 `ninja: no work to do.`。
- Wasmtime Debug `motionplayer` 静态库：编译成功，立即复跑为 `ninja: no work to do.`。
- 完整 Web Debug 最终目标成功链接 `index.html` / Wasm，并同步 shell prealloc memory。
- Wasmtime guest 成功完成链接、`wasm-opt --translate-to-exnref` 与后处理；首次外层调用在该阶段超过 60 秒，但后台 Ninja 正常完成，随后复跑为 `ninja: no work to do.`。
- Web 完整目标、Wasmtime guest、两套 motionplayer 静态库随后连续增量复跑，四项均为 `ninja: no work to do.`。
- `git diff --check`：通过；仅有工作树既有 LF 到 CRLF 提示。
