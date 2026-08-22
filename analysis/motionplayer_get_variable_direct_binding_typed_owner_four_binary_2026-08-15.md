# MotionPlayer Primary #9 `getVariable`：Engine 直绑与 typed owner 四参考复原

日期：2026-08-15

本文只使用 `reference/binaries/` 中四个当前参考二进制，继续收口 Primary
`Motion.EmotePlayer` 注册表的第 9 项。2026-08-14 的变量路由纵切面已经证明
`EmoteEngine::getVariable(ttstr) -> double` 是 scope/HM4/bound-value 路由器；本轮新增
证据闭合的是它如何进入 NCBind：四端 registrar 都把 Engine 成员指针直接写进 typed
Function object，adjustment 为零，没有 `EmotePlayer::getVariable` forwarding body。

这也修正了本地源结构中的过时注释和转发层。Primary facade 仍以继承方式提供 Engine
payload，但脚本描述符的成员指针属于 `EmoteEngine` 本身。

## 1. 四端 registrar 的直接 target

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Primary registrar | `0x67CEA8` | `0x5612E8` | `0x1001B5130` | `0x1B4DE0` |
| descriptor materialization | `0x67D220..0x67D230` | `0x5613D8..0x5613E2` | `0x1001B5288..0x1001B529C` | `0x1B4F1C..0x1B4F26` |
| stored member target | `0x5341FC` | `0x4979BC` | `0x1001B5D84` | `0x1B5A2C` |
| recovered name | `EmoteEngine_getVariable_guess` | 同左 | 同左 | 同左 |
| member adjustment | `0` | `0` | `0` | `0` |

Android arm64 registrar 直接把 code pointer 和全零 adjustment 写入描述符。两个 32 位
目标在存 target 后分别以 `MOVS R3,#0` 写 adjustment；iOS arm64 同样以 `MOV X3,#0`
传给工厂。四端均没有先落到 facade thunk，再由 thunk 调 Engine 的第二跳。

因此源级形状是：

```cpp
Method(TJS_W("getVariable"), &EmoteEngine::getVariable);
```

不是：

```cpp
NCB_METHOD(getVariable); // EmotePlayer forwarding member
```

## 2. typed Function object 与调度入口

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| create Function | registrar 内联 | `0x56B46C` | `0x1001C75FC` | `0x1C4D58` |
| allocate | registrar 内联 | `0x56B4A0` | `0x1001C7650` | `0x1C4D80` |
| Function ctor | registrar 内联 | `0x56B4DC` | `0x1001C76B4` | `0x1C4E40` |
| main vtable | `0x1A16348` | `0x10B8BB0` | `0x101AE9BA8` | `0x1836A2C` |
| `FuncCall` | `0x68B0C4` | `0x56B544` | `0x1001C774C` | `0x1C4F40` |
| member invoke | `0x68B1E0` | `0x56B604` | `0x1001C782C` | `0x1C4FD4` |
| first-arg conversion | `0x68B2E4` | `0x56B6B4` | `0x1001C78F8` | `0x1C50F0` |

64 位 Function object 是 `0x40` 字节：facade/base object 从 `+0x20` 开始，二字
Itanium member pointer 的 code/adjustment 位于 `+0x30/+0x38`。32 位对象是 `0x24`
字节，对应位置为 `+0x14` 与 `+0x1C/+0x20`。这与 direct Engine target、零 adjustment
共同说明，Primary native payload 的 Engine base 从对象起点即可寻址。

## 3. `FuncCall` 的精确边界顺序

四端的共同外层顺序为：

```text
if membername != null: return TJS_E_MEMBERNOTFOUND
if objthis == null:    return TJS_E_NATIVECLASSCRASH
if result != null:     result.Clear()
if numparams < 1:      return TJS_E_BADPARAMCOUNT
unwrap Primary native payload from objthis
if unwrap fails/null:  return TJS_E_NATIVECLASSCRASH
convert only param[0] to owned ttstr
invoke stored Engine member
return TJS_S_OK
```

由此得到几条容易被常规 wrapper 写法改变的可观察边界：

- 非空 `membername` 优先于 receiver 检查，且不触碰已有 result；
- null receiver 同样不触碰 result；
- receiver 非空后，即使 argc 不足或 native unwrap 失败，result 已经变成 Void；
- 最少只要求一项参数，`param[1..]` 全部忽略；
- `flag` 和 `hint` 不参与这一路径；
- generic converter 内仍保留“缺参则构造 Void Variant”的模板分支，但外层
  `numparams >= 1` gate 使它在本 specialization 中不可达。

Android arm64 的 invoke helper 返回 boolean `1`，外层以位运算映射成 `0/-1`；其余
端虽然编译形状不同，对外成功值仍是 `TJS_S_OK`，unwrap failure 仍是 native-class
crash。

## 4. `tTJSVariant -> ttstr` 的 owner 交接

参数转换不是直接借用 `param[0]` 的字符串指针，而是完成以下 owner 链：

