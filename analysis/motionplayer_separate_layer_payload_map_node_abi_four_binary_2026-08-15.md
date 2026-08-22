# MotionPlayer `SeparateLayerAdaptor` payload-map 节点 ABI 与提交边界四参考复原

日期：2026-08-15

## 1. 本轮结论

四份当前参考二进制共同证明，`SeparateLayerAdaptor` 的两棵有序树在源级都是：

```cpp
std::map<tjs_uint32, SeparateLayerPayload> activeLayers;
std::map<tjs_uint32, SeparateLayerPayload> retiredLayers;
```

ordinal 只存在于 `std::pair<const uint32_t, Payload>::first`。mapped value 直接就是
`SeparateLayerPayload`；key 与 payload 之间只有 64 位 ABI 所需的 4 字节对齐空隙，
不存在本地旧结构中的第二份 `SeparateLayerNode_guess::ordinal`。

两种标准库的节点头不同，但四端都呈现同一源级提交协议：查找/判重，分配节点，写 key，
把 mapped payload 默认构造成全零状态，连接红黑树，最后增加节点计数。resolver 随后才把
source payload 赋给已经发布的 active 节点。因此：

- `operator new` 失败时 map 完全不变；
- 默认 payload 构造没有逐成员可抛调用，四端都被优化为一段 `memset(0)`；
- 节点连接和计数增加发生在 resolver 的 payload copy-assignment 之前；
- 后续 Variant、字符串或 vector 赋值若抛出，已发布节点不会自动从 active map 回滚；
- 删除节点按 C++ 逆声明序释放第二个 vector、第一个 vector、`ttstr`、Variant，再释放
  节点存储。

本文中的绝对地址只用于四参考分析坐标；编译源码只保留语义名。

## 2. fresh 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| resolver | `0x6C3F28` | `0x58DCD4` | `0x100117E88` | `0x115B34` |
| 默认插入/索引 helper | `0x6DA0EC` | `0x59B764` | `0x100129BA8` | `0x128B74` |
| hint/position helper | `0x6DA1DC` | `0x59BA20` | 内联于上一项 | 内联于上一项 |
| 节点连接/计数提交 | `0x1480ED0` | `0x59BABA` | `0x100129C88` | `0x128BF8` |
| 单节点 erase/destroy 链 | `0x1481090` → `0x6D8BE8` | `0x59BB42` | `0x100129CE0` | `0x128C24` |

Android arm64 的第一项是 libstdc++ `emplace_hint` 风格内部 helper：resolver 已内联
`lower_bound`，缺键时才调用它。Android armv7 的 `0x59B764` 完整表现为 map 默认索引：
先按无符号 key 搜索，命中即返回 node `+20`，缺键才进入分配 helper。iOS 两端使用
libc++ `__tree` 实现，公共表达式虽然返回 mapped value，内部 helper 仍可能携带
`pair<iterator, bool>` 的插入标志；resolver 不使用该标志。

这些模板函数的原始 C++ identifier 没有保留，所以写回 IDB 的名字继续带 `_guess`。

## 3. map 对象布局

### 3.1 Android / libstdc++

| ABI | active map | retired map | map 大小 | header/root/left/right/count |
|---|---:|---:|---:|---|
| arm64 | adaptor `+64` | adaptor `+112` | 48 | map `+8/+16/+24/+32/+40` |
| armv7 | adaptor `+36` | adaptor `+60` | 24 | map `+4/+8/+12/+16/+20` |

这里 header 项依次是红黑树 header/sentinel、root、leftmost、rightmost 与 node count。
arm64 的 allocator/comparator 空基类布局使 tree header 从 map `+8` 开始；armv7 从
map `+4` 开始。两个 map 紧邻，随后才是 `absolute` 与 per-pass sequence。

### 3.2 iOS / libc++

| ABI | active map | retired map | map 大小 | begin/root/count |
|---|---:|---:|---:|---|
| arm64 | adaptor `+64` | adaptor `+88` | 24 | map `+0/+8/+16` |
| armv7 | adaptor `+36` | adaptor `+48` | 12 | map `+0/+4/+8` |

