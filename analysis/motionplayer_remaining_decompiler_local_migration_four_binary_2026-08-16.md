# MotionPlayer 剩余反编译局部名迁移（MotionSub / phase-2 / particle，2026-08-16）

> 2026-08-16 后续说明：本文的“全量扫描”针对计算表达式中的 `vNN` 局部；它遗漏了
> metadata builder 的循环变量以及注释/字段中沿用的 `aN`、`v0/v1` 标签。最后一批已由
> `motionplayer_final_pseudocode_identifier_migration_four_binary_2026-08-16.md` 用四份 fresh
> 证据迁移；本文三条计算局部的结论仍有效。

## 结论

compiled-source 全量扫描在 spring wrapper 之外只找到三处真正的 `vNN` 计算局部：

- MotionSub angle path 的 `v37`；
- updateLayers phase-2 opacity path 的 `v46`；
- particle spawn angle-inherit path 的 `v154`。

本轮用四个当前 recovery IDB 的完整函数 fresh decompile 重新确认三者的数据来源、所有
consumer 和分支顺序。三处都只是未迁移的 Hex-Rays 临时名，不是未知字段或额外状态；
源码只改名/排版，公式、load 次序和 IEEE 边界保持不变。

## 1. MotionSub：`blendedAngleOffset`

| 目标 | `Player_updateMotionSubNodes_guess` |
| --- | ---: |
| Android arm64 | `0x6BB4A0` |
| Android armv7 | `0x587E00` |
| iOS arm64 | `0x100110EEC` |
| iOS armv7 | `0x10E68C` |

局部先取 active slot 的 `motionDofst`。只有
`motionDocmpl && crossfading && !other.done && other.motionDt != 0`，且 active/other
dofst 不相等时，才由短角度 crossfade helper 改写它。mode 2、3、4 的 `atan2` 结果都加
这份可能 blend 的 offset；mode 1 刻意继续使用 raw dofst。

四端 fresh decompile 在 mode 2/3/4 的加法处分别反复使用同一 local，例如 A64
`0x6BBB58/0x6BBC0C/0x6BBD04/0x6BBD58`，A32 `0x58828E/0x5883B4/
0x588430/0x588756`，iOS A64 `0x100110FC8/0x100111160/0x1001111FC/
0x100111270/0x1001112E4`，iOS A32 具有相同结构。源码名由 `v37` 改为
`blendedAngleOffset`，没有把 mode 1 错接到 blend 值，也没有改变 normalize 循环。

## 2. phase-2：`opacityInheritFlags`

| 目标 | 完整 `Player_updateLayers_guess` | inheritFlags load / opacity branch |
| --- | ---: | ---: |
| Android arm64 | `0x6B871C` | `0x6B8BDC..0x6B8C10` |
| Android armv7 | `0x5856E0` | `0x585B76..0x585BAA` |
| iOS arm64 | `0x10010E544` | `0x10010EABC..0x10010EAE4` |
| iOS armv7 | `0x10BE5C` | `0x10C558..0x10C574` |

该 local 是 node `inheritFlags` 的一次普通 32-bit load。bit `0x400` 选择 parent opacity；
bit 未置位且 Player `independentLayerInherit` 为 false 时选择 root opacity；否则不乘。
Android 两端还继续复用同一 flags local 进入后面的 transform inherit test，iOS 优化器可
拆成不同寄存器，但共同源角色不变。

源码只把 `v46` 改为 `opacityInheritFlags`。保留独立 scope 和 load 位置，不把它提升到
ground-correction 之前，也不与后面的 flags snapshot 合并，以免重入/未来字段写入改变
可观察 load 次序。

## 3. particle：`inheritedDirectionRad`

| 目标 | `Player_updateParticleSystems_guess` | inherit-angle block |
| --- | ---: | ---: |
| Android arm64 | `0x6BC4BC` | `0x6BD3C4..0x6BD3E8` |
| Android armv7 | `0x588A48` | `0x589302..0x58932C` |
| iOS arm64 | `0x100111D08` | `0x100112B34..0x100112B5C` |
| iOS armv7 | `0x10F51C` | `0x11049E..0x1104CC` |

`particleInheritAngle` 非零时，local 初值是 `dirRad + PI`；parent accumulated flipX
为 false 时覆盖回 raw `dirRad`。随后才乘 `360 / (2*PI)` 加到已经按 parent flip parity
选择正负号的 sampled particle angle，并用两个 while 归一化。

源码名由 `v154` 改为 `inheritedDirectionRad`。没有把条件重写成符号乘法或把 PI 移到
degree 域，因而保留原浮点结合、signed zero、Inf/NaN 和 normalize 行为。

## 4. provenance 与验证约束

- 地址只记录在本文和 IDB，不写入 compiled-source comments；
- stripped 函数/helper 名继续保留 `_guess`；
- 四个 recovery IDB 已在三组函数/分支补充语义角色注释并保存；
- compiled-source `sub_/loc_/LABEL_/vNN` 扫描不再命中这三条生产计算路径。
