# MotionPlayer Web 诊断地址来源隔离（当前四参考，2026-08-15）

## 1. 结论

portable MotionPlayer 的 opt-in logo-chain Web 诊断中残留了四个裸地址标签：
`0x6BB33C`、`0x6BB598`、`0x6BBB6C`、`0x6D5B90`。它们来自仓库另一套以
Android `libgame.so` 为目标的 differential/Frida oracle 坐标，不是
`reference/binaries/` 当前四份参考的地址，也不是运行时算法常量。

这些字符串只作为 `logoChainTraceLogf(..., func, ...)` 的展示字段，不能驱动控制流；
但继续输出会把旧 oracle 坐标伪装成当前四参考证据。portable 源码因此改为语义标签：

| 诊断 stage | 旧 func 标签 | 新 func 标签 |
| --- | --- | --- |
| `updateLayers.phase1` / `.var` / `phase2.node` | `0x6BB33C` | `Player::updateLayers` |
| `updateLayers.phase2.parent_lookup` / `.accum_final` | `0x6BB598` / `0x6BBB6C` | `Player::updateLayersPhase2_MainLoop` |
| `draw.d3d` | `0x6D5B90` | `Player::renderToD3DAdaptor` |

Web sidecar 的 stage 名和消息 payload 保持不变；默认/native-shaped 数据路径、容器、
调用顺序和数值行为均未改变。

## 2. 当前四参考函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_updateLayers_guess` | `0x6B871C` | `0x5856E0` | `0x10010E544` | `0x10BE5C` |
| `Player_renderToD3DAdaptor_guess` | `0x6D2F70` | `0x59761C` | `0x100123844` | `0x122AAC` |

Android ARM64 最能直接显示污染：把旧标签当作当前地址解析时，`0x6BB33C` 落在
`Player_updateShapeGeometry_guess`，`0x6BB598` 和 `0x6BBB6C` 落在
`Player_updateMotionSubNodes_guess`，`0x6D5B90` 则落在
`Player_ncb_registerMembers_guess`。它们都不是标签声称的当前路径。

## 3. native absence

在四份 recovery IDB 中分别进行了普通 string 搜索，并对以下代表文本补做
ASCII/UTF-8、UTF-16LE 和 UTF-32LE byte 搜索：

- `updateLayers.phase1`；
- `draw.d3d`；
- `0x6BB33C`；
- `0x6D5B90`。

全部编码、全部目标均为零命中。`updateLayers.phase2.parent_lookup` 也在普通 string
搜索中四端零命中。结合两个 native 函数的 fresh lookup，可确定这些文本只属于本地
opt-in Web 诊断，不属于四参考原始字符串/调用链。

## 4. differential oracle 的来源边界

`tests/differential/oracle_runner/frida_motion_agent.js`、
`frida_motion_stage_agent.js` 与 `adapters/motion_playback.py` 仍显式声明自己的
Android oracle 偏移和 attach 契约。它们是另一目标文件的可执行配置，不是 portable
源码注释或当前四参考恢复名，本轮不擅自改写。后续若迁移 oracle，必须以所选择 oracle
文件的独立 build-id/架构/函数定位为依据，不能从本地语义标签反推偏移。

## 5. IDB 写回与验证

四份 recovery IDB 已在两个当前函数入口注明：native body 不含 logo-chain stage 或旧
地址标签，Web 诊断只能使用语义来源名；同时添加 bookmark 并原位保存成功。

源码修改后执行 `cmake --build out/web/debug -j 8`，相关 TU 重新编译并成功完成
`libmotionplayer.a`、`index.html` 与 `index.wasm` 链接；仅出现仓库既有 warning。
`git diff --check` 同样通过。这里不把仅存在于 Web sidecar 中的字符串替换表述为
native 行为恢复。
