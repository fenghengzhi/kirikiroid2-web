# MotionPlayer `Player_loadMotion` dispatch、返回槽与所有权四端复原（2026-08-14）

## 结论

本轮重新以 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考二进制为共同真值，纵向复核了 `Player_loadMotion_guess` 从
`onFindMotion` 回调、回调结果属性读取到 `ResourceManager.findMotion` 的完整数据流。
旧 `libkrkr2.so` 注释不作为证据。

此前 portable 实现已经正确恢复了 callback-adjusted chara/motion、context 参数与完整
load-result owner，但仍压平了五个可观察边界：

1. `chara`、`motion` 是由 helper 自己持有并可覆盖的按值 `ttstr` 参数；
2. `onFindMotion` 和 `findMotion` 使用同一个隐藏 sret/result Variant，调用间不清空；
3. 两次 `FuncCall` 的状态码都被忽略；
4. callback result 与 ResourceManager dispatch 都通过 owning conversion 独立持有；
5. callback 属性读取使用 `TJS_MEMBERMUSTEXIST`，失败时采用空字符串，即使失败 getter
   已经写了输出也不转换该输出。

这些边界现已写回 `PlayerCore.cpp`，并由专门的失败但写入/失败且不写入差分探针固定。

## 四端入口与 ABI

| 目标 | helper | 大小 | 直接 caller |
| --- | ---: | ---: | --- |
| Android arm64 | `0x6AE2F0` | `0x808` | `0x6AF710`, `0x6B04E4` |
| Android armv7 | `0x57F654` | `0x27C` | `0x580200`, `0x58099C` |
| iOS arm64 | `0x1001067BC` | `0x3C4` | `0x1001075F0`, `0x100107F3C` |
| iOS armv7 | `0x103BBC` | `0x3C0` | `0x104BDC`, `0x105578` |

两份 AArch64 目标的优化后 ABI 共同为：

```text
X0 = Player *
X1 = ttstr *chara-by-value storage
X2 = ttstr *motion-by-value storage
X8 = tTJSVariant *hidden sret/result
```

helper 入口立即把 `X8+16` 的 Variant type 写成 Void。两个字符串 owner 由 caller 先
CopyRef 后交给 helper，helper 在 callback 后直接替换这两个局部 owner；返回前 caller
再销毁它们。这不是两个 `const ttstr&` 借用。

两份 ARMv7 目标把 sret 地址作为显式首参，随后是 Player 与两个按值字符串 storage。
IDA 恢复库现分别使用对应的显式 ABI prototype，不把四种调用约定强行伪装成一个机器
签名；portable C++ 源级签名统一为：

```cpp
tTJSVariant Player::loadMotionResult_guess(ttstr chara, ttstr motion);
```

## 共同数据流

四端共同的源级控制流可以归一化为：

```text
result = Void                                      // 唯一返回槽

if currentDispatch.Object != null:
    requestRaw = Dictionary()                      // raw factory owner
    requestRaw.chara  = chara                      // MEMBERENSURE + static hint
    requestRaw.motion = motion                     // MEMBERENSURE + static hint

    retain currentDispatch
    request = Object/ObjThis closure(requestRaw)   // two retained refs
    currentDispatch.onFindMotion(request, static-hint,
                                 result=&result)
    ignore callback status
    destroy callback argument closure

    responseCopy = CopyRef(result)
    response = responseCopy.AsObject()             // AddRef or throw
    destroy responseCopy

    charaValue = Void
    status = response.PropGet(MEMBERMUSTEXIST,
                              "chara", hint=null,
                              result=&charaValue,
                              objthis=response)
    if status failed:
        adjustedChara = empty ttstr
    else:
        adjustedChara = ttstr(charaValue)
    chara = adjustedChara                          // replace by-value owner

    motionValue = Void
    status = response.PropGet(MEMBERMUSTEXIST,
                              "motion", hint=null,
                              result=&motionValue,
                              objthis=response)
    if status failed:
        adjustedMotion = empty ttstr
    else:
        adjustedMotion = ttstr(motionValue)
    motion = adjustedMotion                        // replace by-value owner

    release response
    release currentDispatch
    release request

rmCopy = CopyRef(player.resourceManager)
rm = rmCopy.AsObject()                             // AddRef or throw
destroy rmCopy

path = Variant("motion/" + chara + "/" + motion)
contextArg = CopyRef(player.findMotionContext)
pathArg = CopyRef(path)
args = { &contextArg, &pathArg }

rm.findMotion(args, result=&result)                // same result slot
ignore findMotion status

destroy pathArg
destroy contextArg
destroy path
release rm
return result
```

这里的 `result=&result` 复用是本轮最重要的修正。它意味着 `findMotion` 返回失败且不写
output 时，并不存在额外的“失败即 Void”逻辑：

- 没有 current dispatch 时，槽从入口开始一直是 Void，因此返回 Void；
- 有 current dispatch 且 `onFindMotion` 写入对象时，该对象仍留在槽内，因此原样返回
  callback response；
- `findMotion` 即使返回失败，只要写了 output，新的 output 仍然生效；
- 两个状态码本身都不会清空槽、选择 fallback 或触发返回分支。

## dispatch owner 与清理顺序

### `onFindMotion` 阶段

