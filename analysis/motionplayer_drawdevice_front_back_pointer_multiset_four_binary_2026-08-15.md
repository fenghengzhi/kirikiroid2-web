# DrawDevice FrontItems / BackItems pointer multiset 四参考恢复

日期：2026-08-15

范围：`reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、iOS armv7
四份 motionplayer 参考二进制。本轮专门复核 DrawDevice root 的 `FrontItems`、
`BackItems` 节点载荷、比较器、重复值、等价区间删除、索引重排顺序和异常边界。

## 1. 结论

两张树都不是旧恢复所写的：

```cpp
std::multimap<tjs_int, D3DLayerObject *>
```

四端共同支持的源级结构是：

```cpp
struct FrontItemLess_guess {
    bool operator()(const D3DLayerObject *left,
                    const D3DLayerObject *right) const {
        return left->getFrontIndex() < right->getFrontIndex();
    }
};

struct BackItemLess_guess {
    bool operator()(const D3DLayerObject *left,
                    const D3DLayerObject *right) const {
        return left->getBackIndex() < right->getBackIndex();
    }
};

std::multiset<D3DLayerObject *, FrontItemLess_guess> FrontItems;
std::multiset<D3DLayerObject *, BackItemLess_guess> BackItems;
```

比较器的原始类型名已被 strip，因此本地名字必须保留 `_guess`。但是以下语义不是猜测：

- 每个节点只有一个 `D3DLayerObject *` 载荷，没有独立整数 key；
- Front 比较器从两个 pointer 各自指向的对象读取 front index 并做 signed `<`；
- Back 比较器对应读取 back index；
- 相同 index 被视为等价；插入不执行唯一性拒绝，因此是 multiset，不是 set；
- `equal_range(object)` 同样通过 lookup object 的实时 index 搜索；
- 等价区间中再用 pointer identity 找到目标，只删除第一个目标节点。

这纠正了项目中从旧 `libkrkr2.so` 方向遗留的 `multimap<int, pointer>` 注释和实现。

## 2. 节点大小与唯一载荷

| 目标 | 插入证据 | `operator new` 大小 | 节点 payload 偏移 | payload |
|---|---:|---:|---:|---|
| Android arm64 | `AddChild` `0x529CFC`；front `0x529DA8`，back `0x529E3C` | `0x28` | `+0x20` | child pointer |
| Android armv7 | front `0x49532E` / `0x495368`；back `0x4953BE` / `0x4953F8` | `0x14` | `+0x10` | `*valuePointer` |
| iOS arm64 | front `0x1002337A8`；back `0x100233894` | `0x28` | `+0x20` | `*valuePointer` |
| iOS armv7 | front `0x2325E0`；back `0x232658` | `0x14` | `+0x10` | `*valuePointer` |

64 位节点的 `0x20` 字节树基部加一个八字节 pointer，恰为 `0x28`；32 位节点的
`0x10` 字节树基部加一个四字节 pointer，恰为 `0x14`。反之，如果载荷是
`pair<const int, D3DLayerObject *>`，节点还必须容纳整数 key、对齐和 pointer，四端都不会
得到上述分配大小。

载荷写入也直接排除了 map：

- Android arm64 在 `0x529DC4` / `0x529E58` 把 child pointer 直接写到 node `+0x20`；
- Android armv7 在 `0x4953A2` / `0x495432` 把 `*a4` 直接写到 node `+0x10`；
- iOS arm64 在 `0x1002337D0` / `0x1002338BC` 把 `*a2` 直接写到 node `+0x20`；
- iOS armv7 在 `0x2325F2` / `0x23266A` 把 `*a2` 直接写到 node `+0x10`。

没有任何相邻 key store。

## 3. 两套 STL ABI 的容器与节点布局

root 内两张容器的起点和大小保持此前恢复的四端对象布局：

| 目标 | FrontItems | BackItems | 单容器大小 | ABI 形态 |
|---|---:|---:|---:|---|
| Android arm64 | root `+0x48` | `+0x78` | `0x30` | libstdc++ tree header |
| Android armv7 | root `+0x2C` | `+0x44` | `0x18` | libstdc++ tree header |
| iOS arm64 | root `+0x48` | `+0x60` | `0x18` | libc++ begin/end-node tree |
| iOS armv7 | root `+0x2C` | `+0x38` | `0x0C` | libc++ begin/end-node tree |

节点的共同逻辑布局为：

```text
Android 64: color/pad, parent, left, right, object                       // object +0x20
Android 32: color,     parent, left, right, object                       // object +0x10
iOS 64:     left, right, parent, black-byte/pad, object                  // object +0x20
iOS 32:     left, right, parent, black-byte/pad, object                  // object +0x10
```

因此“Android 与 iOS 容器对象大小不同”只来自 STL tree/header ABI，不表示两端有不同
key/value 语义。

## 4. 比较器直接读取实时对象字段

四端索引偏移一致：64 位对象 front/back 为 `+0x18/+0x1C`，32 位为
`+0x0C/+0x10`。

Android arm64 的 `AddChild` 最直观：

```text
newFront = child->front_index
oldFront = node->object->front_index
if newFront < oldFront: descend left
else:                   descend right

