# SourceCache::Entry 构造、链表节点与生命周期四端复核（2026-08-13）

## 结论

四个当前参考目标共同证明，`SourceCache::loadSource` 先在栈上默认构造一个
`Entry` 候选，再读取 descriptor；缓存容器是 `std::list<Entry>`，颜色变化
命中采用 `push_front(copy)` 后 `erase(old)`，不是 `splice`。共享源码层面的
payload 顺序为：

```cpp
struct Entry {
    tTJSVariant key;
    tTJSVariant layer;
    ttstr src;
    tjs_int blendMode;
    tjs_int colors[4];
    tjs_int byteWeight = 0;
};
```

默认构造只初始化两个 Variant、`ttstr` 和带类内初始化器的
`byteWeight`。`blendMode` 与四个 `colors` 没有默认初始化；正常路径会在读取
颜色前覆盖 `blendMode`。当 descriptor 的 `color` 为 Void 时，参考实现只写
`colors[0]`：

```text
colors[0] = (blendMode & 0xf0) != 0 ? 0xff808080 : 0xffffffff
// colors[1], colors[2], colors[3] remain indeterminate
```

后续代码仍然读取这三个未初始化槽：命中时参与四槽相等比较，不等时复制进
旧 entry 并重新 bake；未命中时参与 bake，随后又被复制进新 list node。这是
四端都存在的原插件源级未初始化边界，不是反编译器遗漏，不能用 `{}`、数组
类内初始化器或四槽广播“修复”。

## 四端 loadSource 映射与候选构造

| 目标 | `loadSource` | 候选初始写入 | Void color 单槽写入 | 四槽后续读取 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | 独立序言 `0x6A4F88`，当前 IDA 错误合并进 `0x6A4CD4` 开始的函数 | `0x6A4FE4..0x6A4FF0` | `0x6A5224` | `0x6A547C..0x6A54B4` |
| Android armv7 | `0x57ACC8` | `0x57ACFE..0x57AD04` | `0x57AE04` | `0x57AE38..0x57AF2E` |
| iOS arm64 | `0x1001009AC` | `0x1001009F4..0x100100A00` | `0x100100B6C` | `0x100100BB0..0x100100BD8` |
| iOS armv7 | `0xFDB50` | `0xFDB98..0xFDBA0` | `0xFDD14` | `0xFDD5A..0xFDE5E` |

逐端栈证据：

- Android arm64 将候选 key Variant type、layer Variant type、src 指针和
  byteWeight 分别清零；Void 分支只向第一个颜色槽写一次 `STR W8`。非 Void
  分支则在 `0x6A529C`、`0x6A52F4`、`0x6A534C`、`0x6A53AC` 分别覆盖四槽。
- Android armv7 的 `v34`、`v35[2]`、`v36`、`v41` 分别对应两个 Variant
  type、src 和 byteWeight；`IntValue_guess`、`v38/v39/v40` 直到 descriptor
  分支才写。Void 分支只有 `LODWORD(v38) = default`。
- iOS arm64 清零 `v20[4]`、`v21[4]`、`v22`、`v27`；表示 blendMode 与
  colors 的 `IntValue_guess/v24/v25/v26` 不在初始清零集合。Void 分支只写
  `v24`。
- iOS armv7 清零 `v39[2]`、`v40[2]`、`v41`、`v47`；表示 colors 的
  `v43..v46` 未初始化，Void 分支只写 `v43`。

四端非 Void 分支均严格把 color Variant 转为 Object，再对索引 0..3 调用
“存在则读取，否则默认 0”的整数访问器。因此这里不是“一个颜色广播至四角”
的接口；只有明确提供可索引对象时四槽才全部确定。

## `std::list<Entry>` 节点布局与复制

四端节点 helper 证明容器元素就是按值保存的完整 `Entry`，而不是指针、
map value 或共享对象。ABI 节点大小不同属于 STL/指针宽度差异：64 位节点
为 `0x58`，32 位节点为 `0x3c`；共享 payload 字段及复制次序完全一致。

| 目标 | 节点复制 / push helper | 链接方式 |
| --- | ---: | --- |
| Android arm64 | `0x6E8040`，已命名 `SourceCache_listAllocateCopyEntryNode_guess` | helper 只分配并复制；`0x145EFDC` 单独链接到指定位置 |
| Android armv7 | `0x5A67DE`，已命名 `SourceCache_listAllocateCopyEntryNode_guess` | helper 只分配并复制；随后调用 `0xD3B884` 链接 |
| iOS arm64 | `0x100100E54`，已命名 `SourceCache_listPushFrontCopyEntry_guess` | helper 内部链接到 front 并递增 size |
| iOS armv7 | `0xFDFB0`，已命名 `SourceCache_listPushFrontCopyEntry_guess` | helper 内部链接到 front 并递增 size |

