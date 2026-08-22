# SourceCache 孤立 `loadRawSourceVariant` helper 四端清理（2026-08-16）

## 结论

本地 `SourceCache::loadRawSourceVariant(Player *, name, resolvedKey)` 是上一轮删除
Player/SourceCache by-name 方便入口后遗留的孤立实现，不属于四个当前参考二进制所显示的
SourceCache 数据流：

- 全仓只有 declaration 与 definition，没有 caller；
- 它按名字调用 `ResourceManager::findSource(context, name)`，随后只做外部 storage
  规范化，不读取 descriptor，也不进入 SourceCache 的 `std::list<Entry>`；
- 它的注释声称由 `Player::findSourceForNode_guess` 调用，但实际调用图中不存在这条边；
- 四端真实 Player render fallback 都在 Player 持有的 SourceCache TJS 对象上动态调用
  `loadSource(source, descriptor)`；
- 四端真实 `SourceCache::loadSource` 都从 descriptor 读取 key、src、blendMode、color，
  再按 `(key, src, blendMode)` 查找同一张 list cache。

因此该 helper 既不是 native SourceCache 私有边，也不是真实 Player resolver 的兼容包装。
本轮删除它的声明、定义，以及只由它需要的 `<string>` / `StorageIntf.h` 依赖；不改变
`Player::findSourceForNode_guess` 的独立 RM 解析路径，也不改变 descriptor-keyed cache。

## 四端函数映射

| 目标 | SourceCache `loadSource` | Player render-source resolver |
|---|---:|---:|
| Android arm64 | `0x6A4F88`（合并 chunk 的真实入口） | `0x6BEF50` |
| Android armv7 | `0x57ACC8` | `0x58AD94` |
| iOS arm64 | `0x1001009AC` | `0x1001143E0` |
| iOS armv7 | `0xFDB50` | `0x111E08` |

Android arm64 recovery IDB 仍把 constructor、loadSource 与 clearCache 合并为一个函数；
这里保留数据库结构，只在 `0x6A4F88` 使用真实入口 label/comment，不破坏性地重新切分函数。

## 真实 SourceCache 边界

四个 `loadSource` callback 都接收两个借用 dispatch：

```text
source     // 用于 bake/drawLayer；cache entry 不持有它
descriptor // key/src/blendMode/color 的属性容器
```

共同数据流为：

1. 从 descriptor 复制 `key` Variant、`src` 字符串并读取 `blendMode`；
2. 读取可选的四个 packed color；
3. 在 SourceCache 自身的 `std::list<Entry>` 中按精确
   `(Variant key, ttstr src, int32 blendMode)` 查找；
4. 同色 hit 原位返回 Layer；变色 hit 重 bake 后执行
   `push_front(copy)` + `erase(old)`；
5. miss 时才先 trim，然后创建 Layer、bake 并插入表头。

这条边界没有 `Player *`、名字输出参数或 `resolvedKey`，也没有调用
`ResourceManager::findSource`。Android armv7、iOS arm64、iOS armv7 的 fresh decompile
直接显示完整 descriptor/list 流；Android arm64 的 fresh disassembly 在合并 chunk 中显示
相同的 descriptor 读取与内联三元组比较。

## 真实 Player fallback 的 receiver 与 owner

四个 `Player_resolveRenderSource_guess` 的非内部-Layer分支同构：

```text
receiver   = owning copy of Player.persistentSourceCacheObject
arg0       = owning copy of current source Variant
arg1       = owning copy of Player.persistentSourceDescriptor
result     = receiver.FuncCall("loadSource", arg0, arg1)
release arg1, arg0, receiver in native cleanup order
```

`loadSource` literal 的 xref 位于这次 SourceCache receiver call，而不是
`Player::loadSource(name)`。完整四端 literal/xref 表已记录在
`motionplayer_player_dead_sourcecache_facades_four_binary_2026-08-16.md`。

## 与 `findSourceForNode_guess` 的分离

生产代码仍然存在一条独立的 node source-resolution 路径：它根据 node 的当前 slot 与
Player 持有的 motion context 调用 ResourceManager，并把解析到的对象、texture 与诊断键
写回 node source state。该路径不调用本轮删除的 helper；render item 最终进入 SourceCache
时使用的是已经准备好的 source Variant 与 persistent descriptor。

旧 helper 把这两阶段重新揉成一个“按名字取 raw Variant”的无 caller 方法，还绕开真正的
descriptor/list cache，因此保留它会继续制造一条不存在的源代码结构与错误调用链。

## 源码与恢复库修正

- `cpp/plugins/motionplayer/SourceCache.h`
  - 删除私有 `loadRawSourceVariant` declaration；
  - 删除由其独占的 `<string>` include。
- `cpp/plugins/motionplayer/SourceCache.cpp`
  - 删除孤立 definition 与反向声称 caller 的过时注释；
  - 删除由其独占的 `StorageIntf.h` include。
- 四份 recovery IDB
  - 在真实 SourceCache loadSource 入口标注 `(source, descriptor)` 与 list-cache 边界；
  - 在真实 Player resolver 标注 SourceCache receiver、两个 Variant owner 及无 by-name RM 边；
  - 修正 Android arm64 loadSource 入口残留的早期“key-only”错误注释；
  - 强制刷新相关反编译并回读，随后原位保存四份 recovery IDB。

## 验证

- `loadRawSourceVariant` 在 `cpp/` 与 `tests/` 中零匹配；
- ordinary/headless Emscripten syntax-only 均成功；仅出现既有 `_tss` deprecation warning；
- Web Debug `motionplayer` 31/31 成功；
- Wasmtime Headless Debug `motionplayer` 31/31 成功；
- Web Debug 完整增量构建 3/3 成功并重新链接 `index.html/index.wasm`；
- 限定 `git diff --check`：删除后无内容级 whitespace error，仅有工作树既有换行提示。
