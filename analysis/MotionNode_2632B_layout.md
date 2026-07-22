# MotionNode 内存布局 (2632B / 0xA48, Player+200 deque 元素)

> 权威来源：libkrkr2.so **反汇编 (disasm)**。所有偏移均有 ARM64 指令级证据，load/store 宽度直接定字段大小
> (LDRB/STRB=byte, LDR/STR W=dword, LDR/STR X=qword, LDR/STR D=double, LDR/STR S=float)。
> 禁止从本地代码或字段名反推。
>
> **方法论**：以 `disasm` 为准，**不要信 hex-rays `decompile` 推断的偏移**（buildNodeTree 的 decompile 把
> node 与 slot base 混淆，导致首版文档出错）。
>
> **2026-07-22 lifecycle correction**：`node+2024/+2048/+2072` 是三个相互
> 独立的 `std::vector<MeshPoint>` owner。`MotionNode_copy @ 0x6F468C` 逐个
> CopyAssign，`MotionNode_destroy_guess @ 0x6F4C8C` 逆序析构。旧的“两条
> vector / previous-frame alias”模型已被这组 copy+dtor 证据否定。

---

## 0. stride / 容器（钉死）

- 容器在 Player(EmotePlayer) 内：deque 控制三元组 `player+0xC8/0xD0/0xD8/0xE8/0xF0/0x100`
  （advanceNodeFrames 用 `a1[25]/a1[26]/a1[28]`=+200/+208/+224；anchor eval `LDP[X19,#0xE8]` 等）。
- **节点 stride = 2632 (0xA48)**，指令级确认：
  - `MOV W15,#0xA48; MADD X23,X21,#0xA48,X26` @0x6c06b8 (anchor eval)
  - `MOV W8,#0xA48` @0x6b73cc (advanceRootAndNodes 内联 seek)
  - `MOV W22,#0xA48` @0x6ba134 / @0x6ba2c8 (rewindRootAndNodes)
  - `v11 = v6 + 2632*v9`（buildNodeTree）
- deque 间接寻址（libstdc++ deque）：`idx = i - K*((begin-map0)>>3)`；非零取 `*(map[idx])`，否则线性 `begin+2632*i`。

---

## 1. ★ 两个 ClipSlot[536B] 子结构（钉死，与 ClipSlot_536B_layout.md 一致）

| slot | node 绝对 base | stride | 证据 |
|---|---|---|---|
| **slot[0]** | **node + 320 (0x140)** | 536 (0x218) | `ADD X9,X20,#0x140; MADD X8,X22,#0x218,X9` @0x6b7ef8/0x6b7f08 (advNodeFrames)；`ADD X20,X19,#0x140; MADD ...,#0x218` @0x699d5c (evalTL) |
| **slot[1]** | **node + 856 (0x358)** | 536 (0x218) | slot[0]+536=856；advNodeFrames `UMADDL X22,W11,#0x218,X9` (W11=对端 index) @0x6b7f0c |

- 活动 slot = `node + 320 + 536*activeSlotIndex`，activeSlotIndex = `*(int*)(node+0x570)`。
- 对端 slot index = `(~activeSlotIndex)&1`（`MVN W8,W22; AND W11,W8,#1` @0x6b7ef4）。
- **slot 内部 536B 完整字段表见 `analysis/ClipSlot_536B_layout.md`**（frameIndex@+0, time@+8, mask@+20,
  invisible@+24, interpFlag@+25, mergedFlag@+26, color@+72×4, opacity@+88, blend@+44,
  transform ox/oy/coord/fx/fy/angle/zx/zy/sx/sy @+56..+160, easing curves @+168..+268,
  clipStartTime@+328, timeModulo@+336, hasContent@+344, secondaryFlag@+345, easing flags @+544/+564/+584,
  eval-result 字段 @+392..+528 等）。本文不重复 slot 内部，只列 **node 顶层字段** 与 slot 在 node 中的投影。

