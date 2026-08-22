# MotionPlayer deque #7 Clamp 条目、容器与异常边界四端恢复（2026-08-13）

> 2026-08-16 builder source-identity 更新：本文的 entry ABI、append commit、字段顺序和
> no-rollback结论保持不变；builder getter现已由 fresh 四体恢复为 copied-input root accessor、
> retained outer metadata source+accessor和六个 typed named reads。enabled/type复用 Engine-wide
> slots，var_lr/var_ud由 Bust/Chain/Clamp共享，min/max还与 EmotePlayer HM5 range Dictionary setter
> 共享。完整地址、owner栈、UTF-16复核和回归见
> `motionplayer_clamp_builder_ncb_accessor_shared_hint_four_binary_2026-08-16.md`。

## 1. 范围与结论

本文只闭合 `EmoteEngine` 的 deque #7 `clampControl` 容器纵切面：builder 的追加
顺序、element 自然 ABI、libstdc++/libc++ 的 block 公式、默认构造、异常后状态和
析构。clamp 的运行时归一化、二维 remap、mirror 与 Player bind 数据流仍见
`analysis/motionplayer_clamp_mirror_four_binary_2026-08-11.md`。

本轮证据全部重新来自 `reference/binaries/` 的四份当前参考，不沿用旧
`libkrkr2.so` 注释。四端共同证明本地声明顺序已经正确：

```cpp
struct EmoteClampControlEntry_Deque7 {
    int type = 0;
    double minValue = 0.0;
    double maxValue = 0.0;
    ttstr varLr;
    ttstr varUd;
};
```

它没有 controller 或其他 owning pointer。默认追加先令整个 native entry 处于全零
状态，builder 再按 `type -> var_lr -> var_ud -> min -> max` 写入。追加成功后没有
`pop_back` 异常回滚；任何后续 property getter、字符串构造或字符串赋值异常都会使
这个全零或部分完成的 entry 继续留在 Engine 成员中。最终销毁时只需按逆成员顺序
释放 `varUd`、`varLr`。

## 2. 四端地址映射

| 源码角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| clamp builder | `0x66C23C` | `0x55892C` | `0x1001AB0A8` | `0x1AA760` |
| default emplace | builder 内联 | boundary helper `0x567FD0`，普通路径内联 | `0x1001AB440` | `0x1AAB40` |
| element range destruction / clear | `0x680BD4` | `0x563324`，连续子范围 `0x563398` | `0x1001B777C` | `0x1B7178` |
| deque 完整析构 | `0x681998` | `0x563DEC` | `0x1001B899C` | `0x1B805C` |
| 独立 builder EH cleanup | landing blocks 内联于 builder | landing blocks 紧随 builder body | `0x1001AB384` | `0x1AAA42` |

Android ARM64 的默认追加完全展开在 builder 中；Android ARMv7 只在 block 边界
调用独立 helper。iOS 两端则都保留完整的 libc++ default-emplace helper。这个差异
只来自 STL 实现和优化器，不代表不同的源级 API。

## 3. builder 数据流

四端可归一成同一段源级伪代码：

```cpp
for (int metadataIndex = 0; metadataIndex < count(clampControl);
     ++metadataIndex) {
    Variant elem = clampControl[metadataIndex];
    if (!getBool(elem, L"enabled"))
        continue;

    clampDeque.emplace_back();
    ClampEntry &back = clampDeque.back();
    back.type = getInt(elem, L"type", 0);
    back.varLr = getString(elem, L"var_lr");
    back.varUd = getString(elem, L"var_ud");
    back.minValue = getDouble(elem, L"min");
    back.maxValue = getDouble(elem, L"max");
}
```

共同边界如下：

- `enabled` gate 位于追加之前；disabled metadata 不产生占位 entry；
- builder 不在入口清空旧 deque，所以重复调用会继续追加；
- 原 metadata index 不写入 entry；
- builder 不注册 HM6，也没有 controller allocation；
- `type` getter 使用默认值 `0`；四端的 `min` / `max` 路径没有在 builder 中额外
  排序、交换、夹取或 `min <= max` 校验；
- 五个字段严格按上述顺序读取和写入，不能为了“布局顺序”把两个 double 提前读取；
- runtime 使用的声明布局是 `type, min, max, varLr, varUd`，builder 的写入顺序却是
  `type, varLr, varUd, min, max`。二者不是矛盾：前者决定 ABI，后者决定可观察的
  property 访问和异常部分状态。

## 4. element 自然 ABI

### 4.1 64 位共同布局

Android ARM64 与 iOS ARM64 的 entry 都为 40 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 4 | `int type` |
| `+4` | 4 | 对齐 padding |
| `+8` | 8 | `double minValue` |
| `+16` | 8 | `double maxValue` |
| `+24` | 8 | `ttstr varLr` |
| `+32` | 8 | `ttstr varUd` |

### 4.2 Android ARMv7

