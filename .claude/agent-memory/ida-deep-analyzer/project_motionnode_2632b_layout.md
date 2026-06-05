---
name: motionnode-2632b-layout
description: MotionNode (Player+200 deque 元素, stride 2632/0xA48) 的字节级字段表与证据来源；改 std::deque<MotionNode> 为 inline deque 的硬前提
metadata:
  type: project
---

MotionNode = Player(EmotePlayer)+200/+204 deque 的元素，**stride 2632 (0xA48)**，字节级证据。
完整字段表见 `analysis/MotionNode_2632B_layout.md`。

**deque 容器拓扑裁决见 [[node-deque-no-sentinel]]**：node-deque **无 trailing sentinel**，dequeSize==realNodeCount(root+N)，walk 的 `dequeSize-1` 是 +1 偏置抵消(`start.last-start.cur` 代替 `start.cur-start.first`)非 sentinel。本地 _nodes 已正确，walk 上界=`_nodes.size()`。

**重要方法论更正**：应以 `disasm`（带真实指令偏移 + load/store 宽度）为准，不要信 hex-rays `decompile` 的偏移。
hex-rays 把 clip-slot 写成 node+320，实际汇编证明 slot stride=0x218 且活动 slot base = node+536*activeSlotIndex（slot[0]@node+0）。

**钉死的关键偏移（ARM64 disasm 指令级证据）：**
- 容器：deque 控制字段 player+200/+208/+224；遍历 stride **2632 (0xA48)**（`MOV W15,#0xA48; MADD X23,X21,X15,X26` @0x6c06b8 anchor；三 advance/rewind 函数 op_any=2632 均命中）。
- node+8 = childTimeline ptr；`*(child+0x28)`=childEvalTime(double)（`LDR D0,[X8,#0x28]` @0x699c84）。
- **2 个 ClipSlot[536B]**：slot[0]@node+320(0x140)，slot[1]@node+856(0x358)，stride 536(0x218)。活动 slot=node+320+536*activeSlotIndex。slot 内部完整 536B 字段见 analysis/ClipSlot_536B_layout.md。`ADD X9,X20,#0x140; MADD X8,X22,#0x218,X9`@0x6b7ef8 (advNodeFrames) + evalTL `ADD X20,X19,#0x140`@0x699d5c。
- **mergedFlag = slot+26 = node+346(0x15A) / node+882(0x372)**（slot[0]/slot[1]）。**任务描述的 346/882 正确！** 字节确认 `LDRB[X20,#0x15A]`@0x6b7fb4 / `LDRB[X20,#0x372]`@0x6b7fd4 门控两次 mergeFrameContent。
  注意区分：evalTL 里的 slot+0x158(344)=hasContent、slot+0x159(345)=secondaryFlag，是 slot 相对偏移的**不同字段**，勿与 node 绝对 +346 mergedFlag 混（我第一版就栽在这）。
- node+0x1C(28) = **nodeType**（已证实！`LDR W8,[X19,#0x1C]; CMP#0xA/5/4` @0x699c0c evalTL；`[X23,#0x1C]` @0x6c06bc anchor；buildNodeTree 判 12/3/0）。之前误以为是 vtable dispatch。
- node+0x570(1392) = activeSlotIndex(int)，`LDRSW X22,[X19,#0x570]` @0x699b14。
- node+0x5E1(1505) = anchor active gate(byte)，anchor `CBZ` 跳过；evalTL 不读。
- node+0x600/0x608/0x610/0x618/0x620 = angle/scaleX/scaleY/slantX/slantY (double, eval 结果)；+0x628(1576) opacity(dword 0..255)。
- node+0x5E8/0x5F0/0x5F8 = pos a/b/c (double)；+0x64..0x70 = color RGBA (dword×4)。
- node+0xC8(200) = imagesValid 标志(byte, anchor 写0/1)；+0xCC PSB dispatch 缓存；+0xE8/0xF0 = sourceWidth/Height(double, anchor 从 PSB 物化)。
- node+0x7CC(1996) = **findSource gate(int)** — 在 **advanceNodeFrames** @0x6b7fec (`LDR W8,[X20,#0x7CC]; CBNZ`)，字节确认 `88 CE 47 B9`。advRoot/rewind 通过调 advanceNodeFrames 间接触发，自身不直接以立即数 1996 寻址（首版误标 advRoot/rewind，已更正）。
- node+0x2C(44) = forceFlag(byte, advance 写1 @0x6b7fbc)。findSource arg: node+0xC8(imagesValid) + slotActive+0x164/+0x15C。
- advanceNodeFrames seek: slotA.frameIndex(slot+0) 与 count-2 **SIGNED** 比较（空流 count-2 为负→no-op，移植 uint 必坑）。
- node+0x980(2432)=type10 通道结果 / anchor exp-damp 除数；+0x988(2440) anchor opacity 缩放；+0x9A8(2472) anchor color 缓存块(double, 步长0x20)。
- node+0x7D0(2000) = type1-pos-mode gate(==1 走 sub_6996E8/sub_69AC4C)；+0x7E8(2024) pos-interp 目标块。
- node+0x7A9(1961) = isLinkedChild 标志(byte, buildNodeTree 接入父 list 后置1)；+0x34(52) stencil/typeFlag(&4 gate)。

**未确定：** node 头部 [0,0x64) 大部分（+0 vtable? +0x10/+0x14/+0x24 语义）；clipSlot/xformSlot 是否物理同段的最终钉死（需 buildNodeTree slot-init disasm + byte dump）；+0x57C/0x580 与 frame counter 三连各自含义。

**证据函数：** evaluateTimeline@0x699AE4, anchor eval@0x6C0528, buildNodeTree@0x6B51F0, advanceNodeFrames@0x6B7E44, advanceRootAndNodes@0x6B6ADC, rewindRootAndNodes@0x6B9A3C —— 全部 disasm 已取到。

**工具陷阱：**
- get_bytes/insn_query 的参数 schema 是 deferred 的，**必须先 ToolSearch select 加载 schema** 再调用，否则报 missing 'queries'/'regions'。
- 并行发大量畸形（schema 未加载）调用会**把 IDA MCP server 打到无响应数分钟**——务必先加载 schema、串行小批量发。
- disasm 比 decompile 可靠（带真实偏移 + LDRB/LDR W/LDR X/LDR D 宽度直接定字段大小）。max_instructions 默认5000足够。
- insn_query 用 `func` + `op_any=<十进制偏移>` + `include_disasm:true` 精确捞某偏移的所有访问，最高效。
