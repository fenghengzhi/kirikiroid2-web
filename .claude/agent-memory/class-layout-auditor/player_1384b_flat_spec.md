---
name: player-1384b-flat-spec
description: motion::Player (libkrkr2.so) 1384B 权威字段表 — 78 个 NCB 名 + 偏移 + 类型 + ctor 初值,由 Player_ctor (0x6CED30) + 78 个 getter/setter + setVariable 反编译三方验证
metadata:
  type: project
---

# motion::Player 1384B (0x568) 权威字段规格

operator new(0x568u) @ 0x6F6DC0 (Player_factory) 确认 sizeof(Player)=1384B。
NCB 暴露 78 个成员名 (Player_ncb_registerMembers @ 0x6D69C8)。

## 字段表（按偏移升序，**字段名 = NCB 暴露名 / 反编译来源**）

| 偏移 | 大小 | 类型 | 字段 (NCB名) | ctor 初值 | 证据 |
|---|---|---|---|---|---|
| 0   | 8  | vptr | _vptr | this | ctor *a1=a1 |
| 8   | 8  | ptr | _ownerEmotePlayer | 0 | ctor a1[1]=0; initNodeFields 写=a1 |
| 16  | 8  | u64 | _? | 0 | ctor a1[2]=0 |
| 24..48 | 32 | std::map(RBTree) | nodeLabelMap | sub_6DD228 cleared | ctor memset+sub_6DD228 in dtor |
| 48  | 16 | list sentinel | _listHead48 | self-link a1+4 | ctor a1[6]=a1[7]=a1+4 |
| 72..184 | 112 | scratch / params | (未细分,memset 0x48) | 0 | ctor memset(a1+9, 0, 0x48) |
| 184..264 | 80 | KiriKiri std::deque<MotionNode 2632B> | nodes | sub_6F4E90 init | ctor; +200=currentRoot |
| 200 | 8 | MotionNode* | currentRootNode | (deque begin) | getRootX 走 +200 |
| 208/216/224/240/256 | (deque internals) | block ptrs / size | | | getAllplaying/setVariable |
| 264..320 | 56 | KiriKiri HashMap1 (key→u32?) | HM1: variableKey-hash → entry | float load 1.0 ctor +296=1065353216 | sub_149EDF8 prime; dtor sub_6DD1A0 |
| 320..384 | 64 | KiriKiri HashMap2 (key→dispatch) | HM2: variableKey → dispatch | float load 1.0 +352 | getVariable +320 lookup; dtor tTJSVariant_Release(v[1]) |
| 384/392 | 16 | dynamic array stride 56B | renderList | 0/0 | dtor `v4+=7` 56B-stride |
| 400 | 8 | ? | | 0 | |
| 408/416/424 | 24 | someList (sub_6DD144) | sub_6DD144 cleanup | 0 | dtor sub_6DD144(a1+408, *(a1+424)) |
| 432..448 | 16 | list sentinel | _listHead432 | self-link a1+52 | ctor a1[54]=a1[55]=a1+52 |
| 448..484 | scratch params (memset 50h) | | 0 | ctor memset(a1+23,0,0x50) |
| 484 | 20 | ttstr | _emoteEdit (?) | empty | dtor sub_A0F778(a1+484) |
| 508 | 20 | ttstr | _label (?) | empty | dtor sub_A0F778(a1+508) |
| 528 | 20 | ttstr | _? | empty | dtor sub_A0F778(a1+528) |
| 548 | 20 | ttstr | _? | empty | dtor sub_A0F778(a1+548) |
| 568..608 | params | | 0 | ctor memset (overlap) |
| 608..612 | u32 | _? | 0 | |
| 616 | 20 | ttstr | _? | empty | dtor sub_A0F778(a1+616) |
| 636 | 20 | ttstr | _? | sub_A0F5E0 from a2 | ctor sub_A0F5E0(a1+636,a2) |
| 656 | 20 | ttstr | _? | empty | dtor +656 |
| 676 | 20 | ttstr | "color" | sub_A0FCC0 init | ctor PropGet "color" stored as ttstr; tjsRandomGen? (verify) |
| 696 | 20 | ttstr | _? | empty | dtor +696 |
| 716 | 20 | ttstr | _resourceManagerKey | from a2 dispatch via sub_A0FCC0 then PropGet "color" | ctor uses v18 dispatch |
| 736 | 20 | ttstr | _? | empty | dtor +736 |
| 760 | 8 | (something)* (delete-able) | _? | 0 | dtor sub_6CFFB8 + operator delete + clear |
| 768 | 8 | refcounted string value* | pending stealthMotion | 0 | Player_play flush + Release/null |
| 776 | 8 | refcounted string value* | pending stealthChara | 0 | chara-path flush + Release/null |
| 864..908 | 44 | sub_7E2344 inline container | _? | sub_7E2344 init | dtor sub_7E24AC |
| 908 | 1 | bool | _preview | 0 | ctor a1[908]=0; initNodeFields writes 1; getPreview reads +1096!? — actually +1096 is the NCB getter |
| 909 | 1 | bool | _useD3DFlag | 0 | setUseD3DFlag writes +909 |
| 910/911 | padding | | 0 | |
| 912 | 4 | int | _pixelateDivision | **100** | ctor +912=100; setPixelateDivision/getPixelateDivision |
| 916..960 | reserved/params | | 0 | ctor zero blocks |
| 960 | 8 | refcounted string value* | chara | 0 | NCB chara getter; dtor Release(+960) |
| 968 | 8 | refcounted string value* | stealthChara | 0 | NCB stealthChara getter; dtor Release(+968) |
| 976 | 8 | refcounted string value* | motion | 0 | NCB motion getter; dtor Release(+976) |
| 984 | 8 | refcounted string value* | stealthMotion | 0 | NCB stealthMotion getter; dtor Release(+984) |
| 992 | 20 | ttstr | _transformOrder | empty | dtor +992; initNodeFields copies +1012 oddly |
| 1012 | 20 | ttstr | outline | empty | get/setOutline |
| 1032 | 20 | ttstr | outline (2nd?) | empty | confirmed at +1032 in get/setOutline |
| 1052 | 20 | ttstr | meshline | empty | get/setMeshline |
| 1072 | 20 | tTJSVariant | motion `tag` frame-array dispatch | empty | written by initNonEmoteMotion; enumerated by skipToSync/progress |
| 1092 | 1 | bool | completionType | 0 | ctor +1092=0; get/setCompletionType |
| 1093 | 1 | u8 | _? (=byte_1AB84A8) | byte_1AB84A8 | ctor *((BYTE*)a1+1093)=v19 |
| 1094 | 1 | bool | cameraActive | 0 | get/setCameraActive |
| 1095 | 1 | bool | stereovisionActive | 0 | get/setStereovisionActive |
| 1096 | 1 | bool | preview | 0 | getPreview→+1096 |
| 1097 | 1 | bool | independentLayerInherit (colorWeightFlag) | 0 | getColorWeightFlag→+1097 |
| 1098 | 1 | bool | syncWaiting | 0 | getSyncWaiting→+1098 |
| 1099 | 1 | bool | playing | 0 | getPlaying→+1099 |
| 1100 | 1 | u8 | hasCamera/cameraFOV (raw flag) | 0 | getCameraFOV→+1100 reads byte |
| 1104 | 8 | double | cameraPosition | 0 | getCameraPosition |
| 1112 | 8 | double | coordinate | 0 | getCoordinate |
| 1120 | 8 | double | _? | 0 | |
| 1128 | 8 | double | frameLastTime | 0 | getFrameLastTime (NB: resourceManager getter also reads +1128, returns *1000/60) |
| 1136 | 8 | double | frameLoopTime | 0 | getFrameLoopTime / getLastTime → +1136 |
| 1144 | 4 | int | project | 0 | get/setProject |
| 1148 | 4 | int | maskMode | 0 | get/setMaskMode |
| 1152 | 4 | _? | | 0 | |
| 1156 | 4 | u32 (packed ARGB) | colorWeight | 0xFF808080 (-8355712) | ctor +1156=-8355712; get/setColorWeight |
| 1160 | 8 | double | priorDraw | **1.5** | ctor a1[145]=0x3FF8000000000000; getPriorDraw→+1160 |
| 1168 | 8 | double | meshDivisionRatio | 1.0 | setMeshDivisionRatio→+1168; ctor a1[146]=0x3FF0000000000000 |
| 1176 | 8 | double | outsideFactor | **1.0** | ctor a1[147]=0x3FF0000000000000; getOutsideFactor→+1176 |
| 1184..1240 | 56 | KiriKiri HashMap3 | HM3 (single-linked, sub_6DD018 cleanup) | float load 1.0 ctor +1216=1065353216 | dtor sub_6DD018 chain |
| 1240..1296 | 56 | KiriKiri HashMap4 | HM4 (dispatch-valued) | float load 1.0 +1272 | dtor Release(v[1]) |
| 1296..1384 | 88 | KiriKiri std::deque<160B> | _? deque | sub_6F4FD8 init | dtor sub_6CF678 |

