# MotionPlayer NodeLabelMap 插入、查找与生命周期四参考复原（2026-08-15）

## 结论

四个当前参考二进制一致证明，Player 的节点索引不是 hash table，也不是层级路径表，
而是：

```cpp
std::map<ttstr, int, ttstr_utf16_less> rawNodeLabelMap;
```

它的共同源码语义为：

1. 建树递归先向 flat node deque 追加节点，再第一次读取 layer 的原始 `label`；
2. 用 `rawNodeLabelMap[label] = newFlatIndex` 写表；
3. duplicate label 保留第一次插入的 key owner，只把 mapped index 改成最后一个节点；
4. 随后才调用两次 `requireLayerId`，再进入 node initializer；
5. initializer 第二次读取 `label`，所以有副作用的 getter 可以让 map key 与
   `MotionNode::layerName` 不同；
6. 所有本 Player 的目标解析都先调用同一个 raw-label resolver。该 resolver 不拒绝空
   key、不正规化斜杠/大小写，也不验证 map 中的 deque index；
7. 只有 CameraNode 在调用 resolver 前检查 target 的 ttstr backing 是否为 null；
8. teardown 先释放旧节点，再逐节点 Release map key、delete RB-tree node，最后重置
   root/header/count。

本轮还发现一个此前没有进入本地 comparator 的真实边界：四端都先比较 ttstr 的
**backing pointer 是否为 null**。因此普通 null-backed 空串严格小于“人为分配但内容仍为
零长度”的非 null ttstr；二者是两个不同的 map key。只比较 `c_str()` 内容会错误地把它们
合并。

## 四端入口与写入映射

