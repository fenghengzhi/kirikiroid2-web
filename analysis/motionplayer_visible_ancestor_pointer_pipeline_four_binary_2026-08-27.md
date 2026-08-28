# MotionNode visible-ancestor raw-pointer pipeline 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端共同结构是 `MotionNode *visibleAncestor`，不是本Player deque内的整数index。visibility producer
按parent-first order构造指针链；type-3与particle child把父Player node pointer复制到child synthetic
root；child visibility再把它传播到child nonroot nodes；prepared-item builder直接解引用该pointer并
取得/创建ancestor持有的persistent item。

这是跨Player借用链。整数surrogate在父/子Player恰好有同index时可能伪装成功，但一般会错误索引
child自己的deque，既改变topology也引入native不存在的range行为。本轮恢复真实pointer并删除
PreparedRenderItem上未被runtime消费的portable visible-index sidecar。

## 2. visibility producer 四端完整函数

| 平台 | visibility | 完整指令 | node pointer field | drawFlag |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6BACBC` | 81 | `+0x7A0` | `+0x7A8` |
| Android armv7 | `0x58762C` | 89 | `+0x698` | `+0x69C` |
| iOS arm64 | `0x1001107BC` | 66 | `+0x7B0` | `+0x7B8` |
| iOS armv7 | `0x10DF88` | 64 | `+0x674` | `+0x678` |

四端均fresh decompile并完整读取disassembly；全部cursor `done=true`。LP64字段是8-byte store/load，
ILP32是4-byte store/load，且值来自node地址或已有pointer；这直接排除logical int index解释。

## 3. producer伪代码与drawFlag

```text
for each nonroot node in physical parent-first order:
    parent = nodes[node.parentIndex]               // unchecked
    node.visibleAncestor = parent.drawFlag
        ? &parent
        : parent.visibleAncestor

    if activeSlot.done || stencilType==0 || !accumulated.active:
        node.drawFlag = false
    else if forceVisible || ((preview ? 0x1809 : 0x1801) & (1<<nodeType)):
        node.drawFlag = source.valid
    else:
        node.drawFlag = true
```

十进制mask为6153/6145。pointer store发生在本node drawFlag计算之前，读取parent当前frame已经发布的
drawFlag。root不遍历，constructor/root转发写入的pointer与root drawFlag保持原值。

parentIndex、nodeType shift和source fields均信任；没有bounds/type guard。pointer可为null、self或
任意stale address，producer不验证。

## 4. type-3与particle跨Player转发

四端motion-sub完整函数为833/760/709/921条；在child frameProgress/updateLayers之前依次发布：

```text
childRoot.clipAABB = parentMotionNode.clipAABB
childRoot.meshAncestor = separator ? &parentMotionNode : parentMotionNode.meshAncestor
childRoot.visibleAncestor = parentMotionNode.visibleAncestor
```

四端particle-system完整函数为1290/1234/1112/1452条；编译器将两遍child worker展开在同一root，
每个child同样在frameProgress/updateLayers之前把particle node的visible pointer复制给child root。

随后child visibility从root继承该raw pointer。pointer指向parent Player的deque element，而不是child
index；这正是跨Player draw topology的source-level结构。两个Player没有互相AddRef，生命周期只由
外层递归/particle traversal时序保证。

## 5. prepared-item consumer

四端recursive builder完整函数为1507/944/820/1034条。type-3 wrapper和ordinary item各有一个
consumer，顺序共同为：

```text
ancestorItem = null
if node.drawFlag/path eligible and node.visibleAncestor != null:
    ancestor = *node.visibleAncestor                  // unchecked
    if ancestor.preparedItem == null:
        ancestor.preparedItem = new PreparedRenderItem
    ancestorItem = ancestor.preparedItem
currentItem.parentItem = ancestorItem
```

ordinary path在source/color/opacity/stencil字段之后、paint/clip geometry之前消费pointer；type-3
wrapper在aux append之后消费。allocation异常保留此前partial item/list状态。self pointer合法，会让
item.parentItem指向自身；cross-Player pointer合法，会指向父Player node-owned item。没有generation、
deque ownership、range或self guard。

Native PreparedRenderItem不另存ancestor index/pointer，只保留最终borrowed `parentItem`。因此local
derived `visibleAncestorIndex`没有runtime消费者，也不是ABI桥接必需状态。

## 6. 生命周期与失效边界

MotionNode deque保证元素地址在普通append时稳定；old-tree suffix erase/Player root clear会销毁节点。
visibleAncestor不拥有目标，不AddRef、不在MotionNode destructor中Release。正常递归调用期间parent
node早于child更新存在，prepared items由各自node唯一拥有。

re-entrant脚本若在pointer发布后替换/销毁相关tree，后续builder直接解引用stale pointer，属于native
sharp boundary。整数化或重新按label/index解析会意外延长/改变该边界，不能作为安全修复。

## 7. 本地偏差与证据后实施

修改前local：

- `MotionNode::visibleAncestorIndex`以-1为null；
- visibility在parent index和parent surrogate间选择；
- child root复制integer；
- builder用当前Player `_nodes[index]`重新解析；
- PreparedRenderItem再保存一个unused integer sidecar。

完成producer与三个consumer家族的fresh四端完整证据后，已实施：

- MotionNode字段改为borrowed `MotionNode* visibleAncestor=nullptr`；
- selector直接返回`&parent`或`parent.visibleAncestor`；
- type-3/particle child root直接复制pointer；
- builder直接解引用pointer，保留null/self/cross-Player与异常commit顺序；
- 删除PreparedRenderItem integer sidecar；
- 单元用例改为null、parent pointer、self pointer和cross-Player pointer，不再写已删除的node ordinal。

## 8. IDB与验证

四端visibility和particle-system函数已统一命名；visibility、motion-sub、particle-system与builder均添加
pointer数据流注释，visibility添加bookmark，四库保存。

实施后执行`rg`确认compiled source/tests不再存在`visibleAncestorIndex`或MotionNode `.index`，并执行
`git diff --check`、coverage严格12列与duplicate-ID检查。当前缺CMake/Ninja/Emscripten正式工具链，
不能声称unit/Web build通过。