libc++ 把 begin-node cache、root/end anchor 与 size 压成三个 pointer-sized 槽。因此同一
源级声明在 iOS 上只占 Android/libstdc++ 的一半；这也解释了四端 adaptor 后部
`absolute`/sequence 的不同偏移，不能拿某一端的对象偏移硬编码进 portable 源码。

## 4. 节点 ABI：key 后直接是 payload

### 4.1 64 位节点，`0xD0` / 208 字节

| 节点偏移 | Android arm64 / libstdc++ | iOS arm64 / libc++ |
|---:|---|---|
| `+0` | color（4 字节，随后 padding） | left pointer |
| `+8` | parent pointer | right pointer |
| `+16` | left pointer | parent pointer |
| `+24` | right pointer | color byte（随后 padding） |
| `+32` | `uint32_t key` | `uint32_t key` |
| `+36` | 4 字节 alignment gap | 4 字节 alignment gap |
| `+40` | 168 字节 mapped payload | 168 字节 mapped payload |

resolver、payload comparator、Variant copy 和 vector assignment 全都以 node `+40` 为
payload 基址。若 mapped object 里还存在第二份 ordinal，payload 应从 `+48` 开始；四端
所有字段访问都否定了这种布局。

### 4.2 32 位节点，`0x98` / 152 字节

| 节点偏移 | Android armv7 / libstdc++ | iOS armv7 / libc++ |
|---:|---|---|
| `+0` | color | left pointer |
| `+4` | parent pointer | right pointer |
| `+8` | left pointer | parent pointer |
| `+12` | right pointer | color byte（随后 padding） |
| `+16` | `uint32_t key` | `uint32_t key` |
| `+20` | 132 字节 mapped payload | 132 字节 mapped payload |

32 位不需要 key 后的额外 padding。resolver 在两端都把 node `+20` 直接传给
`tTJSVariant` 赋值、payload comparator 和返回值复制；同样没有第二份 ordinal。

## 5. payload 相对节点的拥有型字段

结合此前恢复的 payload ABI，可把析构 helper 的机器偏移还原为完整声明顺序：

| owner | 64 位 node 偏移 | 32 位 node 偏移 | payload 相对偏移 |
|---|---:|---:|---:|
| `layerVariant` | `+40` | `+20` | `+0` |
| `commandSrc` | `+68` | `+40` | 64 位 `+28` / 32 位 `+20` |
| `compositeMeshPoints` | `+128` | `+96` | 64 位 `+88` / 32 位 `+76` |
| `bezierPatchPoints` | `+152` | `+108` | 64 位 `+112` / 32 位 `+88` |

其余整数、Boolean、packed colors、float descriptor 和 corners 都是平凡成员。默认
插入分别执行：

```text
64-bit: write key@node+32; memset(node+40, 0, 0xA8)
32-bit: write key@node+16; memset(node+20, 0, 0x84)
```

这不是“只把几个标量清零”的显示简化：Variant、`ttstr` 和两组 vector 的合法空状态
本来就是全零表示，所以整个 mapped object 可在该构建配置下折成一次 zero fill。

## 6. 插入提交、重复键和异常边界

共同源级效果接近：

```cpp
Payload &payload = activeLayers[ordinal];
// 至此缺键节点已经连接进 activeLayers，size 已增加。
payload = sourcePayload; // 发生在 map helper 返回之后
```

但标准库和优化版本造成一个可观察的分配差异：

| 目标 | 重复键路径 |
|---|---|
| Android arm64 | hint-emplace helper 先分配/清零 `0xD0` 节点，再做最终 uniqueness 检查；重复时按完整 payload 析构顺序释放临时节点，node count 不变 |
| Android armv7 | 外层默认索引先命中旧节点，正常重复键路径不分配；内部分配 helper仍保留最终 uniqueness 失败清理 |
| iOS arm64 | tree search 命中后直接返回旧节点，不分配 |
| iOS armv7 | tree search 命中后返回旧节点，并把 inserted flag 写为 false，不分配 |

