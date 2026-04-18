# Player_updateLayers phase2 累加逻辑深度反编译 (libkrkr2.so)

## 目标函数
- **Player_updateLayers**: `sub_6BB33C` at `0x6BB33C`
- **Player_evaluateTimeline**: `sub_699AE4` at `0x699AE4`
- **sub_699940** (calcLocalMatrix): `0x699940`
- **sub_6BBE20** (eventList pulse): `0x6BBE20`
- **sub_69AE74** (mesh deformation / onGroundCorrection trigger): `0x69AE74`
- **sub_6BAA10** (TJS onGroundCorrection): `0x6BAA10`
- **sub_6B3C78** (PSB node init — reads `inheritMask` at node+40): `0x6B3C78`

字段偏移量（Player 对象 + node 对象部分字段）是 libkrkr2.so 反编译结果的权威表示，分析对应的节点结构如下：

```
// Node 关键字段 (tsukihime-size 2632 bytes per node)
node + 24  : int32_t coordinateMode          ("coordinate" PSB prop)
node + 28  : int32_t type                    ("type" PSB prop)
node + 36  : int32_t parentIndex (signed, 0 when root)
node + 40  : int32_t inheritMask             ("inheritMask" PSB prop)
node + 42  : byte    inheritMask[2] — 低位段 bit 22 (0x00400000)
             → 用作 "joinTarget / skip-parent" gate
             (0x6B3EF0 另独立读 "joinTarget" → node+46 bool)
node + 44  : byte    dirty/needUpdate gate
node + 47  : byte    groundCorrection
node + 52  : int32_t stencilType
node + 120 : double  m11 (localMatrix — calcLocalMatrix out, 2×2 at 120..152)
node + 128 : double  m21
node + 136 : double  m12
node + 144 : double  m22
// === accum block 1 (pos) ===
node + 1504: byte    accum.dirty
node + 1505: byte    accum.active
node + 1506: byte    accum.visible
node + 1507: byte    accum.flipX
node + 1508: byte    accum.flipY
node + 1512: double  accum.posX    ← phase2 中 +1512/+1520/+1528
node + 1520: double  accum.posY
node + 1528: double  accum.posZ
// === accum block 2 (transform scalars) ===
node + 1536: double  accum.angle
node + 1544: double  accum.scaleX
node + 1552: double  accum.scaleY
node + 1560: double  accum.slantX
node + 1568: double  accum.slantY
node + 1576: int32   accum.opacity
// === delta/override block at +1584..+1656 ===
node + 1584: byte    dirtyFlag (reset 标记)
node + 1585: byte    active_override
node + 1586: byte    visible_override
node + 1592: double  posX_delta  ← vaddq_f64(+1592, +1512) at 0x6BB6EC
node + 1600: double  posY_delta
node + 1608: double  posZ_delta
node + 1624: double  scaleX_delta ← vmulq_f64(+1624, +1544) at 0x6BB6A4
node + 1640: double  slantX_delta ← vaddq_f64(+1640, +1560) at 0x6BB6B8
node + 1656: int32   opacity_delta ← *(+1576) = *(+1656)*(+1576)/255 @ 0x6BB6D4
```

Player 对象关键字段（同 `analysis/Player_Class_Layout_libkrkr2so.md`）：

```
Player + 24  : int32_t playerType
Player + 200 : Node*   rootNode pointer (v3 in updateLayers)
Player + 208/216/224/232/240/256 : deque<Node> 内部指针（随机访问所用）
Player + 456 : double  currentTime
Player + 480 : byte    coordRootDirty (某 flush 标记)
Player + 528 : ???     (v99 = a1+528 传给 sub_699940 作为 workArea)
Player + 592 : double  deltaTime                 ← 0x6BB38C: velocity × 它
Player + 608/610/613 : 运行时重入/启动标记
Player + 784 : double  velocityX ("velX")       ← 0x6BB390
Player + 792 : double  velocityY
Player + 800 : double  velocityZ
Player + 908 : byte    isEmoteMode
Player + 1097: byte    independentLayerInherit  ← 最重要 gate
```

