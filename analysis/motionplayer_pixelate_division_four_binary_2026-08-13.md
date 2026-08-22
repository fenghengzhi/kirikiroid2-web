# motionplayer `pixelateDivision` 四参考二进制审计（2026-08-13）

## 1. 结论

四份当前参考二进制都暴露两套同名但互相独立的属性：

1. `Player::pixelateDivision` 是每个 `Player` 对象自己的有符号 32 位整数；
2. `D3DEmoteModule::pixelateDivision` 是每个 `D3DEmoteModule` 对象自己的有符号 32 位整数。

两套字段都由各自构造器初始化为 `100`，getter/setter 都只是原始
32 位 load/store。setter 不 clamp、不取绝对值、不拒绝零或负数，也不触发
dirty、重建、资源加载或向另一套对象传播。

旧注释把 D3DEmoteModule 一侧描述为“移错位置后为兼容而保留的 static”是
错误的。四个参考目标都真实注册 D3DEmoteModule 的同名实例属性；该对象本身
由 DrawDevice 的 Modules 容器按 class-id 持有，已在
`motionplayer_lifecycle_four_binary_2026-08-11.md` 第 15 节审计。

Player 一侧的完整实现簇字段扫描只得到构造器、getter、setter 三处真实
访问。32 位目标中相同数值偏移还会命中 EmoteEngine、MotionNode 或其他类的
double/指针字段，必须结合基址来源排除，不能把这些结果当作
`Player::pixelateDivision` 的渲染消费者。因此目前没有证据支持旧推测所暗示的
atlas 分块、除法或循环边界；对 Player 而言，它是“保存并回读”的公开状态。

## 2. Player 字符串、注册与字段布局

| 目标 | UTF-16 名称 | member registrar 引用 | getter | setter | Player 字段 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x14BE890` | `0x6D5AB8` | `0x6D6D0C` | `0x6D6D14` | `+0x390` / `+912` |
| Android armv7 | `0xD766DC` | `0x5985E2` | `0x5990A4` | `0x5990AA` | `+0x278` / `+632` |
| iOS arm64 | `0x10195CD82` | `0x100124F88` | `0x100125880` | `0x100125888` | `+0x320` / `+800` |
| iOS armv7 | `0x174F0E6` | `0x1241EE` | `0x124AD2` | `0x124AD8` | `+0x238` / `+568` |

四端访问器归一化后完全相同：

```cpp
int Player::getPixelateDivision() const {
    return pixelateDivision;
}

void Player::setPixelateDivision(int value) {
    pixelateDivision = value;
}
```

arm64 使用 `LDR/STR Wn`，armv7 使用 `LDR.W/STR.W`。这里是 32 位截断后的
原始位模式，没有条件分支或额外调用。

## 3. Player 构造默认值与访问集合

| 目标 | Player 构造器 | 装入 `100` | 写字段 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6CC110` | `0x6CC2CC` | `0x6CC2F0` |
| Android armv7 | `0x5935C4` | `0x5936D2` | `0x5936D6` |
| iOS arm64 | `0x10011EC04` | `0x10011ED0C` | `0x10011ED10` |
| iOS armv7 | `0x11D488` | `0x11D66E` | `0x11D672` |

在四端各自完整 Player 实现地址簇中扫描精确字段寻址：

- Android arm64 `#0x390]`：Player 命中仅 `0x6CC2F0`、`0x6D6D0C`、
  `0x6D6D14`；
- Android armv7 `#0x278]`：Player 命中仅 `0x5936D6`、`0x5990A4`、
  `0x5990AA`；其余命中基址属于其他对象；
- iOS arm64 `#0x320]`：Player 命中仅 `0x10011ED10`、`0x100125880`、
  `0x100125888`；`Player_evaluateTimeline_guess@0x1000F6DD0` 的 `X8`
  是 MotionNode 基址并一次复制连续 double，属于同偏移假阳性；
- iOS armv7 `#0x238]`：Player 命中仅 `0x11D672`、`0x124AD2`、
  `0x124AD8`。

