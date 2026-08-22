# MotionPlayer `Player` / `EmoteEngine` receiver 旧注释迁移（四参考，2026-08-15）

## 1. 范围与结论

本轮重新以 `reference/binaries/` 四份当前参考产物校准 `Player.h` 尾部一组旧
Android arm64 注释。结论是：

1. `Player::_noUpdateYet` 是真实的 Player per-frame byte；构造置一，
   `updateLayers` 结束时清零，type-3 child-motion 与 type-6 particle-emitter 路径读取；
2. Motion.EmotePlayer 的 `directEdit` byte 位于外层 `EmoteEngine`，getter、one-way
   setter 和 progress physics gate 在四端命中同一个 byte；
3. 本地 `Player::_physicsDisabled` 没有对应 native Player member，也没有任何本地
   reader/writer，是把 Engine `directEdit` 的旧 A64 偏移误贴到 Player 后留下的伪字段；
4. progress dirty byte、raw wind owner/cache、hair/parts/bust scale triplet 都由
   `EmoteEngine` 拥有；Motion.EmotePlayer 和 D3DEmotePlayer 只是访问层，不产生
   Player 或 D3D shell 副本；
5. Engine progress 的 metadata controller families 是 #4/#5/#6/#8/#9/#10，旧源码
   中“#4-#9”及一串 A64 cursor offset 不能表达真实源结构。

可执行 header 现在只记录这些跨 ABI 的 receiver、owner、数据流和生命周期语义。
本文件集中保存绝对地址与物理 offset；旧 `libkrkr2.so` 注释不参与裁决。

## 2. `Player::_noUpdateYet`

### 2.1 字段矩阵

| 目标 | Player ctor | 字段偏移 | ctor 初始化点 | `updateLayers` 清零点 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CC110` | `+608` | `0x6CC4B4` | `0x6B91D8` |
| Android armv7 | `0x5935C4` | `+408` | `0x5937E4` | `0x5860AA` |
| iOS arm64 | `0x10011EC04` | `+496` | `0x10011EE3C` | `0x10010F064` |
| iOS armv7 | `0x11D488` | `+344` | `0x11D824` | `0x10C8B6` |

Android arm64 的 ctor 使用合并 word store，同时得到
`noUpdateYet=1, adjacentReverseFlag=0`；其余目标可能拆成 byte/word/vector store，
但最终字节值和邻接顺序一致。`Player_updateLayers_guess` 四端入口分别为
`0x6B9080 / 0x586020 / 0x10010EFC4 / 0x10C846`，都在一次 layer 更新完成后清
`noUpdateYet`，不是在 frame core 入口提前清。

### 2.2 消费者

两个独立 pass 在首次 update 时读取它：

| 消费者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| type-3 child-motion pass read | `0x6BBA48` | `0x58820A` | `0x100111788` | `0x10EA02` |
| type-6 particle-emitter pass read | `0x6BC3E0` | `0x588970` | `0x100111BD4` | `0x10F3EC` |

两条路径都用该 byte 选择 mode-2 crossfade derivative 的首次更新分支；普通累计
delta 尚不存在时不会误用上一帧导数。由于字段物理 offset 在四 ABI 中不同，
`Player.h` 删除了旧 `// player+608`，保留构造、消费者与清零时序描述。

## 3. Engine `directEdit` 与伪 `Player::_physicsDisabled`

### 3.1 属性访问器

| 目标 | directEdit getter | directEdit setter | Engine byte offset |
|---|---:|---:|---:|
| Android arm64 | `0x67F358` | `0x67F360` | `+1159` |
| Android armv7 | `0x562104` | `0x56210A` | `+591` |
| iOS arm64 | `0x1001B61F4` | `0x1001B61FC` | `+791` |
| iOS armv7 | `0x1B5FDC` | `0x1B5FE2` | `+407` |

四个 getter 都直接返回这一 Engine byte。四个 setter 都不读取、不转换也不比较
传入的 `tTJSVariant` Boolean，而是无条件把常数 `1` 写到同一个 byte；因此脚本侧
写 `false` 仍会得到 true。这个 one-way trigger 行为必须保留。

`EmoteEngine_progressCore_guess` 入口为
`0x67A3F8 / 0x55FEF0 / 0x1001B4304 / 0x1B3E10`。其非零 dt physics-tail gate 在
`0x67A7F4 / 0x55FFB6 / 0x1001B43E8 / 0x1B3EE0` 读取上表同一 Engine byte；置一后
跳过 physics-only pass。通用 `setVariable` 还用它决定 category 0..2 是走专用
controller 还是 fall through 到 HM7。

### 3.2 为什么删除 `Player::_physicsDisabled`

旧字段声明为 `bool _physicsDisabled = false; // player+1159`，把 Android arm64 的
**Engine** offset 当成了 **Player** offset。四端证据同时否定这一 receiver：

- 属性 getter/setter 的 receiver chain 是 Motion.EmotePlayer wrapper -> EmoteObject ->
  EmoteEngine；
