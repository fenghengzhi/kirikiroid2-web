# D3DEmotePlayer.smoothing 四参考二进制复核（2026-08-13）

## 1. 结论

`D3DEmotePlayer.smoothing` 是 D3D 壳对象自己的 typed Boolean 读写属性，不是
`EmoteObject`、`EmoteEngine` 或内部 `Player` 的状态。四端 getter 都只无符号读取壳
末尾的一个字节；setter 都把 ncbind 已转换的 Boolean 写回同一字节。它具有以下边界：

- 构造默认值为 false；
- 未 `load`、primary 为空时仍可安全读写；
- `clear` 和 `load` 只拆换两个 EmoteObject 槽，不改 smoothing；
- `clone(targetOwner)` 构造一个默认新壳，只 clone primary，因此新壳 smoothing 为
  false，不继承源壳值；
- 在四份 D3D 壳/渲染相关代码范围中，该字节除构造清零及属性 getter/setter 外没有
  消费者。就当前四参考二进制而言，它是可存取但不影响渲染的数据兼容位。

最后一点严格限定于当前四份 1.3.9 二进制：它不是根据属性名推测出的“抗锯齿开关”。

## 2. 字符串、registrar 与 accessor

UTF-16LE `smoothing` 及注册/回调映射如下：

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| 字符串 | `0x14BE9C6` | `0xD767FA` | `0x10196FD44` | `0x17620F0` |
| member registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| name/callback registration | `0x52EC80` | `0x494178` | `0x1002323E0` | `0x23108E` |
| getter | `0x530470` | `0x494A28` | `0x100232E4C` | `0x231AB4` |
| setter | `0x530478` | `0x494A2E` | `0x100232E54` | `0x231ABA` |

四端 IDB accessor 已统一命名为：

- `D3DEmotePlayer_getSmoothing_guess`
- `D3DEmotePlayer_setSmoothing_guess`

精确原生 C++ 符号未保留，因此按仓库规则保留 `_guess`。

## 3. 布局与机器语义

| ABI | shell size | visible | smoothing |
| --- | ---: | ---: | ---: |
| 64-bit | `0x38` | `+0x30` | `+0x31` |
| 32-bit | `0x24` | `+0x20` | `+0x21` |

getter 的四端归一化语义是：

```cpp
return static_cast<unsigned char>(self->smoothing);
```

Android arm64 getter 为 `LDRB [X0,#0x31]`，Android armv7/iOS armv7 为
`LDRB.W [R0,#0x21]`，iOS arm64 为 `LDRB [X0,#0x31]`。返回路径没有
primary null guard，因为根本不读取 primary。

setter 的归一化语义是：

```cpp
self->smoothing = convertedBoolean;
```

Android arm64 在 `STRB` 前额外执行 `AND W8,W1,#1`；另三端直接存低字节。这是
编译器对 C++ `bool` 参数的 ABI/规范化差异，不改变 typed Boolean wrapper 可观察
语义，也不能解释为“setter 忽略参数并恒置 true”。这与相邻的历史拼写
`queing` 属性恰好不同：`queing` 的四端 setter 才是无视参数的 one-way trigger。

## 4. 构造、clear/load 与 clone 生命周期

构造证据：

| 路径 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| shell ctor/factory | typed factory 内联 | `0x497824` | `0x100236300` | `0x235022` |
| 双 flag 清零 | `0x542BE8` | `0x497860` | `0x100236360` | `0x235070` |

四端都用一个 16-bit zero store 同时清掉 visible 与 smoothing。Android arm64 的
`clone` 内联构造也在 `0x530410` 执行同样的 `STRH WZR,[shell,#0x30]`；另外三端
clone 调用上述普通构造函数。因而默认和 clone 边界不是仅由源码 in-class
initializer 推断，而有四端直接机器证据。

`clear` 的四端 body 只按 secondary→primary 顺序 delete/null 两个槽，不访问
scalar/flag 尾部；`load` 先调用/内联同一 clear，再发布新 primary，同样不重置
smoothing。因此状态转换是：

```text
new shell:              smoothing = false
set smoothing = true:  smoothing = true
clear/load:             smoothing 保持 true
clone(source):          clone.smoothing = false
```

## 5. 消费者检索

在每份二进制的 D3D 插件壳、registrar、factory/clone、绘制与 listener 相邻代码范围
内检索壳末尾偏移：

- 64-bit `+0x31` 只有 smoothing getter/setter；
- 32-bit `+0x21` 只有 smoothing getter/setter；
- `+0x30/+0x20` 的额外命中属于 visible accessor、构造时的双 flag 16-bit 清零，
  或同一范围内其他不同对象的字段访问。

getter 的 xref 也只来自 member registrar，没有 native 调用者。故不能把该 flag
继续传播进 Player 渲染配置，也不能在 `clear/load` 时擅自重置。

## 6. 源码、测试与 IDB 改进

源码保持原本已正确的 shell-local `bool` 实现，并补充四端边界注释；同时删除同一
字段块中只对单个平台成立的旧固定偏移注释，以及 D3D 注册表旁的旧单二进制地址叙事。

单元测试新增/强化：

- typed factory 新壳默认 false；
- primary 为空时通过 NCB `PropSet/PropGet` 读写成功；
- true 经 `clear` 后保留；
- 随后写 false 确实清零，证明它不是 `queing` 式 one-way trigger；
- loaded source 为 true 时，clone 仍为 false。

四份 IDB 均补充 registrar/accessor/构造或 clone 注释，并保存到原数据库。

## 7. 验证

- Web Debug 完整目标成功完成最终 `index.html/index.wasm` 链接；前台工具在 64 秒
  到时，但产物时间已更新且随后重跑为 `ninja: no work to do.`；
- Wasmtime Headless Debug 完成 14 个目标（包括普通/guest motionplayer 对象与最终
  链接）；前台同样在 60 秒边界返回超时，输出已经到 `[14/14]`，随后重跑为
  `ninja: no work to do.`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数，并加入既有 `out/syntax-check` Catch2/test
  config，执行 `-fsyntax-only` 成功；唯一诊断是仓库既有的 `_tss`
  literal-operator 弃用 warning；
- 当前配置没有生成可直接运行的 native Catch2 executable，因此这里准确记录为
  完整测试翻译单元编译验证，不把 syntax-only 冒充成运行时执行。
