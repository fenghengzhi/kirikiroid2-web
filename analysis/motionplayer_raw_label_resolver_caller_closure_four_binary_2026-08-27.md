# Player raw-label resolver 全 caller 闭包（MP-D10，2026-08-27）

## 1. 结论

`Player::findNodeByRawLabel_guess` 的源码级 caller 分母已经闭合。四端共同有十种使用
语义：五个 `recursive=false` 的 build/update consumer、`getLayerGetter`、
`getLayerMotion`、递归 visitor，以及 Motion.EmotePlayer/D3DEmotePlayer 两个 raw-label
shape hit-test facade。

Android armv7、iOS arm64、iOS armv7 都保留 9 个对 resolver entry 的直接 code xref；
Android arm64 只有 5 个，因为编译器把五个 `recursive=false` call 全部内联。四端的
源码调用集合相同，没有额外未解释 caller。所有 caller 已由现有 `IMPLEMENTED`
build/update/facade/producer slice 或本报告中的 `getLayerMotion` body 承接，本地调用点和
flag/owner 边界逐行一致，因此 `MP-D10-RAW-LABEL-RESOLVE` 可升级为 `IMPLEMENTED`。

## 2. resolver 本体

| 目标 | resolver | 完整指令数 | 直接 code xref 数 |
|---|---|---:|---:|
| Android arm64 | `Player_findNodeByRawLabel_guess@0x6B2EB8` | 80 | 5 |
| Android armv7 | `Player_findNodeByRawLabel_guess@0x58220C` | 69 | 9 |
| iOS arm64 | `Player_findNodeByRawLabel_guess@0x100109EEC` | 69 | 9 |
| iOS armv7 | `Player_findNodeByRawLabel_guess@0x10777C` | 102 | 9 |

共同主体仍是：

```text
findNodeByRawLabel(name, recursive):
    it = rawLabelMap.find(name)
    if it != end:
        index = uint32(it.value)
        return address of nodes[index]       // no sign/range check

    if !recursive:
        return null

    found = null
    visitChildPlayerDispatches([&](child):
        found = child.findNodeByRawLabel(name, recursive)
        return found == null                 // first hit stops traversal
    )
    return found
```

Android arm64/armv7 和 iOS arm64 的递归 `std::function` capture 走 heap callable；iOS
armv7 的相同三字段 capture 落入 libc++ small-object buffer。这是 STL/ABI 差异，不是
显式平台分支。map 命中的 deque addressing 分别体现 Android 单元素 block 和 iOS
16 元素 block；portable 源继续使用 `std::map<ttstr,int>` 与 `std::deque<MotionNode>`。

递归 child visitor 的 type-4 shipped bug 保持不变：循环 count 次但每次读取 particle
Array index 0；type-3 直接读取 child-player Variant。child null、损坏 map index 和重入后
live deque end 都没有保护。

## 3. 完整 caller 等价类

| 使用语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | flag |
|---|---|---|---|---|---|
| stencil post-link in buildNodeTree | inline in `Player_buildNodeTree_guess@0x6B25D0` | direct in `...@0x581CC8` | direct in `...@0x1001097C8` | direct in `...@0x107060` | false |
| camera constraint phase | inline in `...@0x6B93E0` | direct in `...@0x586228` | direct in `...@0x10010F22C` | direct in `...@0x10CA04` | false |
| camera node target | inline in `...@0x6BAE08` | direct in `...@0x587748` | direct in `...@0x1001108C4` | direct in `...@0x10E048` | false |
| motion-sub mode 4 target | inline in `...@0x6BB4A0` | direct in `...@0x587E00` | direct in `...@0x100110EEC` | direct in `...@0x10E68C` | false |
| particle-emitter model-dt 4 target | inline in `...@0x6BC1B0` | direct in `...@0x588820` | direct in `...@0x100111A6C` | direct in `...@0x10F2CC` | false |
| getLayerGetter | `Player_getLayerGetter_guess@0x6D0CD4` | `...@0x595EF4` | `...@0x100121D64` | `...@0x120B2C` | true |
| getLayerMotion | `Player_getLayerMotion_guess@0x6D0D78` | `...@0x595F74` | `...@0x100121E38` | `...@0x120C2C` | true |
| recursive callable operator | `Player_findNodeByRawLabel_recursive_visitor_guess@0x6EF6EC` | `...@0x5AD280` | `...@0x100141B54` | `...@0x142B6A` | captured true |
| Motion.EmotePlayer contains | `motion_EmotePlayer_contains@0x67EEEC` | `...@0x497BFE` | `...@0x1001B5E84` | `...@0x1B5B74` | true |
| D3DEmotePlayer contains | direct helper `Player_hitTestLayerByRawLabel_guess@0x530F3C` | wrapper `D3DEmotePlayer_contains_wrapper_guess@0x4950F0` calls shared body | wrapper `...@0x100233558` calls shared body | wrapper `...@0x2322EC` calls shared body | true |

