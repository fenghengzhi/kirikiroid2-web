# MotionPlayer Eye/Eyebrow/Mouth metadata `ncbPropAccessor` 源码身份四参考复原（2026-08-16）

## 结论

本轮对 Eye/Blink、Eyebrow 和 Mouth 三个 controller 的 metadata 构造路径重新执行四参考
纵切面，纠正了 portable 端仍以 plugin-local raw dispatch helper 表达该路径的源码结构偏差。
Android ARM64、Android ARMv7、iOS ARM64、iOS ARMv7 共同证明：三个构造函数都先把传入
`tTJSVariant` copy-construct 成临时 Variant，再从该临时值构造一个真实、带 vptr 的
`ncbPropAccessor`。accessor 取得 dispatch reference 后，源 Variant 在同一 full-expression
末尾立即析构；accessor 自己则活到构造函数尾部并 Release。

Eye/Blink 与 Eyebrow 的 `edge`、`node` 也不是直接操作返回 Variant 的手写 helper：每一层
返回值都继续构造独立 `ncbPropAccessor`，每个 pair/row accessor 是循环迭代局部 owner。
三类 controller 还共享同一组八个静态 member hint 槽；Mouth 只消费 slot 0，Eyebrow 消费
slot 0/6/7，Blink 消费全部八槽。

## 四平台构造函数映射

| 参考二进制 | Blink ctor | Eyebrow ctor | Mouth ctor |
| --- | ---: | ---: | ---: |
| Android ARM64 | `0x65FD48` | `0x661BEC` | `0x663078` |
| Android ARMv7 | `0x551B34` | `0x552CDC` | `0x55369C` |
| iOS ARM64 | `0x1001A1C8C` | `0x1001A31F4` | `0x1001A3DE4` |
| iOS ARMv7 | `0x1A0E50` | `0x1A2560` | `0x1A3200` |

Mouth 构造函数在四库中补名 `EmoteMouthController_ctor_guess`。Blink 与 Eyebrow 沿用此前
已闭合的 `EmoteBlinkController_ctor_guess`、`EmoteEyebrowController_ctor_guess`。参考库均已
stripped，因此恢复名继续保留 `_guess`，不把语义识别误写成原作者符号恢复。

## root accessor 的 copy/vptr/source-destroy 链

四端、三个构造函数的共同源码等价于：

```cpp
ncbPropAccessor object{tTJSVariant(dict)};
```

ABI 展开均出现 `tTJSVariant` copy construction、安装 `ncbPropAccessor` vptr、
`AsObject`/dispatch `AddRef`、随后销毁 copied Variant。下表列出每个构造函数最容易核对的
copy/vptr/source-dtor 指令位置：

| 目标 | Blink | Eyebrow | Mouth |
| --- | --- | --- | --- |
| Android ARM64 | `0x65FE04 / 0x65FE1C / 0x65FE58` | `0x661CA0 / 0x661CB8 / 0x661CF4` | `0x6630C8 / 0x6630E0 / 0x66311C` |
| Android ARMv7 | `0x551BAA / 0x551BB6 / 0x551BC2` | `0x552D4A / 0x552D56 / 0x552D62` | `0x5536D0 / 0x5536DA / 0x5536E6` |
| iOS ARM64 | `0x1001A1CDC / 0x1001A1CEC / 0x1001A1D00` | `0x1001A323C / 0x1001A324C / 0x1001A3260` | `0x1001A3E10 / 0x1001A3E20 / 0x1001A3E34` |
| iOS ARMv7 | `0x1A0EF4 / 0x1A0F06 / 0x1A0F16` | `0x1A25FE / 0x1A2610 / 0x1A2620` | `0x1A326A / 0x1A327C / 0x1A328C` |

这条链排除了两种旧解释：传入值不是直接解出裸 `iTJSDispatch2*` 后交给一组自由函数，也
不是让 copied Variant 一直充当 property owner。临时 Variant 只负责向 accessor 交付一次
对象引用；之后 accessor 的独立 AddRef/Release 决定 owner 生命周期。

## 共享 hint family

八槽都是连续的 `tjs_uint32` member hint，槽序在四端完全一致：

| slot | member | Blink | Eyebrow | Mouth |
| ---: | --- | :---: | :---: | :---: |
| 0 | `beginFrame` | 是 | 是 | 是 |
| 1 | `endFrame` | 是 | 否 | 否 |
| 2 | `blinkIntervalMin` | 是 | 否 | 否 |
| 3 | `blinkIntervalMax` | 是 | 否 | 否 |
| 4 | `blinkFrameCount` | 是 | 否 | 否 |
| 5 | `blinkEnabled` | 是 | 否 | 否 |
| 6 | `edge` | 是 | 是 | 否 |
| 7 | `node` | 是 | 是 | 否 |

| 参考二进制 | slot 0 基址 | IDB 表达 |
| --- | ---: | --- |
| Android ARM64 | `0x1AB4EA0` | `g_EmoteControllerMetadataHints_guess` |
| Android ARMv7 | `0x1111438` | `g_EmoteControllerMetadataHints_guess` |
| iOS ARM64 | `0x101B69F50` | 位于宽泛未命名数据项 `qword_101B69A20[166]` 内，精确地址已加注释 |
| iOS ARMv7 | `0x187D970` | `g_EmoteControllerMetadataHints_guess` |

四库对 slot 0 基址的完整 xref 都只落在这三个构造 family；Blink 继续引用后七槽，Eyebrow
只引用 slot 6/7，Mouth 不再引用其他槽。因此 portable 端恢复为一组共享全局 hint，而不是
为三个类分别建立同名缓存。

## nested accessor 对象图与数据流

Blink/Eyebrow 的共同数据流可写成：

