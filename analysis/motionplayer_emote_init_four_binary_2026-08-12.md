# MotionPlayer emote 二次选片 / load-result 生命周期四端复原（2026-08-12）

## 结论

本纵向重新以 `reference/binaries/` 的四份当前参考二进制为共同证据，独立复核了
emote wrapper motion 在角度变化后选择 secondary motion 的完整路径。旧源码名
`initEmoteMotionLike_0x6B2E90` 中的地址已经失效：Android arm64 的真实入口不是
`0x6B2E90`，而是 `0x6B0270`；旧地址落在当前 `Player_resetAndReleaseOldNodeTree_guess`
函数范围内。其余三个目标的同一数值也不代表此 helper。因此 portable 源码改为纯语义
名 `Player::initEmoteMotion_guess`，地址只保留在本文。

本轮确认了两个此前没有被完整表达的可观察行为：

1. secondary load 失败调用的是普通、非 important 的 `TVPAddLog`，不是抛异常；
2. `findMotion` 返回的完整 Variant/dispatch owner 在字段 copy-assign 和
   `initNonEmoteMotion` 返回之前一直存活。原来的 `bool ensureMotionLoaded(...)`
   会在进入 initializer 前销毁返回容器，生命周期过短。

portable 源码现已拆出 `loadMotionResult_guess`：它只执行 callback/资源查找并返回完整
Variant；`playImpl` 和 emote 二次选片各自在自己的原生作用域内持有返回 owner、按
0/1 顺序提交字段，然后才进入后续分流。随后完成的全调用者审计证明四端 load helper
都只有这两个调用语义；旧 `ensureMotionLoaded` 与所有非播放入口调用均已删除。完整
证据见 `motionplayer_load_callers_four_binary_2026-08-12.md`。

2026-08-14 的 helper 内部纵向又确认：`onFindMotion` 与 `findMotion` 复用同一个隐藏
sret/result Variant，两个状态码都被忽略；callback result 与 ResourceManager 都通过
owning Object conversion 持有。因而 `findMotion` 失败且不写 output 时，已有 callback
response 仍会被 helper 返回，而不是被改成 Void。完整 owner、属性失败与异常边界见
`motionplayer_player_load_motion_dispatch_four_binary_2026-08-14.md`。

## 四端入口、大小与 ABI