---

## Player_updateLayers 整体流程伪代码

```pseudo
Player_updateLayers(a1 /*Player*/):

  // === phase-pre : 根节点速度 / 阻尼 ===                     @ 0x6BB33C..0x6BB428
  root = a1->rootNode                                          // +200
  a1->608 = 0                                                  // 清脏标
  if a1.velX != 0: root.1584=1; root.posX += a1.dt * a1.velX   // +1592 不是，这里是 +1592 就是…
  // 实测：+1592 += dt*velX   (root 的 delta slot)
  // 同理 velY/velZ 加到 +1600/+1608
  if a1.damp != 1.0:  dampPow = pow(damp, dt/60)
                      a1.velX *= dampPow; a1.velY *= dampPow; a1.velZ *= dampPow

  // === phase-1a : 把所有非 root 节点的 +1504 拷回 +1512 系列 === @ 0x6BB42C..0x6BB4D4
  // (*(QWORD)(node+192)= *(QWORD)(node+1528);
  //  *(OWORD)(node+176)= *(OWORD)(node+1512);)
  // — 这是把上一帧的 accum pos 拷到 lastFramePos (+176..+192) 用作速度/惯性差分

  // === phase-1b : 用 root 的 delta block 初始化 root 的 accum ===
  memcpy(root+1504, root+1584, 0x50)                           // @ 0x6BB4E0
  root+1584 = 0  (清掉 root delta dirty)

  // === phase-1c : eventList pulse (onEvent) ===
  sub_6BBE20(a1)                                               // @ 0x6BB4EC

  // === phase-1d : root 的 localMatrix 计算 ===
  if !a1.isEmoteMode: sub_699940(root, a1+528)                 // @ 0x6BB508
  // sub_699940 使用 transformOrder[4] 里的 0/1/2/3 依次 flipXY/rotate/scale/slant
  // 结果写到 node+120..152 (m11, m21, m12, m22)

  // === phase-2 : MAIN LOOP — 非 root 节点累加 ===             @ 0x6BB524..0x6BBB6C
  for i in 1..numNodes-1:
      node = nodeAt(i)
      // 2.1 向上寻找"透传 parent"
      parent = node.parent
      while (parent+42 & 0x40) != 0:   // = inheritMask & 0x00400000 (joinTarget)
          parent = parent.parent
      // 注意：root 的 inheritMask bit22 永远为 0，不会跨过 root

      // 2.2 决定是否评估
      v25 = a1+610 || node+47 || parent+1504 || node+1584
            // 即：Player.forceDirty | groundCorrection | parent.accum.dirty | node.delta.dirty
      if !Player_evaluateTimeline(node, v25, a1.currentTime):
          continue                                             // 当前帧没变，跳过累加

      // 2.3 取 active slot（两 slot 其中 clip_startTime<=t 的那个）
      activeIdx = node.activeSlotIdx                          // +1392
      clipSlot  = node + 536*activeIdx                        // 每 slot 536 字节

      node.1584 = 0  // delta-dirty 清

      // 2.4 clipSlot+344 ("hasSync") 分支
      if clipSlot[344]:
          // "sync" 节点：继承 parent accum 全量
          memcpy(node+1504, parent+1504, 0x50)                 // @ 0x6BB618
          node.flags.update(...)                               // reactive flags
          node.m11..m22 = parent.m11..m22                      // @ 0x6BB784
          continue                                             // LABEL_19: 下一节点

      // === 2.5 常规 accum 路径（非 sync） ===                 @ 0x6BB630..0x6BBB6C
      // 2.5.1 dirty/active/visible 组合
      if node.visibleOverride: LOBYTE(v29) = parent.accum.visible != 0
      node.accum.dirty     = 1
      node.flags.flipX    ^= node.delta.flipX
      node.accum.flipX     = v31 ^ v30
      node.accum.visible   = v29
      node.accum.active    = v29 & (parent.dirty? != 0)
      // 2.5.2 delta → accum 合并 (pre-matrix)            @ 0x6BB6A4..0x6BB700
      accum.scale2  = delta.scale2  * accum.scale2       // vmulq_f64(+1624,+1544)  — scaleX/scaleY
      accum.slant2  = delta.slant2  + accum.slant2       // vaddq_f64(+1640,+1560)  — slantX/slantY
      accum.opacity = delta.opacity * accum.opacity /255  // (+1656 × +1576) / 255
      accum.pos1    = delta.pos1    + accum.pos1         // vaddq_f64(+1592,+1512)  — posX/posY
      accum.pos2    = delta.pos2    + accum.pos2         // vaddq_f64(+1608,+1528)  — posZ + (something @+1536)
      // 2.5.3 mesh deformation (parent has meshType)
      if parent.meshType != 0: sub_69AE74(parent, node)  // @ 0x6BB714

      // 2.5.4 position transform via parent's matrix    @ 0x6BB718..0x6BB7E4
      lx = accum.posX; ly = accum.posY; lz = accum.posZ
      if parent.coordinateMode != 0:   // "coordinate"==1 (3D)
          v39 = parent.m11*lx + parent.m12*lz            // posX contribution
          v40 = parent.m21*lx + parent.m22*lz            // posZ contribution
          accum.posX = v39 + parent.accum.posX
          accum.posY = ly  + parent.accum.posY           // Y 直通不变换
          accum.posZ = v40 + parent.accum.posZ
      else:                             // 2D (coordinateMode==0)
          v39 = parent.m11*lx + parent.m12*ly
          v38 = parent.m21*lx + parent.m22*ly
          accum.posX = v39 + parent.accum.posX
          accum.posY = v38 + parent.accum.posY
          accum.posZ = lz  + parent.accum.posZ           // Z 直通不变换

      if node.groundCorrection: sub_6BAA10(a1, node, parent)   // TJS callback

      // 2.5.5 opacity 第二重 multiply              @ 0x6BB7FC..0x6BB830
      v46 = node.inheritMask
      v47 = parent
      if (v46 & 0x400) != 0 || !a1.independentLayerInherit:
          if (v46 & 0x400)==0:
              v47 = root                              // fall back to root when inherit=true
          node.accum.opacity = v47.accum.opacity * node.accum.opacity / 255

      // === 2.5.6 matrix/attr 累加 —— 三路分歧 ===   @ 0x6BB83C..0x6BBB6C
      if (~v46 & 0x1FC) == 0:
          // -------- SIMPLE 全位 set 路径 --------   @ 0x6BB848..0x6BB8EC
          // v46 的 bits 0x1FC (2..8) 全部为 1 → 所有属性都继承
          sub_699940(node, workArea)                   // compute node.m11..m22 from localState
          accum.flipX ^= parent.flipX                 // @0x6BB85C
          accum.flipY ^= parent.flipY                 // @0x6BB8B8
          accum.angle  += parent.angle
          accum.scale2  = parent.scale2  * accum.scale2   // vmulq_f64 (scaleX,scaleY)
          accum.slant2  = parent.slant2  + accum.slant2
          // 矩阵乘 parent × local (4 doubles)         @ 0x6BB890
          node.m11 = parent.m11 * local.m11 + parent.m21 * local.m12
          node.m21 = parent.m11 * local.m21 + parent.m21 * local.m22
          node.m12 = parent.m12 * local.m11 + parent.m22 * local.m12
          node.m22 = parent.m12 * local.m21 + parent.m22 * local.m22

      else:
          // -------- COMPLEX 部分位 set 路径 --------- @ 0x6BB8F4..0x6BB924
          // Step-A: 对 SET 的位，直接 parent 继承（异或/相加/相乘）
          if (v46 & 0x004): accum.flipX  ^= parent.flipX
          if (v46 & 0x008): accum.flipY  ^= parent.flipY
          if (v46 & 0x010): accum.angle  += parent.angle
          if (v46 & 0x020): accum.scaleX  = parent.scaleX * accum.scaleX
          if (v46 & 0x040): accum.scaleY  = parent.scaleY * accum.scaleY
          if (v46 & 0x080): accum.slantX  = parent.slantX + accum.slantX
          if (v46 & 0x100): accum.slantY  = parent.slantY + accum.slantY

          if a1.independentLayerInherit:
              // ★★★ LABEL_68 (0x6BB918): INDEPENDENT PATH ★★★
              sub_699940(node, workArea)
              // ！！！不做 parent × local 矩阵乘 ！！！
              // node.m11..m22 保持 sub_699940 算出来的 *local-only* 值
              // (不受 parent 矩阵影响)
              // 并 *不* 写回 accum.m11..m22 — accum 仍是上面 Step-A 的组合
          else:
              // LABEL_76 (0x6BB9BC..0x6BBB6C): DEPENDENT / 4-phase
              // Phase A: undo root contribution (只对 SET bit)
              if (v46 & 0x004): accum.flipX  ^= root.flipX
              if (v46 & 0x008): accum.flipY  ^= root.flipY
              if (v46 & 0x010): accum.angle  -= root.angle
              if (v46 & 0x020): accum.scaleX /= root.scaleX
              if (v46 & 0x040): accum.scaleY /= root.scaleY
              if (v46 & 0x080): accum.slantX -= root.slantX
              if (v46 & 0x100): accum.slantY -= root.slantY
              // Phase B: sub_699940
              sub_699940(node, workArea)
              // Phase C: re-apply root contribution (同 SET bit)
              if (v46 & 0x004): accum.flipX  ^= root.flipX
              if (v46 & 0x008): accum.flipY  ^= root.flipY
              if (v46 & 0x010): accum.angle  += root.angle
              if (v46 & 0x020): accum.scaleX *= root.scaleX
              if (v46 & 0x040): accum.scaleY *= root.scaleY
              if (v46 & 0x080): accum.slantX += root.slantX
              if (v46 & 0x100): accum.slantY += root.slantY
              // Phase D: 矩阵乘 parent × local
              node.m11 = parent.m11 * local.m11 + parent.m21 * local.m12
              node.m21 = parent.m11 * local.m21 + parent.m21 * local.m22
              node.m12 = parent.m12 * local.m11 + parent.m22 * local.m12
              node.m22 = parent.m12 * local.m21 + parent.m22 * local.m22

  // === phase-3 : 各类 post-process subroutine ===           @ 0x6BBC60..0x6BBCA8
  sub_6BC000(a1)   // camera constraint (type=9)
  sub_6BC4F0(a1)   // icon(source) size propagation
  sub_6BD8DC(a1)   // stencil propagate
  Player_processCameraNode(a1)
  sub_6BDCC0(a1)
  sub_6BDE94(a1)
  sub_6BE0C0(a1)
  sub_6BEDD0(a1)
  sub_6BF0DC(a1)
  Player_evaluateCameraNodes(a1)                             // type=10 camera

  // === phase-4 : 清 dirty / 重新计算 lastPos ===
  if Player+480: clear all nodes' +176..+192 (reset lastFramePos to 0)
  else:          lastFramePos = accum.pos - prev.lastFramePos  (惯性速度)
  for each node: node+44=0, node+1504=0                      // @ 0x6BBD2C
  ...
  a1+608=0; a1+480=0
```

