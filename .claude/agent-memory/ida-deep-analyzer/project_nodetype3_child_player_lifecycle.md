---
name: nodetype3-child-player-lifecycle
description: nodeType=3 child Player 完整生命周期(创建/释放/dtor级联)+ child+0 共享 root owner + child+8 非拥有反向链 + re-init 同 this 不是 new
metadata:
  type: project
---

nodeType=3 sub-player 的**正常成功路径**由 TJS-refcount + C++ dtor 级联释放，无拥有环。边界例外：`CreateAdaptor/sub_6F1794@0x6F1794` 返回 0 时不接管或释放传入 native，`Player_initNodeFields@0x6B46D4..0x6B46E0` 只把 void Variant 写入 node+1912，因此该失败路径会留下未拥有的 native；禁止在移植端用主动 `delete` “修安全性”。

**创建** (Player_initNodeFields@0x6B3C78 case3 @0x6B43C0):
- `operator new(0x568)` 无条件建 child Player(1384B),NO 复用检查(注释已在 IDB)。
- `Player_ctor(child, parent+992)` 用 parent 的 RM dispatch 作 ctor 参数。
- 0x6B43D0/0x6B43DC `v27=*parent; STP X8,X20,[X21]`：`child+0=parent+0`，即共享的 root/render-owner 裸指针；**child+8=parent this 裸指针(无 AddRef)**。该结论由 ctor@0x6CED70 的 `child+0=child`、render@0x6C2804/0x6C2B8C，以及 camera@0x6BDB88 取 `*(Player**)this`、0x6BDB9C..0x6BDBC0 读取 draw-affine 交叉确认；旧“vtable word”解释错误。
- 创建 child 的 NCB dispatch via `sub_6F1794`(CreateAdaptor→QueryInterface dword_1AB8848→native instance,设 instance+8=child),AddRef,存入 **node+1912** tTJSVariant(0x6B46E0 sub_A0FB64 CopyRef,覆盖旧值)。失败时 CopyRef 的是 void Variant，native 不会被 delete。

**释放路径(核心)**:
- node 从 deque erase 或 deque 整体销毁 → 每个元素跑 `MotionNode_destroy_guess`(sub_6F4Cxx)。
- 该 dtor @0x6F4D2C `tTJSVariant_dtor_guess(node+1912)` → 释放 child dispatch ref → child native-instance Destruct(sub_6FDFFC/6FE040,gate `!*(inst+16)`) → `Player_dtor(child)` → `operator delete` → **递归**(child 自己的 dtor 又销毁它的 node deque,释放孙 child+1912)。
- Player_nodesDeque_destroy(sub_6F436C) 对每个 erased 元素调 MotionNode_destroy_guess。

**两个触发点**:
1. **同 player rebuild**: Player_buildNodeTree→resetAndReleaseNodes@0x6B56F8 内 deque.erase(sub_6F3E0C)→Player_nodesDeque_destroy→释放旧子树。re-init **在同一 this 上**(initNonEmoteMotion@0x6B365C 全用 a1,调 buildNodeTree(a1)@0x6B3A80)。binary 从不为 re-play 新建 Player。
2. **顶层 Player 丢弃**: TJS object refcount→0 → native-instance Destruct(sub_6FE040)→Player_dtor@0x6CFADC。dtor 内 `Player_nodesDeque_destroy(this+184/+23...)@0x6CFB80` 销毁全部 node→级联释放整棵子树+1912。dtor 还释 +992 RM/+636/+656 RM dispatch/+1012/4 hashmap/controllerDeque/SLA(+760 via this+95? sub_6CFFB8@0x6CFB54)。

**child+0/+8 不成环**: 两者均为裸存，Player_dtor 不 Release。child+0 指向整棵子树共享的 root/render owner，child+8 指向 immediate parent；子树唯一拥有边=parent node+1912→child dispatch(单向)。

**本地泄漏定位**: 本地"每 10s new 新 motion Player,旧的从不 reset/dtor"= 偏离二进制。二进制要么 (a) re-init 同 this(buildNodeTree reset 释放旧子树),要么 (b) 旧 TJS object refcount 归零触发 Player_dtor 级联。本地若新建 C++ 对象但没有把旧对象接入 TJS-refcount→Destruct→dtor 链,则 deque 析构(MotionNode dtor 释 +1912)永不触发 → 整棵 nodeType=3 子树泄漏。缺的就是 Player_dtor 的 Player_nodesDeque_destroy 级联(或 buildNodeTree 的 resetAndReleaseNodes 级联)从未在被丢弃对象上跑。
