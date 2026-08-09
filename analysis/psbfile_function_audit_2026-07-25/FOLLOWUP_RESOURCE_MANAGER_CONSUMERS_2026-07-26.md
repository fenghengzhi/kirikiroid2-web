# Follow-up：ResourceManager consumer 与共享 `ttstr` 哈希闭环

日期：`2026-07-26`。本文件记录 psbfile 审计沿真实 motionplayer consumer 继续复核时
闭合的三组确定差异：`ResourceManager::findSource/isExistMotion/findMotion` 的 split
分隔符构造、两条 motion 直查路径的 nullable project-key 数据流，以及这些入口与
Player/Emote/ResourceManager 内部容器共用的 `ttstr` unordered-container 哈希。

这些函数和容器实例都在 psbfile 114-address MANIFEST 之外；它们只作为跨模块 consumer
follow-up，不增加逐函数报告，也不改变 `114 = 99 ALIGNED + 15 EVIDENCE_LIMITED` 的统计。

## fresh Android 正证据

本轮主审计 fresh 反编译了 `ResourceManager_isExistMotion@0x6A96F8`、
`ResourceManager_findMotion@0x6A9ED4` 与 `ResourceManager_findSource@0x6AAB3C`：

- 三个入口传给 `splitTtstrLike_0x697D34` 的 `/`，以及 `findSource` blank 分支传入的
  `:`，都是 UTF-16 字面量构造的 `ttstr`，不是 `tjs_char` 单字符 overload；
- `isExistMotion/findMotion` 仅在 `projectKey` 非 Void 时进入直查，并严格按 String
  取出 borrowed `tTJSVariantString *`；该 raw 指针本身仍可能为 null；
- raw string 指针经 `ttstr_c_str`（null 输入仍为 null）后深拷贝成独立临时 `ttstr`，
  再用于 unordered-map 查找；临时 key 随 full-expression 清理。没有 caller 预先把
  null 改写为空字面量，也没有对 raw string 做额外 AddRef；
- `findSource` 的 map key 已经是入参 `ttstr moduleKey`，直接进入同一外层 map 查找，
  不经过 project-key Variant 转换。

为避免只在一个调用点看见相同算术就误判共享 hasher 已闭合，本轮还 fresh 复核了全部
本地共享 `motion::detail::ttstr_hash` 别名所对应的 Android unordered-container 路径：

| 容器族 | Android 正证据地址 |
| --- | --- |
| ResourceManager 外层查找 / consumer | `0x6A96F8`、`0x6A9ED4`、`0x6AAB3C` |
| Player HM1 | `0x6F52AC` |
| Player HM2/HM4 与 Emote double-map 共用实例 | `0x686944` |
| Player HM3 | `0x6F2674` |
| Emote sets | `0x689760` |
| Emote 非 double map | `0x6885CC` |
| ResourceManager record 的四个 inner-map 实例 | `0x6E2060`、`0x6E2150`、`0x6E2484`、`0x6E2574` |

各实例都证明同一完整 key policy，而不只是相同的 Jenkins 算术：

1. `ttstr.Ptr == null` 时直接返回 hash `0`，且不读取 Hint；
2. Ptr 非 null 时先读 backing string rep 的 `Hint@+68`，非零则直接复用；
3. Hint 为零时按 UTF-16 code unit 执行 `1025 / 9 / 32769` 混合；
4. 计算结果为零时改成 `0xFFFFFFFF`，再写回 `Hint@+68` 并返回。

因此 null-backed `ttstr` 的 hash 是 `0`；非 null 的空 payload 会走算术并得到非零
sentinel `0xFFFFFFFF`。两者不能在 functor 边界合并。raw C-string helper 的 null 输入
仍返回 `0xFFFFFFFF`，因为只有 `ttstr` functor 能看见 backing Ptr 是否存在。

## Android 关键伪代码（10 行）

```text
parts = split(path, ttstr(L"/"))
if blank-path: dims = split(parts[1], ttstr(L":"))
if projectKey is Void: skip direct lookup
else require String; raw = projectKey.AsStringNoAddRef()
key = ttstr(raw ? ttstr_c_str(raw) : null); direct = map.find(key)
hash(ttstr key): if key.Ptr == null return 0
hint = key.Ptr->Hint@+68; if hint != 0 return hint
h = JenkinsUTF16(key.Ptr, 1025, 9, 32769)
if h == 0: h = 0xFFFFFFFF
key.Ptr->Hint = h; return h
```

