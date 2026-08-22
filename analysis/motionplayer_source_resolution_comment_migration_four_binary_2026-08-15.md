# MotionPlayer source-resolution / SourceCache 注释迁移（四参考，2026-08-15）

## 1. 本轮结论

`Player.h` 原有两组注释把 Android arm64 的绝对地址与接收者角色写进了 portable
declaration，并把本地 `friend class SourceCache` 错解释成 SourceCache 持有 Player
back-pointer。对四个 `reference/binaries/` recovery IDB fresh 反编译后，正确边界是：

1. 外层 Player timeline caller 已经选出 active clip slot，并把其中独立的 `src`、
   `icon` 字符串传给 source resolver；resolver 本身接收 `SourceState*`、
   `ResourceManager*`、`src`、`icon`，不回读 Player slot；
2. KRKR atlas helper 接收并原地更新同一 `SourceState`，ResourceManager/module map
   持有缓存与 texture owners，节点只保留 texture borrow；
3. 该 atlas helper 恰有两个 native caller：find-source 与 render-time D3D texture getter；
4. native SourceCache 构造器只建立三个 Variant owner、cache counters 与 list，完整对象
   没有 Player pointer；本地 friendship 只服务一次借用 `Player&` 的 Web helper。

因此本轮只迁移注释和证据，不改变 source-resolution 执行代码。

> 2026-08-16 补充：上句只描述 2026-08-15 当轮的工作范围，不能再作为“执行代码
> 已与四端一致”的结论。随后对 resolver 尾部 generic fallback 的逐指令复核发现，本地
> 直接调用 native `ResourceManager::findSource`、提前把 context 转成 `ttstr`、以及
> Object/null 的友好过滤都偏离参考实现；执行边界已经按
> `motionplayer_source_fallback_dispatch_owner_failure_four_binary_2026-08-16.md` 修正。

## 2. fresh 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNode_findSource_guess` | `0x691CC8` | `0x570500` | `0x1000F316C` | `0xEF97C` |
| `Player_loadKrkrAtlasSource_guess` | `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| `SourceCache_ctor_guess` | `0x6A4CD4` | `0x57AADC` | `0x10010071C` | `0xFD824` |
| `SourceCache_getBufLayer_guess` | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |
| `ResourceManager_findSource_guess` | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| render-time atlas caller | `0x6EE440` | `0x5AC518` | `0x10014019C` | `0x1414C0` |

四端函数大小差异很大，主要来自 Android libstdc++ 与 iOS libc++ map/vector/string
实现以及 inlining；入口参数角色、分支顺序和 owner 边界一致。

## 3. resolver 的实际接收者与 caller 链

四份 decompile 的 resolver 都可归纳为下列源码形状：

```cpp
void findSource(SourceState *destination,
                ResourceManager *resourceManager,
                const ttstr &src,
                const ttstr &icon);
```

第一个参数在四端都直接承载 `valid/blank/object/texture/origin/size/clip/rect/path`
写入；它不是 Player this，也不是 MotionNode base。第三、第四参数在 caller 形成时已经
来自 selected clip slot。外层本地接口 `Player::findSourceForNode_guess(node)` 才负责
`node.activeSlot().srcValue/iconValue` 的选择，再把 `node.source` 交给 native-shaped
resolver。

fresh xref 显示 resolver 在每个目标都恰有五类 timeline caller：

| caller 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize node slots | `0x6B3B14` | `0x582968` | `0x10010A764` | `0x1080CC` |
| forward streams | `0x6B4718` | `0x583208` | `0x10010B278` | `0x108AE6` |
| parameterized seek | `0x6B5418` | `0x5839CA` | `0x10010BBD0` | `0x109568` |
| Join restore/prune | `0x6B5980` | `0x583C10` | `0x10010C364` | `0x109D32` |
| reverse streams | `0x6B76F0` | `0x584D60` | `0x10010D8B4` | `0x10B142` |

这解释了 `SourceState` 为什么必须是 node-persistent 状态：absolute seed、双向增量、
parameter seek 与 Join restore 都会复用/重写同一对象，而不是每次建立临时 descriptor。

## 4. KRKR atlas helper 的共享边界

四端 atlas helper 的两个且仅两个代码 xref 为：

| 目标 | find-source call | render-time texture call |
|---|---:|---:|
| Android arm64 | `0x691FD0` | `0x6EE528` |
| Android armv7 | `0x570668` | `0x5AC598` |
| iOS arm64 | `0x1000F335C` | `0x100140244` |
| iOS armv7 | `0xEFB9C` | `0x141594` |

helper 首先把 SourceState path 按 `/` 分段并要求第一段为 `src`；prefix 不匹配时在
module lookup 前返回 false。进入 KRKR path 后，它从 ResourceManager loaded-module map
查 module key，再从 module-owned packed-source map 查完整 path。miss 时按 group/image
分段构造 atlas page，随后再次查 packed entry。成功时把 packed entry 的 origin、size、
clip、texture rect 与 texture borrow复制到 SourceState，并返回 true。

texture 的 owning reference 位于 ResourceManager/module atlas entry；SourceState 只保存
raw texture pointer。find-source 和 render getter 共享 helper，因而不存在一份隐藏的
Player atlas cache，也不能把 helper 注释成“Player-owned texture resolver”。

## 5. SourceCache 对象没有 Player back-pointer

四份 native constructor 的共同源码顺序为：

```text
owner Variant = CopyRef(constructor owner)
primaryLayer Variant = strict owner.primaryLayer get
bufLayer Variant = global Layer(owner, primaryLayer) result
currentCacheBytes = 0
cacheLimitBytes = int32 constructor argument bit pattern
entries list = empty sentinel
```

SourceCache 的三份 Variant 物理布局为：

| 字段 | Android/iOS 64-bit | Android/iOS 32-bit |
|---|---:|---:|
| owner Variant | `+0` | `+0` |
| primaryLayer Variant | `+20` | `+12` |
| bufLayer Variant | `+40` | `+24` |
| current-cache bytes | `+60` | `+36` |
| cache limit | `+64` | `+40` |
| list begins | `+72` | `+44` |

Android 旧 libstdc++ list 与 iOS libc++ list 的 sentinel/size tail 不完全相同，但三份
Variant 后紧接两个 uint32 与 list；四端任何位置都没有额外 Player pointer。constructor
参数第二项是 Variant owner closure，第三项是 cache size，也没有 Player 参数。

本地 `SourceCache::loadRenderSourceLayerFromItem_guess(Player&, item)` 是 Web render
adapter。它在一次调用期间借用 Player，临时把 Player 的 persistent motion-context
Variant 转成 `ttstr`，并通过 `player.nativeRM()` 调共享 atlas helper。`friend class
SourceCache` 只是允许这段借用路径访问 private members；SourceCache 实例不会保存该
引用，更不会控制 Player 生命周期。旧注释中的“back-pointer”与 Android arm64
`Player+528/+1012` 都已从 compiled header 删除。

## 6. 源码迁移

`Player.h` 现在用纯语义说明：

- active slot 的选择发生在 Player 外层，resolver 接收已经选好的 strings；
- persistent SourceState 按引用更新，atlas texture 是 ResourceManager-owned borrow；
- atlas helper由 find-source 与 render-time getter 共享；
- SourceCache friendship只授权一次借用 Player 的 Web lookup，不代表 stored pointer。

绝对地址、caller 矩阵、对象布局与 STL 差异只保存在本文和四份 recovery IDB。
