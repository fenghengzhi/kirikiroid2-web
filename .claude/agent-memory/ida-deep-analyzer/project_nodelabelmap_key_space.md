---
name: project-nodelabelmap-key-space
description: Player+24 node-label map (std::map<ttstr,int>) keyed by RAW PSB "label", NOT buildNodePathKey path; buildNodePathKey serves HM3 only. Cluster F/M5.
metadata:
  type: project
---

# Player+24 node-label map key 空间 (cluster F / M5)

**事实**: libkrkr2.so 的 Player+24 (`a1+3` qword) = `std::map<ttstr,int32_t>` (RB-tree)，
write 和所有 read 都用 **raw label** 同一 key 空间。`Player_buildNodePathKey` 构造的
path "/a/b/c" **从不**进入 Player+24，只服务 HM3 (Player+1184 libstdc++ unordered_map)。

**Why:** 解决本地 _nodeLabelMap write/read key 形态不一致的对账需求。

**How to apply:** 本地 NodeTree.cpp:118 write 端误用 path 作 _nodeLabelMap key，
read 端 (PlayerLayerQuery getLayerMotion/getLayerGetter) 用 raw narrow(name) →
永久 mismatch。修复方向 = write 端改用 raw node.layerName (PSB "label")。

## 关键地址 (字节/反编译已验证, IDB 已重命名+注释+save)
- Player_buildNodePathKey @0x6B5C1C: segment="/"+*(ttstr*)(node+0)label; 父链=*(uint32*)(node+36)parentIndex;
  prepend (sub_A1359C(segment,accum)=segment++accum); root(idx0)不emit; 空label仍emit"/".
  消费者只有 HM3: resetMotionState 0x6B2E08, pruneHM3 0x6B84C4 (对path做FNV hash).
- Player_buildNodeTree_recursive @0x6B4A6C: 0x6B4CE4 处
  *(uint32*)Player_nodePathMap_lowerBoundInsert(a1+3,&labelVar)=deque_idx;
  key=Motion_propGetByName(node,L"label")@0x6B4CA8=raw PSB label. parentIndex写node+36.
  每node 2x requireLayerId→node+16/+20.
- Player_buildNodeTree @0x6B51F0 post-pass: type==12 && (node+52 stencilType &4):
  遍历 node+2576 maskList, find(a1+3, raw元素@Motion_propGetIndexVariant 0x6B5454);
  目标门控 tgt.type==3||==0; 设 tgt+1961=1; push 进 node+2600/2608/2616 向量.
- Player_nodePathMap_find @0x6F2228 / lowerBoundInsert @0x6B50DC: RBNode i[2]right/i[3]left/
  i[4]key(ttstr)/i+5(=+40)value(int idx); 比较 sub_9B1ED0(wide ttstr); insert sub_6F1DC8.
- Player_findNodeByRawLabel @0x6B5AD8: find(Player+24, a2透传 raw); create标志=1时插桩.

## reader 站点 (全部 raw, 无一调 buildNodePathKey)
- Player_getLayerMotion @0x6D38F4 / Player_getLayerGetter @0x6D3998: raw TJS name 实参
- D3DEmotePlayer_contains @0x530BB4: raw TJS name
- EmoteEngine_resolveShapeAnchor @0x67B9CC: raw shape label
- sub_6BC000 @0x6BC174: node+536*slot+836 内 target-label PSB 字段 (raw)
- childMotion case4 connectTarget @0x6BE7B4: node+536*slot+684 connectTarget (raw)
- particleEmitter @0x6BF048 / cameraNode_type5 @0x6BDB00: node 内字段 (raw)
