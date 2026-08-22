# MotionPlayer 测试 provenance 注释迁移（四参考，2026-08-15）

## 目标

本批只迁移两条已经过时的测试注释，不改变 production 或测试语义：

1. `motionplayer-ttstr-hash-test.cpp` 仍声称 hash 来自旧 `libkrkr2.so`；
2. `motionplayer-dll.cpp` 仍把 canvas direct gate 归因于单个旧 A64 内部地址。

两条注释都容易让后续恢复者把历史目标或函数内部位置误当成当前四参考身份，因此按当前
证据门槛重新取得四端函数证据后再迁移。

## ttstr hash 四端证据

| 目标 | 新鲜证据位置 | 形式 |
| --- | --- | --- |
| Android A64 | `Player_bindParameterValue_guess` `0x6C1A48`，核心混合 `0x6C1BB4..0x6C1BE8` | caller 内联 |
| Android A32 | `tTJSHashFunc_ttstr_Make` `0x497AFA` | 共享 specialization |
| iOS A64 | `tTJSHashFunc_ttstr_Make` `0x100039AEC` | 共享 specialization |
| iOS A32 | `tTJSHashFunc_ttstr_Make` `0x3798C` | 共享 specialization |

四端共同算法是逐 UTF-16 code unit 执行
`(1025*x) ^ ((1025*x)>>6)`，尾部乘 9、xor-shift 11、乘 32769；非 null payload 的最终零
hash 改为 `UINT32_MAX`。Player/Engine 私有容器还会读取或填写 `ttstr` backing 的 Hint cache。

因此测试头注释改为“四参考独立恢复”。同时纠正旧注释暗示的过强结论：相同 hash 是 native
bucket 选择的必要条件，但 libstdc++ 与 libc++ 的 bucket policy、node chain 和迭代顺序不同，
不能声称 Web 仅凭 hash 就会与 Android unordered iteration byte-for-byte 相同。

## canvas renderer 身份与 Web-only 字段隔离

| 目标 | 完整函数 |
| --- | --- |
| Android A64 | `Player_renderToCanvas_guess` `0x6C4820` |
| Android A32 | `Player_renderToCanvas_guess` `0x58E2CC` |
| iOS A64 | `Player_renderToCanvas_guess` `0x1001186E0` |
| iOS A32 | `Player_renderToCanvas_guess` `0x11653C` |

新鲜 lookup/disasm 再次确认四端 render dispatch 都属于上述完整函数。旧测试注释中的
`0x6C7B44` 既不是当前四参考映射表，也不能作为抽出的
`shouldUseDirectRenderPath_guess` 的独立 native function 身份。

本地 helper 表达四端 inline gate 的共同输入：item blend mode、Player completion type 与
item parent link。`visibleAncestorIndex` 和 `childItems` 是 Web 侧准备/诊断拓扑字段，不进入该
direct-path 判定；测试刻意修改它们，只用于证明 gate 不误读 Web sidecar 状态。注释现改成
上述语义，不再保留单目标绝对地址。

## 落地与验证

- 只修改两处测试注释；断言和可执行代码不变。
- `plan.md` 同步登记本批和此前已闭合的两个纹理异常矩阵。
- 四份 recovery IDB 在 hash 入口/内联块和完整 canvas renderer 入口补记 provenance 注释并
  原位保存；绝对地址只留在本分析映射表和 IDB 中。

