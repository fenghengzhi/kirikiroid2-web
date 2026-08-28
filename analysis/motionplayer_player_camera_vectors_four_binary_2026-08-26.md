# Player cameraTarget / cameraPosition（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getCameraTarget | `0x6C9AF4` | `0x592100` | `0x10011CA20` | `0x11B2E8` |
| getCameraPosition | `0x6C9C54` | `0x592174` | `0x10011CA9C` | `0x11B3C0` |

四端均为只读 NCB property callback；没有对应 setter，也不与
`cameraAlive`、`cameraActive` 或 `stereovisionActive` 做 getter 时门控。

## 2. 共同源代码形状

```cpp
Variant getCameraTarget() const {
    Array result = createFreshArray();
    result.Items.push_back(Real(_cameraTargetX));
    result.Items.push_back(Real(_cameraTargetY));
    result.Items.push_back(Real(_cameraTargetZ));
    return result;
}

Variant getCameraPosition() const {
    Array result = createFreshArray();
    result.Items.push_back(Real(_cameraPosX));
    result.Items.push_back(Real(_cameraPosY));
    result.Items.push_back(Real(_cameraPosZ));
    return result;
}
```

每次调用都通过 TJS Array class 创建新的 Array object 和 Items owner，按
X、Y、Z 顺序追加三个 type-tag `5`（Real）的 Variant。返回的并非对 Player
内部三个 double 的借用视图；不同调用之间 Array identity 独立。

## 3. 字段坐标

| 端 | cameraPosition X/Y/Z | cameraTarget X/Y/Z |
|---|---|---|
| Android arm64 | `Player+0x48/0x50/0x58` | `Player+0x60/0x68/0x70` |
| Android armv7 | `Player+0x28/0x30/0x38` | `Player+0x40/0x48/0x50` |
| iOS arm64 | `Player+0x30/0x38/0x40` | `Player+0x48/0x50/0x58` |
| iOS armv7 | `Player+0x18/0x20/0x28` | `Player+0x30/0x38/0x40` |

六个字段在四端均是连续的 IEEE-754 double；ABI 差异来自前置成员尺寸，
不表示不同的源代码成员顺序。

## 4. 生命周期和边界

- Array 构造成功后，由返回 Variant 持有 Dispatch 引用；局部 Array/Items
  临时 owner 按标准 RAII 路径释放。
- 三个元素逐个 materialize 为独立 Real Variant；不存在对 Player 字段的
  指针或引用逃逸。
- getter 不检查 camera 状态，因此会返回构造期的六个 `0.0`，也会在 camera
  后续 inactive/dead 时返回仍保存在字段里的最后值。
- NaN、正负无穷和 signed zero 均不归一化；getter 只复制 double bit-value
  到 TJS Real。
- Array/element allocation 或 append 抛出时的精确 ABI unwind 表仍留给统一
  异常边审计；正常返回路径的 owner 关系已经四端闭合。

## 5. 本地对应与验证状态

本地 `PlayerLayerQuery.cpp` 的两个 getter 与四端共同形状一致。已有单元用例
覆盖 fresh identity、长度 3、Real 元素、默认零值、X/Y/Z 顺序，以及 camera
状态门控发生在更新数据流而非 getter 内部。

四个 IDB 已统一命名为 `Player_getCameraTarget_guess` / 
`Player_getCameraPosition_guess`，添加字段和 owner 注释并保存。正式工具链当前
不可用，因此状态记为 `EVIDENCED_4_4`，未声称实际执行测试。

## 6. 2026-08-27 EH 闭包

`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 已重新读取
八个完整 getter、四端 append/reserve helper、iOS arm64 LSDA cold cleanup 与 iOS armv7
SjLj cleanup：Android arm64、iOS arm64 和 iOS armv7 会在 append/result-copy 异常时析构
局部 Array Variant；只有 Android armv7 的完整函数/相邻 catalog 无本帧 cleanup。四端都
在三个 append 完成后才写返回槽，不会返回 partial Array。该 row 现为 `IMPLEMENTED`；
正式构建仍不可用。
