# MotionPlayer `Player.tags` 四参考二进制对照（2026-08-12）

## 1. 范围与结论

本纵向只覆盖 `Motion.Player.tags` 的注册、typed NCB property 边界、普通
motion 初始化时的 owner 提交、内部消费者以及销毁顺序。四个目标共同表明：

```cpp
tTJSVariant Player::getTags() const {
    return _tagFrameSourceVariant;
}
```

`tags` 不是每次访问都重新求值的 motion 属性，也不克隆 tag frame 容器。
它按值返回 Player 中持久保存的 `tTJSVariant`，因此会对同一个 dispatch 做一次
CopyRef。property 注册没有 setter；写入在 typed property dispatch 层返回
`TJS_E_ACCESSDENYED`，不会进入 Player。

本轮没有发现需要修改的本地执行语义。原来的内联 getter 和
`NCB_PROPERTY_RO(tags, getTags)` 与四端一致；实际修订是清理旧单 ABI 注释，并
补上直接覆盖底层 property dispatch 错误优先级和 owner 生命周期的回归测试。

## 2. 四端地址与对象字段对照

地址和 ABI 偏移只记录在本分析文档中。

| 证据 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| `tags` UTF-16 字符串 | `0x14D63BC` | `0xD85CCA` | `0x10195CA4C` | `0x174EDB0` |
| Player 成员注册函数 | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| `tags` 注册点 | `0x6D4310` | `0x598004..0x59801A` | `0x1001246B8` | `0x1239E4..0x123A02` |
| getter | `0x6D69F8` | `0x598E50` | `0x100125544` | `0x124748` |
| property create/register | 内联于注册函数 | `0x5B0EE4` / `0x5B0EA0` | `0x100146C14` / `0x100146BB8` | `0x147478` / `0x147434` |
| property `PropGet` | `0x6F482C` | `0x5B0F54` | `0x100146CC0` | `0x147590` |
| property `PropSet` | `0x6F4918` | `0x5B0FE0` | `0x100146D60` | `0x1475F6` |
| tag 持久 Variant 偏移 | `+1072` | `+732` | `+960` | `+668` |
| 普通 motion 初始化函数 | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| `tag` 读取/提交 | `0x6B0B20`, `0x6B0B50..0x6B0B58` | `0x580C9A`, `0x580CA2..0x580CA8` | literal `0x1001082E8`, PropGet `0x100108304`, copy-assign `0x100108308..0x100108310` | `0x1059B6`, `0x1059E6..0x1059EA` |
| Player 析构函数 | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |
| tag Variant 释放点 | `0x6CD01C` | `0x593C98` | `0x10011F32C` | `0x11DD96` |

getter 的四端伪代码只因 sret ABI 和字段偏移不同：

```text
Player_getTags_guess(sret, player):
    Variant_CopyRef(sret, player.tagFrameSourceVariant)
    return sret
```

没有属性查找、数组遍历、类型转换、容器复制或 Player 状态修改。

## 3. typed NCB property 对象结构

四端使用同一个 ncbind typed property 模板实例。64 位对象大小为 `0x50`：
getter 的成员函数指针对位于 `+48/+56`，setter 对位于 `+64/+72`。32 位对象
大小为 `0x2C`，返回的嵌入 property interface 位于对象 `+20`；getter 对位于
`+28/+32`，setter 对位于 `+36/+40`。

`tags` 创建时填入 getter，并把 setter 两个机器字都清零。64 位 Android 把小型
create/register 路径直接内联到 Player registrar，其他三端保留独立的同模板
函数。这是代码生成差异，不是源代码结构差异。

## 4. `PropGet` / `PropSet` 的精确错误优先级

四端汇编的共同控制流如下。Android armv7 和 iOS armv7 的反编译文本一度把
`-1007/-1008` 显示得不直观，但缺 getter 分支对预装的 `-1008` 执行加一，汇编
结果与 64 位目标一致。

```text
PropGet(membername, result, objthis):
    if membername != null: return TJS_E_MEMBERNOTFOUND   // -1001
    if getter is null:     return TJS_E_ACCESSDENYED     // -1007
    if objthis is null:    return TJS_E_NATIVECLASSCRASH // -1008
    if result != null: clear result to Void
    resolve Player native instance from objthis
    invoke the no-argument Variant getter

PropSet(membername, param, objthis):
    if membername != null: return TJS_E_MEMBERNOTFOUND   // -1001
    if setter is null:     return TJS_E_ACCESSDENYED     // -1007
    if objthis is null:    return TJS_E_NATIVECLASSCRASH // -1008
    if param is null:      return TJS_E_FAIL             // -1
    resolve Player native instance and invoke setter
```

因此 `tags` 的有效默认成员写入必定在空 setter 检查处停止。即使同时传入空
`objthis` 和空 `param`，结果仍是 `TJS_E_ACCESSDENYED`。带非空嵌套成员名的
读写则更早返回 `TJS_E_MEMBERNOTFOUND`。

