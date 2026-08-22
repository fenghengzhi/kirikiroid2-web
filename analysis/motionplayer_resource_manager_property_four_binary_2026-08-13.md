# Motion.Player.resourceManager 四端只读属性与三重 owner（2026-08-13）

## 结论

本轮以 `reference/binaries/` 中四个 1.3.9 参考二进制为共同真值，重新复核了 `Motion.Player.resourceManager` 的宽字符串、NCB 注册、getter、构造/析构 owner 顺序和脚本边界。旧 `libkrkr2.so` 单端地址注释不作为证据。

四端共同语义为：

- Player 构造只接收一个 ResourceManager dispatch 输入，并把同一个 `tTJSVariant` 分别 CopyRef 到三个持久字段；这三个字段是独立 owner，不是三个借用指针。
- 前两个 owner 分别服务于 find-source 和 render SourceCache 路径；第三个是公开 `resourceManager` getter 使用的 canonical owner。
- `resourceManager` 是真正的只读 NCB 属性。四端注册器都给出 getter，并把 setter 成员指针及其附加字清零；不存在脚本 setter。
- getter 对 canonical Variant 做 CopyRef。它保持原始 Variant 类型和 Object/ObjThis 身份，不克隆对象、不解包 native、不查询任何成员，也不改写另外两个 owner。
- 默认以 Void 构造 Player 时，三个 owner 与 getter 结果均为 Void。
- Player 析构先释放较后的 canonical owner，再继续逆序释放前两个 owner。getter 已返回的 Variant 是独立 owner，因此 Player 销毁后仍可保持 ResourceManager dispatch 存活。
- 旧端口中的 `setResourceManager(tTJSVariant)` 没有调用者，也没有四端 NCB/生命周期对应物；本轮删除它，避免在构造后同时改写三个 owner 和非 owning native cache，从而制造参考实现不存在的公开可变面。

## 宽字符串与注册

`resourceManager` 使用 UTF-16LE。四端 byte search 都只有一个完整命中，并且都落入 `Player_ncb_registerMembers_guess`：

| 目标 | UTF-16 字符串 | 注册器 | 名称装配/注册点 |
|---|---:|---:|---:|
| Android arm64 | `0x14D6366` | `0x6D3DA8` | `0x6D3F90` |
| Android armv7 | `0xD85C74` | `0x597EC8` | `0x597F22` |
| iOS arm64 | `0x10195C9DC` | `0x1001244F8` | `0x100124568` |
| iOS armv7 | `0x174ED40` | `0x123848` | `0x1238B2` |

64 位注册体把 getter 地址写入 typed no-argument Variant property descriptor，并把 setter 相关槽全部写成零。32 位注册体把 getter 作为 `R2`，把 `R3` 与栈上的 setter member-pointer words 全部置零。它们最终都进入已识别的 `NCB_PlayerNoArgVariantProperty_register_guess` 模板实例。

因此端口使用 `NCB_PROPERTY_RO(resourceManager, getResourceManager)` 是结构一致的；脚本 `PropSet` 返回 `TJS_E_ACCESSDENYED`，而不是静默改写内部 owner。

## Getter 与 canonical 字段

| 目标 | getter（本轮统一命名） | canonical Variant 偏移 | 指令形态 |
|---|---:|---:|---|
| Android arm64 | `0x6D67F4` | `+992` (`+0x3E0`) | `ADD X1,X0,#0x3E0; MOV X0,X8; B VariantCopyRef` |
| Android armv7 | `0x598D3C` | `+684` (`+0x2AC`) | 返回对象与 `this+0x2AC` 进入 VariantCopyRef |
| iOS arm64 | `0x100125448` | `+880` (`+0x370`) | `ADD X1,X0,#0x370; MOV X0,X8; B VariantCopyRef` |
| iOS armv7 | `0x124642` | `+620` (`+0x26C`) | 返回对象与 `this+0x26C` 进入 VariantCopyRef |

四个函数均已命名为 `Player_getResourceManager_guess`。64 位使用隐藏 sret 寄存器，32 位使用各自 ABI 的显式返回对象地址，但源级语义相同：

```cpp
tTJSVariant Player::getResourceManager() const {
    return _resourceManager;
}
```

这不是 `ResourceManager *` getter。返回对象仍是输入 dispatch 的 TJS closure；若 Object 与 ObjThis 都非空，CopyRef 分别对二者 AddRef。重复 getter 因而返回同一 closure 身份的独立 owner，而不是新的 ResourceManager 实例。

## 构造：一个输入，三个独立 CopyRef