---

## inheritMask 位语义表（权威来自反编译证据）

**字段**：`*(int32_t*)(node + 40)`，PSB key = `"inheritMask"`（见 sub_6B3C78 @0x6B3F94）。

| bit mask   | 位索引 | 含义 (控制什么"继承自 parent") | 关闭时默认行为 | 证据地址 |
|------------|------|---------------------------------|------------------|----------|
| **0x00000004** | bit 2 | flipX 继承 | accum.flipX = state.flipX（无 XOR parent） | 0x6BB8F4, 0x6BB938 |
| **0x00000008** | bit 3 | flipY 继承 | accum.flipY = state.flipY | 0x6BB8F8, 0x6BB940 |
| **0x00000010** | bit 4 | angle 继承 (加法) | accum.angle = state.angle | 0x6BB8FC, 0x6BB954 |
| **0x00000020** | bit 5 | **scaleX 继承 (乘法)** | accum.scaleX = state.scaleX（不乘 parent.scaleX） | 0x6BB900, 0x6BB968 |
| **0x00000040** | bit 6 | **scaleY 继承 (乘法)** | accum.scaleY = state.scaleY（不乘 parent.scaleY） | 0x6BB904, 0x6BB97C |
| **0x00000080** | bit 7 | slantX 继承 (加法) | accum.slantX = state.slantX | 0x6BB908, 0x6BB990 |
| **0x00000100** | bit 8 | slantY 继承 (加法) | accum.slantY = state.slantY | 0x6BB90C, 0x6BB9A4 |
| **0x00000400** | bit 10 | opacity 二次乘 parent（不是 root） | 使用 root.opacity（当 independentLayerInherit=false 时） | 0x6BB808..0x6BB830 |
| **0x00400000** | bit 22 (low byte +42 bit 6) | **joinTarget / parent 透传（skip-parent）** | 使用自身直接父 | 0x6BB598, 0x6BB5AC, 0x6BB5BC |

