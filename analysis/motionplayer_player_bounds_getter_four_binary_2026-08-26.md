# Player bounds getter（四参考二进制，2026-08-26）

## 1. callback 与字段坐标

| 端 | getBounds | minX / minY / maxX / maxY |
|---|---:|---|
| Android arm64 | `0x6C9E64` | `Player+0x98/0xA0/0xA8/0xB0` |
| Android armv7 | `0x59226C` | `Player+0x78/0x80/0x88/0x90` |
| iOS arm64 | `0x10011CBD4` | `Player+0x80/0x88/0x90/0x98` |
| iOS armv7 | `0x11B53C` | `Player+0x68/0x70/0x78/0x80` |

四端 callback 和其 binary64 分类 helper 均已 fresh decompile + disassemble。

## 2. 共同源代码形状

```cpp
Variant getBounds() const {
    Dictionary result = createFreshDictionary();

    // 顺序可观察：先 Y，再 X；都是 ordered >=。
    if (maxY >= minY && maxX >= minX) {
        result["left"]   = Real(minX);
        result["top"]    = Real(minY);
        result["right"]  = Real(maxX);
        result["bottom"] = Real(maxY);
        result["width"]  = Real(maxX - minX);
        result["height"] = Real(maxY - minY);
        result["isValid"] = Boolean(
            category(minX) == 0 &&
            category(maxX) == 0 &&
            category(minY) == 0 &&
            category(maxY) == 0);
    } else {
        result["isValid"] = Boolean(false);
    }
    return result;
}
```

Dictionary 在 AABB ordering test 之前无条件创建，每次调用返回不同的 Dispatch
owner。ordered 分支严格按 `left, top, right, bottom, width, height, isValid`
顺序发布七个成员；unordered 分支只发布 `isValid`，不会发布值为 Void 的另外
六个 key。所有 SetValue 都带 `TJS_MEMBERENSURE` (`0x200`) 并复用进程级 key
hint；返回状态在正常源代码形状中被忽略。

## 3. ordering 与 IEEE-754 分类

四端 helper 地址：

| 端 | helper |
|---|---:|
| Android arm64 | `0xA0C7A0` |
| Android armv7 | `0x75F618` |
| iOS arm64 | `0x1002583C4` |
| iOS armv7 | `0x259750` |

它不是普通 `std::isfinite` boolean，而是按 sign/exponent/mantissa 产生分类码：

| 输入类别 | code |
|---|---:|
| 非负 finite（含 `+0.0`） | 0 |
| positive NaN | 1 |
| `+inf` | 2 |
| negative finite（含 `-0.0`） | 8 |
| negative NaN | 9 |
| `-inf` | 10 |

`getBounds` 只接受 code 0。因此 ordered negative AABB 仍发布六个几何值，但
`isValid=false`；`-0.0` 也 invalid。NaN 会先令 ordered comparison 失败，得到
只有一个 key 的 Dictionary；无穷若仍满足 ordering，则发布几何值后判 invalid。
四个分类调用的短路顺序是 minX、maxX、minY、maxY。

## 4. 生命周期与本地实现

Dictionary factory reference 被 accessor wrapper 接管；返回 Variant 写入相同的
object/objthis Dispatch 指针并获得自己的返回期 owner，随后 accessor 释放局部
factory owner。没有 Player 字段地址逃逸。各成员由临时 Real/Boolean Variant
完成发布；失败/抛出边按各 ABI 的 EH metadata 清理临时值和 Dictionary owner。

本地 `PlayerCore.cpp` 与共同形状一致，包括 Y-before-X、七/一 key 两种 shape、
分类规则与进程级 hint 复用。既有单元测试已覆盖 fresh Dictionary、空 sentinel、
finite/negative/negative-zero/NaN/infinity 以及上游 calcBounds 的主要遍历边界，
本轮无需修改语义代码。正式工具链不可用，状态记为 `EVIDENCED_4_4`。

四个 IDB 已统一命名 `Player_getBounds_guess` 与
`binary64_sign_special_code_guess`，补充字段/分类注释并保存。

## 5. 2026-08-27 EH 闭包

`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 已闭合每个
`SetValue` 的 prefix commit、返回槽发布点和四端 cleanup：Android arm64 landing、
iOS arm64 LSDA cold ordinary cases 与 iOS armv7 ordinary SjLj states 会 Release fresh
Dictionary；iOS 两端的 destructor-throw states terminate；只有 Android armv7 的完整
函数/相邻 catalog 无本帧 cleanup。任何失败都不会返回 partial Dictionary。该 row 现为
`IMPLEMENTED`；正式构建仍不可用。
