---
name: m3-container-keytype-verdict
description: M3 cluster (4 Player HM + var-track deque) container-selection fresh-decompile 2026-06-03/2026-06-04 — 4 HM 确证是 libstdc++ std::unordered_map(非 inline),无需重写; 旧 2 key-type gap 已 RESOLVED; 真实剩余=populate-wiring(HM3/HM4 空)
metadata:
  type: project
---

Fresh-decompile 2026-06-03 (read-only) 核实 M3 cluster 容器选型两大命题。
2026-06-04 二次复核(read-only): 旧 GAP-A/GAP-B 两个 key-type retype 均已落地,见文末更新。

**命题1: 4 个 Player HM 是 libstdc++ std::unordered_map,不是 inline open-addressing → 无需 STL→inline-HM 重写。CONFIRMED。**
- 证据: insert 0x686A4C 字面调用 `std::__detail::_Prime_rehash_policy::_M_need_rehash`; rehash 0x686C5C 是教科书 libstdc++ `_M_rehash`(size==1 走 single-bucket a1+48 优化, 否则 operator new(8*a2)+memset; 按 node+24 cached hash 重链; before-begin sentinel = 控制结构+16)。node=operator new(0x20)={+0 next,+8 key(ttstr owning AddRef),+16 value(raw double),+24 cached hash}。控制结构48B:{+0 bucket,+8 nbkt,+16 before-begin,+24 count,+32 loadFactor 1.0f,+40 next_resize}。
- 本地 _evalCascadeMap/_perNodeLayerStateMap/_variableSnapshotMap 用 unordered_map<ttstr,V,ttstr_hash,ttstr_equal> = 正确选型,已对齐。**player_4_hashmaps.md 与 Player.h:341 用"内联/inline KiriKiri HM"措辞误导**——它们就是标准 std::unordered_map(自定义 ttstr hash),勿据此做 open-addressing 重写。

**命题2: getVariable(0x533E1C) 与 setVariable(0x671228) 操作 disjoint maps。CONFIRMED。**
- setVariable 0x671228 this=EmotePlayer(~1576B),写 HM7@+1440 + 5 controller deque(+256/336/416/576/656)。getVariable 读 Player+264(HM1)/+320(HM2)/+1240(HM4)。不同对象不同 offset,桥接仅 progress bind-loop,非同一 HM2。

**旧 2 GAP 均已 RESOLVED (2026-06-04 复核确认):**
- GAP-A(原高,已修): `_evalResultValues`(HM2 镜像 @Player+320) 现为 `detail::LabelValueMap`=unordered_map<ttstr,double,ttstr_hash,ttstr_equal>(Player.h:1361, 注释记 2026-06-03 retype)。写/读全程用 detail::widen(label) 或直接 ttstr key(PlayerVariable.cpp:394/454/494)。✅ 对齐。
- GAP-B(原中,已修): `_nodeLabelMap`(Player+24) 现为 `detail::NodeLabelMap`=std::map<ttstr,int,ttstr_utf16_less>(Player.h:1216, player_containers.h:84)。比较器 ttstr_utf16_less 对应 sub_9B1ED0 UTF-16 code-unit 序。✅ 对齐。注:Player.h:1158-1159 的 `_layerIdsByName`/`_layerNamesById`(std::string 键)是 Web host 的 Cocos/DOM layer-id 适配 map,在 1384B 二进制无 HM 对应(port 发明),**不是** Player+24——别把它们误判成 GAP-B 残留。

**EvalCascadeState 偏移已确证(本次复编 0x6C4664/0x6C4668)**: 二进制 HM1 node writeVal=node+32(0x6c4968 `*(double*)(v30+32)=a4`), weight=node+40=1.0(0x6c4964 首插)。即 player_4_hashmaps.md 的 V+48/V+56 是错的; value_structs.h:144 的 V+32/V+40 是对的。(node+8/+16 是 chainDispatches vector, node+48 是 type3/4 controller list head。)

var-track deque(Player+1296): isLabelInBindScopeList 0x6CD16C 扫 std::deque(stride 160B=20 qword, cascadeKey@+0, 比较 sub_9B1ED0)。本地 VariableLabelScope(160B 字段对齐)+VariableLabelScopeDeque=std::deque ✅。

**真实剩余 GAP = populate-wiring(非容器选型,非 key-type):**
- HM3(_perNodeLayerStateMap)与 HM4(_variableSnapshotMap)本地**恒空**:唯一 writer 是 Player_resetMotionState_clearAndRebuild@0x6B2D3C(loop2 写 HM4, loop3 写 HM3),该函数本地无 port + 无 caller。HM3 key=Player_buildNodePathKey@0x6B5C1C(path ttstr,非 raw label,与 Player+24 不同 key 空间), value 填充=Player_HM3_initValueFromNode@0x699510(688B node→V 快照,读 ~24 个 node 字节偏移,本地未暴露)。HM4 key=var-track item ttstr label, value=item+16(由 interpolateVarTrackValues@0x6BBE20 HOLD/LERP 算出)。两者皆 DEFERRED(已在 Player.h 注释标注地址)。getVariable cascade 因此 HM4-first 永远 miss → 落 HM1/HM2(bindParameterValue 已 live),logo 路径不受影响。
- 架构忠实度注: getVariable 本地是 motion::Player 方法,二进制 0x533E1C 是 EmoteEngine 级 wrapper(读 engine+1064=Player*)再调 sub_6CD16C/6CD23C/6CD39C。本地把三段(scope-scan→HM4→HM1/HM2)合进单个 Player::getVariable,语义等价但少一层 dispatch 包装;非容器问题。