newBack = child->back_index
oldBack = node->object->back_index
if newBack < oldBack: descend left
else:                 descend right
```

对应加载集中在 `0x529D50..0x529D90` 与 `0x529DE4..0x529E24`。Android armv7
front/back helper 在 `0x49533C..0x49534E`、`0x4953CC..0x4953DE` 做同一双重
解引用。iOS arm64 在 `0x1002337E0..0x100233804`、
`0x1002338CC..0x1002338F0`，iOS armv7 在 `0x2325FC..0x232610`、
`0x232674..0x232688` 也完全一致。

等价值走右支，并且每次都会继续分配节点，没有 `set::insert` 的等价拒绝检查。这是
`std::multiset` 的 equal insertion 路径。

## 5. erase：equal_range 后按 pointer identity 删除首个

| 目标 | EraseFront | EraseBack | equal-range helper |
|---|---:|---:|---|
| Android arm64 | `0x529F78` | `0x52A18C` | 两个函数内联 lower/upper bound |
| Android armv7 | `0x492474` | `0x49253C` | `0x49544E` / `0x495508` |
| iOS arm64 | `0x10023031C` | `0x100230440` | `0x100233980` / `0x100233AC8` |
| iOS armv7 | `0x22F336` | `0x22F3CC` | `0x2326CE` / `0x2327A6` |

精确源级语义为：

```cpp
auto range = items.equal_range(object);
for(auto it = range.first; it != range.second; ++it) {
    if(*it == object) {
        items.erase(it);
        return true;
    }
}
return false;
```

例如 Android arm64 `EraseFront` 的界限搜索在 `0x529F8C..0x52A010` 反复执行
`node->object->front_index` 与 `object->front_index` 的比较；之后
`0x52A018..0x52A030` 才逐节点检查 `node->object == object`。Back 函数只把字段改为
`+0x1C`。其余三端是相同状态机。

删除只覆盖第一个 pointer-identical 节点。它不会删除同一对象的所有重复项，也不会只因
index 等价而误删另一个对象。

## 6. index setter 的真实时序

| 目标 | setFrontIndex | setBackIndex |
|---|---:|---:|
| Android arm64 | `0x529E8C` | `0x52A0A0` |
| Android armv7 | `0x49241C` | `0x4924E4` |
| iOS arm64 | `0x1002302B4` | `0x1002303D8` |
| iOS armv7 | `0x22F304` | `0x22F39A` |

四端顺序严格一致：

```text
if oldIndex == newIndex:
    return
if Parent == null:
    object.index = newIndex
    return