四端都先从当前 raw dispatch 字段取得 Object，再 AddRef。创建的 Dictionary 本身也有
独立 Variant owner；作为 callback 参数构造的 closure 又持有 Dictionary dispatch。
callback 返回后先销毁参数 closure，再从共享 result 槽 CopyRef 并强制做 owning Object
转换。属性读取完成后的正常清理顺序是：

1. callback result Object；
2. current dispatch；
3. request Dictionary。

因此，属性 getter 重入、清除外部 owner 或 callback 返回失败都不能让这三个调用中对象
在其原生作用域内提前销毁。非对象 callback output 在 owning Object conversion 处抛出，
而不是静默跳过 callback 调整。

### `findMotion` 阶段

四端都执行 `CopyRef(resourceManager Variant) -> AsObject() -> destroy Variant copy`，只留下
一份独立 dispatch owner跨越路径构造、两个参数 CopyRef 和 `findMotion`。这与直接使用
`AsObjectNoAddRef()` 借用 canonical field 不同。非对象 ResourceManager 在 conversion
处抛出；null Object 后续仍是原生的不安全边界，没有本地 `return Void` 保护。

参数 0 和参数 1 都是独立 Variant storage：参数 0 CopyRef 持久 context 字段，参数 1
CopyRef 局部 path Variant。与 `Player::isExistMotion` 的参数 0 直接别名持久字段不同，
`findMotion` 对参数 Variant 的原地改写不会直接改 Player 的 context 字段。

## callback 属性失败边界

Android arm64 的两个直接 PropGet 在 `0x6AE4F4`、`0x6AE5C0` 明确传 flags `0x400`、
hint `0`；其他三端通过同语义 property helper 传入空字符串 default，恢复到 vtable
调用后同样是 required PropGet。每次读取先准备一个独立空字符串 default：

- getter 成功：把 getter output 交给普通 Variant-to-ttstr 转换；
- getter 失败：复制空 default，不转换 getter output；
- 因而“失败但写入 `must-be-ignored`”的恶意 dispatch 最终仍产生空 chara；
- `motion` 读取发生在 chara owner 已经被替换之后；
- 两个读取都不回退到 caller 请求的旧字符串。

`findMotion` 使用自己的静态成员 hint；callback 的两个属性读取传空 hint。portable
实现现在保持了这个区别。

## Player 字段映射

偏移只记录在分析文档中，不进入可编译源码注释：

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| current dispatch closure Object | 经 Player 当前 bridge owner | 经 Player 当前 bridge owner | 经 Player 当前 bridge owner | 经 Player 当前 bridge owner |
| ResourceManager Variant | `+992` | `+684` | `+880` | `+620` |
| context Variant | `+1012` | `+696` | `+900` | `+632` |

`playImpl` 和 `initEmoteMotion` 仍是四端各自仅有的两个直接 load caller；本轮不改变该
调用集，只恢复 helper 内部 owner、result slot 与异常边界。

## Portable 源码修正

- `Player.h`
  - `loadMotionResult_guess` 两个字符串改为按值；
  - 增加未注册的 test-only entry，用于注入 current dispatch 并直接观察完整返回 Variant。
- `PlayerCore.cpp`
  - 一个 result Variant 同时传给 `onFindMotion` 与 `findMotion`；
  - 删除两个 FuncCall 状态码分支；
  - current dispatch、callback result 和 ResourceManager 恢复独立 retain/release；
  - callback result 和 ResourceManager 恢复强制 Object conversion；
  - `onFindMotion` 恢复非空静态成员 hint；callback 参数 closure 在调用后立即清理，
    Dictionary 的独立 raw owner 保持到属性消费结束；
  - request 的两个 `MEMBERENSURE` 属性写入恢复各自的静态 member hint；
  - callback property flags 恢复为 `TJS_MEMBERMUSTEXIST`，失败恢复空字符串；
  - context 与 path 恢复独立参数 Variant CopyRef；
  - 路径直接使用 `ttstr` 拼接，避免 Unicode 名经过窄字符串往返。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 新增 callback/result/RM 三个可观测 dispatch；
  - callback 返回 `TJS_E_FAIL` 但写 response Object；
  - `chara` getter 返回 `TJS_E_FAIL` 但写入错误字符串，验证最终路径为空 chara；
  - `motion` getter 成功写入 `adjusted`；
  - `findMotion` 返回 `TJS_E_FAIL` 且不写 output，验证 helper 返回同一个 response Object；
  - 同时验证 required flags、context CopyRef、最终路径和非对象 RM 抛异常。

## IDB 改进

四份 recovery IDB 均完成并保存：

- `Player_loadMotion_guess` 函数级注释补充共享 sret、两个状态码忽略、by-value ttstr、
  empty default、owner 与清理顺序；
- 两份 AArch64 prototype 标出 `X0/X1/X2/X8`；
- 两份 ARMv7 prototype 标出显式 sret、Player 与两个字符串 storage；
- 应用类型后重新导出 prototype，四份均保留 `tTJSVariant_guess` / `ttstr_guess` 类型；
- 四库原位保存均返回 `ok=true`。

## 验证

- 完整 Web Debug 构建成功，重新编译 `PlayerCore.cpp`、链接 `libmotionplayer.a`，并完成
  `index.html` / Wasm 最终链接；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  defines、includes 与 ABI 参数执行 `-fsyntax-only` 成功；唯一诊断为仓库既有 `_tss`
  literal-operator 弃用警告；
- 当前 Web preset 不构建原生 Catch executable，因此不把新增差分 case 的编译成功
  误报为运行时执行。