### 重要结论
1. **position (posX/Y/Z) 根本没有 inheritMask 位！** 累加在 0x6BB6EC..0x6BB7E4 之间**无条件**进行：先 `+1592 += +1512` (delta)，然后 `pos = parent.m × localPos + parent.pos`。无论 inheritMask 什么值都会加 parent.pos。
2. **0x20 / 0x40 控制的是 scaleX / scaleY**，不是位置或矩阵。它们关闭时，`accum.scaleX = state.scaleX`（保留本地），不乘 parent.scaleX。
3. **0x79c vs 0x7fc 的差集 0x060 = bits 0x20|0x40 = scaleX & scaleY 继承位**。
4. **矩阵累加的关键 gate 是 `independentLayerInherit` (Player+1097)，不是 inheritMask**。这是 Player-level 的布尔值，整棵树共享。

---

## type=0 vs type=2 累加路径差异

**没有按 node.type 分叉的累加逻辑**！phase2 主体代码对所有 type 一视同仁。差异来自：

1. **evaluateTimeline 的 switch (node.type)** (`@0x699C14`): type=4/5/10 各自写专用 slot 字段（粒子/音频/摄像机），其他 type 返回 1 不特殊处理。type=0（normal layer）和 type=2（transform layer）在 evaluateTimeline 里**走同一路径**，只填 common fields（pos/scale/flip/angle/opacity/slant）。
2. **sub_699940 的 calcMatrix** 只看 node+84..96 的 `transformOrder[4]`，不看 type。type=2 和 type=0 的 matrix 都从这 4 个槽里按 0/1/2/3 依次 flipXY→rotate→scale→slant。
3. **Phase 3 中 `sub_6BC000` (camera) / `Player_processCameraNode` 会按 type 专门处理 9/10 等类型**——type=2 和 type=0 都**不**经过专门处理。