复制顺序为：

1. 分配 list node；Android helper 先清空尚未链接的 prev/next；
2. copy-construct `key` Variant；
3. copy-construct `layer` Variant；
4. 复制 `src` 的引用计数指针并 AddRef；
5. 连续复制六个 32 位整数：`blendMode`、四个 `colors`、`byteWeight`；
6. 链接节点；iOS helper同时维护 list size，Android 在独立链接 helper 中
   完成拓扑更新。

Android arm64 `0x6E80C0..0x6E80F4`、iOS arm64
`0x100100EEC..0x100100F0C` 以及 iOS armv7 `0xFE09C..0xFE0C8` 的异常
清理进一步证明 copy construction 的阶段性生命周期：第二个 Variant 构造
失败时先析构已经构造的第一个 Variant，再释放裸节点；节点尚未链接，因此
不会进入普通 erase。Android armv7 使用相同的 C++ 异常构造模型，具体 landing
pad 被编译器折叠在相邻异常表/函数块中，未发现语义差异。

## 命中、提升与销毁顺序

颜色完全相同的命中直接返回当前 node 的 `layer`，不会移动节点。颜色变化
命中的共享控制流为：

```text
result = old.layer
copy candidate.colors[0..3] into old.colors
bakeSource(source, old)
push_front(copy(old))     // key/layer/src ownership acquired first
erase(old)                // then release old src/layer/key
return result
```

这条顺序保证新旧节点短暂同时拥有 key/layer/src。四端调用点：

| 目标 | copy/push | old erase |
| --- | ---: | ---: |
| Android arm64 | `0x6A54EC` + `0x6A54F4` | `0x6A54F8..0x6A5520` 内联 |
| Android armv7 | `0x57AE6A` + `0x57AE70` | `0x57AE78` |
| iOS arm64 | `0x100100C14` | `0x100100C20` |
| iOS armv7 | `0xFDD90` | `0xFDD9C` |

普通节点销毁先从双链表摘除（并在 libc++ 两端递减 size），再按成员构造的
逆序释放 `src -> layer -> key`，最后 `operator delete`：

| 目标 | erase helper / 内联区间 |
| --- | ---: |
| Android arm64 | `0x6A54F8..0x6A5520`；完整 list 析构还见 `0x6A4F28..0x6A4F50` |
| Android armv7 | `0x5A67B0`，已命名 `SourceCache_listEraseEntryNode_guess` |
| iOS arm64 | `0x1000FFABC`，已命名 `SourceCache_listEraseEntryNode_guess` |
| iOS armv7 | `0xFCD2E`，已命名 `SourceCache_listEraseEntryNode_guess` |

`SourceCache` 自身异常展开/析构同样直接销毁 list nodes，再逆序析构持久成员
Variant；不会调用公开 `clearCache`，因此这个边界不会向 entry Layer 发送
脚本可见的 `Invalidate`。

## 本地实现对照

- `SourceCache::Entry` 字段顺序与四端节点复制顺序一致；只有
  `byteWeight = 0` 有类内初始化器。
- `Entry entry;` 保留类类型成员的默认构造和三个标量区域的未初始化状态；
  没有改为 `Entry entry{}`。
- descriptor 按 `key -> src -> blendMode -> color` 的顺序读取；Void color
  分支只写 `entry.colors[0]`，没有广播或补零。
- 命中分支用 `std::list<Entry>::push_front(*it)` 后 `erase(it)`，自然复现
  copy/AddRef 先于旧 node Release 的顺序。
- `SourceCache::~SourceCache() = default` 让 list 直接逆序释放元素成员，不
  复用会调用 `Invalidate` 的公开 `clearCache()`。

本轮没有为缺失 color 构造运行时单元测试：任何读取后三个 indeterminate
`tjs_int` 的 C++ 测试本身都进入 undefined behavior，测试若先补零又会掩盖
正要保留的参考边界。该路径由四端汇编、节点 copy helper 和后续四槽比较的
静态证据共同覆盖；现有非 Void color 测试继续覆盖确定值路径。

## IDB 改进

四端节点 copy/push 与 erase helper 已按上表命名，并在候选初始化、Void
color 单槽写入、颜色变化 copy-before-erase、节点字段复制及逆序析构位置添加
注释。Android arm64 的 loadSource 仍被 IDA 合并进前一个 constructor 函数；
其 `0x6A4F88` 独立栈序言及完整 return/landing-pad 证明它是独立 callback，
分析时应从该地址读取汇编，不应把 constructor 的伪代码当成 loadSource。
