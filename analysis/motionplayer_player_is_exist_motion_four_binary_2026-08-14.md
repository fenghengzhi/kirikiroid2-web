# MotionPlayer Player isExistMotion 四参考二进制恢复（2026-08-14）

## 函数映射

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6CDBD4` | `0x5942F4` | `0x10011F558` | `0x11E054` |

四份 recovery IDB 均已命名为 `Player_isExistMotion_guess`，返回类型恢复为 `bool`。
语义裁决仅依赖当前 `reference/binaries/` 四份二进制。

## 路径构造

四端按同一顺序创建临时字符串：

```text
"motion/" + Player.stealthChara + "/" + inputName
```

输入 `ttstr` 按值传入。空/null stealthChara 或 name 仍参与普通 ttstr 拼接，不存在路径
合法性检查或短路。中间临时 string owner 在完整 path Variant 建立后释放；拼接或引用计数
操作抛异常时 EH/SjLj 释放已建立的临时 owner。

## ResourceManager dispatch 协议

调用参数数组恰为：

```text
params[0] = &Player.findMotionContextVariant   // 持久成员本体
params[1] = &localPathVariant
```

参数 0 不是局部 CopyRef。自定义 ResourceManager dispatch 可通过 `*params[0] = value` 原地
替换 Player 的持久 project/motionKey context；这一别名行为在调用返回后仍可观察。

ResourceManager 取自 canonical `_resourceManager` Variant。四端都执行无 AddRef 的普通
Object 转换：非 Object 抛 Variant conversion exception；Object 内部空 dispatch 随后仍被
解引用，没有 `nullptr -> false` 保护路径。

调用形状为：

```text
rm.FuncCall(0, "isExistMotion", &globalHint,
            &result, 2, params, rm)
```

`FuncCall` 状态码未参与任何分支。返回后无条件执行 `tTJSVariant::operator bool()`：Void 为
false，非空 Object/Octet 为 true，Integer/Real 非零为 true，String 走其整数转换规则。
因此失败状态但写入 truthy result 仍返回 true；状态失败且 result 保持 Void 才表现为 false。

正常清理顺序是 result Variant 后构造、先析构；path Variant 随后析构。持久 context 参数
不在函数内析构，ResourceManager dispatch 也只是借用，没有临时 AddRef/Release 对。

## 与先前 Web 实现的差异

先前源码有三层额外保护，四端均不存在：

- 非 Object 或空 ResourceManager 返回 false；
- 把 context 复制到局部 Variant，阻断参数别名；
- `FuncCall` 失败时返回 false，跳过 result bool 转换。

现已改为强制 `AsObjectNoAddRef()`、参数 0 直接指向持久 context、忽略状态并无条件转换。
回归 probe 在返回 `TJS_E_FAIL` 的同时把 result 写为 1、把参数 0 改成
`"mutated-project"`，验证结果为 true 且 `Player.project` 被持久改写；整数 ResourceManager
验证转换异常。

## 验证与 IDB

- 聚合 `motionplayer-dll.cpp` 复用 Web Debug 的真实 Emscripten defines/includes/ABI 参数
  通过 `-fsyntax-only`，唯一诊断为仓库既有 `_tss` warning；
- `Web Debug Build` 完整编译/链接成功；PlayerResource 编译诊断另有仓库既有
  `imagepacker.h` 重复/误置 `nodiscard` warnings；
- 四份 IDB 已写入函数名、bool 原型、参数别名、调用状态、bool 转换和清理顺序注释并
  保存。

## 未外推的部分

- 参数 0 可被任意自定义 dispatch 修改是调用 ABI 的直接结果；标准 ResourceManager native
  本身把 project 参数按值接收，不会回写它。不能把恶意 probe 的回写说成标准路径副作用；
- 本纵切面不重新裁决 ResourceManager 内部 direct-then-full-scan，后者已在
  `motionplayer_resource_manager_motion_queries_random_four_binary_2026-08-14.md` 闭合。
