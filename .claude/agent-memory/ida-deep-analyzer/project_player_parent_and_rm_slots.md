---
name: player-parent-and-rm-slots
description: Player+8 = parentPlayer 链 (NOT port 发明); RM dispatch 三槽 +636/+656/+992 用途; child Player 继承机制
metadata:
  type: project
---

# Player+8 = parentPlayer 链 (binary 确证, 非 port 发明)

P3-B 证据核实 (2026-06-05, dossier=analysis/MotionPlayer_P3B_Evidence_2026-06-05.md):

- **Player+8 = parent Player 指针**。ctor 0x6ced70 初始化 +8=0 (根 Player 无 parent)。
- 写入点 `Player_initNodeFields@0x6b43dc` (stencilType==3 child 构造): `*(child+8)=parent`。
  同处 0x6b43cc `Player_ctor(child, parent+992)` —— child RM 参数 = parent+992 dispatch (继承)。
- 读取点 `sub_6B1ABC@0x6b1bb8` (变量值上溯): self HM2(+320) 查 label miss → `v8=*(Player+8)` 取 parent →
  扫 parent+280 type3/type4 node-list (v21 链) → 继续上溯。全链 miss 返回 0.0。
- 本地对应: PlayerVariable.cpp:267 `player=player->_parentPlayer` 上溯 = 这条链 (但本地每层只查
  HM2/_evalResultValues, 省略了 binary 每层 parent+280 node-list 扫描 0x6b1c1c..0x6b1d88)。

**结论: `_parentPlayer` 100% 有 binary 对应字段 Player+8, 非 port 发明。** CLAUDE.md M7 教训正例。

# RM dispatch 三槽 (ctor 0x6CED30 单参)

ctor 单参 `(this, iTJSDispatch2* rm_dispatch)`, 无 parentPlayer。同一 RM dispatch 拷三份:
- **+636** (0x6cee9c): findSource self-object dispatch。findSource@0x694928 PropGet(idx dword_1AB8098)
  解包出 RM native ptr → 直读 native +224(spec int)/+88(HashMap A bucket)/+96(bucket count)。
  fallback FuncCall L"findSource"@0x69547c。混合: dispatch-facade-over-native。
- **+656** (0x6ceeb0): 渲染期 RM (renderToCanvas 0x6c75ac / sub_6C9CA8)。未逐行展开。
- **+992** (0x6cef28): 规范 RM。getResourceManager@0x6d9414 (NCB "resourceManager") 返回它;
  findMotion@0x6d056c / loadMotion@0x6b1478 经它 FuncCall L"findMotion"; child Player 从此继承。

工厂 Player_factory@0x6f6dc0 → sub_6F6E6C 取 createInstance 第一个 TJS 参数(RM dispatch)→ ctor。

# 本地 RM ownership 偏差 (P3-B 迁移面)

本地持 native `_resourceManagerNative` (ResourceManager value) + 反包 `_resourceManager` variant (方向相反);
binary 只有三个 dispatch variant 槽。child 继承本地是 `child->_native=_native` (拷 native),
binary 是 child ctor 参数=parent+992 dispatch。迁移面清单见 dossier 块 3。