| 目标 | emote init | 大小 | load helper | ordinary init | `TVPAddLog` 单参数 wrapper |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6B0270` | `0x7CC` | `0x6AE2F0` | `0x6B0A3C` | `0xA16CA4` |
| Android armv7 | `0x5807E0` | `0x2FE` | `0x57F654` | `0x580C28` | `0x76483A` |
| iOS arm64 | `0x100107D38` | `0x3A8` | `0x1001067BC` | `0x100108258` | `0x1002591D4` |
| iOS armv7 | `0x105350` | `0x3A4` | `0x103BBC` | `0x1058F8` | `0x25A52E` |

四个入口均设置为：

```c
void Player_initEmoteMotion_guess(void *self, unsigned int playFlags);
```

函数只把 `playFlags` 的低字节继续传给 ordinary init；这解释了优化后伪代码中的
`char` 临时量，但 caller 仍以普通整数 flag ABI 传参。四个 `TVPAddLog` wrapper 均设置为：

```c
void TVPAddLog_guess(const void *line);
```

wrapper 的下一层共同以第二参数 0 进入日志系统。Android armv7 的深层函数还能直接
看到 `TVPConsoleLog(line, false)`；两份 iOS 与 Android arm64 的时间戳、日志 deque、
console 输出和 important=false 数据流与其一致。

## 直接调用图

### Android arm64

| call site | caller |
| ---: | --- |
| `0x6AFAA4` | `Player_playImpl_guess` |
| `0x6BBE84`, `0x6BBEC8` | `Player_updateMotionSubNodes_guess` |
| `0x6BC9A0`, `0x6BD48C` | `Player_updateParticleSystems_guess` |
| `0x6BE3B4` | `Player_setAngleDeg_guess` |
| `0x6BE484` | `Player_frameProgress_guess` |
| `0x6BEDA0` | `Player_stepParticleChildren_guess` |
| `0x6CA528` | `Player_setAngleRad_guess` |

### Android armv7

| call site | caller |
| ---: | --- |
| `0x58038C` | `Player_playImpl_guess` |
| `0x588578` | `Player_updateMotionSubNodes_guess` |
| `0x58A58C` | `Player_setAngleDeg_guess` |
| `0x58A66A` | `Player_frameProgress_guess` |
| `0x58ACB4` | `Player_stepParticleChildren_guess` |

### iOS arm64

| call site | caller |
| ---: | --- |
| `0x100107804` | `Player_playImpl_guess` |
| `0x100111888` | `Player_updateMotionSubNodes_guess` |
| `0x100113A18` | `Player_setAngleDeg_guess` |
| `0x100113B84` | `Player_frameProgress_guess` |
| `0x1001142A8` | `Player_stepParticleChildren_guess` |

### iOS armv7

| call site | caller |
| ---: | --- |
| `0x104E06` | `Player_playImpl_guess` |
| `0x10EC04` | `Player_updateMotionSubNodes_guess` |
| `0x111428` | `Player_setAngleDeg_guess` |
| `0x111584` | `Player_frameProgress_guess` |
| `0x111CC8` | `Player_stepParticleChildren_guess` |

Android arm64 保留 9 个直接 xref，其余三体各 5 个。这是当前产物真实的 outlining/
inlining 形态，portable 源码只统一语义 helper，不把直接 xref 数量伪造成一致。

## Player 字段角色的四端交叉映射

这些偏移只用于证明字段角色，不进入编译源码注释。

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| retained emote angle | `+464` | `+296` | `+352` | `+236` |
| camera angle | `+472` | `+304` | `+360` | `+244` |
| division Variant | `+484` | `+316` | `+372` | `+256` |
| prior selected index | `+504` | `+328` | `+392` | `+268` |
| motionList Variant | `+508` | `+332` | `+396` | `+272` |
| active motion-content Variant | `+528` | `+344` | `+416` | `+284` |
| live stealth-chara ttstr | `+968` | `+672` | `+856` | `+608` |
| matched-key/context Variant | `+1012` | `+696` | `+900` | `+632` |

64 位 split 结果的 `ttstr` element 是 8 字节 owner，第三项位于 begin+16；32 位是
4 字节 owner，第三项位于 begin+8。四体都直接读取第三项，不先比较 vector size。

## 共同源码级控制流

```text
angle = cameraAngle + retainedEmoteAngle
while angle < 0.0:
  angle += 360.0
while angle >= 360.0:
  angle -= 360.0

divisionOwner = owning closure copy(divisionVariant)
count = divisionOwner.count
index = 1
if count >= 2:
  do:
    previous = divisionOwner[index - 1]
    if previous < angle:
      current = divisionOwner[index]
      if current >= angle:
        break
    ++index
  while index < count

selected = index % count
if selected == priorSelected:
  release divisionOwner
  return
priorSelected = selected

motionListOwner = owning closure copy(motionListVariant)
pathOwner = ttstr(motionListOwner[selected])
parts = split(pathOwner, "/")
secondaryOwner = owning copy(parts[2])

loadResult = loadMotion(copy(liveStealthChara), copy(secondaryOwner))
release two load arguments

if loadResult.Type != Void:
  loadDispatchOwner = closure(loadResult)
  temp0 = loadDispatchOwner[0]
  motionContent.copyAssign(temp0)
  destroy temp0
  temp1 = loadDispatchOwner[1]
  matchedContext.copyAssign(temp1)
  destroy temp1
  initNonEmoteMotion(lowByte(playFlags))
  release loadDispatchOwner
else:
  message = "motion not found " + liveStealthChara + "/" + secondaryOwner
  TVPAddLog(message)                 // important=false
  destroy message temporaries
  motionContent.clear()
  matchedContext.clear()

