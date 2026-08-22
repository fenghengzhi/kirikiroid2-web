# MotionPlayer hair/parts/bust scale 四参考二进制审计

## 结论

`hairScale`、`partsScale`、`bustScale` 是 `EmoteEngine` 中连续排列的三个
`double`，同时被 `Motion.EmotePlayer` 与 `Motion.D3DEmotePlayer` 的同名成员
读写。三个字段在四个构造实现中都初始化为 `1.0`。D3D 壳没有独立 scale
副本；其访问器沿主 `EmoteObject` 槽进入 Engine。

旧端口把 D3D 的 `hairScale/partsScale` 接到类尾部两个影子字段，并把真实前两个
字段解释为默认 `0.0` 的 bust spring constant；这与四份当前参考二进制均不符。

## 宽字符串和注册入口

普通字符串搜索找不到这些 TJS 宽字面量；UTF-16LE byte 搜索得到：

| 目标 | `hairScale` | `partsScale` | `bustScale` | D3D registrar |
|---|---:|---:|---:|---:|
| Android arm64 | `0x14BEA0C` | `0x14BEA20` | `0x14BEA36` | `0x52E8E4` |
| Android armv7 | `0xD76840` | `0xD76854` | `0xD7686A` | `0x494078` |
| iOS arm64 | `0x10196FD8A` | `0x10196FD9E` | `0x10196FDB4` | `0x100232278` |
| iOS armv7 | `0x1762136` | `0x176214A` | `0x1762160` | `0x230F46` |

iOS 另有一组 EmotePlayer 表字符串；Android 两张表共享常量池。四端 D3D 表均按
`queing -> hairScale -> partsScale -> bustScale -> assignState` 的次序注册。

## D3D getter/setter 映射

| 目标 | hair get/set | parts get/set | bust get/set |
|---|---|---|---|
| Android arm64 | `0x5304D0` / `0x5304E0` | `0x5304F0` / `0x530500` | `0x530510` / `0x530520` |
| Android armv7 | `0x494A6E` / `0x494A7C` | `0x494A8A` / `0x494A98` | `0x494AA6` / `0x494AB4` |
| iOS arm64 | `0x100232EA8` / `0x100232EB8` | `0x100232EC8` / `0x100232ED8` | `0x100232EE8` / `0x100232EF8` |
| iOS armv7 | `0x231AFA` / `0x231B08` | `0x231B16` / `0x231B24` | `0x231B32` / `0x231B40` |

24 个访问器均已 fresh decompile。归一伪代码为：

```cpp
double getScale(D3DShell *self, size_t fieldOffset) {
    return *(double *)((char *)self->primary->engine + fieldOffset);
}

void setScale(D3DShell *self, size_t fieldOffset, double value) {
    *(double *)((char *)self->primary->engine + fieldOffset) = value;
}
```

没有 null guard、clamp、dirty 写入、Player 转发或壳层缓存。调用前必须已由
`load/clone` 建立 primary EmoteObject；这是与其它 D3D Engine 访问器相同的生命
周期前置条件。

## Engine 字段偏移和构造默认值

| 目标 | Engine ctor | hair | parts | bust | ctor 值 |
|---|---:|---:|---:|---:|---|
| Android arm64 | `0x67B76C` | `+1184` | `+1192` | `+1200` | 三个 `1.0` |
| Android armv7 | `0x560948` | `+616` | `+624` | `+632` | 三个 `1.0` |
| iOS arm64 | `0x1001B7FB0` | `+816` | `+824` | `+832` | 三个 `1.0` |
| iOS armv7 | `0x1B7788` | `+428` | `+436` | `+444` | 三个 `1.0` |

64 位构造器以连续 qword/vector store 写入 IEEE-754 `1.0`；32 位构造器分别写
低半 `0` 和高半 `0x3FF00000`。Android/iOS 的绝对偏移差异来自 STL/ABI
布局，字段相对次序和行为一致。

这些值也作为物理步进的 multiplier 被消费：hair/parts 两项进入两条 chain
family，bust 项进入 hair/parts spring step。内部消费者不改变三个字段的脚本可见
名称或构造默认值。

## 本地逐行比较与修正

- D3DEmotePlayer 原先：hair/parts 读写类尾 `_hairScale/_partsScale` 影子；现在三项
  都沿 primary EmoteObject 进入真实 Engine triplet。
- Engine 原先：真实前两项命名为 `_bustSpring1Const/_bustSpring2Const` 且默认
  `0.0`，第三项为未命名 scalar；现在按注册成员重命名为
  `_hairScale/_partsScale/_bustScale`，默认均为 `1.0`。
- 删除类尾 `_hairScale/_partsScale/_bustScale/_bodyScale` 四个非二进制影子；
  `bodyScale` 不存在于四端 D3D member table。
- EmotePlayer 的 method 与 property、D3DEmotePlayer 的 property、以及物理步进
  统一引用同一组三字段。
- 四份 IDB 的 24 个 D3D getter/setter 已统一写入 `_guess` 名称和 `double`
  prototype，并原位保存。

## 验证状态

- 新增单元测试覆盖三字段的 `1.0` 默认值和 D3D getter/setter 往返。
- Web Debug 增量重建通过，编译/链接 9 个受影响目标；随后再次构建返回
  `ninja: no work to do`。
- Wasmtime Headless Debug 增量重建通过，编译/链接 16 个受影响目标；随后再次构建
  返回 `ninja: no work to do`。
- 使用 Web 编译参数和 Catch2 头目录，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 Emscripten
  `-fsyntax-only` 通过，仅有项目既存的 `_tss` 字面量弃用警告。
- `git diff --check` 通过，仅报告工作区已有的 LF/CRLF 转换提醒；临时语法检查配置
  已删除。
- Windows 原生 Catch2 仍受同轮已记录的外部 `cocos2dx:x64-windows` vcpkg
  overlay port `C2491` 构建错误阻塞；项目本身尚未进入原生编译阶段，未改动全局
  vcpkg 来规避。