结论：**父 type=2、子 type=0 组合不会触发任何特殊累加路径**。父作为 `transform layer`（type=2）在 phase2 里按常规节点累加出 `accum.pos=(619, 608, 0)` 和 `accum.m=(2.987, 0, 0, 2.987)`，这些值可以正常被 type=0 子节点使用。

---

## type=0 child 没有 0x40 位时的实际默认行为

bug 说子节点 inheritMask=0x200079c，**缺失 0x40**（scaleY 继承位）。依伪代码：

- **进入 COMPLEX 路径**（因为 `~0x79c & 0x1FC = 0x060 ≠ 0`）。
- 0x40 位 = scaleY 继承，未设 → **`accum.scaleY = state.scaleY`**（不乘 parent.scaleY）。
- **scaleX 继承（0x20 位）同样未设** → `accum.scaleX = state.scaleX`。
- **position 累加仍然正常发生**（无 mask gate），应该得到 `accum.posX = parent.m × localPos + parent.posX`。
- **矩阵 m11..m22** 取决于 `independentLayerInherit`：
  - 若 **true**：只执行 sub_699940（`node.m11..m22 = local transform only`），**不乘 parent**。
  - 若 **false**：sub_699940 + parent × local 矩阵乘。

**因此，如果 bug 的 9 个字母子节点 accum.pos 全部 (0,0,0) 且 accum.m 全部 identity**：
- **不可能是 scale 继承位缺失导致** — 那只影响 scale 分量。
- accum.m = identity 在 **COMPLEX + independentLayerInherit=true** 路径下 **符合预期**（m11..m22 来自自身 localMatrix，state.scaleX=state.scaleY=1、angle=0、slant=0 → identity）。
- 但 **accum.pos=(0,0,0) 违反伪代码的无条件 `pos += parent.pos` 行为**。