```text
dict Variant copy
  -> root ncbPropAccessor object
       -> hinted GetValue<Variant>("edge")
            -> edge ncbPropAccessor
                 -> GetArrayCount()
                 -> 每个 index: GetValue<Variant>(i)
                      -> iteration-local pair accessor
                           -> GetValue<tjs_int>(0)
                           -> GetValue<tjs_int>(1)
                           -> int -> float pair
       -> hinted GetValue<Variant>("node")
            -> node ncbPropAccessor
                 -> GetArrayCount()
                 -> 每个 index: GetValue<Variant>(i)
                      -> iteration-local row accessor
                           -> GetArrayCount()
                           -> 每项 GetValue<tjs_int>(j) -> float
```

每个 named/indexed `GetValue<tTJSVariant>` 的返回临时值在相应 accessor 构造完成后的
full-expression 末尾析构。pair/row accessor 在每次循环末尾 Release；root、edge、node 三个
长生命周期 accessor 到正常构造尾才按 node→edge→root 的逆构造顺序 Release。异常清理块也
沿相同 owner 边界展开，已经构造成功的内层 owner 先释放，不把源临时 Variant 当作替代 owner。

Mouth 没有 nested array，它只从 root accessor 读取 hinted `beginFrame`，然后在构造尾释放
root accessor。

## 模板 helper 身份与边界行为

| helper 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| named integer | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |
| named real | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |
| named bool | `0x660AB4` | `0x552124` | `0x1000F3078` | `0xEF7F0` |
| named Variant | caller 内联 | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| array count | `0x56CA74` | `0x4BEB84` | `0x1000F30F4` | `0xEF8B4` |
| indexed Variant | caller 内联 | `0x5334E0` | `0x1000691F8` | `0xED9A8` |
| indexed integer（本 family） | `0x660B9C` | `0x4C7970` | `0x100069180` | `0xEF730` |

indexed integer 这一 emitted clone 同时被 NodeTree 和本轮 Eye/Eyebrow 调用，因此四库保守
命名为 `ncbPropAccessor_GetValueArrayInteger_NodeTreeEmote_guess`。其他翻译单元仍有未闭合的
同模板 clone，例如 Android ARMv7 `0x58B114`、iOS ARM64 `0x100114880`、iOS ARMv7
`0x112318`；没有把本 family 的证据外推并强行合并这些实例。

helper 的共同边界是：

1. named/indexed `GetValue<T>` 都以 accessor 内的 `_obj` 同时作为 receiver 和 `objthis`；
2. getter flags 为 0，hint 只用于 named 版本；
3. `PropGet`/`PropGetByNum` 的 HRESULT 不参与返回决策，即使返回负值，只要 callee 写入了
   Variant，模板仍销毁临时值之前完成目标类型转换；
4. `GetArrayCount()` 是 `PropGet(0, "count", nullptr, ...)` 后转整数，不是 dispatch
   `GetCount()`；
5. integer 坐标先按 TJS integer 转换，再由 controller 显式收窄/转成 `float`。

## portable 源码与回归测试

本轮更新：

- `EmoteBlinkController.cpp`、`EmoteEyebrowController.cpp`、
  `EmoteMouthController.cpp` 直接构造真实 `ncbPropAccessor`，删除这三条路径的
  `motionPropGet*` 展开；
- `MotionDispatch.h` 与 `RuntimeSupport.cpp` 增加八个共享、零初始化的
  `emoteController*Hint_guess` 槽；
- edge/node/pair/row 全部恢复成独立 accessor，借助临时 Variant full-expression 表达原生
  source-destroy 时点；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 新增 controller metadata accessor probe。

probe 让每次 getter 写入已知 Variant 后故意返回 `TJS_E_FAIL`，仍应得到构造后的 scalar、
edge pair `{2,-3}` 与 node row `{4,-5,6}`。测试同时锁定调用次序、flags=0、准确 hint 地址、
`objthis == receiver`，并验证三个构造函数共享 `beginFrame` hint、Blink/Eyebrow 共享
`edge`/`node` hint。

## Recovery IDB 写回

四份 recovery IDB 已完成：

- Mouth constructor 语义命名；
- 本 family indexed integer helper 的 NodeTree/Emote 共享命名；
- Android ARM64、Android ARMv7、iOS ARMv7 的 hint family 数据命名；iOS ARM64 在宽泛
  数据项内部的精确 slot 0 地址加行注释；
- 三个 constructor、indexed integer helper、hint base 的 source identity、HRESULT 和
  owner-lifetime 注释；
- Blink constructor 的 `Emote controller ncbPropAccessor metadata family` 书签；
- 三构造函数加 helper 共 16 个函数的强制重新反编译与 readback。

## 验证

完成以下验证：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer test TU syntax-only 均通过，
   只有仓库既有 `_tss` warning；
2. Web Debug 与 Wasmtime Headless Debug 最终串行构建均成功，复跑为
   `ninja: no work to do.`；
3. 两个最终 `index.wasm` 均可被 `llvm-objdump -h` 正常解析并列出完整 section table；
4. 三个 controller 源文件的旧 `motionPropGet*` 定向扫描为零，编译源码的旧
   `libkrkr2`/绝对地址扫描为零；
5. 本专题源码、测试、文档与 `plan.md` 的限定 `git diff --check` 通过。

构建过程中曾让 Web 与 Wasmtime 两个完整链接并发运行；shell wrapper 超时后子进程仍在，
此时过早再次启动 Web 链接使 `llvm-objcopy` 读到尚未写完的 `index.wasm`，产生一次瞬时
输入损坏报错。待原链接进程全部结束后串行复跑，两 preset 均成功，且两个最终 wasm 均经
`llvm-objdump` 解析。因此该记录是并发写同一输出的验证调度伪影，不是源码、链接器输入或
最终产物错误。
