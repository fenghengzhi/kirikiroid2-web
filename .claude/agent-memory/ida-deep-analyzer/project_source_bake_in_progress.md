---
name: source-bake-happens-in-progress-not-draw
description: Motion.Player source 纹理烘焙(findSource@0x6948E8 GPU上传)发生在 progress 帧循环的 node-advance 里,不在 draw; 三处 advance 共用同一 nodeType 掩码门控
metadata:
  type: project
---

Motion.Player 的 source 纹理解析+GPU 上传 = **findSource@0x6948E8**, 发生在 **progress 阶段**, 不在 draw。

**Why:** 调查 DRACU title.psb 黑屏(node tree 建出38个但 source 从不烘焙)。误以为烘焙在 draw/playCompat 入口。

**调用链:** progressCompat@0x6D2A98 → progress_inner@0x6C106C → 三选一:
- firstFrame(+481==1): reseekTimelineCursors@0x6B86C8 → per-node@0x6B91B0 → initNodeTimeline@0x6B64AC → findSource(0x6B6734)
- +376!=0: advanceNodeFrames@0x6B7E44 → findSource(0x6B8038)
- 正常前进/loop-wrap: advanceRootAndNodes@0x6B6ADC → node-walk → findSource(0x6B7338)

**烘焙门控(三处字节级一致):**
```
if (*(int*)(node+1996) != 0
 || ((player+1092/*preview*/ ? 6153 : 6145) & (1 << *(int*)(node+28)/*nodeType*/)) != 0)
    findSource(node+200, player, slot+356, slot+348);
```
- 6145=0x1801=bits{0,11,12}; 6153=0x1809=bits{0,3,11,12}(preview加type3)
- nodeType 0/11/12 直接烘焙; type3(child-motion)仅preview; type7(shape)/type10(camera)靠 node+1996
- 还需 timeline cursor 未到末尾(`*v45 < count-2`)且至少parse一帧(v47=1)才进烘焙块,空/单帧流(count<2)→no-op

**loadSource@0x6A7BA8(→bake sub_6A6BE0: drawLayer+setSize+copyRect+fillRect+operateRect)** 是 SourceCache/ResourceManager 的 NCB 方法, 仅 draw/TJS 路径调用, 与 Player 自身渲染的 findSource 是两条独立路径。

**playCompat vs onFindMotion 前提纠正:** 非两个入口。playCompat@0x6D2C08→play@0x6B21E8→playImpl@0x6B2284→initNonEmoteMotion@0x6B365C(内含buildNodeTree@0x6B3A80)。onFindMotion 非独立函数=loadMotion(playImpl@0x6B2330)。playImpl 只设状态位(+481 firstFrame/+1099 loopArmed/+1128 lastTime/+1136 loopTime), 不烘焙。playImpl gate@0x6B22D4: 同motion且无Force/AsCan→直接return不重建tree(但 title 建出38node 证明此gate已走)。烘焙永远在随后 progress 帧循环。

**Apply:** 调查"node建好但source不上屏"先查 progress 阶段 advance 分支是否被走 + 各 node 的 nodeType(+28)/node+1996 是否满足 0x1801 掩码, 不要查 draw/loadSource。本地若把烘焙挂在 loadRenderSourceByName/ensureEntryBackingBitmap(draw路径)而非 progress 的 findSource, 即偏离二进制架构。
