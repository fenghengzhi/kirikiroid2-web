---
name: player-field-collisions
description: motion::Player port 端字段重复 / 命名漂移总表 — 5 处同一 binary 偏移有 ≥2 本地字段，含合并建议
metadata:
  type: project
---

# Player 字段碰撞表（截至 2026-05-31）

5 处确认的 binary 偏移 → port 双字段冲突。每条都是 P0 重构候选。

| binary 偏移 | binary 类型 | port 字段 A | port 字段 B | 真相 / 建议 |
|---|---|---|---|---|
| **+480** | byte (queuing/progress gate) | `_queuing` (Player.h:655) | `_progressFlags` (Player.h:699) | 同字段；保留 `_queuing`（与 NCB `queuing` 一致），删 `_progressFlags` |
| **+1120** | double (frameTickCount cursor) | `_frameTickCount` (Player.h:683) | `_frameLoopTime` (Player.h:650) | 同字段；保留 `_frameTickCount`，删 `_frameLoopTime`；PlayerFrameProgress.cpp:820 的 `_frameLoopTime += actualDelta` 是 port 发明的双轨累加，应删除 |
| **+481** vs **+1099** 名义混淆 | +481=byte firstFrame, +1099=byte allplaying | `_firstFrame` (Player.h:701) ✅ | PlayerUpdateChildMotion.cpp:169 把 `_allplaying = true` 解释为"写 +481" | binary 0x6BE4E8 `STRH W8, [X23, #0x1E0]` 16-bit 同时写 +480/+481；应改写为 `_queuing=true; _firstFrame=true`（不是 `_allplaying`）。`_allplaying` 是 +1099，由 timeline 路径独立设置 |
| **+1099** | byte (Player_getPlaying) | `_allplaying` (Player.h:659) | `_loopArmed` (Player.h:704) | **隐藏 H4 — 本次审计新发现**：二者都声称 +1099；PlayerFrameProgress.cpp:961/979 清 `_loopArmed`，PlayerCore.cpp:617 设 `_allplaying`；操作同一字节。binary `Player_getPlaying @ 0x6D9794` `return *(BYTE*)(this+1099)`。建议保留 `_allplaying`，删 `_loopArmed`；或反过来——但二者必须合并 |
| **+1156** | u32 (packed color weight) | `_colorWeightPacked` (Player.h:708) | `_parentColorPacked` (Player.h:964) | 同字段；保留 `_colorWeightPacked`，删 `_parentColorPacked`。sub_6BEB7C 写法 `*(_DWORD*)(child+1156) = *(_DWORD*)(node+100)` 表明 +1156 就是这一个 packed color，父子共享 |

## 单位转换 phantom

| port 抽象 | 行为 | binary 真相 |
|---|---|---|
| `setTickCount/getTickCount` (Player.h:195-198) | 写 `_frameTickCount * 60/1000`、读 `_frameTickCount * 1000/60` | binary `Player_getFrameLastTime @ 0x6D9664` 直接 `return *(double*)(this+1128)`，无 ms↔frame 转换；NCB 名 `tickCount` 应直接映射 `_frameLastTime`，不是 `_frameTickCount * scale` |

## EmotePlayer/EmoteEngine 影子重复

| binary 真实位置 | port 错误位置 | 修复 |
|---|---|---|
| Player+480 `_queuing` | EmoteEngine.h:422 也有 `bool _queuing` | EmotePlayer.h:140-141 delegate 到 `engine()._queuing` 错误；应直接 `setQueuing/getQueuing` 落 Player |
| Player+1097 `_independentLayerInherit` (Player_getColorWeightFlag) | OK | — |

## 未验证但怀疑的 phantom

- `_cameraFOV = 60.0` (double, Player.h:713)：binary `Player_getCameraFOV @ 0x6D96EC` (未亲自反编译) 读 +1100 byte。double vs byte 严重不匹配
- `_findMotionContextVariant` (Player.h:1044)：注释自己承认"exact semantic name is still under investigation"
- `_needsInternalAssignImages` 标 +613：未亲自反编译验证
- `_evalResultList`/`_evalResultListIndex` (Player.h:955-957)：list+index 模拟 LRU，binary HM2@+320 是单纯 hashmap，无 LRU；属于 port 发明

## 参考

- 完整字段表见 [[player-1384b-flat-spec]]
- HM 容器选型见 [[player-container-layout]]
- 反编译证据：`Player_progress_inner @ 0x6C106C`, `Player_updateLayers @ 0x6BBDF8`, `Player_updateLayers_childMotionPass @ 0x6BE0C0..0x6BE4FC`, `Player_getPlaying @ 0x6D9794`, `Player_getCompletionType @ 0x6D9634`, `Player_getPriorDraw @ 0x6D965C`, `Player_ctor @ 0x6CED30`, `sub_6D9920 @ 0x6D9920` (setUseD3DFlag +909)