Parent->EraseFront/Back(object)       // 返回值被忽略
object.index = newIndex               // 必须在新 insert 之前
Parent->FrontItems/BackItems.insert(object)
```

删除必须发生在字段写入前，因为查找 key 就是对象自己的旧字段；插入必须发生在字段写入
后，因为新节点比较时同样读取该字段。旧 `multimap` 恢复虽然碰巧保持了这段控制流，却把
最关键的“key 不是快照，而是 pointer 指向对象里的实时字段”丢失了。

## 7. 重复节点、失序与异常边界

### 7.1 同一对象可以重复插入

`AddChild` 不做去重。等价插入路径总是分配，故同一个 pointer 可以在两张 multiset 中
各出现任意多次。`remove`、析构、parent setter 和 index setter 每次每张表都只删一个。

### 7.2 同一对象的残留重复节点会被原位改 key

关联容器要求节点参与比较的 key 在驻留期间保持不变。这里的 key 是
`node->object->FrontIndex/BackIndex`。若同一 object 有多个节点，setter 只删掉一个，随后
字段写入会同时改变所有残留节点看到的 key，却不会把这些残留节点从原位置摘下重排。
结果是树的有序不变量被破坏；后续 `equal_range`、insert、遍历或 erase 的行为不再能用
正常有序树语义保证。

同样地，只要某条异常/外部破坏路径让 `EraseFront/Back` miss 但旧节点仍存在，setter 仍会
写字段并插入新节点，也会造成“旧节点原位变 key + 新节点新增”的失序状态。旧文档所写的
“stale 整数 key 节点仍在另一个 key 下但树仍有序”不适用于真实 pointer multiset。

### 7.3 setter 插入失败

setter 先删除旧节点，再写新 index，最后才为新节点分配。若 `operator new` 抛出：

- `Parent` 仍非 null；
- 对象字段已是新 index；
- 该对象从对应 multiset 缺席；
- 没有事务回滚，也不调用 detach/changed/window update。

Front 与 Back setter 独立运行，因此 UpdateSettings 处理 front 成功后，后续 back getter 或
back 重排失败时，两张树可以处于不同代次。

### 7.4 AddChild 的部分提交

`AddChild` 的顺序是 child detach hook、可选 parent-has-parent hook、Front insert、Back
insert、root changed hook。若 Front 已成功而 Back 的节点分配抛出，Front 节点保留，Back
节点缺席，changed hook 不执行。前两个虚调用抛出时还没有插入；Front 分配抛出时两张表
都没有新节点。

### 7.5 remove 不清 Parent

公开 remove 每张表只删一个节点；任一成功才调用 detach 与 changed hook，但不把对象的
`Parent` 清零。对象之后析构会沿保留的 Parent 再次尝试 erase。存在重复节点时，这可继续
逐次删除；root 已析构时则是悬空 parent 的生命周期风险。

## 8. 本地恢复影响

`cpp/plugins/DrawDeviceD3D.cpp` 已完成以下同步：

- 两张 `multimap<tjs_int, D3DLayerObject *>` 改为带独立 front/back comparator 的
  `multiset<D3DLayerObject *>`；
- AddChild 和 setter 改为只插入 pointer；
- EraseFront/Back 改为 `equal_range(object)`，并以 `*iterator == object` 检查 identity；
- getChildren、UpdateObjects、capture、Show 改为直接遍历 pointer value；其中 V270 又确认
  getChildren 通过 `ncbArrayAccessor` 取得 native `tTJSArrayNI::Items`，直接
  `emplace_back(ScriptOwner,ScriptOwner)`，没有 `PropSetByNum` script dispatch；
- 注释明确记录 live-key 与重复节点失序边界。

这项修改不改变四个 root 中 FrontItems/BackItems 的成员偏移或容器对象大小，只修正节点
payload、比较器和所有操作的源级语义。

getChildren 的 owner snapshot/live-field重读、exact IsValid、deque block ABI、返回 closure和异常
cleanup 完整证据见
`motionplayer_drawdevice_getchildren_native_array_items_live_owner_deque_lifecycle_four_binary_2026-08-21.md`。
