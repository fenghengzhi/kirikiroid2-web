---

> **历史记录（2026-07-19 已被取代）：** 此 `(owner,label)` clip-key 修补曾解决旧
> flattened snapshot 架构的问题；当前实现已直接消费 `Player+528` raw TJS dispatch，
> `MotionClip/clipList/clipIndex/layerList/sourceCandidates` 均已删除。下文不得再作为
> 当前架构说明。
name: dracu-clip-owner-keyed-isolation-done
description: DRACU title childMotion 无限递归根因=motion clip 按 label key 跨 object 合并 layerList；修复=clip 改 (owner,label) key 复刻二进制 motion/<chara>/<motion> 路径段隔离
metadata:
  type: project
---

DRACU-RIOT 标题屏 childMotionPass 同步无限递归黑屏，根因=端口 motion-clip 跨 object 合并。已修复 (owner,label)-keyed，auditor 完全对齐，web/debug clean。

**二进制隔离机制（ida 实测+反编译双重确认，函数地址权威）**：隔离发生在 **PSB 路径的 chara 段 motion/<chara>/<motion>**，不是复合 key 表，更没有"全局 label→motion 表/Player+548 priority 表聚合"。
- Player_loadMotion @0x6B0F10：sub_A0CC68(L"motion/", chara)@0x6b1308 + sub_A1359C(.,"*/")@0x6b1320 + sub_A1359C(.,motion)@0x6b1380 → findMotion(FuncCall idx16,接收者 Player+992)@0x6b1478 按完整路径导航 PSB 子树。chara=Player+968。
- Player_playImpl @0x6B2284：play 不按 label 查找，直接用 loadMotion 返回 content(v46)→content.FuncCall(idx0)@0x6b24dc 写 Player+528(layer 数组宿主)。chara=*(player+968) @0x6b22e8 LDR X8,[X19,#0x3C8]。
- Player_buildNodeTree @0x6B51F0：从 +528 读 "layer"@0x6B529C 建树。递归体 0x6B4A6C 把每 layer 的 RAW "label"@0x6B4CA8 插 Player+24 label→nodeIndex map@0x6B4CE4(每实例独立 last-wins lowerBoundInsert,跨 object 不共享)。
- ⟹ char/show vs TITLE/show 因 chara 段不同(char vs TITLE)导航到不同 PSB 子树→不同 +528 content→不同 layer[]→两棵独立树。label "show" 从不跨 object 作查找 key。注意：问题线索里的 "+548 priority[] 聚合表" 在 loadMotion/playImpl/buildNodeTree 三函数中**不存在**，是端口发明结构。

**端口架构差异(既有平台边界,本次未动)**：端口 snapshot 是**文件级扁平缓存**(loadMotionSnapshot@RuntimeSupport.cpp:1320 加载整个 PSB→scanValue 遍历整棵树→maybeRecordMotionClip 把所有 object 的 motion 拍平进一个 snapshot)，不是二进制的单 motion content @+528。但 clip.owner=path[size-3]=PSB object(chara)名**已记录**，数据在。

**bug**：clipIndexByLabel 按裸 label key→char/show 和 TITLE/show 撞 key→maybeRecordMotionClip line 1013 clip.layerList.push_back 合并两 object 的 layer=char(12)+TITLE(26 含 3× type3 自引 bg→motion/char/show)=38 节点→自引触发无限递归。TREEDIAG 实测端口 char/show 树 38 节点(node12-37=TITLE 菜单+3 bg type3)。

**修复(忠实复刻 chara 段隔离,把隔离维度从隐含的 PSB 路径段折进 clip key)**：
- RuntimeSupport.h: clipIndexByLabel→clipIndexByOwnerLabel(map<pair<owner,label>,int>) + 保留 clipIndexByLabel 作 label-only last-wins 回退 + 新增 MotionSnapshot::findClipIndex(owner,label)(先 owner-scoped 再回退)。
- RuntimeSupport.cpp maybeRecordMotionClip: dedup/reuse 由 (owner=path[size-3], label) 决定,不再跨 object 合并 layerList。
- PlayerCore.cpp selectActiveClip: owner=narrow(_chara),findClipIndex(owner,label)。
- NodeTree.cpp buildNodeTree: 新增 clipOwner 参数,findClipIndex(clipOwner,clipLabel)。signature 4 处声明同步(NodeTree.h/Player.h×2/def)。
- PlayerMotionLoad.cpp: caller 传 clip->owner(回退 narrow(_chara));onFindMotion 探测@:100 findClipIndex(charaRaw,motionRaw)。

**logo 非回归保障**：logo fixture chara="LOGO"/label="yuzulogo",二进制 analysis/ev_yuzulogo_execution_chain.md:756 实测拼路径 "motion/LOGO/yuzulogo.mtn"=同机制。若 logo 记录 clip 则 owner="LOGO"==_chara 命中;若 logo PSB 无 object/motion 嵌套则 clipList 空走 snapshot.layerList fallback(NodeTree.cpp:363)=改动 inert;万一 owner≠_chara,findClipIndex label-only 回退=改前纯 label-only 行为=零回归。(注:macos mtndump OWNERPROBE 因 vcpkg libarchive 构建失败无法跑,环境问题非本改动,用 analysis 文档+回退路径三重论证替代,无 fixture 触发跨 object 场景=honest gap)。

**auditor 非阻塞观察**：findClipIndex owner 非空但 (owner,label) miss 时落 label-only 回退,理论可跨 owner 命中;二进制此情形 findMotion 导航失败返空,语义略出入。但这是既有回退路径保留(非本修复引入),DRACU 场景良性,无 fixture 触发。后续 PSB 出现该形态时复核。
