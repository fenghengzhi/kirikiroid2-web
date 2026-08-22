# MotionPlayer Player 虚构属性状态清理：四参考二进制联合证据（2026-08-13）

## 结论

本地 `main.cpp` 已注明 `canvasCaptureEnabled`、`clearEnabled`、`hitThreshold` 不在
Motion.Player 的原生 92 项 NCB 表中，但 `Player.h` 仍保留了
`canvasCaptureEnabled`/`hitThreshold` 的公开 C++ getter/setter 和两份从未被读取的
backing state。这会让本地源结构看起来像原版 Player 有一条隐藏属性通道，和四个
当前参考二进制不符。

四端 fresh UTF-16 搜索和 xref 共同证明：

- `canvasCaptureEnabled` 每个目标只有一份宽字符串；唯一 xref 均位于
  `D3DAdaptor_ncb_registerMembers_guess`，对应 D3DAdaptor 的真实 read/write 属性；
- Player 连续注册区没有该名字，也没有 getter/setter descriptor；
- `hitThreshold` 的字符串虽然存在于其他模块/对象路径，但没有任何 xref 落入四端
  `Player_ncb_registerMembers_guess`；
- 本地两个 Player backing field 没有 consumer，setter/getter 也没有 caller；
- D3DAdaptor 的 `canvasCaptureEnabled` 字段、访问器、注册和 render consumer 是另一条
  已由四端证明的真实链，本轮完整保留。

因此本轮从 Player 删除两组 getter/setter 和两份死字段，并把 `main.cpp` 注释改成
明确的类所有权边界，而不是继续声称“C++ fields preserved”。

## 四端 `canvasCaptureEnabled` 证据

| 目标 | UTF-16 字符串 | 唯一注册 xref | 注册函数 |
|---|---:|---:|---|
| Android arm64 | `0x14D5BC8` | `0x6AA808` | `D3DAdaptor_ncb_registerMembers_guess` `0x6AA274` |
| Android armv7 | `0xD85670` | `0x57CD62/0x57CD6A` | `D3DAdaptor_ncb_registerMembers_guess` `0x57CC58` |
| iOS arm64 | `0x10195C0B6` | `0x100103B7C` | `D3DAdaptor_ncb_registerMembers_guess` `0x1001039A4` |
| iOS armv7 | `0x174E41A` | `0x100F50/0x100F56/0x100F6A` | `D3DAdaptor_ncb_registerMembers_guess` `0x100D94` |

四端字符串搜索均只返回这一处；没有第二份 Player 专用同名 literal。既有
`motionplayer_d3d_adaptor_four_binary_2026-08-11.md` 已记录 D3DAdaptor getter/setter、
field offset、constructor default 和 render gate，本专题不重复实现那条链。

## `hitThreshold` 与 absence 边界

四端均能找到 UTF-16 `hitThreshold`，但 xref 分属于其他函数族：

- Android arm64 的 xref 位于 `0x6C3F28`、`0x6C90C4`、`0x8165D4`；
- Android armv7 位于 `0x58DCD4`、`0x591DEC`、`0x63A7DC`；
- iOS arm64 两份 literal 分别进入 `0x1000858A8`、`0x100117E88`、
  `0x10011C628`；
- iOS armv7 两份 literal 分别进入 `0x83B44`、`0x115B34`、`0x11AE24`。

上述列表没有任何一项是四端 Player 注册函数：

```text
Android arm64  Player_ncb_registerMembers_guess 0x6D3DA8
Android armv7  Player_ncb_registerMembers_guess 0x597EC8
iOS arm64      Player_ncb_registerMembers_guess 0x1001244F8
iOS armv7      Player_ncb_registerMembers_guess 0x123848
```

“字符串存在于完整插件”不能推出“字段属于 Player”。这里以注册函数所有权和本地
consumer 集合为边界，避免把相邻对象的成员错误并入 Player。

## 本地对象结构修复

删除内容仅限 Player：

```text
Player::set/getCanvasCaptureEnabled
Player::set/getHitThreshold
Player::_canvasCaptureEnabled
Player::_hitThreshold
```

保留内容：

```text
D3DAdaptor::set/getCanvasCaptureEnabled
D3DAdaptor::_canvasCaptureEnabled
D3DAdaptor NCB canvasCaptureEnabled property
D3DAdaptor renderFromPlayer gate
```

这项清理不改变任何原生可达 Player 行为；它删除的是本地虚构且不可达的状态面，减少
未来字段布局、生命周期和属性所有权分析中的假分支。

## 验证

- `Web Debug Build`：完整重编 motionplayer 并最终链接成功；
- `Wasmtime Headless Debug Build`：完整重编 motionplayer/guest objects 并最终链接成功；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  参数执行 `-fsyntax-only` 成功，唯一诊断为仓库既有 `_tss` 弃用警告；
- `git diff --check` 通过，仅输出工作树既有 LF→CRLF 提示；
- 四份 IDB 均已在 D3DAdaptor 唯一注册点注明类所有权并保存。