缺键路径在四端都是：分配 → 写 key → zero-fill payload → 初始化 node header/连接树 →
rebalance → 增加 size。payload 默认构造本身没有中途 owner 阶段，因此没有“只析构前 N
个成员”的 landing pad；唯一可抛步骤 `operator new` 位于任何节点写入和 tree 发布之前。

resolver 的后续 `payload = sourcePayload` 则可能执行 Variant 引用操作、`ttstr` 引用操作
和两组 vector 深拷贝。它在节点提交之后，四份 resolver 都没有 catch 后 erase active
节点的回滚路径。portable 实现若用“先在栈上完整构造 payload，再一次 insert”会得到更
强的异常保证，但那不是参考二进制的状态机。

## 7. erase 与逆序析构

成功 erase 的共同顺序是：

1. 求 successor，更新 begin/leftmost/rightmost 等缓存；
2. 从红黑树 detach 并 rebalance；
3. 减少 node count（具体在 detach 前后因标准库而异）；
4. 析构第二组 vector；
5. 析构第一组 vector；
6. 释放 `commandSrc`；
7. 析构 `layerVariant`；
8. `operator delete(node)`。

Android arm64 把步骤 1–2 放在通用 libstdc++ helper，调用专用 node destroy 后由 caller
手动递减 map `+40` 的计数；Android armv7 的 `0x59BB42` 将 detach、owner 析构、delete
与 map `+20` 计数递减包在同一模板实例中。iOS 两端的专用 erase helper 先递减
map `+16` / `+8`，再走 libc++ detach/rebalance 与 owner 析构。

这些差异是标准库 ABI，不应在 portable wrapper 中仿造节点头；源级必须依赖
`std::map<uint32_t, Payload>` 的 owner/iterator 规则。

## 8. 本地结构漂移与修复

本轮前本地代码为：

```cpp
struct SeparateLayerNode_guess {
    tjs_uint32 ordinal;
    SeparateLayerPayload_guess payload;
};
using Map = std::map<tjs_uint32, SeparateLayerNode_guess>;
```

这会在 pair key 之外再保存一份 ordinal，使 mapped value 和节点大小都偏离四端 ABI，
并迫使所有 resolver/clear/assign 路径访问 `entry.second.payload`。现已改为：

```cpp
using Map = std::map<tjs_uint32, SeparateLayerPayload_guess>;

SeparateLayerPayload_guess &ensure(tjs_uint32 ordinal) {
    return _nodes[ordinal];
}
```

所有调用者同步改为直接使用 `entry.second`/返回的 payload。`operator[]` 还保留了 native
最重要的提交顺序：缺键时先发布默认 payload 节点，resolver 返回后再执行 source payload
赋值。ordered-map wrapper 只封装 portable 的清理/交换行为，本身只含一个
`std::map`，不会在 map 对象前后增加实例字段。

## 9. IDB 与验证

四份 recovery IDB 已写入：

- 默认插入、hint/position、link commit、erase/destroy 的 `_guess` 语义名；
- 64/32 位节点头、key/payload 偏移、分配大小与 zero-fill 大小；
- resolver 的“key 只保存一次”和“节点先提交、payload 后赋值”注释；
- 直接 payload map 插入与节点逆序析构书签。

源码回归测试静态断言 `Map::mapped_type` 恰为 `SeparateLayerPayload_guess`，并验证重复
`ensure` 返回同一已发布 payload、不重新 value-initialize，以及迭代顺序仍按无符号 ordinal
升序。验证结果：

- 完整 motionplayer Catch2 TU 的 Emscripten `-fsyntax-only` 检查通过；唯一诊断为仓库
  既有 `_tss` literal-operator 弃用 warning；
- `Web Debug Build` 完整 Ninja 构建更新 C++/archive/Wasm 产物，完成后再次执行预设返回
  `ninja: no work to do.`，最终链接状态为成功；
- 定向 `git diff --check` 无 whitespace error，只有工作树行尾策略的 LF→CRLF warning；
- 四份 recovery IDB 名称回读成功，随后四次原位 `idb_save` 均返回 `ok: true`。

构建只能证明 portable 实现内部一致；本文件的四端 fresh decompile/disasm 才是节点布局、
标准库差异和提交/销毁边界的依据。