Android ARMv7 遵循该目标的 AAPCS/EABI double 对齐，entry 为 32 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 4 | `int type` |
| `+4` | 4 | 对齐 padding |
| `+8` | 8 | `double minValue` |
| `+16` | 8 | `double maxValue` |
| `+24` | 4 | `ttstr varLr` |
| `+28` | 4 | `ttstr varUd` |

### 4.3 iOS ARMv7

iOS ARMv7 在本参考 ABI 中只按 4 字节对齐 double，entry 因而为 28 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 4 | `int type` |
| `+4` | 8 | `double minValue` |
| `+12` | 8 | `double maxValue` |
| `+20` | 4 | `ttstr varLr` |
| `+24` | 4 | `ttstr varUd` |

两个 ARM32 大小不同是 ABI 对齐结果，不是 `#ifdef` 字段。portable 源码应继续保留
自然成员声明，不能插入固定 padding 或硬编码 32/28 字节大小。

## 5. default emplace 与 deque block ABI

| 参考 | STL | entry 大小 | block allocation | 每 block entry 数 |
| --- | --- | ---: | ---: | ---: |
| Android ARM64 | libstdc++ | 40 | `0x1E0` / 480 | 12 |
| Android ARMv7 | libstdc++ | 32 | `0x200` / 512 | 16 |
| iOS ARM64 | libc++ | 40 | `0xFF0` / 4080 | 102 |
| iOS ARMv7 | libc++ | 28 | `0xFF8` / 4088 | 146 |

### 5.1 Android libstdc++

Android ARM64 普通路径以两个 16-byte zero store 和一个 8-byte zero store 覆盖完整
40 字节；边界路径申请 480 字节新 block，同时在旧 `finish.cur` 上做相同的全记录
清零，再把 finish cursor 切到新 block。

Android ARMv7 普通路径调用 `__aeabi_memclr8(end.cur, 0x20)`。边界 helper 先在需要
时扩容 deque map，再申请 512 字节 block，把它登记到下一个 map slot；随后仍对旧
`finish.cur` 清零 32 字节，最后才把 finish node/first/last/cur 切换到新 block。换言之，
旧 block 最后的 current slot 是本次新增 entry，新 block 提供新的尾后 sentinel。

这两端都符合 libstdc++ deque “在旧 finish cursor 构造元素，再将 finish 移到下一
block”的边界策略。

### 5.2 iOS libc++

iOS ARM64 helper 根据 size 定位 `102` 项 block 中的目标槽，必要时先调用 map/block
grow helper；随后以 `STR/STP` 清零全部 40 字节，再将 size 加一。

iOS ARMv7 helper 以 `146` 为每 block 项数。它将 Q 寄存器置零，在目标处做两个相距
12 字节的 16-byte vector store；两个写区间分别覆盖 `0..15` 与 `12..27`，虽有 4 字节
重叠，联合覆盖恰好是完整 28-byte entry。size 只在清零之后递增。

四端因此都支持源级 `deque.emplace_back()` / value-initialized default entry；不应
把某一端的 map header、block 常数或 strength-reduced 除法复制进 portable C++。

## 6. 异常路径和部分初始化状态

四端 builder 都在五个字段读取之前完成 deque size/finish 更新。检查 Android 内联
landing blocks、Android ARMv7 紧随 body 的 landing 区，以及 iOS 两个独立 cleanup
helper 后，共同结果是：

- cleanup 只析构当时仍存活的栈上 Variant、dispatch owner、property getter
  temporary 和临时 `ttstr`；
- cleanup 不调用 deque #7 的 pop、clear、range destructor 或完整 destructor；
- 没有 catch-and-continue；局部清理后继续 native unwind；
- 已追加 entry 仍由 Engine 成员拥有，并在以后 reset/正常析构时释放其字符串。

因此异常点与稳定的成员状态为：

| 抛出阶段 | 已追加 entry 中可保留的状态 |
| --- | --- |
| default emplace 自身失败 | size 尚未成功增加时没有 entry；具体 STL allocation 回滚由对应实现负责 |
| `type` getter | 完整零 entry；getter 正常返回后才立即写入 `type` |
| `var_lr` getter/转换/赋值 | `type` 已写；`varLr` 仍空或处于该赋值路径已经形成的状态；其余为零/空 |
| `var_ud` getter/转换/赋值 | `type`、完成的 `varLr` 保留；`varUd` 为空或部分完成；两个 double 仍为零 |
| `min` getter | `type` 和两个字符串已完成；`minValue`、`maxValue` 仍为零 |
| `max` getter | `type`、两个字符串和 `minValue` 已完成；`maxValue` 仍为零 |

primitive store 本身不抛异常，所以对 `type/min/max` 更精确的边界是“getter 返回后
立即完成 store”。字符串内部引用计数操作的具体抛出能力受 native `ttstr` 实现限制；
恢复源码必须至少保留赋值顺序和“无 entry rollback”这一可观察拓扑。

## 7. 析构与容器生命周期

四端 range/clear body 对每项都执行：

```text
release entry.varUd
release entry.varLr
advance sizeof(ClampEntry)
```

