# Motionplayer 根可达分母阶段性审计（已重新打开，2026-08-27）

> 终态注记（2026-08-29）：本文是 2026-08-27 纠正过早闭包结论的阶段性报告，其中“当前明确
> 开放项”已由后续逐任务工作闭合或按证据边界收口。当前权威状态为 146 `CLOSED_STATIC`、
> 15 `VERIFIED`、2 `EVIDENCE_BLOCKED`，见 `analysis/motionplayer_tasks_status.tsv` 与最终重建报告；
> 下文旧状态不得再作为当前未完成清单。

## 1. 纠正说明

本报告原先把后期154个 semantic slice的聚合对账误写成了 `tasks.md` 全范围的根可达
最终闭包。随后按 `tasks.md` 的163个原始 ticket逐项重建分母时，发现该结论不成立，现已
撤销“最终完成”状态。

直接反例包括：

- `MP-A30`要求恢复 `D3DEmotePlayer` 独立类层级、typed factory、clone和参考故意保留的
  TODO行为；当前 coverage没有任何含 `D3DEmotePlayer` 的独立行；
- `EmoteAngleController`、`EmoteBlinkController`、`EmoteEyebrowController`、
  `EmoteLoopController`、`EmoteMouthController`、`EmoteSelectorController`、
  `EmoteSpring`、`EmoteVarController`、`EmoteWindEmitter`及其内部容器/owner没有独立
  coverage映射；
- `D3DEmoteModule`、`EmoteMeshResolver`、`EmoteBlinkRng`等本地对象族同样未进入现有
  implementation-path分母；
- `tasks.md` 的 MP-V03..V07要求正式 Wasmtime/ADB/render差分、Web Debug构建和unit-test
  runtime；当前机器没有完成这些验证，coverage中 `VERIFIED=0`。

因此，以下旧推论全部失效：

1. “非 NCB root-reachable helper/function-pointer/vtable已完整闭合”；
2. “对象、controller、容器和owner分母已完整闭合”；
3. “最终只剩两个平台浮点边界”；
4. “152 IMPLEMENTED + 2 PLATFORM_BOUNDARY等于 tasks.md全部完成”。

两个浮点平台行仍是有效的已知边界，但不再被解释成唯一剩余项。

## 2. 当前权威分母

新的权威任务分母是：

- `tasks.md` 中163个唯一原始任务ID；
- 生成台账 `analysis/motionplayer_tasks_status.tsv`；
- 生成器 `tools/motionsim/generate_tasks_status.py`。

台账默认把没有逐要求直接证据的项目标为 `OPEN_UNAUDITED`。相似命名、聚合报告、单个
coverage slice或一次negative grep不能自动把原始ticket提升为完成。

原有 `analysis/motionplayer_coverage.tsv` 继续作为已完成语义纵切面的证据源，但不再作为
`tasks.md` 163项的替代分母。MP-F03..F07以及错误加入的最终acceptance聚合行已经重新标为
`EVIDENCE_BLOCKED`，直到逐项台账证明完整闭包。

## 3. 已确认仍有效的成果

以下成果没有因本次纠正失效：

- 四个目标二进制和四个IDB身份已核对；
- 三条模块/依赖根已定位；
- motionplayer.dll NCB候选316/316，`UNMAPPED=0`；
- Player、ResourceManager、SourceCache、ObjSource、D3DAdaptor、SeparateLayerAdaptor、
  updateLayers、frameProgress和主渲染链已有大量四端闭合slice；
- Private `__Private_Motion_GLLayer` class/vtable/Draw_GPU链已闭合；
- coverage TSV 12列、ID唯一和引用路径校验，以及账本生成器重生成仍有效；
- 选定translation unit的syntax-only检查与 `git diff --check`仍有效。

这些是逐项台账的证据输入，不是全范围完成证明。

## 4. 当前明确开放项

至少包括：

- MP-A30/MP-A31的D3DEmotePlayer与完整DrawDeviceD3D公开表面；
- MP-L05..L08、MP-L14..L16涉及的EmoteObject/Engine/controller owner和clone/state边；
- MP-C06..C08的controller内部deque/vector/track/spring/selector/wind容器；
- MP-R11..R15的controller、RNG、spring、外力和wind状态机；
- MP-V03..V07的正式差分、Web构建和unit/runtime验证；
- MP-V09..V11的 `_guess`、旧名称/注释、inline/dead-strip逐项审计；
- 所有仍为 `OPEN_UNAUDITED` 的原始ticket。

后续每闭合一个原始ticket，都必须在 `motionplayer_tasks_status.tsv` 中写明：对应四端证据、
coverage slice、分析报告、本地实现、验证和剩余gap。只有163行均达到允许的终态，才能重新
发布真正的最终根可达报告。
