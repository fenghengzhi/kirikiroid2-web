# Player 的 LayerGetter producers 与借用生命周期（四参考二进制，2026-08-26）

## 1. 范围

本纵切面从 `Player.getLayerGetter` 与 `Player.getLayerGetterList` 反向闭合
LayerGetter facade 的两个 native producer、raw-label 查找、递归 child/particle 搜索、
flat-node deque 遍历、adaptor 失败元素和借用生命周期。它把 LayerGetter 构造/29 个
getter 的先前切面接到 Player owner 图上。

## 2. 注册与四端函数映射

两个方法在 Player 92 项表中精确为 #84/#85：

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player registrar | `Player_ncb_members@0x6D3DA8` | `...@0x597EC8` | `...@0x1001244F8` | `...@0x123848` |
| `getLayerGetter` | `Player_getLayerGetter_guess@0x6D0CD4` | `...@0x595EF4` | `...@0x100121D64` | `...@0x120B2C` |
| `getLayerGetterList` | `Player_getLayerGetterList_guess@0x6D2368` | `...@0x596CD4` | `...@0x100122DC0` | `...@0x121E18` |
| raw-label resolver | `Player_findNodeByRawLabel_guess@0x6B2EB8` | `...@0x58220C` | `...@0x100109EEC` | `...@0x10777C` |
| LayerGetter CreateAdaptor | `LayerGetter_CreateAdaptor_guess@0x6F2B1C` | `...@0x5AFB24` | `...@0x1001452D0` | `...@0x145B88` |
| Variant wrapper helper | inline | `buildLayerGetterVariant_guess@0x595F20` | `...@0x100121DB0` | `...@0x120B58` |

四个 registrars 的完整 UTF-16LE `getLayerGetter` / `getLayerGetterList` 键以原始字节
和 xref 确认。Android arm64 的 10,800-byte inline registrar 用分段 disasm在
`0x6D60C0..0x6D6220` 取出 callback；其余三端由 compact fresh decompile直接得到。
两个 producer、四个 resolver、四个 CreateAdaptor 和三个独立 wrapper 都在本轮
fresh decompile并写回 IDB 语义名/注释。

## 3. 单对象 producer

共同伪代码：

```text
getLayerGetter(name_by_value):
    node = findNodeByRawLabel(name, recursive=true)
    if node == null:
        return Void

    facade = operator new(sizeof(void *))
    facade.node = node                 // borrowed pointer, no Player AddRef
    return buildLayerGetterVariant(facade)
```

missing label 在 facade 分配前返回 Void。成功时只分配 8/4 字节 facade并写一个 node
pointer；不复制 label、坐标、slot、shape 或 owner，也不保存 node index/generation。

`name` 是 NCB 传入的 `ttstr` by-value owner；resolver 在同步调用期间借用它。递归
visitor closure只在栈上/临时 function object 中保存 name pointer，不把它写入 Player
或 LayerGetter；返回时按普通 ttstr 参数生命周期释放。

## 4. raw-label 查找与递归顺序

`findNodeByRawLabel(name,true)` 的四端共同流程：

1. 先查 Player 的 ordered/raw-label map；命中值是 deque index；
2. 不验证 index 是否非负/小于 deque size，直接按 index 计算 element address；
3. map miss 且 `recursive=false` 时返回 null；
4. map miss 且 `recursive=true` 时，按当前 flat node deque 顺序访问 child Players；
5. 第一个递归命中立即停止完整遍历并向上传回 borrowed MotionNode pointer。

child visitor 对每个 node：

- type 3：从持久 child-player Variant取 native Player，立即递归；
- type 4：保留一次 particle Array dispatch，读一次 count，然后循环 `count` 次；
- 其他 type：跳过。

四端保留一个 shipped bug：type-4 内层循环每次都读取 Array numeric index `0`，不是
循环变量。因此第一个 particle child 被重复搜索 `count` 次，后续元素永远不被这个
共享 visitor 访问。child native pointer 也没有 null guard；错误类型/null adaptor 可在
后续递归调用形成 native null-dereference 边界。

visitor 的外层 deque end 在每次条件检查时重新读取，不是进入函数时快照。底层
Array callback 或 child recursion 若重入并改变容器，后续边界按 live state继续。

## 5. list producer 与 node deque 容器

共同伪代码：

```text
out = fresh TJS Array + borrowed native Items
for index = 1; index < live nodes.size(); ++index:
    facade = new LayerGetter(&nodes[index])
    element = buildLayerGetterVariant(facade)
    Items.emplace_back(element)         // 包括 Void
    destroy local element
return out
```

关键边界：

- index 0 的 synthetic root 永远排除；
- 节点为 0/1 个时仍返回 fresh 空 Array，不返回 Void；
- 不查 label map，不按 label 去重；每个非 root flat node都贡献一个元素；
- `nodes.size()`/deque end 在每轮条件重新读取；
- adaptor 创建返回 null 时 local element 是 Void，仍追加，占据原 index 对应位置；
- incompatible 但非 null shell 作为 object element追加，同时 facade 保持未附着泄漏；
- 返回 Array 长度在无重入修改时精确为 `nodes.size()-1`。

