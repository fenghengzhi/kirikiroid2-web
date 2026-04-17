---
name: Player_updateLayers phase2 累加逻辑权威伪代码
description: libkrkr2.so sub_6BB33C phase2 节点累加的完整伪代码、inheritMask 位语义表、independentLayerInherit 三路分歧路径，以及 m2logo bug 的推导分析
type: project
---

# Player_updateLayers @ 0x6BB33C — phase2 节点累加

## inheritMask (node+40) 位语义 (PSB key "inheritMask")
- 0x004 flipX 继承 (XOR parent)
- 0x008 flipY 继承 (XOR parent)
- 0x010 angle 继承 (加 parent)
- 0x020 scaleX 继承 (乘 parent)
- 0x040 scaleY 继承 (乘 parent)
- 0x080 slantX 继承 (加 parent)
- 0x100 slantY 继承 (加 parent)
- 0x400 opacity 第二乘源选择：set=乘 parent，unset=乘 root
- 0x00400000 (node+42 & 0x40) = "joinTarget/skip-parent" 位，parent 透传给祖父

**注意**: 没有位控制 posX/Y/Z 累加——**位置累加无条件发生**。

## phase2 三路矩阵累加分歧
条件: `(~inheritMask & 0x1FC)` 检查 bits 0x004..0x100 (7 个属性位) 是否**全部**置位

1. **SIMPLE 路径** (`~v46 & 0x1FC == 0`, @0x6BB848): 全部属性都 inherit → `sub_699940` + 全属性 parent-merge + matrix mul parent×local
2. **COMPLEX + independentLayerInherit=TRUE** (@0x6BB918 LABEL_68): 对 SET 位 inherit parent；**仅 sub_699940，NO parent×local matrix mul**。结果 node.m 保持 local-only。
3. **COMPLEX + independentLayerInherit=FALSE** (@0x6BB9BC LABEL_76): 对 SET 位：undo root → sub_699940 → re-apply root → parent×local matmul. 4-phase。

## position 累加（无 mask gate, @0x6BB744-0x6BB7E4）
```
if parent.coordinateMode != 0:   // 3D
    accum.posX = parent.m11*localX + parent.m12*localZ + parent.accum.posX
    accum.posY =                              localY   + parent.accum.posY
    accum.posZ = parent.m21*localX + parent.m22*localZ + parent.accum.posZ
else:                            // 2D (默认)
    accum.posX = parent.m11*localX + parent.m12*localY + parent.accum.posX
    accum.posY = parent.m21*localX + parent.m22*localY + parent.accum.posY
    accum.posZ =                              localZ   + parent.accum.posZ
```

## parent lookup (@0x6BB598-0x6BB5BC)
```
parent = node.parent
while (parent.inheritMask & 0x00400000) != 0:  // bit 22 = joinTarget transparent
    parent = parent.parent
```

## 关键字段偏移 (node 结构 2632 字节)
- node+40 inheritMask (int32)
- node+42 byte = inheritMask byte 2; bit 6 = 0x00400000 joinTarget
- node+84..96 transformOrder[4] (int32×4)
- node+120/128/136/144 m11/m21/m12/m22 (唯一 matrix 存储，local+world 复用)
- node+1392 activeSlotIdx (int32)
- node+1504..1576 accum block (active, visible, flipX/Y, posX/Y/Z, angle, scaleX/Y, slantX/Y, opacity)
- node+1584..1656 delta/override block (对应 accum 偏移+80)
- Player+1097 independentLayerInherit (byte) — 全树共享 gate

## m2logo bug 关键推导
- bug 症状: 9 个 cheeseware type=0 子节点 (inheritMask=0x79c, 缺 0x40) 的 accum.pos=(0,0,0)、accum.m=identity
- **矩阵 identity 在 COMPLEX+independentLayerInherit=true 下是正确**（sub_699940 对 scaleX=scaleY=1/angle=0/slant=0 输出 identity）
- **pos=(0,0,0) 违反伪代码的无条件加 parent.pos**，原因最可能是：
  1. 本地 parentIndex 链错误，parent 实际指向 root 而非 str_locate
  2. Player_evaluateTimeline 首帧返回 0 导致跳过累加，accum 保留默认 0
- libkrkr2.so 没有按 node.type 分叉的累加路径；type=0/type=2 走同样的 phase2 主逻辑
- position 累加"绝对无 mask gate"，0x40 仅控制 scaleY，与位置无关

## Skip-parent (joinTarget) 陷阱
0x00400000 是 inheritMask 的 bit 22 (byte index 2 的 bit 6)。本地代码把它当做 parent lookup 的 while 条件是对的。str_locate 的 inheritMask=0x20007fc **不**含 0x400000，不应被 skip。若本地实现误把 0x20 或 0x40 当 skip 位，会让 parent 错误跳到 nodes[0]。

## 调用的子函数
- sub_699940 @0x699940 — calcLocalMatrix，按 transformOrder 生成 2x2 到 node+120..152
- sub_6BBE20 @0x6BBE20 — eventList pulse，用 TJS Array 访问 event 对象
- sub_699AE4 @0x699AE4 — Player_evaluateTimeline，lerp 两个 slot，返回 bool 是否需更新
- sub_69AE74 @0x69AE74 — mesh deformation + angle/scale from Bezier gradient
- sub_6BAA10 @0x6BAA10 — TJS onGroundCorrection callback
