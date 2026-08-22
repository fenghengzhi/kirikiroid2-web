# DrawDevice transition 状态机、rule 生命周期与 Show 消费边界（四参考）

日期：2026-08-15

本纵切面只采用 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考二进制。旧 `libkrkr2.so` 的地址或注释不作为证据。

## 结论

DrawDevice/root 的 transition API 是一个很小但边界刻意不安全的状态机：

- `startTransition(options)` 严格把 `options` 转成 object，并以
  `TJS_MEMBERMUSTEXIST` 和同一个 object 作为 `objthis` 查询 `method`、`vague`、`rule`；
- 缺少 `method` 时把 method code 写成 `0`；存在时仅大小写敏感字符串
  `universal` 映射为 `1`，其他字符串都映射为 `0`；
- 只有 method code `1` 才重置 vague、释放旧 rule 并尝试取得新 rule texture；
- `startTransition` 正常走到末尾时无条件写 `active=true`、`state=1.0f`；
- `stopTransition` 写 `active=false`、`method=-1`、`state=0.0f`，并释放、清空 rule，
  但故意保留 vague；
- `Show()` 只读 active 和 state。method、vague、rule 在四端 `Show()` 中都没有读引用，
  合成固定使用 `AlphaBlend_SD`，唯一参数是 `opacity=state`。

因此 universal 配置当前只形成“解析 + 引用持有 + 释放”的数据流，并未形成 rule/vague
渲染数据流。不能因为字段名看起来像 KiriKiri universal transition，就补造尚不存在的
rule shader 或 vague 阈值运算。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `DrawDeviceObjectBase::startTransition_guess` | `0x529628` | `0x491F60` | `0x10022FD3C` | `0x22EE74` |
| `DrawDeviceObjectBase::stopTransition_guess` | `0x529A50` | `0x492204` | `0x10022FF28` | `0x22F09C` |
| `DrawDeviceObjectBase::Show_guess` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |

四份 recovery IDB 已将前两项写回上述语义 `_guess` 名；`Show_guess` 是此前已恢复的身份。

## transition 字段布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `bool active` | `+0x108` | `+0x8C` | `+0xA8` | `+0x5C` |
| `int32 method` | `+0x10C` | `+0x90` | `+0xAC` | `+0x60` |
| `float state` | `+0x110` | `+0x94` | `+0xB0` | `+0x64` |
| `int32 vague` | `+0x114` | `+0x98` | `+0xB4` | `+0x68` |
| dormant `tTJSVariant` | `+0x118` | `+0x9C` | `+0xB8` | `+0x6C` |
| retained `iTVPTexture2D *rule` | `+0x130` | `+0xA8` | `+0xD0` | `+0x78` |
| dormant pointer 0 / 1 | `+0x138/+0x140` | `+0xAC/+0xB0` | `+0xD8/+0xE0` | `+0x7C/+0x80` |

vague 与 rule 之间是一个 ABI 大小分别为 24/12 字节的 `tTJSVariant`。四端构造都只写
它的 type discriminator，默认 `tvtVoid` 构造不会清零 payload；根析构是插件范围内唯一
确认消费者。rule 后两枚 pointer-sized 槽只在构造时清零，start、stop、Show、capture、
target helper 与析构均不访问。它们的历史用途和原始拼写无法由 stripped binary 唯一恢复，
所以源码继续使用 `_guess` 名而不把它们接入当前 transition 数据流。

## `startTransition` 精确共同伪代码

```text
object = options.AsObjectNoAddRef()             // strict conversion

status = object.PropGet(MEMBERMUSTEXIST,
                        "method", &method, object)
if status is success:
    methodCode = (method.AsStringNoAddRef() == "universal") ? 1 : 0

    if methodCode == 1:
        vagueValue = 64
        if object.PropGet(MEMBERMUSTEXIST,
                          "vague", &vague, object) is success:
            vagueValue = vague.AsInteger()

        if ruleTexture != null:
            ruleTexture.Release()
            ruleTexture = null

        if object.PropGet(MEMBERMUSTEXIST,
                          "rule", &rule, object) is success:
            layer = tTJSNI_Layer::FromVariant(rule)
            image = layer.GetMainImage()         // exactly once
            ruleTexture = image.GetTexture()
            ruleTexture.AddRef()
else:
    methodCode = 0

active = true
state = 1.0f
```

四端共同支持的关键细节：

1. `options.AsObjectNoAddRef()` 之后没有 null-dispatch guard。object Variant 若携带 null
   dispatch，会沿原始成员调用边界失败，而不是被当成“缺少 method”。
2. `method` 缺失和任意非 `universal` 字符串都得到 method code `0`，但路径不同：前者来自
   PropGet failure，后者来自成功取值后的字符串比较。
3. 非 universal 路径不修改旧 vague，也不释放旧 rule。一次旧 universal 调用留下的 rule
   可以跨后续普通 transition 继续被持有，直到下一次 universal、`stopTransition` 或 root
   析构。
