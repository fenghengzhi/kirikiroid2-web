# Motion.EmotePlayer `serialize/unserialize` typed binding 与 Variant owner 四参考复原（2026-08-15）

## 结论

四个当前参考二进制的 Primary member #11 `serialize` 与 #12 `unserialize`
都不是 `EmotePlayer` 转发函数：registrar 直接把
`EmoteEngine_serializeState_guess` / `EmoteEngine_unserializeState_guess`
写入 typed NCB descriptor，成员指针 adjustment 均为零。

因此对应的源级注册结构是：

```cpp
Method(TJS_W("serialize"), &EmoteEngine::serializeState_guess);
Method(TJS_W("unserialize"), &EmoteEngine::unserializeState_guess);
```

`serialize` 选择无参数、返回 `tTJSVariant` 的 typed specialization；
`unserialize` 选择一项 `tTJSVariant` 按值、返回 `void` 的 specialization，后者与
`draw` / `initPhysics` 复用同一家族。原端口中的两个 `EmotePlayer` forwarding body
会制造参考二进制里不存在的源级层次，现已删除。

## 四端 registrar 与字符串映射

| 目标 | Primary registrar | `serialize` pooled UTF-16LE | `unserialize` UTF-16LE |
|---|---:|---:|---:|
| Android arm64 | `0x67CEA8` | `0x14D3E10`（`unserialize` 后缀） | `0x14D3E0C` |
| Android armv7 | `0x5612E8` | `0xD847AA`（后缀） | `0xD847A6` |
| iOS arm64 | `0x1001B5130` | `0x10196037E`（后缀） | `0x10196037A` |
| iOS armv7 | `0x1B4DE0` | `0x17526E2`（后缀） | `0x17526DE` |

搜索按 UTF-16LE raw bytes 完成。四端都把 `serialize` 表示为同一
`unserialize` pool 中从第二个字符开始的后缀指针；这不是截断字符串，也不是另一项
单字符注册。

| 目标 | `serialize` target materialization | `unserialize` target materialization |
|---|---:|---:|
| Android arm64 | `0x67D2E8` → `0x673220` | `0x67D324` → `0x675424` |
| Android armv7 | `0x561404` → `0x55BB70` | `0x56141A` → `0x55CF3C` |
| iOS arm64 | `0x1001B52C8` → `0x1001AF774` | `0x1001B52E8` → `0x1001B1130` |
| iOS armv7 | `0x1B4F58` → `0x1AEF30` | `0x1B4F76` → `0x1B0B80` |

紧邻 target word 的 adjustment word 四端均为零。Android arm64 的
`unserialize` descriptor 被 registrar 内联构造，另外三端显式调用共享 typed 工厂；
两种代码生成得到相同 descriptor 与外部调用边界。

## `serialize` Function 对象与入口

| 目标 | create Function | allocate / ctor | `FuncCall` | invoke |
|---|---:|---:|---:|---:|
| Android arm64 | `0x67EF34` | create 内联 | `0x68B870` | `0x68B988` |
| Android armv7 | `0x56BB38` | `0x56BB6C` / `0x56BBA8` | `0x56BC10` | `0x56BCBC` |
| iOS arm64 | `0x1001C7EA4` | `0x1001C7EF8` / `0x1001C7F5C` | `0x1001C7FF4` | `0x1001C80A8` |
| iOS armv7 | `0x1C57A8` | `0x1C57D0` / `0x1C5890` | `0x1C5990` | `0x1C5A14` |

64 位普通 Function 对象分配 `0x40` bytes，embedded facade 从 `+0x20`
开始，二字 Itanium member pointer 位于 `+0x30/+0x38`。32 位对象分配
`0x24` bytes，facade 从 `+0x14` 开始，member pointer 位于 `+0x1C/+0x20`。
对象构造后检查 code word 与 adjustment low bit，空 member pointer 会抛
`No method pointer.`。Android arm64 把同一构造序列内联进 create helper；其余三端
保留独立 allocate/ctor helper。

## `FuncCall` 的精确边界顺序

四端共同伪代码为：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND           // -1001；result untouched
if objthis == null:
    return TJS_E_NATIVECLASSCRASH         // -1008；result untouched
if result != null:
    result.Clear()                        // 释放旧 owner，type 先变 void
if numparams < 0:
    return TJS_E_BADPARAMCOUNT            // -1004；result 已是 void

native = unwrap EmotePlayer/Engine instance from objthis
if unwrap fails or native payload is null:
    return TJS_E_NATIVECLASSCRASH         // result 已是 void
