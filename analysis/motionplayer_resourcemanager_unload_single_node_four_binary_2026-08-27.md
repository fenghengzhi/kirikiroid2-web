# ResourceManager::unload 单 module 节点摘除四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::unload` 的共同源码结构为：先执行
`path = TVPGetPlacedPath(path)`，再对 `_loadedModules` 做按 key 的单元素 erase。查找使用
`ttstr_hash` 缓存 hash 和 `ttstr_equal` 精确比较；miss 是无副作用 no-op，hit 才从 outer
unordered-map 链中摘除一个 node，并立即按 `KRKR map -> Win map -> PSBFile -> outer ttstr key ->
node` 的顺序释放该 module 的完整 owner 树。它不清 SourceCache、不修改 spec/random/layer-id
状态、不 shrink/rehash outer buckets，也不记录日志。

本地 normalization、container 类型和 `_loadedModules.erase(path)` 已与共同实现匹配；唯一偏差是
erase 前额外执行 `LOGGER->debug` 和 `path.AsStdString()`。四端 callback 的完整调用流均不存在日志
调用；跨 string-list、UTF-8/ASCII、UTF-16LE 和 UTF-32LE 搜索
`ResourceManager::unload` 也全部为零。本轮删除该额外可观察副作用。

## 2. 四端 callback 映射

| 平台 | callback | 完整指令 | normalize helper | find | erase |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6A697C` | 87 | `0x8EBC80` | `0x6E8CD4`，60条 | `0x6E930C`，68条 |
| Android armv7 | `0x57B6F8` | 47 | `0x6B85DC` | `0x5A7284`，31条；core `0x5A72CE`，34条 | `0x5A78B0`，18条；core `0x5A78E2`，53条 |
| iOS arm64 | `0x100101A28` | 35 | `0x1001937AC` | `0x100139AA8`，68条 | wrapper `0x100139FBC`，28条；unlink `0x10013A02C`，67条 |
| iOS armv7 | `0xFEC04` | 69 | `0x1930B0` | `0x139CEC`，72条 | wrapper `0x13A0FC`，28条；unlink `0x13A148`，101条 |

四个 callback 均 fresh decompile 并完成 full disassembly。四组 find/erase/unlink 主体也全部 fresh
decompile/full disassembly。iOS armv7 另有 `0xFECCA` 的12条 SjLj cleanup：normalize 临时
`ttstr` 尚未转移到入参时若抛出，只销毁该临时值后继续 unwind。

四个 normalize helper 也是同平台 `ResourceManager::load` 开头调用的完全相同目标：

| 平台 | load callsite | unload callsite | shared target |
|---|---:|---:|---:|
| Android arm64 | `0x6A61AC` | `0x6A69B0` | `0x8EBC80` |
| Android armv7 | `0x57B35A` | `0x57B70E` | `0x6B85DC` |
| iOS arm64 | `0x100101308` | `0x100101A48` | `0x1001937AC` |
| iOS armv7 | `0xFE432` | `0xFEC26` | `0x1930B0` |

callback 随后对 helper 返回的 `ttstr` holder AddRef，Release 旧入参 holder，把新 holder写回入参，
再销毁临时 holder；因此查找使用的是 normalization 后的路径，而不是同时保留原始和规范化 key。

## 3. 共同源码伪代码

```text
void ResourceManager::unload(ttstr path) const:
    path = TVPGetPlacedPath(path)
    loadedModules.erase(path)
```

标准库展开后的共同语义为：

```text
hash = ttstr_hash(path)       // holder 内缓存为0时延迟计算并写回
node = find bucket(hash) where cached_hash matches and ttstr_equal(key, path)
if node == null:
    return

