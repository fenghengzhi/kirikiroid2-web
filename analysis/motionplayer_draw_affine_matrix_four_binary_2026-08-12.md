# motionplayer `setDrawAffineTranslateMatrix` 四二进制对照（2026-08-12）

## 结论

四个参考二进制共同证明，`Player.setDrawAffineTranslateMatrix` 与
`EmotePlayer.setDrawAffineTranslateMatrix` 都是普通的、强类型的
`NCB_METHOD`：它们接收六个按固定顺序传入的实数参数，C++ 成员函数返回
`bool`，NCB 再把该返回值写成 TJS `Integer(0/1)`。

这里没有 raw callback，没有单个 matrix 对象模式，也没有读取对象属性并补默认值
的路径。旧的 Web 移植代码在这一点上不是参考实现的等价复原。

本轮证据来自当前会话中对下列四个 IDB 的重新搜索、交叉引用、反编译和必要的
ARM 指令核对；不是沿用旧的 `libkrkr2.so` 注释：

- Android ARM64：`Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- Android ARMv7：`Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- iOS ARM64：`Kirikiroid2_1.3.9_iOS_arm64`
- iOS ARMv7：`Kirikiroid2_1.3.9_iOS_armv7`

## 名称、注册点与回调映射

方法名按 UTF-16 宽字符串定位，随后从 Player/EmotePlayer 两套注册器分别追到
dispatch 构造、`FuncCall`、六参数 adapter 和最终成员函数。

| 目标 | 方法名字符串 | Player 注册引用 / 回调 | EmotePlayer 注册引用 / 回调 |
|---|---:|---:|---:|
| Android ARM64 | `0x14D3F1A`（两类共享） | `0x6D5F7C` / 共享 Player body `0x6D22F4` | `0x67DD08` / `0x67F2C8` |
| Android ARMv7 | `0xD848D0` | `0x59871A` / `0x596C40` | `0x561668` / `0x562068` |
| iOS ARM64 | Player `0x10195CEC8`；Emote `0x101960540` | `0x100125150` / `0x100122D54` | `0x1001B5644` / `0x1001B6148` |
| iOS ARMv7 | Player `0x174F22C`；Emote `0x17528A4` | `0x12439C` / `0x121D90` | `0x1B528C` / `0x1B5EEC` |

Android ARM64 的布局需要特别说明：IDA 把 EmotePlayer wrapper 入口
`0x67F2C8` 和远端 Player 共享块 `0x6D22F4` 组织在同一个带远端 chunk 的函数里。
EmotePlayer 入口先从 wrapper 取出内部 Player，再跳入该共享块；Player 的注册项则
直接把共享块作为回调入口。这个布局差异不改变两个类的源级关系：EmotePlayer
只负责转发，实际字段修改发生在 Player。

四个 IDB 中保存的临时恢复名均带 `_guess`，以区分二进制没有保留下来的精确原始
标识符，例如 `Player_setDrawAffineTranslateMatrix_guess`、
`EmotePlayer_setDrawAffineTranslateMatrix_guess` 和
`NCB_*SixDoubleBoolMethod_*_guess`。

## Player 字段写入

六个脚本/C++ 参数的顺序是：

```text
m11, m21, m12, m22, m14, m24
```

内部四个线性分量则按矩阵字段的自然顺序保存：`m11, m12, m21, m22`。
平移分量 `m14, m24` 从 `double` 窄化成 `float` 后保存。

| 目标 | `m11` | `m12` | `m21` | `m22` | `m14`/`m24`（float） | nonidentity flag |
|---|---:|---:|---:|---:|---:|---:|
| Android ARM64 | `+808` | `+816` | `+824` | `+832` | `+840` / `+844` | `+611` |
| Android ARMv7 | `+536` | `+544` | `+552` | `+560` | `+568` / `+572` | `+411` |
| iOS ARM64 | `+696` | `+704` | `+712` | `+720` | `+728` / `+732` | `+499` |
| iOS ARMv7 | `+472` | `+480` | `+488` | `+496` | `+504` / `+508` | `+347` |

四个版本的共同源级伪代码是：