| 目标 | recursive builder | 第一次 label → map 写入 | subscript / emplace helper | key node allocation / attach |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B1E4C` | `0x6B20C4` | `0x6B2498` | `0x6EF1A8` |
| Android armv7 | `0x5818B0` | `0x581A0E` | `0x581C54` | `0x5ACEE8` |
| iOS arm64 | `0x100109328` | `0x1001094B8` | `0x100141740` | `0x100141884` |
| iOS armv7 | `0x106BDC` | `0x106D5E` | `0x142844` | `0x14291E` |

Android 两端的 helper 直接返回 mapped `int *`，caller 随即写入 flat index。iOS 两端的
libc++ unique-emplace helper 返回 node（以及 32 位端的 inserted flag），caller 分别写
node `+0x24` / `+0x14`。两种代码生成都对应同一源码级 `operator[]` 行为。

### 共同伪代码

```cpp
for (int arrayIndex = 0; arrayIndex < layers.count; ++arrayIndex) {
    const int index = nodes.size();
    nodes.emplace_back();
    MotionNode &node = nodes.back();
    node.index = index;
    node.parentIndex = parentIndex;
    node.slot[0].done = true;
    node.slot[1].done = true;

    Variant rawLayer = layers[arrayIndex];
    DispatchOwner layer = retain(rawLayer.object);

    // first read; no hierarchical path construction
    ttstr rawMapLabel = propGetString(layer, L"label");
    rawNodeLabelMap[rawMapLabel] = index;

    node.layerId1 = resourceManager.requireLayerId();
    node.layerId2 = resourceManager.requireLayerId();
    initNodeFields(node, rawLayer);       // reads label a second time
    buildChildren(layer[L"children"], index);
}
```

第一次读取的临时 ttstr 在新 key 路径被 map CopyRef/AddRef。Android arm64 的 node
allocator 在 `0x6EF1E0..0x6EF1F4` 保存 key backing 并原子 AddRef；armv7 在
`0x5ACF10..0x5ACF28` 做同样工作。iOS arm64 在 `0x100141790..0x1001417A8`、armv7
在 `0x142874..0x14288C` 完成对应的原子引用增加。mapped int 随后先被清成 0，再由 caller
写成新 index。

若 key 已存在，四端都不分配新 tree node、不替换 key backing，也不增加第二个 key owner；
caller 仍然无条件覆盖 mapped int。因此 duplicate 的可观察结果是：

```text
key object / Hint address = first insertion
mapped flat-node index    = last insertion
map size                  = one
```

若 tree node allocation 抛异常，map 尚未提交；第一次 label 临时会在 unwind 中释放。由于
flat MotionNode 在 indexed layer getter 和 map insertion 之前已经 append，异常仍会把这个
partial node 留在 deque 中；两个 layer id 尚未申请。

## comparator：null backing 是排序域的一部分

| 目标 | map comparator / content compare | 共同边界 |
|---|---:|---|
| Android arm64 | outer checks + `0x9B07D0` | null < non-null；非 null 用 unsigned UTF-16 三向比较 |
| Android armv7 | `0x4A98C4` | null < non-null；非 null 取三向比较结果 sign bit |
| iOS arm64 | `0x100049AB4` | null < non-null；非 null 取三向比较结果 sign bit |
| iOS armv7 | `0x480F4` | null < non-null；非 null 取三向比较结果 sign bit |

非 null payload 的精确循环为：

```cpp
diff = uint16_t(lhs[0]) - uint16_t(rhs[0]);
while (rhs[i] != 0 && diff == 0) {
    ++i;
    diff = uint16_t(lhs[i]) - uint16_t(rhs[i]);
}
return diff < 0;
```

这意味着：

- 比较大小写敏感；
- `/` 没有特殊意义；
- 顺序按 UTF-16 code unit，不按 UTF-8 byte，也不按 Unicode scalar value；
- surrogate pair 的 high surrogate 参与第一层排序；
- null-backed empty 与 non-null zero-length buffer 不等价。

通常 TJS 字符串 allocator 会把内容为空的普通 String 规范化为 null backing，所以最后一条
主要是低层边界；`TJSAllocVariantStringBuffer(0)` 仍可构造 non-null zero-length backing，
因此它不是不可达状态。

## ABI 与 RB-tree node 布局

| 目标 | Player 中 map 起点 | tree ABI | end/header | key offset | mapped offset | node allocation |
|---|---:|---|---:|---:|---:|---:|
| Android arm64 | `+0x18` | libstdc++ `_Rb_tree` | map `+0x08` | node `+0x20` | node `+0x28` | `0x30` |
| Android armv7 | `+0x0C` | libstdc++ `_Rb_tree` | map `+0x04` | node `+0x10` | node `+0x14` | `0x18` |
| iOS arm64 | `+0x18` | libc++ `__tree` | map `+0x08` | node `+0x1C` | node `+0x24` | `0x28` |
| iOS armv7 | `+0x0C` | libc++ `__tree` | map `+0x04` | node `+0x10` | node `+0x14` | `0x18` |

这些 offset 只用于恢复 native ABI。共享 Web C++ 必须使用 `std::map` 的语义投影，不能硬编码
任一平台的 header/node 布局。

### V248 Player 前缀补证（2026-08-18）

V248 fresh constructor/layout 复核把上表的 map 起点放回完整 declaration context：它直接位于
`rootPlayer,parentPlayer,currentDispatch` 三个 raw pointer 后，后面紧邻 camera position X。
四端 map header 大小分别为 48/28/24/12 bytes，恰好从 `+0x18/+0x0C/+0x18/+0x0C` 延伸到
camera block 的 `+0x48/+0x28/+0x30/+0x18`。portable `_nodeLabelMap` 已迁移到同一源码顺序；
完整 prefix 和 node deque successor 见
`analysis/motionplayer_player_prefix_currentdispatch_nodelabel_camera_bounds_deque_layout_four_binary_2026-08-18.md`。

## raw-label resolver

| 目标 | `Player::findNodeByRawLabel` | map find helper |
|---|---:|---:|
| Android arm64 | `0x6B2EB8` | `0x6EF608` |
| Android armv7 | `0x58220C` | `0x5AD22C` |
| iOS arm64 | `0x100109EEC` | `0x100141A60` |
| iOS armv7 | `0x10777C` | `0x142AC6` |

共同控制流：

```cpp
MotionNode *Player::findNodeByRawLabel(const ttstr &label, bool recursive) {
    auto it = rawNodeLabelMap.find(label); // empty label is allowed
    if (it != rawNodeLabelMap.end())
        return &nodes[it->second];         // no bounds guard

    if (!recursive)
        return nullptr;

    MotionNode *found = nullptr;
    visitChildPlayerDispatches([&](Player *child) {
        found = child->findNodeByRawLabel(label, recursive);
        return found == nullptr;
    });
    return found;
}
```

本地命名继续保留 `_guess`，因为四份 shipped binary 均已 stripped，无法证明原始标识符。
递归 visitor 的 type-3/type-4 顺序与 type-4 element-zero 重复 bug 已在
`motionplayer_recursive_child_visitor_four_binary_2026-08-12.md` 单独闭合；本专题只证明
resolver 与 NodeLabelMap 的连接点。

## 非递归调用面与各自 miss 行为

| 调用面 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | miss / empty 行为 |
|---|---:|---:|---:|---:|---|
| stencil composite target | `0x6B2834` | `0x581DF8` | `0x100109938` | `0x1071C6` | miss 跳过；hit 后只接受 type 0/3 |
| camera constraint anchor | `0x6B9554` | `0x586342` | `0x10010F2E8` | `0x10CAA0` | miss 回 root；empty key 仍可命中 |
| CameraNode target | `0x6BAEE0` | `0x5877E2` | `0x100110970` | `0x10E0C6` | null-backed empty 先跳过；miss target=null、focus=camera |
| motion `dt==4` target | `0x6BBB94` | `0x588234` | `0x100110F7C` | `0x10EAE0` | miss 不产生 computed angle |
| particle `model.dt==4` | `0x6BC428` | `0x5889AC` | `0x100111C20` | `0x10F432` | lookup 前 offset-active 已清 false；miss 保持 false |

Android armv7 与两份 iOS 明确调用 `Player::findNodeByRawLabel(..., false)`。Android
arm64 优化器在五个 update/build site 展开了相同 map-find/deque-index 代码；它仍保留独立的
resolver 函数供递归层查询和 public facade 使用。由三端显式调用与 arm64 等价内联共同反推，
源级公共链应调用 Player member，而不是另造一个 `findNodeByLabel(map, label)` free helper。

更多 public/recursive caller：

- `getLayerGetter`；
- `getLayerMotion`；
- EmotePlayer/D3DEmotePlayer raw-label `contains`；
- child visitor callback 自递归。

这些 caller 都先查当前 Player；只有 local miss 且 `recursive==true` 才进入 child visitor。

## teardown 与 key 生命周期

| 目标 | reset wrapper | map clear site | subtree destroy |
|---|---:|---:|---:|
| Android arm64 | `0x6B2AD8` | `0x6B2DCC` | `0x6DA608` |
| Android armv7 | `0x581F3C` | `0x58214E` | `0x59BD66`（由 `0x5AE708` 调用） |
| iOS arm64 | `0x100109ACC` | `0x100109DFC` | `0x10012A5F0` |
| iOS armv7 | `0x107358` | `0x10767E` | `0x1291CA` |

共同 teardown 顺序不是 `map.clear()` 在最前：

1. retain ResourceManager dispatch；
2. invalidate type-3/type-4 child state，并释放每个旧 node 的 layer ids / active render id；
3. erase flat deque 的非根 suffix，运行 MotionNode 析构；
4. 遍历 NodeLabelMap tree；
5. 对每个 node Release key 的 tTJSVariantString backing；
6. delete tree node；
7. 清 root/leftmost/rightmost/count；
8. release ResourceManager owner。

Android 的 erase helper 递归 right subtree、再沿 left child 迭代；iOS 对 left/right 都递归后
释放 key 并 delete node。遍历形状属于 STL ABI 差异，owner 释放与最终空树语义一致。

## 本地旧实现差异与修复

### 1. 旧单端 helper 身份失效

`PlayerUpdateLayersInternal.h` 原注释把 free helper 对齐到旧 `Player_nodePathMap_find
@0x6F2228`，并引用 `0x6B4CB0` 与 `sub_9B1ED0`。这些不是四个当前参考中的函数身份；
当前 Android arm64 的真实 builder/map-find/content-compare 分别位于上表中的
`0x6B1E4C`、`0x6EF608`、`0x9B07D0`，另外三端又有各自 ABI 实现。

修复：删除旧地址注释和 fabricated free helper；stencil、camera constraint、CameraNode、
motion dt==4、particle dt==4 全部回到共享 `Player::findNodeByRawLabel_guess(..., false)`
调用链。

### 2. comparator 丢失 backing-null 排序

旧 `ttstr_utf16_less` 直接比较两个 `c_str()`。`ttstr::c_str()` 会把 null backing 投影成静态
空串，因此 null-backed empty 与 allocated-empty 被错误合并。

修复：先执行：

```cpp
if (a.IsEmpty()) return !b.IsEmpty();
if (b.IsEmpty()) return false;
```

随后才做 unsigned UTF-16 code-unit 比较。

### 3. particle 多出的 bounds guard

旧本地 particle path 在 lookup hit 后额外要求
`targetIdx < nodes.size()`。四端均通过共享 resolver 直接消费 mapped index，没有这个
graceful guard；camera constraint、CameraNode、motion target 和 stencil 也相同。

修复：particle 改为消费 resolver 返回的 `MotionNode *`。这同时恢复真实 member 调用链并
移除 fabricated 上界保护。

### 4. 回归覆盖

`motionplayer-dll.cpp` 新增/调整边界覆盖：

- null-backed empty 与 `TJSAllocVariantStringBuffer(0)` 产生的 non-null empty 是两个 key；
- null key 排在 non-null empty 前；
- duplicate 写入后 map size 不增、mapped index last-wins；
- duplicate key 仍持有第一次插入对象的 Hint 地址；
- surrogate high code unit `0xD800` 在 UTF-16 排序中小于 `0xE000`；
- camera constraint 可查 empty key，而 CameraNode 的 null-backed empty gate 先跳过查找。

完整测试 TU 已通过 Emscripten 语法编译；当前 Web preset 不提供 Catch2 runtime target，
因此这不应误记为运行时用例已执行。

## IDB 持久化

四份 recovery IDB 已加入：

- NodeLabelMap subscript/emplace、insert/attach、find、comparator、subtree destroy 的
  `_guess` 名称；
- raw insertion、resolver、五类非递归 caller 与 clear site 的精确注释；
- insertion / resolver / teardown 三组书签。

四份最终 `idb_save` 均返回 `ok=true`，写回各自
`out/ida-recovery/{android-arm64,android-armv7,ios-arm64,ios-armv7}` 数据库。

## 验证

- `motionplayer-dll.cpp` 完整 Emscripten response-file 语法检查：通过，仅既有 `_tss`
  deprecated literal-operator warning；
- `cmake --build --preset "Web Debug Build"`：通过，35 steps，44.5 秒，完成
  `libmotionplayer.a`、插件静态库和最终 `index.html` / Wasm link；仅既有 `_tss`、
  Emscripten pthread + memory growth、JSPI 与 JS library warning；
- 编译源码 targeted scan：旧 `0x6F2228`、`0x6B4CB0`、`sub_9B1ED0`、
  `Player_nodePathMap_find`、`findNodeByLabel`、`findCameraNodeTargetIndex` 命中为零；
- targeted `git diff --check`：通过，仅工作树既有 CRLF conversion warning；
- 四份 recovery IDB save：全部 `ok=true`。

Web preset 没有 Catch2 runtime target；因此本节严格区分“完整测试 TU 语法编译成功”与
“运行时用例已执行”，后者本轮未声称。