- progress core 的 receiver 自身就是 EmoteEngine；
- 四端没有通过 inner `Player*` 访问这一 byte 的对应路径；
- 仓库内该本地字段除声明外零引用，没有构造后 writer、runtime reader 或析构行为。

所以它不是尚未接线的兼容状态，而是会人为扩大、错排 portable Player 的结构残留。
本轮直接删除声明，并把 Engine backing member 从会与 Player 同名混淆的
`_syncWaiting` 改为 `_directEdit`。inner `Player::_syncWaiting` 仍是独立的 timeline
cooperative-stop 状态，其写入点、事件与 releaseSyncWait 行为完全不变。

## 4. dirty、wind 与 scale 的 Engine 所有权

### 4.1 相邻 Engine 区域

| Engine member/region | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| raw wind owner | `+1128` | `+564` | `+760` | `+380` |
| five wind floats | `+1136..+1152` | `+568..+584` | `+768..+784` | `+384..+400` |
| directEdit byte | `+1159` | `+591` | `+791` | `+407` |
| progress dirty byte | `+1162` | `+594` | `+794` | `+410` |
| hair/parts/bust doubles | `+1184/+1192/+1200` | `+616/+624/+632` | `+816/+824/+832` | `+428/+436/+444` |

四端 `EmoteEngine_ctor_guess` 入口为
`0x67B76C / 0x560948 / 0x1001B7FB0 / 0x1B7788`。constructor 将 wind raw owner 和
五个 float cache 清零，directEdit 置零，并把三个 scale 初始化为 `1.0`。dirty 的
member-initializer 状态是 false；随后 ctor body 在四次 direct-controller seed 前
反复写 true，所以成功构造的 Engine 最终 dirty=true，构造失败展开仍只销毁已完成前缀。

wind owner 是单个 raw owning pointer。重建时先 delete 旧对象，等 replacement 完整
初始化后才发布新 pointer；若中间分配/构造抛出，native 的历史 dangling-slot 边界不能
被 `unique_ptr::reset` 悄悄改变。bust/hair springs 只借用该对象作为 collision curve。
Player 的 start/stop wind 入口在 portable 层只是通过非 owning Engine back-pointer
转发，不拥有 emitter 或 cache。

### 4.2 D3D scale forwarding

代表性的 D3D getter（setter 紧邻且沿同一 receiver chain）如下：

| 目标 | hair getter | parts getter | bust getter |
|---|---:|---:|---:|
| Android arm64 | `0x5304D0` | `0x5304F0` | `0x530510` |
| Android armv7 | `0x494A6E` | `0x494A8A` | `0x494AA6` |
| iOS arm64 | `0x100232EA8` | `0x100232EC8` | `0x100232EE8` |
| iOS armv7 | `0x231AFA` | `0x231B16` | `0x231B32` |

每个访问器都执行 D3D shell -> primary EmoteObject -> EmoteEngine -> triplet field。
没有 null guard、clamp、dirty write、Player forwarding 或 shell cache。Motion.EmotePlayer
的同名属性也命中同一组三个 Engine double。于是 `Player.h` 只保留“无 duplicate
scale/directEdit byte”的边界说明，`EmoteEngine.h` 删除三个字段上的旧 A64 offset，
保留连续顺序、默认值和 physics multiplier 语义。

## 5. Engine controller families 的旧注释纠正

fresh `EmoteEngine_progressCore_guess` 显示，实际 metadata/controller step families 是
#4 eye、#5 eyebrow、#6 mouth、#8 transition、#9 selector、#10 loop。#7 是 clamp/range
角色，不是旧注释中用连续“#4-#9”能够表达的第六个 step family。各 family 的 deque
header/cursor offset 随 old-libstdc++/libc++ 和指针宽度变化；输出则统一发布到
Engine HM7，再通过 Player bridge 消费。

因此 `EmoteEngine.h` 的 removed parallel-controller residue 注释现在明确列出
`#4/#5/#6/#8/#9/#10 -> HM7`，不再保存 Android arm64 cursor 或 HM7 offset。没有
恢复任何 Player-side animator bucket。

## 6. 源码结果与验证要求

本轮修改：

- `Player.h`：移除 `_noUpdateYet` 的单端 offset；删除伪 `_physicsDisabled`；把
  scale、dirty、wind 说明改成 receiver/owner 语义；
- `EmoteEngine.h`：把 Player owner、wind cache、directEdit、scale triplet 和
  controller-family 注释改成跨 ABI 语义；
- 不触碰尚待专门四参考复核的 explicit padding、layout-suffix field names 或其它
  container trace comments。

删除 bool 是真实结构修正，不是纯注释迁移。因此必须用完整 motionplayer unit
translation unit 和 Web Debug final link 验证，不能只依赖局部 grep。四份 recovery IDB
也必须保存本轮 noUpdate/directEdit/scale/Engine-owner 校准注释与书签。