```cpp
bool Player::setDrawAffineTranslateMatrix(
    double m11, double m21, double m12,
    double m22, double m14, double m24) {
  storedM11 = m11;
  storedM12 = m12;
  storedM21 = m21;
  storedM22 = m22;
  storedM14 = static_cast<float>(m14);
  storedM24 = static_cast<float>(m24);

  nonIdentity =
      m11 != 1.0 || m21 != 0.0 || m12 != 0.0 ||
      m22 != 1.0 || m14 != 0.0 || m24 != 0.0;
  return nonIdentity;
}

bool EmotePlayer::setDrawAffineTranslateMatrix(/* same six doubles */) {
  return player->setDrawAffineTranslateMatrix(/* same order */);
}
```

ARMv7 和 iOS ARMv7 的初始 Hex-Rays 输出把部分浮点比较组合得不够直观；本轮又
核对了回调的原始指令，六个原始 `double` 都参与精确的 identity 比较。比较发生在
平移分量窄化存储之前，所以 flag 由原始 `double m14/m24` 决定，而不是由保存后的
`float` 决定。

由 IEEE 比较直接得到的边界行为：

- `+0.0` 和 `-0.0` 都等于零，因而 signed zero 仍属于 identity。
- 任一分量为 NaN 时，相应 `!=` 为真，flag 和返回值都是 nonidentity。
- 平移参数即使窄化为 `float` 后发生精度丢失，identity 判定仍使用窄化前的值。
- setter 每次都无条件覆盖六个保存字段和 flag；没有“仅非 identity 才写入”的分支。

## NCB 调用链

Android ARM64 的 Player 与 EmotePlayer dispatch 在各自注册器中内联构造。其余三组
二进制保留了可独立辨认的 create/allocate/constructor 函数。

| 目标 / 类 | create | allocate | function ctor | `FuncCall` | six-double invoke |
|---|---:|---:|---:|---:|---:|
| Android ARM64 / Player | 注册器 `0x6D5F7C` 内联；vtable `0x1A1E3B8` | 同左 | 同左 | `0x6F7AD4` | `0x6F7BF0` |
| Android ARM64 / EmotePlayer | 注册器 `0x67DD08` 内联；vtable `0x1A16FA8` | 同左 | 同左 | `0x68CEBC` | `0x68CFD8` |
| Android ARMv7 / Player | `0x5B40FC` | `0x5B4130` | `0x5B416C` | `0x5B41D4` | `0x5B4298` |
| Android ARMv7 / EmotePlayer | `0x56D0A4` | `0x56D0D8` | `0x56D114` | `0x56D17C` | `0x56D240` |
| iOS ARM64 / Player | `0x10014A988` | `0x10014A9DC` | `0x10014AA40` | `0x10014AAD8` | `0x10014ABB8` |
| iOS ARM64 / EmotePlayer | `0x1001C9884` | `0x1001C98D8` | `0x1001C993C` | `0x1001C99D4` | `0x1001C9AB4` |
| iOS ARMv7 / Player | `0x14BDA2` | `0x14BDCC` | `0x14BE8C` | `0x14BF8C` | `0x14C024` |
| iOS ARMv7 / EmotePlayer | `0x1C74FC` | `0x1C7524` | `0x1C75E4` | `0x1C76E4` | `0x1C777C` |

四组实现的模板行为一致：

1. `FuncCall` 是普通 typed NCB method specialization，不是
   `NCB_METHOD_RAW_CALLBACK`。
2. `argc < 6` 返回 `TJS_E_BADPARAMCOUNT`（`-1004`）。这个返回发生在 result
   写入之前，因此调用者已有的 result 内容保持不变。
3. `param[0]` 到 `param[5]` 按顺序经 `tTJSVariant::AsReal` 转换；额外参数不读取、
   不转换，直接忽略。
4. adapter 调用 `bool (Class::*)(double, double, double, double, double, double)`。
5. result 非空时，返回的 `bool` 被写成 `tvtInteger` 的 `0` 或 `1`；result 为空时
   setter 仍执行，只是不写返回 variant。
6. `Void` 沿模板的数值转换路径成为零；参考二进制没有旧移植代码中的手动 Void/
   null 拒绝。
