# Player variableKeys（四参考二进制，2026-08-26）

## 1. callback

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6CE77C` | `0x5948D0` | `0x1001200B4` | `0x11EDC0` |

四端均已 fresh decompile + disassemble，并命名为
`Player_getVariableKeys_guess`。

## 2. 共同源代码和数据流

```cpp
Variant Player::getVariableKeys() {
    Array result = createFreshArray();
    for (const VariableLabelScope &scope : variableLabelScopes)
        result.Items.emplace_back(String(scope.cascadeKey));
    return result;
}
```

- Array/Items 在遍历前创建；空 scope deque 也返回 fresh empty Array，而非 Void
  或共享 singleton。
- 直接向 `tTJSArrayNI::Items` 的 native Variant deque 尾部构造 String Variant；
  不调用脚本层 `add`，没有中间 vector。
- 严格保留 scope deque 顺序、重复 key 与 empty `ttstr`；不排序、不去重、不
  过滤。
- 每个 String Variant CopyRef `scope.cascadeKey` 的字符串 owner。Android arm64
  内联展示对 ttstr refcount 的 acquire/release 原子加一；其他端由 append/copy
  helper 完成相同所有权转移。
- getter result 独立持有 Array Dispatch；scope deque 或 Player 后续销毁不依赖
  返回 Array 的 lifetime，反之返回 Array 的修改也不暴露 scope 容器。

## 3. 内部 scope deque

| 端 | Player 中的 deque 起始 | scope stride | 每 block 元素数 | block payload |
|---|---:|---:|---:|---:|
| Android arm64 | `+0x520` | `0xA0` | 3 | `0x1E0` |
| Android armv7 | `+0x388` | `0x80` | 4 | `0x200` |
| iOS arm64 | `+0x488` | `0xA0` | 25 | `0xFA0` |
| iOS armv7 | `+0x330` | `0x80` | 32 | `0x1000` |

`cascadeKey` 在 scope offset 0。Android 两端展开 libstdc++ deque cursor；iOS
两端展开 libc++ map + start-offset/size 算法。stride 差异来自 32/64 位成员尺寸，
block policy 差异来自 STL 实现；共同源代码不应固化这些 ABI 常数。

## 4. 本地和验证

本地 `PlayerCore.cpp` 已与四端共同形状一致。既有单元测试覆盖空场景 fresh
identity、物理顺序、duplicate 与 empty key。本轮不需修改语义代码。四个 IDB
已补充容器布局/owner 注释并保存；正式工具链不可用，状态为
`EVIDENCED_4_4`。

精确的 append allocation failure/unwind 顺序保留到最终跨 ABI 异常边审计；
正常容器和所有权路径已闭合。

## 5. 2026-08-27 EH 闭包

`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 已完成该
最终审计：四端 String append/reserve、Android map-before-block 内部提交、iOS
split-buffer staging 和 caller local-owner cleanup/absence 均已展开。失败时可能已有前缀
Items，但 Array 尚未写入返回槽；Android arm64、iOS arm64 LSDA cold 与 iOS armv7
SjLj 路径销毁局部 owner，只有 Android armv7 保留无本帧 landing 的目标边界。该 row
现为 `IMPLEMENTED`；正式构建仍不可用。