后面三个目标复用 `motion_EmotePlayer_contains` 的 body；Android arm64 为 D3D owner chain
保留独立 hit-test helper，同时 Motion.EmotePlayer callback 自身也直接调用 resolver。
因此 A64 的两个 hit-test xref 与其余三端“一个 shared body + 一个 wrapper caller”是同一
源码 facade 集合的不同优化结果。

## 4. caller 边界

### 4.1 五个非递归 consumer

- buildNodeTree 的 stencil mask post-link 只解析本 Player 的 raw-label map；miss 跳过，
  命中 type 0/3 才追加 borrowed pointer；nodeCount/maskCount 是各自一次性快照。
- camera constraint miss 回退 synthetic root。
- camera node 的 backed target miss 让 `targetNode=null`，但 focus 回退当前 camera node。
- motion-sub mode 4 miss 使 angle absent。
- particle-emitter model-dt 4 miss 写 valid=false，但保留旧 XYZ。

这五处都必须传 `false`；若误用递归搜索，child motion/particle 中的同名 layer 会改变
camera/mask/angle/emitter 结果。Android arm64 的内联不是另一套 resolver。

### 4.2 getLayerMotion

四端 callback 完整指令数为 45/40/25/59。共同流程：

```text
temporaryLabel = ttstr(inputVariant)
node = findNodeByRawLabel(temporaryLabel, true)
destroy temporaryLabel
if node != null:
    CopyRef node.childPlayerVariant into result
else:
    result = Void
```

临时 label 在复制 persistent child-player Variant 之前销毁；返回值是 Variant CopyRef，
不是 borrowed node pointer。ordinary missing 不抛出。

### 4.3 两个 facade hit test

两个 facade 都保留 by-value label owner，在同步递归 resolver 返回后读取当前 node 的
`HitData` 并调用共享 `GeometryShape_contains_guess`。missing 返回 false；不检查 node
visible/active/label-empty/update state。D3D wrapper 在进入共享 body 前通过
`D3D shell -> primary EmoteObject -> Engine -> Player` 解包，并在返回后释放 by-value
label owner。

## 5. 本地逐行对照与已闭 companion slices

| 本地调用点 | 证据承接 |
|---|---|
| `cpp/plugins/motionplayer/NodeTree.cpp:319` | `MP-C12-PLAYER-BUILD-NODE-TREE` |
| `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp:39` | `MP-C21-PLAYER-CAMERA-CONSTRAINT-PHASE` |
| `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp:553` | `MP-C23-PLAYER-CAMERA-NODE-PHASE` |
| `cpp/plugins/motionplayer/PlayerUpdateChildMotion.cpp:255` | `MP-C25-PLAYER-MOTION-SUB-PHASE` |
| `cpp/plugins/motionplayer/PlayerUpdateParticles.cpp:214` | particle-emitter implemented slice |
| `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:682-698` | resolver/recursive visitor 本体 |
| `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:701-713` | 本报告 getLayerMotion |
| `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:716-721` | `MP-D10-LAYERGETTER-ONE` |
| `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:769-775` | facade raw-label hit-test helper |
| `cpp/plugins/motionplayer/EmotePlayer.cpp:493-496`、`:552-553` | D3D/Motion facade wrappers |

本轮无需修改 C++。本地已经保留五处 false、其余递归 true、unchecked index、live child
visitor、temporary ttstr 和 Variant CopyRef owner 顺序。

## 6. 验证与 disposition

- 四个 resolver 均 fresh decompile，完整 80/69/69/102 条 disassembly cursor 全部完成；
- 四端全量 xref 得到 5/9/9/9，Android arm64 的五个 missing direct edges 均由完整
  build/update phase 报告证明为内联；
- fresh 读取四个 getLayerMotion、四个 recursive visitor、四个 Motion.EmotePlayer
  contains、A64 独立 hit-test helper和三个 D3D wrapper；
- caller 全部交叉链接到本地调用点和现有 implemented slice；
- IDB 已写入语义名、完整 caller-closure 注释和书签。

正式构建仍因当前环境缺少 CMake/Emscripten/Ninja/完整依赖不可用；这不再是 resolver
或 caller 分母的语义 gap。剩余开放项集中在 MotionNode 未知 source types 与精确 EH
frontier。