7. 只传一个 matrix 对象时首先因为参数个数不足而失败。不存在读取 `m11`、`m21`、
   `m12`、`m22`、`m14`、`m24` 属性的分支，也不存在 identity 默认值补齐逻辑。

dispatch 生命周期也与标准 NCB method 对象一致：注册时在堆上构造带 dispatch vtable
和内嵌 iMethod facade 的对象，由类工厂接收并注册；facade 的 `Release` 走该共享 NCB
基类的 no-op 实现。这里没有 raw callback 专用对象的额外所有权路径。

## D3DEmotePlayer 边界

四个参考二进制的 D3DEmotePlayer 注册表里都没有
`setDrawAffineTranslateMatrix`。该方法只属于 Player 和 EmotePlayer 的暴露面。
因此旧移植版中未注册但仍存在的 D3DEmotePlayer compatibility wrapper 也不是需要
保留的参考源结构。

## 与本地旧实现的差异及修正

旧实现包含四类不符合参考二进制的行为：

- 用 `NCB_METHOD_RAW_CALLBACK` 注册，而非 typed `NCB_METHOD`。
- 除六参数形式外，还接受单个对象，并从对象读取六个可选属性、补 identity 默认值。
- 手动拒绝 Void/null，清空 result，并以 Void/TJS success 结束，而不是返回
  `Integer(0/1)`。
- 保留一个空的 Player overload 和未注册的 D3DEmotePlayer compatibility wrapper。

本轮已经把声明和实现改回六个 `double` 的 `bool` 成员函数，EmotePlayer 改为同签名
转发，Player/EmotePlayer 注册改为 `NCB_METHOD(setDrawAffineTranslateMatrix)`，并删除
上述两个死 compatibility 入口。相关源码注释不再携带过时的旧二进制地址。

2026-08-14 后续 fresh audit 又确认四端 setter body 均为 0 string references、无 callee；
本地字段逻辑后原本无条件执行的 `matchedMotionPath()` 属于 Web diagnostic 泄漏，现已移入
显式 trace gate。该修正不改变本节六参数、float translation storage 或 exact identity
语义；完整证据见
`analysis/motionplayer_draw_entry_diagnostic_isolation_four_binary_2026-08-14.md`。

受影响文件：

- `cpp/plugins/motionplayer/Player.h`
- `cpp/plugins/motionplayer/PlayerCore.cpp`
- `cpp/plugins/motionplayer/PlayerDrawDispatch.cpp`
- `cpp/plugins/motionplayer/EmotePlayer.h`
- `cpp/plugins/motionplayer/EmotePlayer.cpp`
- `cpp/plugins/motionplayer/main.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

新增测试覆盖：直接 Player setter 的 identity、signed zero、六个单分量非 identity、
NaN、EmotePlayer 返回值转发，以及脚本层的单对象 arity failure、已有 result 保持、
多余参数忽略、`Integer(0/1)` 返回和 Void-to-zero 转换。

## IDB 回填

四个 IDB 均已回填 setter、EmotePlayer wrapper、NCB `FuncCall`、six-double invoke 和
dispatch 构造链的 `_guess` 名称与语义注释。应用类型后对 23 个 setter/FuncCall/
invoke 函数执行了强制重新反编译，全部成功；四个数据库随后均原位保存成功。

## 验证

- Web Debug `motionplayer` 静态库：通过。
- Wasmtime Debug `motionplayer` 静态库：通过。
- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过；只有既有 `_tss`
  deprecated literal warning。
- 完整 Web Debug `index.html` 链接：通过。仅有既有 Emscripten pthread/
  memory-growth、JSPI 和 JS library warning。
- 完整 Wasmtime Debug `krkr2_wasmtime_guest` 构建及 exnref 转换：通过。仅有既有
  `_tss` deprecated literal、`imagepacker.h` `nodiscard` 和 Emscripten warning。
- stale compat 扫描：`setDrawAffineTranslateMatrixCompat`、旧 matrix-object 标记和本轮
  替换掉的旧地址/符号均无残留命中。
- `git diff --check`：退出码 0；输出只有工作树既有的 LF/CRLF 转换提醒，没有
  whitespace error。