destroy loadResult
destroy secondaryOwner
destroy parts elements, then vector storage
destroy pathOwner
release motionListOwner
release divisionOwner
```

关键的 owner 边界来自四体共同次序：成功分支先调用 ordinary init，随后才 Release
由 `loadResult` 构造的 closure，并在共同尾部销毁 `loadResult` Variant。本地旧实现让
`ensureMotionLoaded` 在返回 bool 之前销毁完整结果容器，虽然元素已经各自 AddRef，仍然
改变了容器 dispatch 的 Release 时点和潜在析构副作用。

primary play 路径使用同一个 load helper 和相同的返回 Variant 结构。为避免修好 emote
却保留 primary 的同类差异，`playMotionImpl_guess` 也改为在其完整分流期间持有
`loadResult`。新增测试用一个 result-array dispatch 析构哨兵证明：读取 motion `type`
属性时完整 result owner 仍存活，play 返回后才析构。

## 精确边界行为

### 角度归一化

- 使用两个显式 `while`，不是 `fmod`；
- `-0.0` 不进入任一循环，并保留其比较行为；
- NaN 不进入循环，随后所有有序区间比较都为 false；
- 正负 infinity 会在对应循环中永不收敛；
- 不加入有限值检查、迭代上限或一次性模运算。

### division 选择

- 区间严格为 `(previous, current]`：下界排除，上界包含；
- 先读 previous；只有 `previous < angle` 才读 current，property getter 顺序可观察；
- count 小于 2 时不读任何 numeric element，仍计算 `1 % count`；
- count 为 0 时没有 guard；portable C++ 保留原始模零边界；
- 重复、逆序或 NaN division 值没有校验；
- 未匹配到区间时 index 到达 count，随后 `count % count == 0`；
- selected 与 prior 相等会在 motionList property read、split、load、log 和字段 clear
  之前立即返回。

### motionList / split

- `motionList[selected]` 先走普通 Variant-to-ttstr 转换；
- delimiter 是单个 `/`；
- 无 path 结构校验，无 vector size guard，直接复制 `parts[2]`；
- split vector、第三项 owner 和原始 path 在 load/ordinary-init 期间仍存活；
- 不恢复旧 port 曾加入的空字符串、两段路径或 fallback 行为。

### load 结果与失败

- load helper 仍通过当前 NCB dispatch 的 `onFindMotion` 调整查找局部副本；
- 两个字符串是 helper 的按值 owner；callback 回写不会修改 caller 保存的原始 label；
- callback 和 `findMotion` 共用一个 result 槽，两个 FuncCall 状态码都被忽略；
- required `chara` / `motion` 属性读取失败时使用空字符串，即使 getter 失败前写了 output；
- callback result 与 ResourceManager 都被强制转为并独立持有 Object，非对象值会抛出；
- Player 的 motion label 不改为 callback-adjusted 值；
- success gate 是 `Type != Void`，不是 `Type == Object`；非 Void/非 object 值仍会进入
  numeric property access，不被本地静默转为加载失败；
- element 读取顺序固定为 0 后 1；每个临时 Variant 在下一步前销毁；
- ordinary init 发生在完整 load-result owner 与 split/path owners 的作用域内；
- failure 日志使用原始 live stealth-chara 与 `parts[2]`，不是 callback 回写值；
- 日志文本精确拼接为 `motion not found <chara>/<secondary>`；
- 日志在两个 Player Variant clear 之前；clear 顺序为 motion-content 后 context；
- failure 不在本 helper 内清 playing byte，也不回滚 prior selected index；
- `TVPAddLog` 不抛异常，也不写 important log。

## 与修改前 portable 源码的差异

| 项目 | 修改前 | 四端共同语义 / 修改后 |
| --- | --- | --- |
| helper 名 | `initEmoteMotionLike_0x6B2E90` | `initEmoteMotion_guess`，无过时地址 |
| load helper 返回 | `bool`，内部提交 0/1 字段 | 返回完整 Variant，由 caller 提交 |
| result owner 生命周期 | initializer 前析构 | initializer 返回后析构 |
| success gate | `result.Type() == tvtObject` | `result.Type() != tvtVoid` |
| failure side effect | 只 clear 两字段 | 先 `TVPAddLog`，再按序 clear |
| primary play 生命周期 | `ensureMotionLoaded` 返回前析构结果 | 完整 type/emote/ordinary 分流期间持有结果 |
| 边界 guard | count/split 无 guard | 保持无 guard |

## Portable 源码与测试改动

- `Player.h`
  - 声明 `loadMotionResult_guess`；
  - `initEmoteMotion_guess` / `initNonEmoteMotion_guess` 使用语义名。
- `PlayerCore.cpp`
  - load callback/findMotion 与字段提交分层；
  - emote helper持有完整结果到 ordinary init 之后；
  - 恢复精确 `TVPAddLog` 失败副作用；
  - 保留非 Void、模零、split 第三项等危险边界。
- `PlayerTimeline.cpp`
  - primary play 持有完整 load result 覆盖 type 分流和 initializer 生命周期。
- `PlayerFrameProgress.cpp`、`PlayerUpdateChildMotion.cpp`、
  `PlayerUpdateParticles.cpp`
  - caller 统一改用新的语义名。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 新增 result-array dispatch 析构哨兵；
  - 验证 `type` property 读取时 result 仍存活、play 返回后才释放；
  - 同时锁定传给 ResourceManager 的路径 `motion/hero/idle`。

## IDB 改进

四份 IDB 已完成：

- emote helper 统一名称与 `void(void *, unsigned int)` 类型；
- 四个普通日志 wrapper 统一为 `TVPAddLog_guess` 并设置单参数类型；
- emote helper 入口注释记录选择区间、无 guard、result owner、日志与双 clear；
- 日志 wrapper 注释记录 important=false；
- 名称/类型应用后再次 fresh decompile，四体均显示：
  - `Player_initEmoteMotion_guess`；
  - `TVPAddLog_guess`；
  - `Player_initNonEmoteMotion_guess` 在 load-result closure 释放之前；
- 四份数据库原位保存均返回 `ok=true`。

## 验证状态

- `motionplayer` Web Debug 目标完成重编译与静态库链接；完整 Web Debug 也完成最终
  `index.html/index.wasm` 链接，随后增量复核为 `ninja: no work to do`；
- Wasmtime guest 完成所有受影响对象重编译、最终链接和 exnref 转换，输出文件尺寸与
  时间戳更新；随后增量复核为 `ninja: no work to do`；
- 完整 Catch2 翻译单元用 Web Debug 的真实 Emscripten 定义/头路径执行
  `-fsyntax-only` 通过，仅有仓库既有 `_tss` deprecation warning；
- stale helper 名称扫描无编译源码命中；
- `git diff --check` 通过，仅报告工作区既有 LF/CRLF 转换提示；
- 当前工作区没有可直接运行的原生 Catch executable，因此不把新增 Catch case 误报为
  已执行；现阶段已验证其完整翻译单元可编译。

## 后续状态迁移

`initEmoteMotion` 已不再是 MotionSub 链上的未审计黑盒。`Player_loadMotion_guess` caller
审计也已独立封账：四端均只有 primary play 与 emote 二次选片两个调用语义。

本文件原先把 `Player_evaluateTimeline_guess` 的 type-specific 输出列为下一纵向；该项已在
`motionplayer_evaluate_timeline_four_binary_2026-08-13.md` 闭合，并于 2026-08-15 再次 fresh
复核 type 4/5/10 的 gate、copy/lerp 和物理输出范围。随后复查的 ordinary initializer
字段/容器提交顺序与异常边界已由
`motionplayer_init_non_emote_four_binary_2026-08-12.md` 闭合；其后的 node-frame parser、
reset、merge、absolute initializer、异常保留和 slot/deque 生命周期则已由
`motionplayer_node_timeline_slot_helpers_four_binary_2026-08-14.md` 闭合。因此这里不再保留
“node-frame 剩余属性顺序”的陈旧待办。
