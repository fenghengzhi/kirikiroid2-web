---
name: getlayernames-mapping
description: Player::getLayerNames 真实地址 0x6D10E0 (非 sub_6D1018); 发 raw label; sub_6D1018=processedMeshVerticesNum; sub_6B601C=mesh-count visitor
metadata:
  type: project
---

**纠正：getLayerNames 不是 sub_6D1018。** IDA 把两个独立函数合并了 (0x6D10E0 有自己的 SUB SP #0x80 序言)。已在 IDB 拆分+重命名+save。

- **Player_getLayerNames @0x6D10E0** (NCB "getLayerNames" @reg 0x6D88C8): sig (Player* this, tTJSVariant* args@X1, tTJSVariant* result@X8). 创建 TJS Array(sub_704CB8), 中序遍历 **Player+24 std::map<ttstr,int>** (header@+32, leftmost@+48, _Rb_tree_increment=sub_1485230), **emit 每个 node KEY = RAW PSB "label" ttstr (node+32), 按 key 升序**。无 path/前缀/拼接。args[0]=可选子串过滤 (ttstr_indexOf=sub_A0CBEC, 含子串才发; 无参发全部)。无 nodeType/visible 门控。**不调用 sub_6B601C, 不碰 node deque, 不下降 type3/type4。**

- **Player_getProcessedMeshVerticesNum @0x6D1018** (NCB Property "processedMeshVerticesNum" @reg 0x6D8850): 返回 *(this+1152) + 跨 child-player 累加。用 visitor sub_6B601C + accum callback sub_6F55D4 (`**ctx += getProcessedMeshVerticesNum(child)`).

- **Player_visitChildPlayerDispatches @0x6B601C**: 遍历 Player+200 node deque(stride 2632, 不跳 idx0). type4->particle Array@node+2296, type3->child-Player dispatch@node+1912 (PropGet idx200). 每元素调 a2.callback(a2, dispatch_ptr). 传 dispatch 指针非字符串。**仅 mesh-count 用, 与 layer names 无关。**

- Player+24 map key 来源: buildNodeTree_recursive@0x6B4CA8 `Motion_propGetByName(L"label")`=raw label, lowerBoundInsert(a1+3=+24) value=deque index.

**本地对账**: PlayerLayerQuery.cpp:143 getLayerNames 遍历 _nodeLabelMap 发 key = 已与 binary 对齐 (注释里 "PORT DIVERGENCE→sub_6B601C / M5-2 re-port" 基于错误映射, 应删)。缺口: 本地无 args[0] 子串过滤。
