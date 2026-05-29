---
name: player-pimpl-split
description: motion::Player 本地 pimpl 拆分映射 — 哪些二进制内联字段落在 Player.h 直接成员,哪些落在 PlayerRuntime;以及本地多出/类型错/偏移误标的字段清单
metadata:
  type: project
---

# motion::Player 本地 pimpl 拆分 (Player.h + PlayerRuntime) vs 1384B 二进制

二进制无 pimpl,1384B 全部内联。本地拆为:
- `class Player` 直接成员 (Player.h:563-773): 标量/bool/ttstr/少量 tTJSVariant
- `shared_ptr<PlayerRuntime>` (RuntimeSupport.h:223): 全部容器 + 渲染管线

## PlayerRuntime 承载的二进制内联容器
| 二进制偏移 | 容器 | PlayerRuntime 成员 |
|---|---|---|
| +24 node label map | std::map | nodeLabelMap |
| +184 节点 deque (2632B) | KiriKiri deque | nodes (std::deque<MotionNode>) |
| +264/+320/+1184/+1240 | 4 哈希表 | 🔬未定 ↔ motionsByKey/timelines/layerIdsByName/layerNamesById/renderLayerStates/disabledSelectorTargets (6个,映射未确定) |
| +384 renderList (56B) | 裸数组 | preparedRenderItems (std::vector<PreparedRenderItem ~400B>) |
| +936 variableList (44B) | 裸数组 | variableLabelEntries |
| +1296 deque (160B) | KiriKiri deque | 🔬未定 |

## 已知字段错误 (写入 TODO P0)
- `_pixelateDivision` (+912=100) **缺失**
- `_project` 类型错: tTJSVariant 应为 tjs_int (+1144)
- `_completionType` 类型错: int 应为 bool (+1092)
- `_tjsRandomGenerator` 偏移误标 player+992,实为 player+676
- `ttstr _transformOrder` (+992) **缺失**
- `_hairScale/_partsScale/_bustScale` 误标 player+1184/1192/1200 — 这是 **EmotePlayer 1496B 变体**偏移,1384B Player 的 +1184 是哈希表 HM3,误植
- `_colorWeightPacked` 与 `_parentColorPacked` 都标 +1156,实为同一字段

## 本地多出 (二进制 1384B Player 不存在,疑似 EmotePlayer 变体或本地新增功能等价物)
_variableAnimators, _type4..8ControllerAnimators, _evalResultList/Index,
_mirrorPositive/NegativeCache, _emote*State, _windState, _*OuterForce — 需审计归属。

参见 [[player-container-layout]]。权威偏移表: analysis/Player_Class_Layout_libkrkr2so.md;
对齐 TODO: analysis/Player_Class_Layout_Alignment_TODO.md。