## vtable
vtable 槽 0 写入 ctor 中 `*a1 = a1`,意味着 **vtable 通过 EmotePlayer 子类设置**(Player 基类本身没有虚函数 — ctor 只把 a1 写入自己作为占位)。

实际上 Player 的虚函数表在 EmotePlayer_ctor (0x67E38C) 中被覆盖。motion::Player 本体没有 virtual public 方法 —— 所有 NCB 暴露的方法通过 NCB callback 蹦床调用，不走 vtable。

## ctor 顺序关键步骤
1. `*a1 = a1` (vtable 占位)
2. a1[1]=0, +24/+40/+48..192 全 memset 0
3. +48/+56 list sentinel self-link
4. a1+184 deque<MotionNode> init (sub_6F4E90)
5. HM1 init: sub_149EDF8 prime=10 → +264/+272/+280/+296 fields
6. HM2 init: 同上 → +320/+328/+336/+352
7. +384..408 zero, +432/+440 list sentinel self-link
8. +484 ttstr init from a2 (sub_A0F5E0)
9. +656 ttstr init from a2
10. +864 sub_7E2344 inline container init
11. +912=100 (_pixelateDivision)
12. +924 ttstr init from a2
13. HM3 init → +1184/+1192/+1200/+1216
14. HM4 init → +1240/+1248/+1256/+1272
15. +1296 deque<160B> init (sub_6F4FD8)
16. v17=sub_9C8440(0) dispatch
17. ttstr +676 init from v17 (sub_A0FCC0)
18. v18=sub_9C8440(0) dispatch
19. ttstr +716 init from v18; PropGet "color" stored at +716
20. 最终一段写各 ttstr/double 默认值 + 创建第一个 MotionNode 节点(dword_1AA40D8 写入 deque)
21. tTJSVariant_Release(v18), Release(v17)

## 不可对齐边界
- **vtable**: 本地若继承自任何抽象基类必须与二进制一致;二进制中 Player 无虚函数,本地不应加 virtual
- **deque/hashmap**: 二进制用 KiriKiri 自定义实现(prime buckets+single-linked chain),本地用 std::deque/std::unordered_map 只能算 ⚠️
- **嵌入式 vs 指针**: 所有容器全部内联在 1384B 内, 无任何 unique_ptr/shared_ptr

参见 [[player-container-layout]] [[player-pimpl-split]]