unlink node from global/bucket chain
repair bucket predecessor/head links
destroy node.value.krkrSourceEntries
destroy node.value.winSourceTextures
destroy node.value.file
destroy node.key
delete node
size -= 1
// keep outer bucket allocation/count/max-load-factor
```

源码级表达应继续使用 `unordered_map::erase(path)`；不应把标准库 bucket 修复逻辑手写回 C++。

## 4. 查找与 key 边界

- 四端都先读取 `ttstr` holder 中缓存 hash；缓存为0时按平台现有 `ttstr_hash` helper计算并写回。
  真正算出的0会按现有 ttstr 规则缓存为非零 sentinel。
- Android libstdc++ node 保存完整 cached hash，并先比较 hash，再走 `ttstr_equal`。iOS libc++ 同样
  先比较 node hash，再比较 key；其 bucket index 对2的幂 bucket_count走mask，否则走无符号mod。
- 比较是精确 `ttstr` equality；没有 lowercase、basename、slash替换或前缀匹配。所有路径等价化只
  来自前置 `TVPGetPlacedPath`。
- miss 不调用 mapped-value destructor，不修改size/buckets，也不返回可见删除数量。
- callback 不读取 erase 的标准库返回值；脚本方法仍走 typed void method publication。

## 5. hit owner 与容器生命周期

单节点 hit 复用 `unloadAll` slice 已闭合的同一 `LoadedResourceRecord` owner布局：

1. 摘除 outer hash node，使后续 find 不再可达；
2. 销毁 `krkrSourceEntries`，逐entry对非null texture调用 `Release`，再销毁ttstr key/node和inner
   bucket storage；
3. 销毁 `winSourceTextures`，执行同样的 texture/key/node/bucket owner链；
4. Release `PSBFile` owner；
5. 销毁 outer ttstr key并释放outer node。

只删除命中的 module；其他 module、SourceCache list、outer bucket allocation和容器policy全部保留。
已存在的外部 `PSBValueDispatch`、ObjSource或纹理若各自持有引用，依其自身引用计数继续存活；
`unload` 不主动 invalidate这些 facade。

## 6. STL/ABI差异

### 6.1 Android libstdc++

- find helper返回目标的 predecessor/before-node 表达；callback确认 predecessor及其next都非null后
  才调用erase。
- arm64 erase内联 bucket predecessor修复、node value/key析构、operator delete和size递减。
- armv7 wrapper先找到 predecessor，再tail-call core；core修复链、调用共同outer-node destroy并
  递减size。

### 6.2 iOS libc++

- find返回目标node本身，null即miss。
- erase先由unlink helper修复bucket/global链、把node从容器摘除、把node next置null并递减size；
  wrapper随后以临时 unique-owner 形式销毁KRKR/Win/PSB/key/node。
- iOS 的mask/mod选择和临时owner只是 libc++ 实现细节，不是源级平台分支。

## 7. 异常、并发与极端边界

- `TVPGetPlacedPath` 抛出：map尚未访问；normalize临时值按平台 EH 规则销毁，原调用异常继续传播。
- miss：包括空map、bucket空、hash collision后key不等；全部no-op。
- hit：无catch。正常库约定下 texture `Release` 不抛；若外部虚实现违约抛C++异常，node已经开始或
  完成从容器摘除，已释放的mapped成员不回滚。Android的size递减位于node destruction之后，iOS
  位于unlink阶段，因此这种违约异常下两类STL会暴露不同的partial-destruction内部状态。
- erase不shrink/rehash；删除最后一个node后outer buckets仍保留，后续load可复用。
- 无锁；与load/find/unloadAll或texture-map填充并发会形成unordered_map和owner data race。
- 传入路径对象在callback内部被替换成normalized holder；这发生在find之前。脚本包装层拥有自己的
  参数Variant/ttstr副本，不会把normalized字符串写回调用者脚本变量。

## 8. 本地逐行对照与修正

本地修正前：

```cpp
void ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    path = TVPGetPlacedPath(path);
    _loadedModules.erase(path);
}
```

四端共同目标：

```cpp
void ResourceManager::unload(ttstr path) const {
    path = TVPGetPlacedPath(path);
    _loadedModules.erase(path);
}
```

保留按key erase，而不是改写为显式 find/iterator erase：前者正是共同源码语义，四个二进制中的
find/unlink差异由各自标准库模板展开产生。删除日志同时删除额外UTF-8转换、format、logger状态读取、
潜在线程同步和异常点。

## 9. 验证边界

- 已删除额外logger/`AsStdString`，把unload body从pending提升为implemented；
- 已执行 `git diff --check`、coverage/NCB TSV字段校验和NCB确定性重生成，两次生成逐字节一致；
- 四端IDB已补充callback、map find/erase/unlink与normalizer的命名、注释、书签并原位保存；
- 当前缺少CMake/Ninja/Emscripten和完整依赖头，不能宣称正式unit/Web build；
- 不伪造fixture；可执行环境恢复后应补 normalization hit/miss、最后节点删除、collision和外部owner
  持有边界测试。