## 本地逐行对照与闭环

| 伪代码行 | 当前本地复刻 |
| --- | --- |
| 1 | `cpp/plugins/motionplayer/ResourceManager.cpp:439-440,580-581,632-633` 使用 `TJS_W("/")`，隐式构造临时 `ttstr` 并选择 `splitTtstrLike_0x697D34(ttstr, const ttstr &)`。 |
| 2 | `ResourceManager.cpp:454-455` 对 blank 参数使用 `TJS_W(":")`，同样不走 `tjs_char` overload。 |
| 3-4 | `ResourceManager.cpp:590-592,641-643` 先保留 Void gate，再用 `AsStringNoAddRef()` 取得 borrowed raw pointer；未增加引用计数或安全替代对象。 |
| 5 | `ResourceManager.cpp:593-597,644-648` 用 nullable raw UTF-16 指针构造 full-expression 临时 `ttstr` 后直接 `_loadedModules.find`；`findSource` 则在 `:476-480` 直接使用其 `ttstr moduleKey`。 |
| 6-7 | `cpp/plugins/motionplayer/internal/ttstr_hash.h:50-60` 先通过 `GetHint()` 区分 null backing，再复用非零 Hint。 |
| 8-10 | `ttstr_hash.h:27-45,62-65` 保留原 UTF-16 混合和 computed-zero sentinel，并把结果写回 string rep 的 Hint。 |

共享 functor 由 `ResourceManager.h`、`internal/player_containers.h` 与 `EmoteEngine.h` 中的
unordered map/set 声明共同使用；上表各 Android 实例均给出了独立正证据，因此这次共享
实现修改不是从某一个容器外推其他容器。

## 六维影响

| 维度 | 闭环结论 |
| --- | --- |
| 源代码结构 | 恢复 UTF-16 `ttstr` separator overload、project raw-string → 临时 `ttstr` 表达式，以及完整而非仅算术部分的共享 hasher。 |
| 数据流 | borrowed nullable project string 不再被 caller 归一化；hash 数据流严格为 Ptr gate → Hint gate → UTF-16 mix → sentinel → Hint write-back。 |
| 调用链 | 三个 ResourceManager consumer 仍经同一 split helper；直查仍由 `_loadedModules.find` 触发同一 hasher，fallback/raw-node 导航未被折叠或改写。 |
| 对象生命周期 | project Variant 不额外 AddRef；直查 key 是独立深拷贝临时量并按 full-expression 析构；Hint 写回发生在共享 backing string rep 上。 |
| 内部容器实现 | ResourceManager、Player 和 Emote 的相关 unordered map/set 继续共用一个 `ttstr_hash`，现已恢复 Android 的 null bucket hash、Hint cache 与 bucket-distribution 输入。 |
| 边界行为 | Void 跳过直查；非 Void 严格要求 String；null-backed key→0，非 null 空 payload/任何 computed-zero→`0xFFFFFFFF`，两条边界不再混同。 |

## 当前验证边界

本轮当前源码上的原生验证结果为：

- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- `motionplayer-dll`：`1386 assertions in 21 test cases`；
- `psbfile-dll`：`577 assertions in 10 test cases`。

这些结果覆盖共享 functor 的 null/Hint/sentinel 单元边界，并对完整 motionplayer 与 psbfile
原生套件作非回归守护。它们不能代替 Android 反编译证据证明源码 factorization。

最终当前源码的 Web Debug 已重编、链接通过；最后一处 `cpp/` 更新后也重新构建了
Wasmtime guest。m2logo/yuzulogo 完整捕获 25/63 帧，trace SHA-256 与汇总记录一致；
structural comparator 仍有既有的 31/21 个 opacity ±1 mismatch，故退出码为 1。这里仅把
构建与 trace 稳定性作为非回归证据，不把既有 drift 写成 oracle 全绿，也不以测试替代
Android 反编译证据。完整命令、产物与数值结果见 [SUMMARY.md](SUMMARY.md)。
