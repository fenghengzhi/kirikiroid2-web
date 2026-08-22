# MotionPlayer Eye/Eyebrow/Mouth builder `ncbPropAccessor` 源码身份与元素 owner 生命周期（四参考，2026-08-16）

## 结论

对四份当前参考二进制重新反编译后，`EmoteEngine` 的 Eye、Eyebrow、Mouth 三个 metadata
builder 可以恢复为同一个源码骨架：

1. 输入 control Variant 先复制一份，再构造循环外唯一的 root `ncbPropAccessor`；
2. root accessor 通过 `GetArrayCount()` 对 retained dispatch 做一次无 hint 的 `count`
   `PropGet`；
3. 每轮通过 root accessor 的 indexed `GetValue<tTJSVariant>` 取得一个独立的 source
   element Variant；
4. source element 再复制一次，用来构造 iteration-local element `ncbPropAccessor`；该构造
   临时 Variant 随即析构，但原 source element 继续存活；
5. element accessor 读取 `enabled`，enabled 时把仍然存活的 source element Variant 传给
   对应 controller constructor；
6. deque raw-owner append 后，仍用同一个 element accessor 读取 `label`；Mouth 再读取
   `talkLabel`；
7. 每轮公共尾部先释放 element accessor，再析构 source element Variant；disabled 分支也走
   同一清理顺序；
8. 循环完成或 count 为零时，最后才释放 root accessor。

这不等价于只保留裸 `tTJSVariant` 并反复调用 plugin-local `motionPropGet*` helper。区别包括
真实 polymorphic accessor owner、named/indexed typed template、全局 member-hint identity、忽略
getter HRESULT 的边界，以及 element source Variant 跨 controller 构造和 publication 的作用域。

## Builder 映射

| builder | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Eye | `0x669B5C` | `0x55739C` | `0x1001A91F4` | `0x1A8800` |
| Eyebrow | `0x669F7C` | `0x557618` | `0x1001A9540` | `0x1A8B68` |
| Mouth | `0x66A39C` | `0x557894` | `0x1001A988C` | `0x1A8ED0` |

四库中的函数名都保留 `_guess`，因为参考产物已 stripped；这些名称是经过四端交叉验证的
语义恢复名，不是原始符号泄露。

## Root accessor 构造与 count snapshot

每个函数开头都能分开看到 input Variant copy、`ncbPropAccessor` vptr/dispatch 建立，以及 copy
临时 Variant 析构。对应位置如下：

| builder | Android ARM64 copy/vptr/temp dtor | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | --- | --- | --- | --- |
| Eye | `0x669B90 / 0x669BA8 / 0x669BE4` | `0x5573B4 / 0x5573BE / 0x5573CA` | `0x1001A921C / 0x1001A922C / 0x1001A9240` | `0x1A8822 / 0x1A8844 / 0x1A886E` |
| Eyebrow | `0x669FB0 / 0x669FC8 / 0x66A004` | `0x557630 / 0x55763A / 0x557646` | `0x1001A9568 / 0x1001A9578 / 0x1001A958C` | `0x1A8B8A / 0x1A8BAC / 0x1A8BD6` |
| Mouth | `0x66A3D0 / 0x66A3E8 / 0x66A428` | `0x5578AC / 0x5578B6 / 0x5578C2` | `0x1001A98B4 / 0x1001A98C4 / 0x1001A98D8` | `0x1A8EF2 / 0x1A8F16 / 0x1A8F40` |