---

## 我们 bug 的推导结论

### 结论 1（最关键）：accum.pos=(0,0,0) 不是 inheritMask 导致的
伪代码证据（0x6BB744..0x6BB7E4）明确显示 position 累加**与 inheritMask 无关**，无条件做：
```
accum.posX = parent.m11*localX + parent.m12*localY + parent.posX
accum.posY = parent.m21*localX + parent.m22*localY + parent.posY
accum.posZ = localZ + parent.posZ  (coordinateMode=0 分支)
```
如果 9 个字母 accum.pos 全部 (0,0,0)，只有两种可能：

**(A) parent 查找错误**：`parent` 实际指向了 `nodes[0]` (root) 而非 `str_locate`，root 的 accum.pos=(0,0,0) 且 root.m = identity，此时 `localX/Y/Z` 若也为 0 → accum.pos=(0,0,0)。这种情况 **极有可能** 因为：
- 若本地 `parentIndex` 查找链条损坏（例如 `parentIndex` 为 -1/0），会回退到 root。
- 本地 `while` 循环的条件测试错（例如把 `inheritMask & 0x00400000` 写成其他 mask），可能误把 `str_locate` 识别为 skip-parent。**但 0x79c & 0x00400000 = 0**，str_locate 不应被 skip。

**(B) `Player_evaluateTimeline` 返回 0 跳过累加**：伪代码 0x6BB5F0 若 evaluateTimeline 返回 0，走 LABEL_19 直接 `v16=v18+1` 进下一轮——**不会累加**，但也不会把 accum 重置为 (0,0,0)；accum 保留上一次有效值（或 never-initialized = 0 如果首次）。**首帧情况** 完全能解释 accum.pos=(0,0,0) 且 accum.m=identity。

### 结论 2：accum.m = identity 在 independentLayerInherit=true 下是"正确"的
0x6BB918 (LABEL_68) 显示：**若 Player.independentLayerInherit=true**，非 sync 非全继承路径下，**node.m11..m22 仅由 sub_699940 产生（local-only）**，从不与 parent.m 相乘。对 type=0 字符文本子节点，localState 通常 scale=1/angle=0/slant=0 → sub_699940 产出 identity 矩阵。

