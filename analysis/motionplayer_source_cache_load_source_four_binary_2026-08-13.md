# Motionplayer SourceCache::loadSource 四二进制复原（2026-08-13）

## 结论

四个当前参考二进制的 `SourceCache::loadSource` 具有同一条数据流：

1. 从 descriptor 读出 `key`、`src`、`blendMode` 与可选 `color`。
2. 缓存 identity 是严格的三元组 `(key, src, blendMode)`：
   - `key` 使用 Variant 严格真实比较；
   - `src` 使用 `ttstr` 内容比较；
   - `blendMode` 使用 int32 相等比较。
3. 命中且四个 packed color 全等时，直接返回原 Layer，不调整链表顺序。
4. 命中但颜色变化时，只覆盖四个 color，重新 bake 原 entry，然后
   `push_front(copy)` 再 `erase(old)`；Layer、key、src、blendMode 与 byteWeight
   都从旧 entry 复制/保留。
5. miss 时先执行裁剪；随后才创建 Layer、bake、把 byteWeight 加入 current bytes，
   并把新 entry 插到表头。

本轮最重要的审计纠错是：32 位和 iOS arm64 的链表循环只出现一个私有 helper
调用，但该 helper 不是“仅 key 比较器”；它一次比较完整三元组。Android arm64
把同一逻辑直接内联到 `loadSource`。因此端口原有三元组条件是正确的，本轮没有
把一次中途的 key-only 误判保留在最终源码中。

## 输入与散列

- Android arm64:
  `reference/binaries/Kirikiroid2_1.3.9_Android_arm64-v8a.so`
  - SHA-256:
    `05E2FF4C77F1561608AD7703153D2FB09855BF223237A85DC2267FFF1388564F`
- Android armv7:
  `reference/binaries/Kirikiroid2_1.3.9_Android_armabi-v7a.so`
  - SHA-256:
    `A15C238EC6F21C17D0889B064AE1AD47EC85B4F1530A3611F206B7190FF456AF`
- iOS fat reference:
  `reference/binaries/Kirikiroid2_1.3.9_iOS`
  - SHA-256:
    `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3`
  - 本文分别检查 arm64 与 armv7 slice。

## 入口和私有 helper

