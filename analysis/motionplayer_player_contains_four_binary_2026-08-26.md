# Player contains（四参考二进制，2026-08-26）

## 1. callback

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6D071C` | `0x595AF8` | `0x1001218E8` | `0x12065C` |

四端均已 fresh decompile + disassemble。Android arm64 此前被 IDA 错并入
`clear` 的连续大函数；本轮先重建 `clear` 精确尾 `0x6D071C`，再恢复
`contains` 为 `0x6D071C..0x6D08E4` 独立函数并重新反编译。

## 2. 共同源代码形状

```cpp
bool Player::contains(double x, double y) {
    for (size_t i = 1; i < nodes.size(); ++i) {
        MotionNode &node = nodes[i];
        if (node.nodeType == 1 &&
            GeometryShape_contains(node.shapeGeometry, x, y))
            return true;
    }

    bool found = false;
    visitChildPlayerDispatches([&](Player *child) {
        if (child->contains(x, y)) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}
```

local scan 严格排除 synthetic root index 0，只接受 nodeType 1，并在第一个 shape
hit 立即返回；因此后续 malformed child 不会被转换。shape 算法复用已闭合的
Point/Circle/Rect/Quad helper 与 ordered/NaN 边界。

| 端 | MotionNode stride | node-owned HitData |
|---|---:|---:|
| Android arm64 | `0xA48` | `Node+0x680` |
| Android armv7 | `0x8E0` | `Node+0x590` |
| iOS arm64 | `0xA58` | `Node+0x690` |
| iOS armv7 | `0x8B4` | `Node+0x570` |

child phase 复用 shared visitor，因而继承其全部边界：

- visitor 自身扫描完整 flat deque，包含 root；root 只在 direct shape phase 被
  排除，若被恶意改成 type 3/4，递归 phase 仍会处理它；
- type 4 按 count 次重复 numeric index 0；type 3 读取 persistent child Variant；
- child Player 是 borrowed native pointer，无 null guard；non-object conversion
  抛出，wrong-native/null 后递归是 crash boundary；
- first child hit 令 lambda 返回 false，visitor 立即终止；
- 无 cycle/depth guard。

std::function capture 包含 x/y/found 三个地址：Android arm64/armv7 分别 heap
allocate `0x18/0x0C`，iOS arm64 因 capture 超过 SBO heap allocate `0x20`，
iOS armv7 放入 libc++ SBO。allocation 仅发生在 local scan 完全 miss 后。

## 3. 本地和测试

本地 `PlayerLayerQuery.cpp` 与共同形状一致。本轮新增用例锁定：root shape 排除、
non-root local hit、local-first 对 malformed child 的短路，以及 local miss 后的
conversion throw。四个 IDB 已命名/注释并保存。

正式工具链不可用，测试未执行。2026-08-27 已由
`motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md` 补齐
heap/SBO allocation、manager/vtable、normal destroy 与 target local-landing 差异；本项现为
`IMPLEMENTED`。