随后三个函数都只调用一次 array-count helper。该 helper 对 accessor `_obj` 执行
`PropGet(0,"count",nullptr,&temporary,_obj)`，不检查 HRESULT，随后把 temporary 转成 integer
并析构。四端 helper 映射为：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x56CA74` | `0x4BEB84` | `0x1000F30F4` | `0xEF8B4` |

因此失败 getter 只要写入 count Variant，循环仍按写入值执行；这里不是 `GetCount()`，也没有
`TJS_SUCCEEDED` gate。

## Indexed source Variant 与第二份 accessor-copy

Android ARMv7、iOS ARM64、iOS ARMv7 都保留独立 indexed-Variant helper；Android ARM64 在这
三个 caller 中把同一模板展开。helper 对 root accessor `_obj` 执行
`PropGetByNum(0,index,&temporary,_obj)`，忽略 HRESULT，把 temporary copy 成返回
`tTJSVariant`，再析构 temporary。

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| caller 内联 | `0x5334E0` | `0x1000691F8` | `0xED9A8` |

最清楚的四端物化序列如下。表中 `indexed` 是 source element 的产生点，`copy/accessor/temp
dtor` 是第二份 Variant copy、element accessor 建立和构造临时销毁：

| builder | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | --- | --- | --- | --- |
| Eye | `0x669C30 / 0x669C4C..0x669C68 / 0x669CA8` | `0x557418 / 0x557420..0x55742E / 0x557432` | `0x1001A9290 / 0x1001A929C..0x1001A92AC / 0x1001A92B4` | `0x1A88A6 / 0x1A88B2..0x1A88C4 / 0x1A88C8` |
| Eyebrow | `0x66A050 / 0x66A06C..0x66A088 / 0x66A0C8` | `0x557694 / 0x55769C..0x5576AA / 0x5576AE` | `0x1001A95DC / 0x1001A95E8..0x1001A95F8 / 0x1001A9600` | `0x1A8C0E / 0x1A8C1A..0x1A8C2C / 0x1A8C30` |
| Mouth | `0x66A484 / 0x66A490..0x66A4AC / 0x66A4EC` | `0x557910 / 0x557918..0x557926 / 0x55792A` | `0x1001A992C / 0x1001A9938..0x1001A9948 / 0x1001A9950` | `0x1A8F78 / 0x1A8F84..0x1A8F96 / 0x1A8F9A` |

第二份 copy 只服务于 `ncbPropAccessor(const tTJSVariant&)` 的 `AsObject()` owner acquisition。
它随即销毁；第一份 source element 则一直保留，并直接传入 `EmoteBlinkController`、
`EmoteEyebrowController` 或 `EmoteMouthController` constructor。这解释了反编译栈上同时存在的
source Variant storage 与 element accessor vptr/dispatch storage，不能把两者合并成一个裸
dispatch 临时值。

## Named getter、hint identity 与 `objthis`

三类 builder 的 `enabled` 都通过同一个 bool hint，三类 `label` 都通过同一个 string hint；
Mouth 的 `talkLabel` 有自己的 string hint：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `label` | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |
| `enabled` | `0x1AB4F20` | `0x11114B8` | `0x101B69FD0` | `0x187D9F0` |
| `talkLabel` | `0x1AB4F54` | `0x11114EC` | `0x101B6A004` | `0x187DA024`（反编译显示为 `dword_187D9DC[18]`） |

四端 xref 复核还显示：

- `label` 被 variable-list、Eye/Eyebrow/Mouth/Transition/Selector/Timeline builder 以及 state
  serialize/restore 路径复用；
- `enabled` 被 Bust/Chain/Eye/Eyebrow/Mouth/Transition/Selector/Loop/Clamp builder 复用；
- `talkLabel` 的真实 code consumer 仅为 Mouth builder。

三个 named getter 的 flags 都是 0，dispatch receiver 和 `objthis` 都是 element accessor 的同一
retained `_obj`。bool helper 映射为 `0x660AB4 / 0x552124 / 0x1000F3078 / 0xEF7F0`；named
string 在 Android ARM64 caller 内联，另外三端分别是 `0x492100 / 0x1000F18DC / 0xEDCB0`。
两类 helper 都忽略 `PropGet` HRESULT，消费 callee 已写入的 Variant；string helper先取得独立
`ttstr` owner，再析构临时 Variant。

关键 call site：

| builder/property | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Eye enabled / label | `0x669CC4 / 0x669DA4` | `0x55744A / 0x5574B6` | `0x1001A92D0 / 0x1001A9338` | `0x1A88F2 / 0x1A8956` |
| Eyebrow enabled / label | `0x66A0E4 / 0x66A1C4` | `0x5576C6 / 0x557732` | `0x1001A961C / 0x1001A9684` | `0x1A8C5A / 0x1A8CBE` |
| Mouth enabled / label / talkLabel | `0x66A508 / 0x66A600 / 0x66A688` | `0x557944 / 0x5579B4 / 0x5579FC` | `0x1001A996C / 0x1001A99DC / 0x1001A9A48` | `0x1A8FC4 / 0x1A9042 / 0x1A90BC` |

iOS 反编译器有时把重叠 UTF-16 数据显示成一字符 `"t"`；四端宽字节搜索与既有
`motionplayer_mouth_builder_dual_publication_four_binary_2026-08-15.md` 已证明完整 key 是
`talkLabel`，本轮没有把这个 IDA presentation artifact 写进 portable 源码。

## 每轮清理与 root 尾部释放

共同清理顺序由 disabled 和 enabled 两条路径汇合后的 vptr reset、virtual Release、Variant
dtor 明确给出：

| builder | Android ARM64 element/source/root | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | --- | --- | --- | --- |
| Eye | `0x669E14 / 0x669E34 / 0x669E44..0x669E5C` | `0x557512 / 0x55751E / 0x557538..0x557540` | `0x1001A9390 / 0x1001A93AC / 0x1001A93C0..0x1001A93D4` | `0x1A89B6 / 0x1A89CA / 0x1A89DC..0x1A89EA` |
| Eyebrow | `0x66A234 / 0x66A254 / 0x66A264..0x66A27C` | `0x55778E / 0x55779A / 0x5577B4..0x5577BC` | `0x1001A96DC / 0x1001A96F8 / 0x1001A970C..0x1001A9720` | `0x1A8D1E / 0x1A8D32 / 0x1A8D44..0x1A8D52` |
| Mouth | `0x66A710 / 0x66A730 / 0x66A740..0x66A758` | `0x557A64 / 0x557A70 / 0x557A8A..0x557A92` | `0x1001A9AB8 / 0x1001A9AD4 / 0x1001A9AE8..0x1001A9AFC` | `0x1A9126 / 0x1A913A / 0x1A914C..0x1A915A` |

表中每格依次为 element accessor cleanup、source element Variant dtor、root accessor cleanup。
这也解释了正确 portable declaration order：先声明 source element，再声明 element accessor，
使 C++ scope 退出时自然得到 accessor→source 的逆序销毁。基于同一 RAII 结构可推断异常展开也
会按当前 live owner 的逆声明顺序清理；本轮直接证据锁定的是正常、disabled-continue 和零 count
尾部路径。

controller raw-pointer 到 deque owner 的非事务 publication、disabled hole、duplicate/empty key
与 Mouth 双 key 顺序不在本轮改变，继续保持既有四参考专题记录的边界。

## Portable 源码与测试

`cpp/plugins/motionplayer/EmoteEngine.cpp` 本轮完成：

- 三个 builder 都由 plugin-local raw `motionPropGetCount/ByNum/Bool/String` 改为真正的 root 与
  element `ncbPropAccessor`；
- 用显式 `tTJSVariant(input)` 和 `tTJSVariant(element)` 表达两次可见的 Variant copy；
- 保留独立 `const tTJSVariant element`，把它传给 controller constructor，并让它活到迭代尾；
- 增加共享 `engineEnabledHint_guess`、既有 `engineLabelHint_guess` 的三 builder 复用，以及
  Mouth-only `engineTalkLabelHint_guess`；
- publication 顺序、sparse metadata index、raw-pointer leak boundary 与 map overwrite 行为不变。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增两层 dispatch probe：

- root `count` 与 indexed element getter 都先写有效 Variant、再返回 `TJS_E_FAIL`；
- outer probe 在 `PropGetByNum` 内立刻清除自己的 element owner，证明返回 source Variant 仍让
  element 存活；
- element 的 `enabled`、controller metadata、`label`、`talkLabel` getter 同样写值后返回失败；
- 测试逐项核对 flags=0、index 0、hint 非空、`objthis == receiver` 和精确 read order；
- Eye/Eyebrow/Mouth 的 enabled hint 指针必须相等，label hint 指针必须相等，enabled/label/
  talkLabel 三种 identity 必须互异；
- builder 返回后 source 与 element accessor 都已离开作用域，失去最后 owner 的 element dispatch
  恰好析构一次。

## Recovery IDB 写回

四份 recovery IDB 已完成：

- 三个 builder 入口写入 root accessor/count source identity 注释与 V133 书签；
- indexed getter、retained source Variant、第二份 accessor copy、controller ctor 参数 owner 加注；
- 每轮 accessor→source 与循环尾 root 清理点逐项加注；
- label/enabled/talkLabel 三个精确 hint 槽加注；iOS ARMv7 的 talkLabel slot 位于已有 data item
  内部，故把精确 `0x187DA024` 记录在 `0x1A90BC` call-site 注释中；
- 十二个 builder function 全部 force-recompile，disassembly readback 确认函数名和新增注释；
- 四库均已原位保存。

## 验证

完成以下验证：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer test TU syntax-only 均通过，只有
   仓库既有 `_tss` warning；
2. Web Debug 增量构建 `3/3` 成功；
3. Wasmtime Headless Debug 增量构建 `4/4` 成功；
4. `out/web/debug/index.wasm` 与 `out/wasmtime/debug/index.wasm` 均可由 `llvm-objdump -h`
   正常解析；
5. 三个 builder 的旧 raw getter 定向扫描为零，新增编译源码注释没有旧 `libkrkr2.so` 或参考
   绝对地址；
6. 本专题源码、测试、文档和 `plan.md` 的限定 `git diff --check` 通过。