return invoke(callInfo, storedMemberPointer, native)
```

`flag` 与 `hint` 未读。零参数 specialization 的 arity 条件字面上是
`numparams < 0`，所以 `0` 以及任意非负 surplus count 全部接受；`params` 只被存入
call-info，不会解引用。这与“无参方法拒绝正参数”的常规直觉不同，属于脚本可见边界。

注意前两条 gate 的先后关系：给出非空 `membername` 时即使 `objthis` 也是 null，仍返回
`TJS_E_MEMBERNOTFOUND`；只有通过 top-level member gate 与 receiver gate 后才清 result。

## 返回 `Variant` 的 owner handoff

| 目标 | copy ctor | copy assignment | temporary dtor | eager `Clear` |
|---|---:|---:|---:|---:|
| Android arm64 | `0xA0DEE0` | `0xA0E464` | `0xA0E078` | `0xA0E090` |
| Android armv7 | `0x760178` | `0x760440` | `0x760238` | `0x76024A` |
| iOS arm64 | `0x100319B0C` | `0x100319E14` | `0x100319A60` | `0x100319B38` |
| iOS armv7 | `0x31EF80` | `0x31F1C0` | `0x31EF1C` | `0x31F014` |

invoke 不是把 hidden-sret 返回槽直接当脚本 result。四端一致执行：

```text
returnedTemporary = (native + adjustment)->serializeState()
ownedCopy = tTJSVariant(returnedTemporary)       // 第二次 copy construction
if result != null:
    *result = ownedCopy                          // retain source before clear/copy
destroy(ownedCopy)
destroy(returnedTemporary)
return success
```

因此 null result 只跳过最后 copy assignment；Engine snapshot、两个临时值构造和两个析构
仍全部执行。正常析构次序固定为第二个 owned copy 在前、hidden-sret returned temporary
在后。Android arm64 的 landing pads 还明确显示：assignment 抛异常时先析构 owned copy、
再析构 returned temporary；copy construction 本身抛异常时只析构已经存在的 returned
temporary，然后继续 unwind。不会发布部分 result。

Android arm64 的 invoke helper 内部返回 `bool`：native 不存在为 false，完成 handoff
为 true；外层用位运算映射成 `-1/0`。另外三端的 helper 直接返回 `-1/0`。四端最终
`FuncCall` 成功码均为 `TJS_S_OK`，native-null failure 均为 `-1`，这是 helper ABI/
代码生成差异，不应分裂 portable API。

成员指针调用遵循 Itanium 二字编码：先将 adjustment word 算术右移一位并加到 native
receiver；low bit 为一时再经调整后对象的 vtable 解析 virtual code word。本项
adjustment 为零且 code word 是非虚的 Engine 函数地址，但通用 specialization 仍保留
完整虚成员路径。

## `unserialize` 的 owner 边界

`unserialize` 复用已经由 `initPhysics` 纵切面闭合的一 Variant 按值、void 返回 typed
family：receiver gate 后先清 result，少于一参时返回 bad-param-count；随后复制
`param[0]` 到 wrapper-owned Variant，并按值再交给 Engine restore。多余参数忽略。
本纵切面的新证据是 registrar 的 target 本身：四端都存 Engine restore core 与零
adjustment，而不是存本地 `EmotePlayer::unserialize`。

## 源码与回归落点

- `EmotePlayer.h/.cpp` 删除 #11/#12 forwarding 声明和定义；
- `main.cpp` 保持脚本名不变，直接注册两个 inherited Engine 成员；
- 静态断言锁定 `tTJSVariant (EmoteEngine::*)()` 与
  `void (EmoteEngine::*)(tTJSVariant)`；
- typed runtime 回归覆盖 membername/receiver/result-clear precedence、负 argc、foreign
  receiver、非负 surplus、null result、fresh Dictionary 返回，以及按值 restore 的
  surplus 忽略行为；
- 四份 recovery IDB 已补齐 registrar、Function/FuncCall/invoke、Variant copy/clear/dtor
  的语义名、类型、注释和书签。

## 验证

- `motionplayer-dll.cpp` Emscripten syntax-only 通过，仅保留既有 `_tss` deprecated
  literal warning；
- `Web Debug Build` 完成 10-step 增量编译与最终 `index.html`/wasm 链接；
- 定向 `git diff --check` 通过，仅有工作树既有的 LF→CRLF 提示；
- 四份 recovery IDB 在名称/类型/伪代码回读后均原位保存成功。