这正是声明顺序 `varLr, varUd` 的逆成员析构。`type/min/max` 是平凡成员，没有析构；
entry 中也没有 controller pointer，因此不存在 delete、unique_ptr、borrow 或 controller
ctor-failure 问题。

完整 deque destructor 在 element destruction 后再释放 block 与 map：Android 使用
libstdc++ iterator-range helper 后释放全部 block/map；iOS clear helper 先将 size 归零并
裁剪 spare blocks，完整 destructor 再释放余下 block 和 libc++ map。最终源级语义均是
`std::deque<EmoteClampControlEntry_Deque7>` 的正常成员析构。

Engine 的声明逆序中，deque #7 在 #8 transition 之后、#6 mouth 之前析构。Clamp entry
不借用相邻 controller，故这里没有类似 selector (#9) 借用 transition (#8) 的寿命约束。

## 8. 本地恢复结论

本地 `EmoteEngine.h/.cpp` 在本轮前已经具有正确的字段声明和 builder 语句顺序：

- 自然布局自动得到四端各自的 `40 / 32 / 28` 字节 ABI；
- `emplace_back()` 先建立零/空 entry；
- 五个 property 按 native 顺序赋值；
- 没有 builder-local clear、HM6 注册或 post-emplace rollback；
- 默认成员析构按 `varUd -> varLr` 释放。

所以本纵切面不需要为了追逐反编译器形状而改动业务代码。恢复动作是补全四端容器
证据、纠正“只知道 entry 大小但未闭合 block/EH”的文档缺口，并把 helper 语义写回
四份 recovery IDB。

## 9. IDB 写回与验证

四份 IDB 已完成：

- 命名可独立识别的 default-emplace、range destruction 与 iOS EH cleanup helper；
- 在四个 builder 记录全记录清零、五字段写入顺序、无 HM6/clear/pop 和部分 entry
  保留边界；
- 在 range/clear 与完整 deque destructor 记录精确字段偏移、逆析构顺序、block 大小
  和 block/map 释放阶段；
- 四份 recovery IDB 均已原位保存成功。

本轮没有修改可编译业务语句；最近一次 deque #6 语义迁移之后的 Web 完整构建与
motionplayer 测试 TU 语法检查已经通过。本轮文档/计划修改另以 `git diff --check`
检查；不把未执行的 Catch2 runtime 结果表述为已通过。

## 10. 2026-08-15 当前四参考复核与源码迁移

为排除旧 `libkrkr2.so` 注释继续影响 portable 源码，本节只使用
`reference/binaries/` 当前四份参考重新反编译 builder 本体。入口和本轮直接复核点为：

| 参考 | builder | Count | enabled gate | default append | 首字段写入 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x66C23C` | `0x66C2D0` | `0x66C39C` | inline `0x66C3B8`; block grow `0x66C414` | `0x66C450` |
| Android ARMv7 | `0x55892C` | `0x558962` | `0x5589D0` | fast zero `0x5589E4`; boundary call `0x5589F8` | `0x558A2A` |
| iOS ARM64 | `0x1001AB0A8` | `0x1001AB100` | `0x1001AB184` | helper call `0x1001AB190` | `0x1001AB1F4` |
| iOS ARMv7 | `0x1AA760` | `0x1AA7E0` | `0x1AA850` | helper call `0x1AA85A` | `0x1AA8C2` |

四端再次闭合出同一语义：Count 只读取一次；disabled 行不创建 placeholder，也不在
其他容器保留原始 index；成功 default append 是提交点；随后严格按
`type -> var_lr -> var_ud -> min -> max` 读取和写入。字段声明顺序仍是
`type, min, max, varLr, varUd`，因此不能从内存偏移误推源读取顺序。builder 不 clear、
不校验或交换 min/max、不拒绝空变量名、不向 controller-ref map 发布两个变量名，也
不在后续 getter/conversion 失败时回滚已追加项。

portable 源码本轮只做证据驱动的语义迁移：循环与记录局部名改为
`metadataCount / metadataIndex / metadata / entry`，并在声明和实现中明确 Count
snapshot、disabled 无占位、no-clear/no-registration 以及 post-emplace no-rollback。
没有为 32-bit 强加统一 size assert：Android ARMv7 的 32 字节和 iOS ARMv7 的 28
字节仍由各自 ABI 的 `double` 对齐自然产生。

回归用例增加并覆盖：

- builder 之前已有 deque entry 时不清空；第二次调用继续追加；
- disabled 且缺少其余字段的行完全跳过；
- 缺失 `type` 得到零，两个空变量名保留；
- `min=5, max=-5` 原样保存，不做范围归一化；
- `var_lr/var_ud` 不创建 controller-ref，已有 sentinel ref 保持不变。

四份 recovery IDB 已写回语义局部名、Count/commit/首字段注释和 builder bookmark，
并再次原位保存成功。验证结果：

- motionplayer Catch2 测试翻译单元的 Emscripten 语法检查通过（仅既有 `_tss`
  deprecated warning）；
- `cmake --build --preset "Web Debug Build"` 完整编译和链接通过；
- 没有把未运行的 Catch2 runtime 断言表述为运行通过。
