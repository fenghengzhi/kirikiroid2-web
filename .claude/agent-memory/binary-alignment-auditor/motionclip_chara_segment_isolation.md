---
name: motionclip-chara-segment-isolation
description: motion-clip 隔离的二进制权威机制(chara 段路径导航,非复合 key 表) + 端口 (owner,label) keying 修复对齐结论
metadata:
  type: project
---

motion-clip 隔离发生在 PSB **路径的 chara 段** "motion/<chara>/<motion>"，不是复合 key 表。已三函数独立反编译核实(2026-06-22 审计 ✅ 完全对齐)。

**Why:** DRACU-RIOT 标题屏 childMotionPass 无限递归黑屏，根因=端口按 bare label 做 clip key，把 title.psb 的 char/show 与 TITLE/show 两个同名 motion 的 layer 合并进同一 clip，多出 TITLE 自引 type3 bg → 无限递归。

**反编译证据(地址全核实):**
- Player_loadMotion @0x6B0F10: sub_A0CC68(L"motion/",chara)@0x6b1308 + sub_A1359C(.,"*/")@0x6b1320 + sub_A1359C(.,motion)@0x6b1380 → findMotion(player+1012, FuncCall idx16)@0x6b1478 按完整路径导航 PSB。a2=chara/a3=motion 由 onFindMotion@0x6b106c 从 player+992 RM PropGet 回填。
- Player_playImpl @0x6B2284: chara=*(player+968)@0x6b22e8 → loadMotion@0x6b2330 → content.FuncCall(idx0)@0x6b24dc → player+528@0x6b2500。play 全程不按 label 跨 object 查找,content 纯由 chara 段路径决定。
- buildNodeTree_recursive @0x6B4A6C: key=layer.PropGet("label")@0x6b4ca8=RAW PSB label(非 buildNodePathKey path); Player+24.lowerBoundInsert(a1+3)@0x6b4ce4=last-wins,每实例独立(a1=this),跨 object 不共享。

**端口修复(平台边界=snapshot 文件级扁平缓存):**
- 写入: maybeRecordMotionClip 用 (owner=path[size-3], label) keying — RuntimeSupport.cpp:970-993; gate@:955-957 校验 path 形如 object/<owner>/motion/<label>。
- 查询: MotionSnapshot::findClipIndex(owner,label) — RuntimeSupport.h:228-245,owner-scoped 优先 + label-only last-wins 回退(仅 miss 时)。
- 4 消费点全 owner-scoped: selectActiveClip(owner=narrow(_chara)) PlayerCore.cpp:1104; free buildNodeTree NodeTree.cpp:360; onFindMotion 探测 PlayerMotionLoad.cpp:106; buildNodeTree caller(clip->owner 回退 _chara) PlayerMotionLoad.cpp:441-444。
- clipIndexByLabel 外部裸读取点=0(grep 确认),仅写入:993+findClipIndex内部回退:240。

**How to apply:** 审计 motion 选择/clip 解析路径时,凡涉及"同名 motion 跨 object"必查是否经 findClipIndex owner-scoped;勿把 clip 隔离误判为复合 key 表机制。非阻塞残留: findClipIndex owner 非空但 (owner,label) miss 时 label-only 回退可能跨 owner 命中,二进制此情形 findMotion 导航失败返空,语义略出入,但无 fixture 触发(既有回退路径保留,非本修复引入)。