### 5.1 Android node deque

| 目标 | MotionNode element stride | 大对象 block capacity |
|---|---:|---:|
| Android arm64 | 2632 | 1 |
| Android armv7 | 2272 | 1 |

libstdc++ 对大于 512-byte 的 element 使用每 block 一个 node。iterator/indexing 保存
map node pointer、block first/cur/last；跨元素时从下一个 map slot 取独立 heap block。
反编译中的乘法逆元是编译器把除 stride/iterator 常量化后的结果，不是源哈希算法。

### 5.2 iOS node deque

| 目标 | MotionNode element stride | libc++ block capacity | 满 block 字节 |
|---|---:|---:|---:|
| iOS arm64 | 2648 | 16 | 42368 |
| iOS armv7 | 2228 | 16 | 35648 |

libc++ 大对象 deque 固定每 block 16 个 element，通过 `(start+index)/16` 选 block、
`(start+index)%16` 选 element。iOS arm64 机器码以 bit shift/mask实现相同公式；
armv7 用 word shift/mask。portable 源应继续用 `std::deque<MotionNode>`，不能硬编码
这些 ABI stride，但完整容器账本必须保留其边界。

## 6. CreateAdaptor 与失败 owner

两个 producer 都把预先分配的 facade交给
`ncbInstanceAdaptor<LayerGetter>::CreateAdaptor(facade,false,false)`：

1. 用 LayerGetter ClassInfo class object以一个 Void 参数创建空 adaptor shell；
2. class object不存在、CreateNew 错误/null、底层抛出：facade 不删除；
3. shell 成功且 class-specific adaptor存在：写 facade pointer，non-sticky adaptor成为
   facade owner；
4. shell 非 null但 adaptor 不兼容：返回 shell，facade 不附着也不删除；
5. wrapper 把非 null shell 作为 Object+ObjThis Variant，各 AddRef一次，再平衡 shell
   的局部引用；null 结果构造 Void。

因此成功附着后，脚本对象 Invalidate/析构删除 facade，但只删除这一指针壳；facade
绝不删除/Release MotionNode或 Player。普通失败泄漏与 shape CreateAdaptor 路径一致。

## 7. 借用生命周期与悬空边界

owner 图：

```text
Player
  owns std::deque<MotionNode>
       ^
       | raw borrowed pointer (no ref/index/generation)
LayerGetter facade
  owned by non-sticky ncb adaptor
  owned by returned script object Variant
```

返回 Variant可以比 Player活得更久，因为它没有对 Player/node deque增加引用。以下
操作会让仍存活的 getter悬空：Player析构、node deque clear/replacement、删除/重建
树，或任何使对应 element生命周期结束的路径。std::deque 端部增长通常保持已有
element引用，但这不是 getter保存 generation 的保证；清空/擦除/owner销毁仍无检查。
29 个 getter 已证明每次直接解引用 raw pointer，所以悬空不会安全退化为 Void。

精确 Player析构中 `_nodes` 相对其他 owner 的逆序销毁位置属于完整 Player/MotionNode
生命周期切面；本报告只闭合“producer不持有 Player、adaptor只持有 facade”这条边。

## 8. 本地逐行对照

`PlayerLayerQuery.cpp` 当前：

- `findNodeByRawLabel_guess` 先查 map、无 index gate，并在 recursive miss 时走共享
  child visitor；
- visitor按 live deque end遍历，type-4 每次读 index 0，type-3 直接递归；
- `getLayerGetter` missing 时 Void，命中后 `new LayerGetter(&node)`；
- `buildLayerGetterVariant` 保留 non-sticky CreateAdaptor 的所有普通失败泄漏；
- `getLayerGetterList` 从 index 1 循环，重新读取 `_nodes.size()`，不去重，并把失败
  Void 元素照常追加。

正常与边界语义逐项一致，不修改运行 C++。

## 9. 异常与剩余项

正常失败返回、null/incompatible shell 和 borrowed owner 已闭合。四端 exception table
对 facade `operator new`、CreateNew 抛出、Array append 抛出时各局部 Variant/Array
清理的精确 landing-pad frontier 尚未全部展开；尤其 Android armv7 extab 与 iOS
arm64 LSDA 仍需独立审计。完整 Player析构、node-tree replacement 和 deque erase/
copy owner 顺序也进入 MotionNode/Player lifecycle 总账。

## 10. 2026-08-27 producer EH 闭包

`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md` 已重新读取四端
single/list/adaptor 与三端独立 wrapper，并补齐 Android arm64 landing、iOS arm64
LSDA-only cold cleanup和 iOS armv7 SjLj cleanup。三端会析构 active return/current element
Variant与 outer Array；Android armv7无本帧 cleanup。CreateAdaptor抛出时 raw facade和
global dispatch引用四端共同泄漏，普通 null/incompatible规则不变。该 EH row 现为
`IMPLEMENTED`；完整 MotionNode source order仍由独立条目承接。