Android armv7 的典型假阳性是
`Player_evaluateTimeline_guess@0x5732A8`：该处从 `MotionNode+0x278`
读一个 double，并不是从 Player 读 int。此例也说明仅凭“偏移相同”推导字段
调用链会产生错误结论。

## 4. D3DEmoteModule 的独立同名属性

| 目标 | 名称/注册引用 | getter | setter | 模块字段 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x14BE890` / `0x52E6F0` | `0x52E82C` | `0x52E834` | `+0x14` / `+20` |
| Android armv7 | `0xD766DC` / `0x493EE6` | `0x493FFA` | `0x493FFE` | `+0x10` / `+16` |
| iOS arm64 | `0x10196FC02` / `0x10023216C` | `0x100232260` | `0x100232268` | `+0x14` / `+20` |
| iOS armv7 | `0x1761FAE` / `0x230E9C` | `0x230F38` | `0x230F3C` | `+0x10` / `+16` |

Android 两份因常量合并而由 Player 和 D3DEmoteModule registrar 共用一份
UTF-16 字面量；iOS 两份保留两份不同地址的相同文本。两种编译结果都不改变
属性属于两个不同 native class、两个不同实例字段的事实。

D3DEmoteModule NCB 构造桥与默认值证据：

| 目标 | 构造桥 | 默认值写入证据 |
| --- | ---: | ---: |
| Android arm64 | `0x54177C` | `0x5417E0` 写入常量块 `{alphaOp=0, pixelateDivision=100, maxW=0, maxH=0}` |
| Android armv7 | `0x4A30F0` | `0x4A312E` 把 `0/100` 写到 `+12/+16` |
| iOS arm64 | `0x100244AA8` | `0x100244B00` 把包含 `0/100` 的 64 位常量写到 `+16` |
| iOS armv7 | `0x244F64` | `0x244FE4` 写入包含 `0/100/0/0` 的常量块 |

模块 getter/setter 同样分别只有一个 32 位 load/store，没有值域检查或跨对象
传播。Player 写为 `INT_MIN` 并不会改变模块的 `100`；模块随后写任意值，也不
会改变 Player 已保存的值。

## 5. 脚本边界

两套 NCB 属性都以 C++ `int` 注册。脚本整数经 typed adaptor 转为 `int` 后，
原生 setter 原样保存，getter 再包装为 TJS integer。对 native `int32` 值域：

- `INT_MIN`
- `-1`
- `0`
- `1`
- `100`
- `INT_MAX`

四端访问器均无特殊分支。尤其是 `0` 和负数不会在属性访问时抛错，也不会被
替换为默认值 `100`。本次移植测试固定了这些边界以及两对象状态不联动。

## 6. IDB 改善

四份 IDB 都新增了下列 `_guess` 名称：

- `Player_getPixelateDivision_guess`
- `Player_setPixelateDivision_guess`
- `D3DEmoteModule_getPixelateDivision_guess`
- `D3DEmoteModule_setPixelateDivision_guess`

同时在两套 registrar、两套构造默认值和四组访问器处写入了字段语义与独立性
注释。四份数据库均已保存。

## 7. 移植修改

- `Player.h`：删除“D3DEmoteModule 是误放 static/兼容遗留”的过时说明，明确
  Player 字段是 raw int32、默认 `100`、当前无直接消费者；
- `D3DEmoteModule.h`：明确模块字段是另一套独立的 per-instance raw int32；
- `main.cpp`：注册点说明修正为四端均存在的双实例属性；
- `motionplayer-dll.cpp`：新增 typed NCB 边界回归，覆盖默认值、六个 int32
  边界和双向不传播。

## 8. 验证

- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过；只有仓库既存
  `_tss` literal-operator 警告；
- `cmake --build out/web/debug -- -j 6`：完整重新编译/链接通过；
- `cmake --build out/wasmtime/debug -- -j 6`：完整重新编译/链接通过；
- 两个构建目录随后各复跑一次：均为 `ninja: no work to do.`；
- `git diff --check`：退出码 `0`，只有工作区既存 LF/CRLF 提示。
