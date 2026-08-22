# Motionplayer SourceCache::eraseSource 旧端口 API 清理（2026-08-13）

## 结论

`SourceCache::eraseSource(ttstr)` 不属于
`reference/binaries/` 中四个当前参考发布物的 SourceCache 源码表面，也没有当前
C++ 调用者。它是仓库在 2026-05-05 首次新增 SourceCache 端口时，为当时按名称
组织的 Web 兼容缓存顺手加入的 API；后来 Entry 已改成当前二进制证明的
`(key, layer, src, blendMode, colors, byteWeight)` 结构，但该死 API 被机械保留。

本轮已从 `SourceCache.h/.cpp` 删除声明与定义，避免继续把旧
`libkrkr2.so`/早期 Web 端口假设误当作四参考二进制行为。

## 四端 NCB 表证据

四个当前 SourceCache registrar：

- Android arm64: `0x6A5988`
- Android armv7: `0x57B0DC`
- iOS arm64: `0x100100F90`
- iOS armv7: `0xFE12A`

四端都只注册：

1. `loadSource`
2. `clearCache`
3. getter-only `bufLayer`

没有 `eraseSource` 字符串、method wrapper 或第四个 member registration。
ResourceManager 在四端重复注册的是上述同一组三个回调，也没有额外 erase 方法。

## 私有函数群证据

围绕 SourceCache 当前实现，四端能对应的私有逻辑包括：

- Entry composite identity 比较：
  - Android arm64 内联在 loadSource；
  - Android armv7 `0x57A0AC`
  - iOS arm64 `0x1000FF9D4`
  - iOS armv7 `0xFCC9C`
- pre-insert trim：
  - Android arm64 `0x6A3EE0`
  - Android armv7 `0x57A106`
  - iOS arm64 `0x1000FFA1C`
  - iOS armv7 `0xFCCD2`
- bake：
  - Android arm64 `0x6A3FC0`
  - Android armv7 `0x57A168`
  - iOS arm64 `0x1000FFB24`
  - iOS armv7 `0xFCD68`
- public clearCache callback 与 list-node erase/destruction helpers。

没有一条四端可配对的“构造 name Variant，按 key 或 src 删除所有匹配节点”的
入口；也没有 wrapper/data registration 指向这种逻辑。

## 仓库来源审计

`git log -S eraseSource` 只追到提交 `9d6f6a5d`
（`Align operateAffine receiver and source cache`，2026-05-05）。该提交首次新增
整个 `SourceCache.cpp`，当时使用的是另一套 Web 兼容 Entry：

- string key / resolvedKey；
- `loadSourceByName`；
- `findSource`；
- `eraseSource` 按 key 或 resolvedKey 删除。

当前仓库后来把 SourceCache 转向 descriptor/list/Layer 缓存结构，却保留了
`eraseSource` 声明并把条件改成 `Variant key || src`。这只能说明端口演化历史，
不能证明原插件存在该 API。

全仓（排除构建输出与 reference）搜索结果仅有声明和定义，没有任何调用者。

## 修改

- 删除 `SourceCache.h` 的 `void eraseSource(ttstr name)`。
- 删除 `SourceCache.cpp` 的死实现。
- 不向 NCB 表添加任何替代成员，因为四端明确不存在该成员。

删除的是无调用者、无二进制对应项的源级死 API，不改变四端可达 runtime 数据流。

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实
  Emscripten 参数执行 `-fsyntax-only`：通过，只有既有 `_tss` warning。
- Web Debug 首次全量头依赖重编/链接超过工具回传时限，但紧接着的增量重跑明确
  返回 `ninja: no work to do`，说明前次构建已完成全部目标。
- Wasmtime guest 首次链接超过工具回传时限；增量重跑成功链接并转换
  `krkr2_wasmtime_guest.wasm`。
- 全仓再次搜索 `eraseSource` 后，只剩本文对已删除旧 API 的审计记录；编译源码、
  头文件和测试中均无引用。
