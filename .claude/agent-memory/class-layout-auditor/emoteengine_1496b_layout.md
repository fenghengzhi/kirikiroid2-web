---
name: emoteengine-1496b-layout
description: EmoteEngine 1496B 类布局审计权威记忆 — 10 deque + 7 controller heap obj 偏移表 + HM2@+1440(非1384) + 本地 EmotePlayer.h:56-101 inline 实现的 5 大偏差 + P0/P1/P2 重构路线图
metadata:
  type: project
---

# EmoteEngine 1496B 类布局审计核心记忆

## 二进制权威偏移表（不再 grep 反编译报告，直接查这里）

ctor=0x67E38C, progress=0x67D01C, size=0x5D8=1496B, **无 vtable** (POD)

| 偏移 | 类型 | 用途 |
|---|---|---|
| 0 | std::deque<48B elem> #1 | Hair/Parts spring 节点 |
| 80 | std::deque<56B> #2 | Bust chain #1 节点 |
| 160 | std::deque<56B> #3 | Bust chain #2 节点 |
| 240 | std::deque<16B> #4 | eye/mouth state machine (sub_663BDC) |
| 320 | std::deque<16B> #5 | 单值变量 (sub_665600) |
| 400 | std::deque<24B> #6 | 复合变量 label1+label2 (sub_666068) |
| 480 | std::deque<40B> #7 | 关键帧存储池 (_guess, no step) |
| 560 | std::deque<24B> #8 | 辅助单值 (sub_666BF8) |
| 640 | std::deque<48B> #9 | 向量变量 6×QWORD (sub_668470) |
| 720 | std::deque<16B> #10 | 预烘焙曲线查表 |
| 800-864 | scalar (OWORD+5 int) + float=1.0f@856 | 未细分 |
| 856..1023, 1272.., 1328.. | 4 个 KiriKiri inline `vector reserve(10)` 块 | **未详细反编译** _guess |
| 1064 | Player* (a1[133]) | 独立 0x568=1384B 堆对象 |
| 1072 | EmoteVarController* 0x80 count=2 | **Position (x,y)** |
| 1080 | EmoteVarController* 0x80 count=1 | **Scale (uniform)** |
| 1088 | EmoteVarController* 0x80 count=4 | **Color RGBA** |
| 1096 | EmoteAngleController* 0x70 | **Angle/Rotation** (shortest-path wrap) |
| 1104 | EmoteVarController* 0x80 count=2 | Hair/Parts physics target |
| 1112 | EmoteVarController* 0x80 count=2 | Bust #1 target |
| 1120 | EmoteVarController* 0x80 count=2 | Bust #2 target |
| 1128-1167 | OWORD ×2 zeroed | 矩阵字段 |
| 1160 | int32_t=1 | (未知) |
| 1162 | **byte _dirty** | progress 主循环 dirty check（本地误放 Player::_emoteDirty）|
| 1168 | double | scale 分母 (_guess) |
| 1184 | double | bust chain #1 spring const |
| 1192 | double | bust chain #2 spring const |
| 1200 | double=1.0 | (a1[150]) |
| **1440** | **libstdcxx std::unordered_map<ttstr,double>** HM2 | labelToValue physics 输出表 |
| 1456 | linked list head | bind 链表 (progress 尾部遍历) |

## 7 controllers 是 POD (两类不同 size, **没有 vtable**)

- `EmoteVarController` 0x80 = 128B: deque<KeyValue20B>(80B) + count/state/3个堆 float* + powCount/phase/invDuration/pad
  - step: sub_666BF8 @ 0x666BF8 (SIMD lerp on count×4 floats)
- `EmoteAngleController` 0x70 = 112B: deque<KeyValue12B>(80B) + state/3 float rad/invDur/powCount/phase/pad
  - step: sub_666634 @ 0x666634 (shortest-path angle wrap)

## 本地 EmoteEngine 5 大偏差（位于 cpp/plugins/motionplayer/EmotePlayer.h:56-101 inline）

1. **HM2 偏移错+类型错**: 本地 `_variableAnimators` @ +1384 + `unordered_map<string, VariableAnimatorState>`，应 @ +1440 + `<ttstr, double>`. Player size 1384 < 1440，HM2 物理上不可能在 Player
2. **缺 7 个 controller heap 对象** (1072-1120): 本地完全没有 EmoteVarController/EmoteAngleController 类
3. **缺 5 个 deque** (offsets 0/80/160/480/720): 本地只 declare 5 个 (240/320/400/560/640，且偏移注释写 +256/+336 是错的)
4. **5 个 deque 的 element type 全部错**: 本地用统一 VariableAnimatorState (~130B+)，二进制是 5 种独立 16/16/24/24/48B POD
5. **Player::_emoteDirty / _emoteMeshDivisionRatio*** 位置错误: 这些字段属 EmoteEngine 不属 Player（与 [[player-1384b-flat-spec]] "Player 1384B" 一致 — 偏移 1162/1168 都 > 1384）

## 容器选型偏差（CLAUDE.md 禁止现代惯用法替代）

- `std::unique_ptr<Player>` (EmotePlayer.h:100) / `std::unique_ptr<EmoteEngine>` (EmotePlayer.h:116) → 二进制是 raw ptr + manual new/delete
- 5 个 deque<VariableAnimatorState> → 5 种各自不同 element type
- 1 个 unordered_map<string, VariableAnimatorState> → libstdcxx ABI alias + ttstr key + double value

## 重构路线图（P0/P1/P2）

P0:
1. 抽 EmoteEngine.h 独立头文件
2. 新增 EmoteVarController.h (0x80) + EmoteAngleController.h (0x70) POD 类
3. 重构 EmoteEngine 字段表按偏移升序（10 deque + 7 ctrl ptr + HM2 alias @+1440）
4. Player::_emoteDirty / _emoteMeshDivisionRatio* 迁回 EmoteEngine
5. 实现 EmoteEngine_progress 主循环骨架

P1:
- EmotePhysics_springStep + stepHairParts + stepBust 物理算子
- 矩阵字段 +1128/+1184/+1192/+1200 重命名 _bustSpring1/2

P2:
- 5 个 sub_xxxxxx step 函数 (sub_663BDC/665600/666068/666BF8/668470)
- 4 个 inline KiriKiri `vector reserve(10)` 块（待详细反编译）

## PLATFORM_BOUNDARY 决策（本目录目前无任何 PB 注释）

- HM2 @+1440: 用 detail::LabelValueMap alias type 占满 56B (libstdc++ ABI)，与 Player+264/+320/+1184/+1240 4 个 KiriKiri HM 一致策略
- std::deque 头部 80B: libstdc++/libc++ ABI 一致，**不是边界**
- 4 个 KiriKiri inline vector reserve(10) 块: 不是 std::vector，是 24B 控制块，待反编译，先 PB 占位

## 关键引用

- 反编译详细规格: [[../ida-deep-analyzer/EmoteEngine_controllers.md]]
- Player 1384B 字段表（确认 Player size < 1440）: [[player-1384b-flat-spec]]
- KiriKiri HM 容器选型策略: [[player-container-layout]]