### 结论 3：检查本地 PlayerUpdateLayers.cpp 的差异
对照本地 `PlayerUpdateLayers.cpp` 上半部（line 600..898）：
- line 603-605：`node.accumulated.posX += node.localState.posX` —— **这是对的**（对应 0x6BB6EC `+1512 += +1592`），但**变量语义颠倒**了：伪代码中 `+1512` 是 localState，`+1592` 是 delta override。本地把 `localState` 加到 `accumulated` 里，顺序倒了。当 delta 为 0（首帧无 setVariable）时，结果一样；**有 delta 时会出错**。
- line 706-722：position 变换路径对应 0x6BB718..0x6BB7E4，**变量命名正确**。但 `node.accumulated.posX` 在 605 行已经含有 `node.localState.posX`。到了 706，`localX = node.accumulated.posX` 就 = localState.posX（因为 accum 应在本阶段处于空白）。**与伪代码一致**。
- line 794..808：opacity 条件乘，**正确对应** 0x6BB808。
- line 854-895：SIMPLE / INDEPENDENT / DEPENDENT 三路分歧，**本地代码在 independent 路径下赋值 `node.accumulated.m11 = localAffine[0]`**——这覆盖了 accum 矩阵。但伪代码 LABEL_68 (0x6BB918) 只调 `sub_699940`，**sub_699940 写 node+120 (node.m11)，不是 node+1544+/etc**。本地 `node.accumulated.m*` 对应偏移是哪个？

### 结论 4：关键字段偏移不一致
本地代码把 `node.accumulated.m11..m22` 视作累加矩阵，**但伪代码的 node+120..152 就是节点的 "node.m11..m22" (localMatrix from sub_699940 and/or parent-multiplied)**，这不是 accum 子结构字段，**就是节点根层字段**。

> 在 libkrkr2.so 中：
> - `node+120..152` 是**唯一**的矩阵存储位置，同时承担 local 和 world 两种角色。
> - 在 SIMPLE 路径和 DEPENDENT 路径，最后一步 `node.m11..m22 = parent.m × local.m` 写回 +120..152。
> - 在 INDEPENDENT 路径，只有 sub_699940 写入，保持 local-only。
> 
> 后续渲染用 node+120..152 直接作为 "node 的 world matrix"。

因此：**accum.m11..m22** 在本地架构中应映射为 **node+120..152**（`node.m11, m21, m12, m22`），而**不是** accum 子结构。

### 结论 5：bug 可能性排序
1. **(最可能)** 本地代码 `node.parentIndex` 查找/赋值链条错误，让 9 个字母查到的 parent 是 `nodes[0]` 而不是 `str_locate`。检查 NodeTree 构建代码 (`cpp/plugins/motionplayer/NodeTree.cpp` 或类似) 是否在 label 重复时正确建立 parent-child 关系。复用已知记忆 `project_label_map_no_dedup.md`：已确认 label dup 节点各自独立存在（9 个 cheeseware 会创建 9 个节点），所以 parentIndex 必须精确指向 str_locate 而不是共同的某个别名。
2. **(次可能)** `_independentLayerInherit = true` 时矩阵路径是 identity（设计如此），现象 "accum.matrix = identity" 符合此分支；但 accum.pos=(0,0,0) 仍是 parent lookup 问题。
3. **(可能)** `Player_evaluateTimeline` 首帧返回 0，跳过累加，而 accum block 初始值就是 0 / identity（C++ 默认构造）→ 显示 (0,0,0) & identity。这种情况下**修代码修不对累加逻辑**，应该查：(a) 为什么 evaluateTimeline 返回 0；(b) 首帧 activeSlot 的 +344 / +345 flags；(c) 第一帧是否有有效的 time/type content。

**下一步建议**（不写代码）：
- 在 phase2 主循环里 **加 trace**，打印每个 cheeseware 子节点的：
  1. `parentIdx` 本地值 vs PSB 里的原始 parent 声明。
  2. `parent.layerName` 实际指向的节点 label。
  3. `Player_evaluateTimeline` 的返回值（本地等价函数的 return）。
  4. 伪代码 0x6BB5E0 的 `v25` 四个布尔（forceDirty / groundCorrection / parent.accum.dirty / node.delta.dirty）。