4. universal 路径先把 vague 写为 64；只有 successful property result 才执行整数转换。
5. universal 路径在查询新 rule 之前释放并清空旧 rule。rule 缺失会留下 null；rule 的
   Variant/native conversion 或后续调用抛异常时，旧 rule 也不会恢复。
6. rule 成功路径没有 layer、main image、texture 的 null guard；`GetMainImage()` 只调用一次，
   texture `AddRef()` 也是无条件调用。端口先前的 graceful-null 和双 `GetMainImage()` 都不是
   参考行为。
7. `active=true/state=1` 位于全部 option 处理之后。此前任一严格转换或成员调用抛异常时，
   这两个最终写入不会发生；但 universal 路径可能已经提交 vague 或释放旧 rule，故异常不
   提供事务式回滚。

## `stopTransition` 精确共同伪代码

```text
active = false
methodCode = -1
state = 0.0f

if ruleTexture != null:
    ruleTexture.Release()
    ruleTexture = null
```

四端都不改 vague。root 析构也会释放仍非空的 rule texture，所以 retained rule 有两个正常
释放入口：显式 stop/下一次 universal replacement，以及最终 root destructor。

## `Show()` 的真实消费面

通过 Window、manager settings 与 target ensure 后，四端先无条件执行 `Show` 自己的
两个 guarded-static 初始化：缓存 `FillARGB` method，再从该 method 缓存 `color` 参数
ID。这个初始化发生在第一次 active 判断之前，因此即使 transition inactive，首次有
Window 的 `Show()` 也会完成这两个 lookup。随后 transition-active 路径等价为：

```text
fillMethod.SetParameterColor4B(colorId, clearColor) // 每个 active Show 只调用一次
fillRect = full rect of FrontTarget                 // 只计算一次
OperateRect(fillMethod, FrontTarget, FrontTarget, fillRect, [])
OperateRect(fillMethod, BackTarget,  BackTarget,  fillRect, [])

CurrentTarget = FrontTarget
draw visible FrontItems whose drawPlane has bit 1

CurrentTarget = BackTarget
draw visible BackItems whose drawPlane has bit 2

if active:                                  // second active read is real
    static method = privateOpenGLManager.GetRenderMethod("AlphaBlend_SD")
    static id = method.EnumParameterID("opacity")
    method.SetParameterFloat(id, state)
    source = FrontTarget over its full rect
    manager.OperateRect(method,
                        BackTarget, BackTarget,
                        full target rect,
                        [source])
```

第二组 `AlphaBlend_SD`/`opacity` guarded statics 位于第二次 active 分支内，所以普通
非 transition Show 不会初始化它们。两个 Fill 共享 FrontTarget 的矩形；并不会为
BackTarget 再读一次尺寸。正常构造使双 target 同尺寸，但该复用仍属于异常/篡改状态下
可观察的原版边界。

静态字符串证据为四端共同的 `FillARGB`、`AlphaBlend_SD`、`opacity`；iOS 两端还直接列出
`color`。完整函数 listing 中的字段读取如下：

| 读取项 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| active | 2 | 2 | 2 | 2 |
| state | 1 | 1 | 1 | 1 |
| method | 0 | 0 | 0 | 0 |
| vague | 0 | 0 | 0 | 0 |
| rule texture | 0 | 0 | 0 | 0 |

这里的“0”来自对各自完整 `Show()` 地址范围内对应成员偏移的重新扫描，不是从反编译器
省略变量推断。第二次 active 检查同样需要保留，不能折叠成进入分支时的单次快照。

## 本地实现修正

`cpp/plugins/DrawDeviceD3D.cpp` 已按四端证据修正：

- 删除 options dispatch 的本地 null guard；
- 删除 rule layer/main image/texture 的本地 null guards；
- 将 rule 的 `GetMainImage()` 收敛为一次；
- 将 `FillARGB`/`color` 恢复为 active 判断前的函数局部静态缓存，每帧只发布一次
  ClearColor，并用 FrontTarget 的同一矩形依序 Fill Front/Back；
- 将 `AlphaBlend_SD`/`opacity` 恢复为第二次 active 分支中的独立函数局部静态缓存；
- 明确记录 `Show()` 只消费 active/state，避免未来把已解析字段误接入未经证实的渲染逻辑。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 增加 missing-method 与 universal-without-vague/rule
的公开脚本面回归，验证两条路径都正常进入 state=1，stop 后回到 state=0。

## 仍未知但不影响本纵切面闭合的事项

- method code `0/1/-1` 是否原本对应一个具名 enum，源码名不可由四份 stripped binary
  唯一证明，因此本地继续保留描述性字段名而不伪造 enum 类型；
- universal 字段可能是未完成功能、兼容旧脚本的残留，或供这四份构建未链接的另一消费者
  预留。四份当前参考只支持“本插件 `Show()` 不消费”，不支持推断其历史设计意图。