| 目标 | `Player_ctor_guess` | find-source owner | render SourceCache owner | canonical owner |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CC110` | `+636` (`+0x27C`) | `+656` (`+0x290`) | `+992` (`+0x3E0`) |
| Android armv7 | `0x5935C4` | `+428` (`+0x1AC`) | `+440` (`+0x1B8`) | `+684` (`+0x2AC`) |
| iOS arm64 | `0x10011EC04` | `+524` (`+0x20C`) | `+544` (`+0x220`) | `+880` (`+0x370`) |
| iOS armv7 | `0x11D488` | `+364` (`+0x16C`) | `+376` (`+0x178`) | `+620` (`+0x26C`) |

每个构造函数都保留输入 dispatch，并在三个不同目的地址上各调用一次该 ABI 的 Variant CopyRef helper：

- Android arm64：`0x6CC27C`、`0x6CC290`、`0x6CC308` 调用 `0xA0DEE0`；
- Android armv7：`0x59368C`、`0x59369A`、`0x5936F0` 调用 `0x760178`；
- iOS arm64：`0x10011ECB8`、`0x10011ECC8`、`0x10011ED3C` 调用 `0x100319B0C`；
- iOS armv7：`0x11D5E4`、`0x11D5F8`、`0x11D6BA` 调用 `0x31EF80`。

第一、第二个 owner 后面隔着 source descriptor、内部 Layer、颜色 Dictionary、内部工作 Layer以及 stealth 字符串等成员，canonical owner 才出现。这种布局解释了为什么三个 owner 不能压成一个字段加两个裸指针：中间路径会创建临时副本并有自己的异常清理/析构边界。

## 析构与返回值生命周期

| 目标 | `Player_dtor_guess` | canonical 释放 | render owner 释放 | find-source owner 释放 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CCEBC` | `0x6CD03C` (`+992`) | `0x6CD0F0` (`+656`) | `0x6CD0F8` (`+636`) |
| Android armv7 | `0x593C24` | `0x593CB4` (`+684`) | `0x593D1C` (`+440`) | `0x593D24` (`+428`) |
| iOS arm64 | `0x10011F2A0` | `0x10011F34C` (`+880`) | `0x10011F3B4` (`+544`) | `0x10011F3BC` (`+524`) |
| iOS armv7 | `0x11DCC4` | `0x11DDBA` (`+620`) | `0x11DE3C` (`+376`) | `0x11DE46` (`+364`) |

四端都先走 canonical 字段所在的较后成员区，再在普通逆序成员析构中依次释放 render owner 和 find-source owner。三次释放互相独立。

由此可直接推出 getter 的边界：

1. getter CopyRef canonical owner；
2. Player 析构释放自己的三个 owner；
3. 先前返回值仍持有自己的 closure 引用；
4. 只有最后一个外部 Variant 释放后，dispatch 才可析构。

这不是“Player 销毁时让 getter 结果失效”的借用语义。

## 端口改动与回归边界

源码改动：

- 删除无调用者、无四端对应物的 `Player::setResourceManager`；
- 保留按值 `getResourceManager()`，并把旧单端地址叙事替换为四端共同语义；
- `main.cpp` 明确记录 getter + 空 setter member pointer 的只读注册；
- 构造、字段顺序和 native cache 注释不再引用过时 `Player_ctor@0x6CED30` / `Player_dtor@0x6CFADC`。

新增单元测试覆盖：

- Object/ObjThis 相同的 probe closure 使一次 Variant CopyRef 精确表现为两次 AddRef；Player 构造额外产生六次 AddRef，证明三个独立 owner；
- 直接 getter 与脚本 getter 都保持同一 closure 身份；
- `resourceManager` 的脚本写入返回 `TJS_E_ACCESSDENYED`；
- nested member 先返回 `TJS_E_MEMBERNOTFOUND`，默认 setter 缺失对空参数/空 receiver 仍返回 `TJS_E_ACCESSDENYED`；
- Player 销毁后 getter 返回值继续持有 dispatch，最后一个外部 alias 清理时才析构 probe；
- Void 构造的 Player getter 返回 Void。

## IDB 改进

四份 IDB 均已完成：

- getter 统一命名 `Player_getResourceManager_guess`；
- registrar、getter、Player ctor、Player dtor 写入跨端一致语义注释；
- 所有绝对地址只保留在本文与 IDB，不传播进可移植源码注释。

## 验证

- Web Debug 全量构建最终链接 `index.html` / `index.wasm` 成功，随后 `ninja` 返回 `no work to do`；
- Wasmtime Debug 的第一次前台调用超时后仍在继续，期间误启动的第二个 Ninja 与它同时操作 `index.wasm`，`llvm-objcopy` 因文件锁返回一次 `permission denied`；原单实例随后完成全部产物，最后单实例复核为 `no work to do`。该并发锁错误不是编译或链接语义错误；
- 使用 Web 构建中 `EmoteEngine.cpp` 的真实 Emscripten 参数，对完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功；只有仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check` 无 whitespace error；
- 四份 IDB 均通过原位保存成功。