- 对比 `independentLayerInherit` 值（Player+1097）是否为 true，并查 m2logo.mtn 的根层 PSB 是否声明 `motionIndependentLayerInherit = true`。
- 确认 type=2 父节点（str_locate）的 `inheritMask` 是否确实不含 0x00400000（joinTarget 透传位）——如果它误含 0x400000，父查找会跳过 str_locate 到 nodes[0]。

---

## 附：phase2 累加公式速查（权威版）

```
v46 = node.inheritMask             // node+40

// 1. 无条件 pre-matrix delta merge (@ 0x6BB6A4..0x6BB700)
accum.scaleX = delta.scaleX * accum.scaleX    // delta at node+1624
accum.scaleY = delta.scaleY * accum.scaleY
accum.slantX = delta.slantX + accum.slantX
accum.slantY = delta.slantY + accum.slantY
accum.opacity= delta.opacity * accum.opacity / 255
accum.posX   = delta.posX   + accum.posX      // delta at node+1592
accum.posY   = delta.posY   + accum.posY
accum.posZ   = delta.posZ   + accum.posZ

// 2. mesh (optional)
if parent.meshType: sub_69AE74(parent, node)

// 3. position transform by parent matrix + add parent pos  (@ 0x6BB744..0x6BB7E4)
if parent.coordinateMode != 0:          // 3D "coordinate":1
    accum.posX = parent.m11*localX + parent.m12*localZ + parent.accum.posX
    accum.posY =                              localY   + parent.accum.posY
    accum.posZ = parent.m21*localX + parent.m22*localZ + parent.accum.posZ
else:                                   // 2D default (coordinate:0)
    accum.posX = parent.m11*localX + parent.m12*localY + parent.accum.posX
    accum.posY = parent.m21*localX + parent.m22*localY + parent.accum.posY
    accum.posZ =                              localZ   + parent.accum.posZ

// 4. groundCorrection (optional)
if node.groundCorrection: TJS onGroundCorrection

// 5. opacity 2nd mul
opaSrc = parent if (v46 & 0x400) else root
if (v46 & 0x400) || !independentLayerInherit:
    accum.opacity = opaSrc.accum.opacity * accum.opacity / 255

// 6. 矩阵 + 属性累加
if (~v46 & 0x1FC) == 0:     // SIMPLE
    sub_699940(node)
    accum.flipX  ^= parent.flipX
    accum.flipY  ^= parent.flipY
    accum.angle  += parent.angle
    accum.scaleX  = parent.scaleX * accum.scaleX
    accum.scaleY  = parent.scaleY * accum.scaleY
    accum.slantX  = parent.slantX + accum.slantX
    accum.slantY  = parent.slantY + accum.slantY
    node.m = parent.m × local.m              // 2x2 matmul
else:
    // 只对 SET 位继承 parent
    for bit in [0x004 flipX, 0x008 flipY, 0x010 angle,
                0x020 scaleX, 0x040 scaleY, 0x080 slantX, 0x100 slantY]:
        if v46 & bit: apply parent-merge (XOR / + / ×) on that attr
    if independentLayerInherit:
        sub_699940(node)         // node.m = local only; NO parent × local
    else:
        for bit: if set, undo root contribution from accum
        sub_699940(node)
        for bit: if set, re-apply root contribution to accum
        node.m = parent.m × local.m
```

## 附录：sub_699940（local matrix 计算）速查

遍历 `node.transformOrder[4]`（来自 PSB "transformOrder" 数组），按槽位的 op 编号 0/1/2/3 依次作用到 2×2 matrix (初值 identity):

| op | 作用 |
|-----|------|
| 0   | flipX / flipY：按 accum.flipX/flipY 的 bool 负号翻转 m11/m12/m21/m22 |
| 1   | rotate：按 accum.angle (度) 生成 rotation matrix 左乘 |
| 2   | scale：accum.scaleX/scaleY 直接按行缩放 |
| 3   | slant：accum.slantX/slantY 加到 off-diagonal |

结果写回 node+120..152 (m11, m21, m12, m22)。

—— END ——