另一个容易漏掉的边界是 result 清理时机：空 `objthis` 在 result 清理之前失败，
所以原 result 保持不变；非空但类型错误的 receiver 在 result 已清成 Void 后才在
native-instance 查找中失败。

## 5. getter 返回值的 owner 流

typed invoke 不把 Player 字段地址直接暴露给 TJS。共同过程是：

1. getter 从持久字段 CopyRef 构造返回 Variant；
2. invoke 模板再经临时 Variant 承接返回值；
3. result 非空时把临时值赋给 result；
4. 按逆序销毁两个临时 Variant。

最终 result 持有自己的 dispatch 引用。result 为空时 getter 仍执行，两个临时
owner 随即释放，净引用计数不变。

## 6. 普通 motion 初始化的提交顺序

四端 `Player_initNonEmoteMotion_guess` 的 owner 获取顺序完全相同：

1. 取得 motion content dispatch 的局部 owner；
2. 读取并提交 `loopTime`；
3. 读取并提交 `lastTime`；
4. 读取 `motion["tag"]` 到局部 Variant，copy-assign 到持久 tag 字段，销毁局部；
5. 读取 `motion["priority"]`，以同样方式提交持久 priority 字段；
6. 复制 priority owner，取索引零，读取其 `content`，提交 root-content 字段；
7. 此后才清 node-label map 和 parameter vector，并开始解析/构树。

四端 Variant copy-assignment helper 分别为 `0xA0E464`、`0x760440`、
`0x100319E14`、`0x31F1C0`。共同语义是先 retain 源 owner，再清理目标旧值，最后
复制 type/payload。object closure 会先 AddRef object 与 objthis，再 Release 目标
原值，所以源、目标原先别名同一个 dispatch 时也安全。

三个持久 owner 都是立即提交，没有事务或回滚。重复初始化时，旧 tag owner 在
tag 阶段就被释放；后续 priority、root content、参数解析或构树抛错不会恢复旧
tag。属性读取 helper 忽略 `PropGet` 的返回码，因此缺失 `tag` 且 dispatch 没有
写 result 时，默认 Void 局部值也会被提交，并在继续初始化前释放旧 tag owner。

## 7. 内部消费者与销毁

`PlayerFrameProgress.cpp` 的 forward、rewind 和 full-reseek 路径都用
`const auto &` 直接借用持久 tag owner，不增加额外 owner。`skipToSync()` 则先以
`const tTJSVariant tagFrames = _tagFrameSourceVariant` 建立一个局部 CopyRef，再
遍历其 frame；这使遍历期间的 dispatch 生命周期独立于字段别名。

Player 析构按成员逆序清理 Variant。持久 tag 字段在其后的 root-content 和
priority owner 已清理后释放自己的 dispatch 引用。通过公共 `tags` getter 留在
脚本或 C++ 调用者中的 Variant 是独立 owner，所以可安全存活到 Player 销毁之后。

## 8. 本地实现与测试对照

本地实现已经精确表达 getter 和只读注册：

```cpp
tTJSVariant getTags() const { return _tagFrameSourceVariant; }
NCB_PROPERTY_RO(tags, getTags);
```

本轮仅做以下源码侧改进：

- `Player.h`：把旧单 ABI getter 地址/字段偏移改为 owner 语义和立即提交说明；
- `main.cpp`：明确 `tags` CopyRef 持久值，以及空 setter 的错误优先级；
- `PlayerFrameProgress.cpp`、`PlayerTimeline.cpp`：清理 tag 消费路径的旧单 ABI
  地址/偏移注释，保留双递增、整数截断和独立局部 owner 等行为描述；
- `motionplayer-dll.cpp`：新增测试，覆盖未加载 Void、加载后同 dispatch 别名、
  Player 外层 property、原始 property dispatch 的四类错误顺序、result 清理时机、
  空 setter 优先级，以及 Player 销毁后保留别名仍有效。

## 9. IDB 改进

四份 IDB 均写入 `Player_getTags_guess` 名称和行为注释；typed property 的
PropGet/PropSet 均已命名并注释。非内联目标还命名了 property create/register，
iOS 两端补充了 native-instance helper 名称。四端 registrar 的 `tags` 注册点、
普通初始化的 `tag` 提交点以及析构的 tag 释放点也已加注释，并在修改后强制重新
反编译确认。

## 10. 验证

- `git diff --check`：通过；
- 使用 Web `compile_commands.json` 中 motionplayer 的实际 Emscripten 参数，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；仅有
  仓库既有的 `_tss` literal-operator 弃用警告；
- `cmake --build out/web/debug --target motionplayer -j 1`：通过；第一次工具调用
  在长增量重编译期间超时，但后台 Ninja 正常完成，随后重跑得到
  `ninja: no work to do`；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest -j 1`：通过，生成
  `krkr2_wasmtime_guest.wasm`；同样由后台编译/`wasm-opt` 正常完成后重跑确认无
  待办工作；
- `cmake --build out/web/debug --target krkr2 -j 1`：通过，成功链接
  `index.html`。输出只有仓库既有的 Emscripten pthread/memory-growth、JSPI 和
  JS library 警告。
