# Player allplaying / hasCamera（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getAllplaying | `0x6CA214` | `0x5924EC` | `0x10011CEA4` | `0x11B8C4` |
| getHasCamera | `0x6CA378` | `0x5925E0` | `0x10011CFA8` | `0x11B972` |

八个 callback 均已 fresh decompile + disassemble；四个 IDB 已统一命名、注释并
保存。

## 2. allplaying 的共同调用链

```cpp
bool Player::getAllplaying() const {
    for (size_t i = 1; i < nodes.size(); ++i) {
        MotionNode &node = nodes[i];
        if (node.nodeType != 3)
            continue;

        Player *child = node.getChildPlayer();
        if (child->getAllplaying())
            return true;
    }
    return localPlayingByte;
}
```

可观察边界：

- synthetic root（index 0）被排除；只有 type 3 nested-motion node 参与；
  type 4 particle children 不参与。
- 深度优先、deque index 顺序、first-true 短路；本地 byte 最后才读取。因此父
  byte 已为 true 时，前面的 malformed type-3 child 仍会先抛出或崩溃。
- `childPlayerVar.AsObjectNoAddRef()` 对非 Object Variant 抛转换异常；随后
  `GetNativeInstance` 以 class ID 查询 native instance。
- child Dispatch/Player 不额外 AddRef：它是从 node persistent Variant 借来的。
  null Dispatch 或 wrong-native Object 会得到 null，但 callback 仍无 null guard 地
  递归解引用，保留 malformed-tree crash boundary。
- 三端在 child recursion 后可见地重新读取 live `nodes.size()`；iOS armv7 因 const
  遍历无受支持的 mutation path 而把 bound hoist。共同源代码仍是普通
  `i < nodes.size()` loop。
- 四端 callback 内都没有日志、路径转换或外部查询副作用。

每端关键布局：

| 端 | MotionNode stride | nodeType | child Variant | local byte |
|---|---:|---:|---:|---:|
| Android arm64 | `0xA48` | `Node+0x1C` | `Node+0x778` | `Player+0x44B` |
| Android armv7 | `0x8E0` | `Node+0x14` | `Node+0x684` | `Player+0x2EF` |
| iOS arm64 | `0xA58` | `Node+0x1C` | `Node+0x788` | `Player+0x3DB` |
| iOS armv7 | `0x8B4` | `Node+0x14` | `Node+0x660` | `Player+0x2AF` |

## 3. hasCamera 的共同容器遍历

```cpp
bool Player::getHasCamera() const {
    for (const MotionNode &node : nodes)
        if (node.nodeType == 5)
            return true;
    return false;
}
```

它扫描 flat deque 的完整 live range，包含 index 0 root；不跳过 done/inactive
node，不读取 `cameraAlive` 或 `cameraActive`，也不递归 type-3/type-4 child。
遇到第一个 type 5 即返回。

四端机器码直接展开各自 deque iterator：Android 两端使用 libstdc++ 风格的
node/block cursor，当前巨型 MotionNode 令每 block 仅容纳一个元素；iOS 两端
使用 libc++ map + start-offset/size 表示，每个 block 16 个 MotionNode。两种 ABI
实现都对应同一 full-range source loop，不应移植成手写 ABI 容器。

## 4. 本地差异修复与验证

本地 `getHasCamera` 已一致。`getAllplaying` 的递归、借用 owner 和 malformed
边界也已一致，但原先在 child-true 与 local-return 两条路径夹带了 `PRTDIAG`
日志及 `matchedMotionPath()` 转换；四端 fresh 反编译均不存在这些副作用。本轮
已删除两段日志，使 getter 恢复为纯递归查询。

既有测试覆盖 root/type-4 排除、descendant first、local-last 与 non-object throw，
以及 hasCamera 对 inactive type-5 node 的结构扫描。本机缺少正式 CMake/
Emscripten 工具链，未执行正式测试；allplaying 状态记为 `IMPLEMENTED`，
hasCamera 记为 `EVIDENCED_4_4`。
