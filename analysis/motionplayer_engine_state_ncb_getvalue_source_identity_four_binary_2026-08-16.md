# Engine state `ncbPropAccessor::GetValue` 源码身份：四参考二进制恢复记录

日期：2026-08-16

## 1. 结论与范围

本纵切面复审 Engine/controller 状态恢复中三组此前由本地匿名函数手写展开的属性读取：

- Base／OuterForce 子状态的 named `tTJSVariant` 读取；
- Eye／Eyebrow request queue 元素 `p0`、`p1` 的 named `float` 读取；
- Var controller 的 `frame`、`prev`、`target` channel 的 indexed `float` 读取。

四个参考共同证明它们不是插件自有 raw-dispatch helper，而是仓库 `ncbind.hpp` 中
`ncbPropAccessor::GetValue<T>` 的三个模板实例。恢复后的源码直接表达为：

```cpp
object.GetValue(name, ncbTypedefs::Tag<tTJSVariant>(), 0, hint);
object.GetValue(name, ncbTypedefs::Tag<float>(), 0, hint);
array.GetValue(index, ncbTypedefs::Tag<float>(), 0);
```

strict optional probe 没有被合并：它把 `MEMBERMUSTEXIST` 结果写入独立临时量，只有非负
status 才转换或复制并提交 caller destination；这与 `GetValue` 的单次无条件消费语义不同。

## 2. 三个模板实例映射

| 模板实例 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| named `GetValue<float>` | `0x661AEC` | `0x552C68` | `0x1001A316C` | `0x1A2490` |
| indexed `GetValue<float>` | `0x665394` | `0x554948` | `0x1001A5494` | `0x1A4A3C` |
| named `GetValue<tTJSVariant>` | Base/OuterForce 内联 | `0x55218C` | `0x1000F1860` | `0xEDBF0` |

三端保留的 Variant 实例现统一命名为
`ncbPropAccessor_GetValueNamedVariant_guess`。两个 float 实例此前仍使用泛化的
`VariantObject_getFloat*` 名；四库现统一恢复为：

- `ncbPropAccessor_GetValueNamedFloat_guess`；
- `ncbPropAccessor_GetValueArrayFloat_guess`。

64 位实例从 `self+8` 读取 dispatch，32 位实例从 `self+4` 读取 dispatch；前一机器字是
`ncbPropAccessor` vptr。它们的形参还保留一个不承载数据的 `Tag<T>` 参数，进一步把函数
身份收紧到 `GetValue` 模板，而不是恰好拥有相同行为的普通 helper。

## 3. named／indexed float 数据流

四端 named float 实例共同执行：

```text
temporary = Void
self.dispatch->PropGet(flags, member, hint, &temporary, self.dispatch)
real64 = temporary.AsReal()
result = narrow<float>(real64)
destroy temporary
return result
```

indexed 实例只把虚调用替换成：

```text
self.dispatch->PropGetByNum(flags, index, &temporary, self.dispatch)
```

关键边界如下：

- getter status 被忽略；失败但写值时仍转换该值；
- 没有 `MEMBERMUSTEXIST` probe，也没有默认值分支；
- named 读取原样转发 process-wide hint；indexed 读取没有 hint 参数；
- receiver 与 `objthis` 都是 accessor 持有的同一 dispatch；
- 转换先在 double/TJS real 域完成，再在模板内部窄化为 float；
- Variant 临时量在 float 结果形成后析构；转换异常走平台 EH cleanup 后继续传播。

request queue 的具体 call-site 对应为：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `p0` | `0x661650` | `0x55292C` | `0x1001A2DE4` | `0x1A201C` |
| `p1` | `0x661670` | `0x552944` | `0x1001A2E00` | `0x1A204C` |

Var channel loop 的三个实例调用为：

| channel | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `frame` | `0x665074` | `0x5546D8` | `0x1001A51D4` | `0x1A474C` |
| `prev` | `0x665144` | `0x554754` | `0x1001A527C` | `0x1A47F0` |
| `target` | `0x665214` | `0x5547D2` | `0x1001A5328` | `0x1A4896` |

每次 indexed read 后立即写相应 native float 数组，因此后续 index 转换抛出时保留已写前缀。

## 4. Variant 模板与 Base／OuterForce 调用链

Base restore 入口分别为 `0x67846C`、`0x55EAC0`、`0x1001B24DC`、`0x1B1F8C`；
OuterForce restore 分别为 `0x67872C`、`0x55EC4C`、`0x1001B26CC`、`0x1B21DC`。
每个入口只构造一个 vptr+dispatch accessor，并在其整个子树序列中保持该 owner 存活。

Android arm64 把 Variant 模板内联。Base 的四次虚调用位于 `0x678514`、`0x678570`、
`0x6785CC`、`0x678628`；OuterForce 的三次调用位于 `0x6787D4`、`0x678830`、
`0x67888C`。每一段都是 flags 0 `PropGet`，随后 copy-construct 返回 Variant，再销毁属性
temporary。

其余三端调用 standalone 实例：

| caller | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|
| Base `coord/scale/color/rotate` | `0x55EB0A/34/5E/88` | `0x1001B2550/58C/5C8/604` | `0x1B2038/7A/BC/FE` |
| Outer `bust/hair/parts` | `0x55EC96/C0/EA` | `0x1001B2740/77C/7B8` | `0x1B2288/CA/30C` |

每个返回 Variant 立即按字段顺序交给 child restore。缺失属性通常留下 Void；前三个 Base
Var child 因自身 outer type gate 安静返回，`rotate` 进入未设 outer type gate 的 Angle restore
并触发其原生异常边界。任何后续失败都不回滚已经恢复的子树。

## 5. 本地恢复与回归

`EmoteEngine.cpp` 删除了 `getTJSVariantProperty`、`getTJSRealProperty` 和
`getTJSRealByNum` 三个手写模板展开：

- request queue 直接调用 named `GetValue<float>`；
- Var channel loop 直接调用 indexed `GetValue<float>`；
- Base／OuterForce 直接调用 named `GetValue<tTJSVariant>`。

新的回归用返回 `TJS_E_FAIL` 但仍写输出的自定义 dispatch，覆盖 Variant、named float、
indexed float 三种模板。断言锁定单次读取、flags 0、member/index、hint、
`objthis == accessor.dispatch`，并确认失败 status 不覆盖 getter 写入值的消费。

## 6. IDB 与验证

四份 recovery IDB 已写入两个 float 模板的语义名和安全 prototype，并在 helper、request
queue call-site、Var channel loop 与 Base／OuterForce restore 上补充源码身份、读取顺序和
提交边界注释；Android arm64 的七个内联 Variant 模板 block 也逐项标注。四库名称强制
读回成功，并加入 `Engine state ncb GetValue template family (2026-08-16)` 书签。

验证结果：

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer 测试翻译单元
  syntax-only 通过，仅有仓库既有 `_tss` 弃用警告；
- Web Debug 与 Wasmtime Headless Debug 增量构建通过；
- 本专题定向 `git diff --check` 无新增内容级 whitespace error；
- 四份 recovery IDB 已原位保存。