1. copy-construct 一份 wrapper-owned `tTJSVariant`；
2. 对该 Variant 执行 `ttstr` 转换，得到中间字符串 owner；
3. 通过原子引用计数 retain/release 序列，把恰好一个 owner 转交给 invoke frame；
4. 析构复制的 Variant 和中间 `ttstr` owner；
5. Engine 调用结束后，由 invoke frame 释放最后一份参数 owner。

四端 64 位用 `LDAXR/STLXR`，32 位用 `LDREX/STREX + DMB` 实现同一引用计数语义。
在字符串转换发生异常时，已经建立的 Variant/ttstr 临时量由模板展开的清理路径负责；
成功返回时，调用 core 期间始终至少有一个稳定的 `ttstr` owner。

这与 `EmoteEngine::getVariable` 内部继续按值复制 label 给 scope scanner、snapshot
reader 或 direct bound reader 的数据流叠加：NCBind owner 覆盖 Engine 入口参数的创建，
Engine 自己的短生命周期副本仍按 2026-08-14 路由纵切面所述释放。

## 5. `double -> tvtReal Variant` 返回交接

member invoke 先应用完整 Itanium member-pointer 规则：把 adjustment 加到 native payload；
若 code 低位表示 virtual member，再从调整后的对象/vtable 解出最终函数。当前四个
descriptor 都是非虚 code pointer 加零 adjustment，但通用模板没有删去虚成员路径。

Engine getter 的返回 double 以原始 64 位值跨过 helper。若 `result != nullptr`：

1. 在栈上建立一份临时 Variant；
2. 写入 type `5`，即 `tvtReal`，payload 为原始 double bits；
3. copy-assign 到调用方 result；
4. 析构该临时 Real Variant。

若 `result == nullptr`，Engine getter仍会完整执行，参数 `ttstr` 也照常释放，只跳过
临时 Real Variant 的构造/copy-assign/destruct。反编译器在两个 AArch64 端把浮点
function pointer 显示为 `long double`/`COERCE_UNSIGNED_INT128`，但寄存器搬运、Variant
type `5` 和四端一致的 64-bit payload 证明这是 Hex-Rays 类型展示伪影，不是 128-bit
源返回类型。

## 6. 调用链和对象生命周期

```text
script Motion.EmotePlayer.getVariable(label, ...surplus)
  -> typed NCBind Function::FuncCall
     -> unwrap ncbInstanceAdaptor<EmotePlayer>
     -> copy Variant(param[0]) -> owned ttstr
     -> stored member pointer EmoteEngine::getVariable
        -> Engine-owned Player
        -> scope hit ? direct bound reader
                     : snapshot-or-bound reader
     -> optional temporary tvtReal Variant -> caller result
     -> release wrapper-owned ttstr
```

Function object 由类注册表拥有；它只保存 facade metadata 和成员指针，不拥有 Engine
实例。每个脚本对象的 adaptor 管理 Primary native payload，Primary 又以继承方式把
同一对象首地址暴露为 Engine self；Engine 内部的单一 `unique_ptr<Player>` 才拥有查询
所需 Player。参数 owner 只覆盖一次调用，返回 Variant owner 只覆盖 result handoff，
两者都不会延长 native adaptor、Engine 或 Player 生命周期。

## 7. 源码、测试与 recovery IDB 落地

源码恢复：

- 删除 `EmotePlayer.h/.cpp` 中不存在于参考二进制的 Primary forwarding declaration/body；
- Primary registrar 改为显式 `Method(..., &EmoteEngine::getVariable)`；
- 保留 `D3DEmotePlayer::getVariable`，因为 D3D 是独立 shell/owner 链，不是 Primary
  descriptor 的 facade thunk；
- `Motion.Player.getVariable` 仍直接绑定 Player bound-value reader，与 Primary Engine
  router 分离。

回归测试同时锁定：

- Engine 成员签名严格是 `double (EmoteEngine::*)(ttstr)`；
- membername/receiver/result-clear/argc/native-unwrap 的顺序；
- surplus 参数被忽略；
- 成功 result 类型是 `tvtReal` 且值来自 Engine router；
- null result 仍成功调用并完成参数 owner 清理。

四份 recovery IDB 已统一补入 one-ttstr/double Function factory、`FuncCall`、invoke、
first-arg converter 的语义名和类型；registrar、边界、ttstr owner 和 Real Variant handoff
均有注释与 bookmark。强制失效 Hex-Rays cache 后，四端回读均重新显示上述命名和共同
边界。

验证结果：

- 完整 motionplayer Catch2 翻译单元使用 Web Debug 的真实 Emscripten 参数执行
  `-fsyntax-only` 成功，只有仓库既有 `_tss` literal-operator warning；
- `cmake --build --preset "Web Debug Build" --parallel` 完成最终 `index.html` 链接；
- 定向 `git diff --check` 通过，并确认源码不再残留 Primary facade getter 或其
  `NCB_METHOD(getVariable)` 绑定；
- 四份 recovery IDB 最终原位保存均返回 `ok=true`。

当前配置仍没有可直接运行的 native Catch2 motionplayer executable，因此这里只把
完整测试翻译单元的真实 Web ABI 编译记录为通过，不把 syntax-only 结果表述为运行时
测试已执行。
