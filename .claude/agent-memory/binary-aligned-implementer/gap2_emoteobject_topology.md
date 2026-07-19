---
name: gap2-emoteobject-topology
description: Gap2 EmoteObject 拓扑重构落地。2026-07-13 纠正 +16 容器为 vector<ttstr> 资源路径，并恢复多参数 load→构造期逐路径加载链。
metadata:
  type: project
---

# Gap2 EmoteObject 拓扑重构 (2026-06-03 落地)

## 权威拓扑 (fresh-decompile EmoteObject_init@0x67DBAC / destroy@0x67F420 确认)

EmoteObject 40B 字段:
- +0 ResourceManager* (operator new(0xE8)=232B, ctor sub_6A88CC)
- +8 EmoteEngine* (operator new(0x5D8)=1496B, EmoteEngine_ctor)
- +16 vector<ttstr> (资源路径数组, ttstrVector_assign_67F0CC)

init 流程: 建 RM(+0) -> sub_67E20C 包 TJS dispatch(2x AddRef) -> EmoteEngine_ctor(收 dispatch 包装非 RM 本体) -> assign +16 vector -> 逐 PSB loadResource。
**Player 的 resourceManager = dispatch 包装(iTJSDispatch2*), 非 RM C++ 对象本体**。RM 本体只 EmoteObject 持有。

dtor@0x67F420 序: ① +8 EmoteEngine(sub_67F4B8+delete) ② +0 RM(sub_6A8B94+delete) ③ +16 vector(逐元素 Release+delete buffer)。

## 本地落地状态

- **G2-A DONE；所有权机制于 2026-07-18 再纠正**: 旧 by-value `ResourceManager` + `shared_ptr<State>` 会生成 EmoteObject RM 与 adaptor RM 两个对象，已被 `0x67DBAC/0x67E20C/0x67F420` 证伪。现为构造体内 `new ResourceManager`、sticky adaptor 指向同一对象、dtor 按 engine→RM→vector 手动释放。
- **G2-B CORRECTED + DONE (2026-07-13)**: 旧 `vector<tTJSVariant>` 结论被 producer/consumer 交叉证伪。现为 `std::vector<ttstr> _modulePaths`；D3DEmotePlayer `load` raw callback 收集全部参数，EmoteObject 构造期逐 path 调 RM.load，最后 loaded variant 进入 snapshot/Player；clone 复用整条 path vector。
- **G2-C DONE (keystone, LIVE)**: EmoteEngine.cpp:1939-1961 bind-loop 实装。遍历 _labelToValueHM7, 每 label: accumulateTimelineContributionLike_0x67C560(mutate value) -> shouldMirrorEvalLabelLike_0x67C6B0(negate) -> bindParameterValueLike_0x6C4668(label, negate?-v:v)。
- **G2-D DEFERRED (risk-can-stop, 非 evidence)**: setVariable 双写 shim 保留。原因: round-trip test(motionplayer-dll.cpp:638-639) setVariable->getVariable 无 progress() 间隔, bind-loop 只在 progress 跑, 删 shim 会让 getVariable 返 0.0 破坏 round-trip 且无 oracle 验 bind-loop 正确性。EmotePlayer.cpp setVariable 注释已标 G2-D DEFERRED。

## bind-loop callee 映射 (binary EmoteEngine 字段 -> Player-side 端口)

2026-07-18 更正：binary sub_67C560/67C6B0 读 EmoteEngine 状态，本地也已归回同一 receiver：
- sub_67C560 -> EmoteEngine::accumulateTimelineContributionLike_0x67C560，直接读 HM3@+936、active labels@+1040 和 56B track deque；
- sub_67C6B0 -> EmoteEngine::shouldMirrorEvalLabelLike_0x67C6B0，直接读 vector@+800 与 HM1/HM2 set caches@+824/+880；
- Player_bindParameterValue@0x6C4668 -> Player::bindParameterValueLike_0x6C4668(ttstr,double) (PlayerVariable.cpp:606): 写 HM1(_evalCascadeMap[joined].writeVal)+HM2(_evalResultValues[raw]) = getVariable 读的两个 map
- Player friend class EmoteEngine -> EmoteEngine 可调 Player private bind helper。旧“路由到 Player 建模”是过渡态，已被证伪。

## CORRECTED 既证伪结论

- class-layout-auditor blueprint 的 `+0 ResourceManager*` 是正确证据；旧实现仅补 by-value/shared-state 仍不忠实。2026-07-18 已改成真实 owning pointer，偏差消除。
- PlayerFrameProgress.cpp:649 旧注释 "bind-loop STUBBED/inert" — G2-C 后已证伪, 就地改为 "LIVE, only crosses on progress()"。
- IDA setVariable@0x671228 a1 注释 "EmotePlayer 1576B" 仍待 rename 为 EmoteEngine 1496B(本会话未改 IDB, 留作 follow-up — 无 idb_save 权变更)。

## 构建/验证

- web/debug + wasmtime guest 双绿(仅编辑现有 .cpp, 无新文件, 无 preset 重跑)。
- macos 单测 motionplayer-dll **无法编译**(PRE-EXISTING, 与 G2 无关): contains(x,y) 2-arg 重载已删(M11 D-09)+ TimelinePlayFlagSequential 已 rename(M11 D-02), 测试文件 2026-05-30 后未同步。round-trip 运行时验证缺口=已有测试 drift, 非 G2 回归。