| 目标 | loadSource | identity helper | trim-before-insert | bake |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6A4F88`（IDA 合并 chunk） | 内联于 `0x6A53D8..0x6A5454` | `0x6A3EE0` | `0x6A3FC0` |
| Android armv7 | `0x57ACC8` | `0x57A0AC` | `0x57A106` | `0x57A168` |
| iOS arm64 | `0x1001009AC` | `0x1000FF9D4` | `0x1000FFA1C` | `0x1000FFB24` |
| iOS armv7 | `0xFDB50` | `0xFCC9C` | `0xFCCD2` | `0xFCD68` |

## Entry 布局与 identity 比较

64 位 list node 的 payload 起点是 node `+0x10`，32 位是 node `+0x08`。
因此 source-level Entry 布局仍是：

```cpp
tTJSVariant key;
tTJSVariant layer;
ttstr src;
tjs_int blendMode;
tjs_int colors[4];
tjs_int byteWeight;
```

identity helper 的相对访问证明如下。

### Android armv7

`0x57A0AC`：

- 先调用 `0x76093C(a1, a2)` 比较 Entry 起点的 Variant key；
- 再调用 `0x497BA0(a1+0x18, a2+0x18)` 比较 src；
- 最后比较 `*(a1+0x1C) == *(a2+0x1C)`，即 blendMode。

调用点 `0x57AE18..0x57AE28` 只在该 helper 返回 true 时终止链表搜索。

### iOS arm64

`0x1000FF9D4`：

- `0x10031A51C(a1,a2)`：Variant key；
- `0x10002E518(a1+0x28,a2+0x28)`：src；
- 比较 `*(a1+0x30) == *(a2+0x30)`：blendMode。

调用点 `0x100100B80..0x100100B98` 对每个节点调用一次该 helper。

### iOS armv7

`0xFCC9C` 与 Android armv7 同构：

- `0x31F740(a1,a2)`：Variant key；
- `0x675B8(a1+0x18,a2+0x18)`：src；
- `*(a1+0x1C) == *(a2+0x1C)`：blendMode。

调用点是 `0xFDD26..0xFDD40`。

### Android arm64 内联版本

`0x6A53D8..0x6A53E4` 先比较 node `+0x10` 的 key。随后：

- `0x6A53E8..0x6A5444` 比较 node `+0x38` 的字符串所有者/长度/内容；
- `0x6A5448..0x6A5454` 比较 node `+0x40` 的 blendMode；
- 任一步不等即跳到下一节点。

这正好是 32 位/另一 64 位 helper 的内联形式，不是发布物之间的语义分叉。

## color 命中边界和链表行为

四端在 identity 命中后都先复制返回 Layer，再逐字比较四个 color。

- 全等：直接走清理/返回路径，不调用 bake，也不移动节点。
- 不等：
  - 写回四个 color；
  - bake 旧 entry；
  - 复制旧 entry 到表头；
  - 删除旧节点；
  - 返回命中前已 CopyRef 的 Layer。

对应关键区域：

- Android arm64: `0x6A546C..0x6A5524`
- Android armv7: `0x57AE2E..0x57AE7C`（后续返回路径延伸到函数尾）
- iOS arm64: `0x100100BA4..0x100100C24`
- iOS armv7: `0xFDD48..0xFDDA0`

该移动是 copy+erase，不是 `std::list::splice`，所以复制时 Variant/string owner
先增持，旧节点随后释放。

## miss、裁剪和超限边界

四端的 trim helper 同构：

1. `currentCacheBytes <= cacheLimitBytes` 时立即返回。
2. threshold 按 uint32 运算：
   `(cacheLimitBytes * 99u) / 100u`，乘法可 wrap。
3. 从表头向表尾遍历，以 uint32 累加 `keptBytes + entry.byteWeight`。
4. 分支使用 signed `<=` 比较 cumulative sum 与 threshold。
5. 会使 cumulative 超阈值的节点立即 erase，并从 current bytes 减去 byteWeight；
   erase 不调用 `Layer::Invalidate`。

关键入口：

- Android arm64 `0x6A3EE0`
- Android armv7 `0x57A106`
- iOS arm64 `0x1000FFA1C`
- iOS armv7 `0xFCCD2`

trim 只在 miss 分支、创建 Layer 之前调用。因此一次新插入可以让 current bytes
超过 limit；只有以后再次发生 miss 时才触发裁剪。算法也不是简单“从尾部删到
够小”，而是按表头到表尾的累计阈值保留/删除。

## 本轮代码和测试变化

- `cpp/plugins/motionplayer/SourceCache.h`
  - 明确 Entry lookup identity 是 `(key, src, blendMode)`；
  - colors 和 byteWeight 不参与 identity。
- `cpp/plugins/motionplayer/SourceCache.cpp`
  - 保持三字段命中条件；
  - 补充 all-four helper/内联形式、同色不移动、变色 copy+erase 的注释；
  - 去掉当前 vertical 的编译源码绝对地址注释。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 新增 composite-identity/trim 回归：
    - 完全相同三元组+同色命中同一 Layer，不重 bake；
    - 同三元组变色重 bake 同一 Layer；
    - 只改变 src 会 miss；
    - 只改变 blendMode 也会 miss；
    - 两个 64-byte entry 可在 100-byte limit 下暂时共存；
    - 下一次 miss 前按 99-byte threshold 保留表头、淘汰旧节点；
    - trim 淘汰不调用 Invalidate，显式 clearCache 才调用。

## IDB 改善

四个 IDB 新增/更新了以下 guessed names：

- `SourceCache_loadSource_guess`（Android arm64 为合并 chunk，使用注释/书签）
- `SourceCache_entryIdentityEquals_guess`（除内联的 Android arm64 外）
- `SourceCache_trimBeforeInsert_guess`
- `SourceCache_bakeSource_guess`

同时在入口与 helper 写入了 identity、颜色命中、pre-miss trim、直接 erase 的语义
注释。Android arm64 `0x6A4F88` 追加了明确 CORRECTION，覆盖中途 key-only 误判；
最终证据和源码都采用 composite identity。

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数和 `out/syntax-check` Catch2/test config
  执行 `-fsyntax-only`：成功；唯一诊断为既有 `_tss` warning。
- Web Debug：成功重编 SourceCache/motionplayer 并链接 `index.html/index.wasm`。
- Wasmtime guest：成功链接并转换 `krkr2_wasmtime_guest.wasm`。
- 当前 CMake 配置没有可直接运行本组 Catch2 用例的 executable，因此上述测试只
  报告完整翻译单元编译门禁，不冒充运行时测试。