> **mergedFlag 的 node 绝对偏移 = slot+26**：slot[0].mergedFlag = node+346(0x15A)，slot[1].mergedFlag = node+882(0x372)。
> **任务描述的 346/882 正确**。advanceNodeFrames 用 node 绝对偏移直接寻址这两个字节门控 merge：
> `LDRB W8,[X20,#0x15A]` @0x6b7fb4（gate slot[0] merge）、`LDRB W8,[X20,#0x372]` @0x6b7fd4（gate slot[1] merge）。
> 字节已抽查确认：0x6b7fb4=`08 6B 40 39`(LDRB,#0x15A)、0x6b7fd4=`08 EB 40 39`(LDRB,#0x372)。

---

## 2. node 顶层字段表（按偏移升序，X19/X20/X23/X26=node）

| 偏移(dec) | 偏移(hex) | 大小 | 语义 | 证据指令 / 函数 |
|---|---|---|---|---|
| 8    | 0x8   | qword-ptr | **childTimeline 指针**；`*(child+0x28)`=childEvalTime(double)，是本节点 seek 的目标时间 | `LDR X8,[X20,#8]; LDR D8,[X8,#0x28]` @0x6b7e80/0x6b7e90 (advNodeFrames)；`LDR X8,[X19,#8]; LDR D0,[X8,#0x28]` @0x699c7c (evalTL) |
| 28   | 0x1C  | int | **nodeType**（evalTL switch 4/5/10；advance/findSource `1<<nodeType` 位测；buildNodeTree 判 12/3/0） | `LDR W8,[X19,#0x1C]; CMP#0xA/5/4` @0x699c0c (evalTL); `LDR W8,[X23,#0x1C]; CMP#0xA` @0x6c06bc (anchor); `LDR W8,[X20,#0x1C]; LSL W8,#1,W8` @0x6b7ff8 (findSource gate) |
| 44   | 0x2C  | byte | **forceFlag**（advance 写 1；evalTL 读入 force 判定） | `STRB W9,[X20,#0x2C]` (=1) @0x6b7fbc (advNodeFrames)；`LDRB W8,[X19,#0x2C]` @0x699b24 (evalTL) |
| 52   | 0x34  | dword | **stencil/typeFlag**（buildNodeTree `&4` gate，type12 子节点收集） | `*(v11+52)&4` @0x6b53b4 (buildNodeTree) |
| 56   | 0x38  | double | lastRatio（crossfade 比值缓存，提前退出判定） | `LDR D0,[X19,#0x38]; STR D8,[X19,#0x38]` @0x699cdc (evalTL) |
| 100  | 0x64  | dword | color R (eval 结果；anchor 颜色游标基址 node+0x67-3) | `STR W8,[X19,#0x64]` @0x699fe0 (evalTL); anchor 循环 @0x6c0ad0.. |
| 104  | 0x68  | dword | color G | `STR W8,[X19,#0x68]` @0x699fe4 |
| 108  | 0x6C  | dword | color B | `STR W8,[X19,#0x6C]` @0x699fec |
| 112  | 0x70  | dword | color A | `STR W8,[X19,#0x70]` @0x699ff4 |
| 120  | 0x78  | double | transform 矩阵分量 m00（anchor 合成） | `LDP D2,D3,[X23,#0x78]` @0x6c0950 (anchor) |
| 128  | 0x80  | double | transform m01 | 同上 |
| 136  | 0x88  | double | transform m10 | `LDP D4,D5,[X23,#0x88]` @0x6c0954 |
| 144  | 0x90  | double | transform m11 | 同上 |
| 200  | 0xC8  | byte | **imagesValid 标志**（anchor: a1[74]==0 → 0；有效 → 1）。findSource 入参 X0=node+0xC8 | `STRB WZR,[X23,#0xC8]` @0x6c072c / `STRB W25,[X23,#0xC8]` @0x6c0760 (anchor); `ADD X0,X20,#0xC8` @0x6b8024 (findSource arg0) |
| 204  | 0xCC  | (struct) | PSB dispatch 缓存对象（`sub_A0FB64(node+0xCC, player+696)`） | `ADD X0,X23,#0xCC` @0x6c0754 (anchor) |
| 232  | 0xE8  | double | **sourceWidth**（anchor 从 PSB "width" 物化） | `STR D0,[X23,#0xE8]` @0x6c07c8 (anchor) |
| 240  | 0xF0  | double | **sourceHeight**（PSB "height"） | `STR D0,[X23,#0xF0]` @0x6c0848 |
| 248  | 0xF8  | double | halfHeight = height*0.5 | `STP D1,D2,[X23,#0xF8]` @0x6c084c |
| 256  | 0x100 | double | halfWidth = width*0.5 | 同上 (D2) |
| 264  | 0x108 | qword | cx（清零） | `STP XZR,XZR,[X23,#0x108]` @0x6c0844 |
| 272  | 0x110 | qword | cy（清零） | 同上 |
| 280  | 0x118 | OWORD | identity 标志块（写 {1.0,1.0}） | `STR Q0,[node+0x118]` @0x6c0860 |
| **346** | **0x15A** | byte | **slot[0].mergedFlag**（=slot[0]+26）advNodeFrames merge 门控 | `LDRB W8,[X20,#0x15A]; CBNZ` @0x6b7fb4 (advNodeFrames) |
| **882** | **0x372** | byte | **slot[1].mergedFlag**（=slot[1]+26）advNodeFrames merge 门控 | `LDRB W8,[X20,#0x372]; CBNZ` @0x6b7fd4 (advNodeFrames) |
| 1392 | 0x570 | int | **activeSlotIndex** | `LDRSW X22,[X19,#0x570]` @0x699b14 (evalTL); `LDRSW X22,[X20,#0x570]` @0x6b7e84 (advNodeFrames，advance 内还写回 `STR W8,[X20,#0x570]` 取反 @0x6b7f40); `LDRSW X20,[X23,#0x570]` @0x6c06d4 (anchor) |
| 1505 | 0x5E1 | byte | **anchor active gate**（anchor: `*(node+0x5E1)==0` → 跳过该节点） | `LDRB W8,[X23,#0x5E1]; CBZ` @0x6c06c8 (anchor) |
| 1507 | 0x5E3 | byte | flag (← slot fx，evalTL copy 分支) | `STRB W9,[X19,#0x5E3]` @0x699b78 |
| 1508 | 0x5E4 | byte | flag (← slot fy) | `STRB W9,[X19,#0x5E4]` @0x699b80 |
| 1512 | 0x5E8 | double | posA (← slot+0x1A0；anchor lerp toward root v14[189]) | `STR X9,[X19,#0x5E8]` @0x699bac; `STR D0,[X23,#0x5E8]` @0x6c0a18 (anchor) |
| 1520 | 0x5F0 | double | posB (← slot+0x1A8；anchor lerp v14[190]) | `STR X9,[X19,#0x5F0]` @0x699bb4; @0x6c0a34 |
| 1528 | 0x5F8 | double | posC (← slot+0x1B0；anchor lerp v14[191]) | `STR X9,[X19,#0x5F8]` @0x699bbc; @0x6c0a4c |
| 1536 | 0x600 | double | **angle**（eval 结果；anchor exp-damp 朝 0/360） | `STR D9,[X19,#0x600]` @0x699e3c; `STR D0,[X23,#0x600]` @0x6c08e0 |
| 1544 | 0x608 | double | **scaleX**（eval 结果；anchor `pow`） | `STR D9,[X19,#0x608]` @0x699e9c; `STR D0,[X23,#0x608]` @0x6c0908 |
| 1552 | 0x610 | double | **scaleY** | `STR D9,[X19,#0x610]` @0x699ef4; @0x6c0924 |
| 1560 | 0x618 | double | **slantX** | `STR D9,[X19,#0x618]` @0x699f54; `STR D0,[X23,#0x618]` @0x6c0934 |
| 1568 | 0x620 | double | **slantY** | `STR D9,[X19,#0x620]` @0x699fd0; `STR D1,[X23,#0x620]` @0x6c0938 |
| 1576 | 0x628 | dword | **opacity**（0..255 整数） | `STR W9,[X19,#0x628]` @0x69a054; `STR W8,[X23,#0x628]` @0x6c09f4 |
| 1904 | 0x770 | qword-ptr | **持久 PreparedRenderItem 指针**；`sub_6C2334` 按需分配并跨帧复用 | `sub_6C2334 @0x6C2334` ordinary/wrapper allocation + population；`MotionNode_destroy_guess @0x6F4C8C` 删除 |
| 1944 | 0x798 | byte | **drawnThisFrame**；每轮 build 前清 0，成功进入 Path A mainList 后置 1；type12 post-pass 读取 | `sub_6C2334 @0x6C2334` main selection / type12 secondary loop |
| 1952 | 0x7A0 | qword-ptr | **visibleAncestor node 指针**；item population 取其 `+1904` 写 item ancestor chain | `sub_6BD8DC @0x6BD8DC` 写；`sub_6C2334 @0x6C2334` 读 |
| 1960 | 0x7A8 | byte | **drawFlag**（Path B 产物；不是普通 Path A nodeType-mask 入列门） | `sub_6BD8DC @0x6BD8DC` 写；`sub_6C2334` 写 item+19/处理 type3 wrapper 时读 |
| 1961 | 0x7A9 | byte | **isLinkedChild 标志**（buildNodeTree 把子节点接入父 list 后置 1） | `*(v19+1961)=1` @0x6b55a8 (buildNodeTree) |
| **1996** | **0x7CC** | int | **findSource gate**（advanceNodeFrames 读后分支决定是否调 findSource） | `LDR W8,[X20,#0x7CC]; CBNZ loc_…findSource` @0x6b7fec (advNodeFrames)。字节确认 0x6b7fec=`08 CE 40 B9`(LDR W,#0x7CC) |
| 2000 | 0x7D0 | dword | **meshType / meshTransform mode**；`==1` 选择 patch 插值/变换路径 | `LDR W9,[X19,#0x7D0]; CMP#1` @0x699be4/0x69a030 (evalTL)；`sub_6C2334` 复制到 item+280 |
| 2012 | 0x7DC | dword | **composite mesh 横向 cell count**；对应点列数为 `count+1` | `sub_6BC4F0 @0x6BCF54..0x6BCF6C` 写并原样传 `sub_6BAF68`；`sub_6C2334 @0x6C2688` 复制到 item+272 |
| 2016 | 0x7E0 | dword | **composite mesh 纵向 cell count**；对应点行数为 `count+1` | `sub_6BC4F0 @0x6BCF58..0x6BCF6C` 写并原样传 `sub_6BAF68`；`sub_6C2334 @0x6C2690` 复制到 item+276 |
| 2024 | 0x7E8 | 24B `std::vector<MeshPoint>` | **raw 4x4 Bezier control patch owner** | `MotionNode_copy @0x6F468C` 第一条 vector copy；`Player_updateLayers @0x6BC4F0` 维护；`sub_6C2334` 复制到 item+376 |
| 2048 | 0x800 | 24B `std::vector<MeshPoint>` | **composite/deformed mesh grid owner**；点数 `(node+2012+1)*(node+2016+1)`、row stride `node+2012+1` | `MotionNode_copy @0x6F468C` 第二条 vector copy；`sub_6BAF68 @0x6BAF68` 构建；`sub_6C2334` 每次复制到 item+344 |
| 2072 | 0x818 | 24B `std::vector<MeshPoint>` | **own-affine-transformed patch owner** | `MotionNode_copy @0x6F468C` 第三条 vector copy；`Player_updateLayers @0x6BC4F0` 写入；`sub_6C2334` type1 分支复制到 item+400 |
| 2224..2280 | 0x8B0..0x8E8 | double×8 | type4 通道结果 | `STR X9,[X19,#0x8B0..0x8E8]` @0x699c34; `STR D9,[X19,#0x8B0..]` @0x69a144 (evalTL) |
| 2288 | 0x8F0 | double | type4 通道结果末项 | `ADD X9,X19,#0x8F0` @0x699c74; `STR D9,[X19,#0x8F0]` @0x69a3d0 |
| 2368 | 0x940 | double | type5 通道结果 | `ADD X9,X19,#0x940` @0x699d34; `STR D9,[X19,#0x940]` @0x69a478 |
| 2432 | 0x980 | double | **type10 通道结果 / anchor exp-damp 时间常数除数** | `ADD X9,X19,#0x980` @0x699d34 (evalTL); `LDR D4,[X23,#0x980]` @0x6c0888 + 作除数 (anchor) |
| 2440 | 0x988 | double | anchor opacity 缩放系数（pow 结果乘子） | `LDR D1,[X23,#0x988]` @0x6c09b4; `STR D0,[X23,#0x988]` @0x6c09f8 (anchor) |
| 2472 | 0x9A8 | double | **anchor color 缓存块起点**（per-channel c0@-0x18/c1@-0x10/c2@-8/c3@0，步长 0x20） | `ADD X25,X23,#0x9A8` @0x6c0acc; `STP D2,D0,[X25,#-8]` @0x6c0c34 (anchor) |

> anchor per-channel 循环（@0x6c0ad0..0x6c0c58）：颜色字节游标基址=node+0x67(=103)，缩放系数基址=node+0x9A8，
> byte 步长 4 / double 步长 0x20，循环 1 或 4 次。颜色字节回写到 node+0x64..0x70 区。

### 2.1 三个 mesh vector 的源码级结论

1. `MotionNode_copy @ 0x6F468C` 在 `0x6F47F4..0x6F480C` 连续复制
   `+2024`、`+2048`、`+2072` 三个 vector；这证明它们是三个独立 owner，
   不是 begin/end/cap 三指针，也不是 current/previous 两帧别名。
2. `MotionNode_destroy_guess @ 0x6F4C8C` 在
   `0x6F4CFC..0x6F4D1C` 以 `+2072 → +2048 → +2024` 的逆声明顺序析构。
3. `Player_updateLayers @ 0x6BC4F0` 保留 `+2024` raw patch，构建
   `+2048` composite grid，并写 `+2072` own-affine-transformed patch。
4. `sub_6C2334 @ 0x6C2334` 分别把它们送到 item
   `+376/+344/+400`；因此本地应声明三个普通
   `std::vector<MeshPoint>` 成员并复刻其复制/清空/析构顺序。这里的 ARM64
   偏移只是分析坐标，不要求 wasm32 对象具有同一字节布局。

---

## 3. 各函数切入条件 / node 字段访问摘要

### advanceNodeFrames (0x6B7E44)  per-node 双 slot ping-pong 前向 seek
1. `targetT = *(double*)(*(node+8) + 0x28)`（childTimeline 的 childEvalTime）。
2. `i = *(int*)(node+0x570)`；`slot0base = node+320`；`slotA = node+320+536*i`，对端 `slotB = node+320+536*((~i)&1)`。
3. 前向循环：`while (slotA.frameIndex(slot+0) <SIGNED count-2 && targetT >= slotB.time(slot+8))`
   → 翻转 activeSlotIndex(`STR ~i,[node+0x570]`)、`parseFrame(slotnew, frameDispatch, frameIndex+1)`、ping-pong。
   **对齐陷阱**：frameIndex 与 `count-2` 是**有符号**比较（空流时 count-2 为负，靠符号比较保持 no-op）。
4. corrective backward：`while (slotA.time > targetT) parseFrame(slot, ..., frameIndex-1)`。
5. `node+0x2C = 1`（forceFlag）。
6. `if (!*(node+0x15A)) mergeFrameContent(node+320, nodeType=*(node+0x1C), frameDispatch)` — gate slot[0].mergedFlag。
7. `if (!*(node+0x372)) mergeFrameContent(node+856, nodeType, frameDispatch)` — gate slot[1].mergedFlag。
8. `if (*(node+0x7CC) || ((1<<nodeType) & (player+0x444 ? 0x1809 : 0x1801)))`
   → `findSource(node+0xC8, layerArg, slotActive+0x164, slotActive+0x15C)`，slotActive=node+320+536*activeSlotIndex。

### advanceRootAndNodes (0x6B6ADC) / rewindRootAndNodes (0x6B9A3C)
- 这两函数主要推进 **Player 级** 的 layer-stream / root-stream / variable-track 游标（player+0x394/+0x398/+0x3A0/+0x1C8 等），
  对每个 node 调 advanceNodeFrames（有 child）或内联同款 2-slot seek（stride 2632 已确认）。
- **node+0x7CC findSource gate** 经 insn_query 在这两函数内**未直接以立即数 1996 出现**——findSource 的 gate
  实际在 advanceNodeFrames(0x6B7E44 @0x6b7fec) 里。advanceRootAndNodes/rewind 通过调用 advanceNodeFrames 间接触发。
  （首版称"出现在 advRoot/rewindRoot"不准确，已更正：gate 在 advanceNodeFrames。）
- rewind 的内联 backward seek 用 `node+8`(=slot+8 time) 与 player+0x1C8 比较递减游标（@0x6ba104..0x6ba114）。

### evaluateTimeline (0x699AE4)  详见 ClipSlot_536B_layout.md §2
- `i=*(int*)(node+0x570)`；slotA=node+320+536*i。
- `if (slotA+0x158 /*hasContent gate*/) { ... }`（注意：这是 **slot 相对 +0x158=344=slot.hasContent**，
  **不是** mergedFlag；mergedFlag 是 slot+26）。
- copy 分支：把 slotA 的 result 字段（slot+392..+480）写入 node+0x64..0x70 / node+0x600..0x628 / node+0x5E8..。
- crossfade 分支：ratio=(time-slotA+328)/(slotB+328-slotA+328)，按 ratio 在两 slot 间 lerp（angle 180°最短路）。
- switch(node+0x1C) 4/5/10 各拷/插一段 type 通道到 node+0x8B0/0x940/0x980。

### anchor eval (0x6C0528)  nodeType==10
1. 遍历 deque(stride 2632)，`if (*(node+0x1C)!=10 || !*(node+0x5E1)) continue`。
2. `*(player+0x265=613)=1`；`if (player a1[74]==0 || !*(player+0x264=612)) { *(node+0xC8)=0; continue; }`。
3. PSB dispatch(player+696) 物化 node+0xE8/0xF0/0xF8/0x100；`*(node+0xC8)=1`。
4. exp-damp：angle/scale/slant(node+0x600..0x620)、opacity(node+0x628)、color(node+0x64.., 缓存 node+0x9A8)
   用 `pow(.., dt)`；pos(node+0x5E8/0x5F0/0x5F8) lerp 朝 root(node+0x140 槽的 v14[189..191])。
5. dt 由 a1[74] / player+0x250 / player+0x490 / node+0x980 复合算出。

---

## 4. 仍未确定 / 需后续钉死
1. **node 头部 [0, 0x64) 大部分语义未定**：已知 +8(childTimeline ptr)、+0x1C(nodeType)、+0x2C(forceFlag)、
   +0x34(stencil)、+0x38(lastRatio)、**+0x24(parentNodeIndex)**。未定：+0(vtable?)、+0x10、+0x14。
   - **+0x24(36) = parentNodeIndex (int)**：父节点 0-based deque 索引（根的子节点=0）。
     写：`Player_buildNodeTree_recursive`@0x6B4A6C `STR W8,[X27-0xA24]`@0x6b4bf8，写入 a2=递归传下的父自身 index v17。
     读：`sub_6B9650`@0x6B9650 `LDR W10,[X10,#0x24]; B.GT`@0x6b9958，祖先链爬升的 climb-to-parent cursor，`<=0` 停（到根/无父）。证据无缺口。
2. **slot 内部 536B**：已完整记录在 ClipSlot_536B_layout.md（slot[0]@node+320 / slot[1]@node+856）。
   注意 slot+0x158=344=hasContent、slot+0x159=345=secondaryFlag，与 node 绝对 +346/+882 mergedFlag(slot+26) 是
   **不同字段**，勿混。
3. **node+0xC8(200)**：byte imagesValid 标志（anchor 写 0/1，findSource arg0）。**不是** deque/容器指针。
4. **node 头与 slot[0] 之间 [0x118, 0x140) 的 40 字节**未枚举（anchor 只用到 +0x118 OWORD）。
5. advanceRootAndNodes 的 variable-track deque（player+0x520..+0x558 区，stride 0xA0/0x38）属 **Player 级** 容器，
   不在 node 内，本表不收。
