# Motion.EmotePlayer.variableKeys 的容器身份与生命周期（四参考二进制，2026-08-13）

## 1. 结论

`Motion.EmotePlayer.variableKeys` 不是每次读取都新建的临时 Array，也不是始终存在的
实时视图。四份当前参考二进制共同实现为：getter 从 `EmoteEngine` 的第一个
variable Variant 字段复制构造返回值；Variant CopyRef 增加 Dispatch 所有权，但不
克隆 Array。因此，同一发布周期内的 getter 结果与 Engine backing field 指向同一
Array；metadata reset 或 selector 同步替换 backing field 后，已经返回给脚本的旧
Variant 仍拥有旧 Array，并可独立活过 Engine 析构。

另一个重要边界是：Engine 构造函数只把三个 Variant 默认构造成 `Void`。在第一次
metadata reset 之前，`variableKeys` 返回 `Void`，不是空 Array。

## 2. 属性注册与 getter

| 目标 | member registrar | `variableKeys` 名称引用 | getter | backing field |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x67CEA8` | `0x67E1DC` | `0x67F380` | `+1208` |
| Android armv7 | `0x5612E8` | `0x5617B6` | `0x562122` | `+640` |
| iOS arm64 | `0x1001B5130` | `0x1001B5858` | `0x1001B621C` | `+840` |
| iOS armv7 | `0x1B4DE0` | `0x1B547A` | `0x1B5FFA` | `+452` |

四个 getter 分别把上述字段地址传入平台对应的 `tTJSVariant` copy-constructor/
CopyRef helper。64 位返回对象使用隐藏返回寄存器，32 位使用隐藏返回首参，因此
反编译形状不同；源级语义均是：

```text
return tTJSVariant(engine.variableLabelsBase)
```

注册项只有 getter，setter 槽为 null；脚本侧 `PropSet(variableKeys, ...)` 返回
`TJS_E_ACCESSDENYED`。四份 IDB 已将 getter 命名为
`EmotePlayer_getVariableKeys_guess`。

## 3. 三个 Variant 的 ABI 布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| published/base labels | `+1208` | `+640` | `+840` | `+452` |
| current labels | `+1228` | `+652` | `+860` | `+464` |
| frame-list Dictionary | `+1248` | `+664` | `+880` | `+476` |

当前参考中的 `tTJSVariant` 宽度在 64 位二进制为 20 字节，在 32 位二进制为
12 字节。三字段按声明正序连续排列；正常析构按
`frame lists -> current labels -> published/base labels` 逆序释放。

## 4. 构造期：三个值均为 Void

Engine 构造入口：

| 目标 | constructor | 三个 Variant type-word 清零 |
| --- | ---: | --- |
| Android arm64 | `0x67B76C` | `0x67BB44/+0x4C8`、`0x67BB48/+0x4DC`、`0x67BB4C/+0x4F0` |
| Android armv7 | `0x560948` | `0x560B9E/+0x288`、`0x560BA2/+0x294`、`0x560BA6/+0x2A0` |
| iOS arm64 | `0x1001B7FB0` | `0x1001B8154/+0x358`、`0x1001B815C/+0x36C`、`0x1001B8164/+0x380` |
| iOS armv7 | `0x1B7788` | `0x1B79BE/+0x1CC`、`0x1B79CC/+0x1D8`、`0x1B79E0` 起始的 zero vector store（`+0x1E4`） |

这些 store 是默认 Variant 的 type word 清零；构造体中没有 TJS Array 或
Dictionary factory 调用。由此得到可观察边界：新建 `EmotePlayer` 的
`variableKeys` 属性为 `Void`。

## 5. metadata reset：建立第一次别名

| 目标 | reset containers | metadata reset | 调用点 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x666B78` | `0x666D08` | `0x667098` |
| Android armv7 | `0x555A04` | `0x555AD8` | `0x555B48` |
| iOS arm64 | `0x1001A66AC` | `0x1001A67BC` | `0x1001A6840` |
| iOS armv7 | `0x1A5D88` | `0x1A5F4C` | `0x1A5FBC` |

四端共同顺序为：

```text
temporary = new TJS Array
published/base = CopyRef(temporary)
clear temporary
current labels = CopyRef(published/base)   // 同一 Array Dispatch
temporary = new TJS Dictionary
frame lists = CopyRef(temporary)
clear temporary
clear HM4 instant-label set
clear HM5 variable-range map
```

`resetVariableContainers` 的唯一普通 caller 是 metadata reset；metadata reset 的
唯一普通 caller 又是 `applyMetadata`：Android arm64 `0x67A8B0 -> 0x67A8D8`、
Android armv7 `0x560020 -> 0x56003C`、iOS arm64
`0x1001B4468 -> 0x1001B4480`、iOS armv7 `0x1B3F58 -> 0x1B3F78`。

因此 reset 刚结束时 published/base 与 current 是两个 owning Variant，持有同一
Array Dispatch。通过 getter 修改 Array 会同时被 current-label 路径观察到。

## 6. variable-list build 与 selector publish

variable-list builder：

| 目标 | builder | current-label field | frame-list field |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x667910` | `+1228` | `+1248` |
| Android armv7 | `0x555FC0` | `+652` | `+664` |
| iOS arm64 | `0x1001A73C0` | `+860` | `+880` |
| iOS armv7 | `0x1A693C` | `+464` | `+476` |

builder 开头新建 Array 并只替换 current-label Variant，随后填充 frame-list
Dictionary；它不替换 published/base Variant。因此 reset 时建立的 Array 别名在
这里分叉。

selector 同步入口分别为 Android arm64 `0x66E0FC`、Android armv7 `0x559A8C`、
iOS arm64 `0x1001AC8A4`、iOS armv7 `0x1AC0D0`。同步开头严格执行：

```text
newBase = new TJS Array
published/base = CopyRef(newBase)
newBase.Items = currentLabels.Items
dirty = true
...selector controller synchronization...
```

这是 Array `Items` 容器的内容复制，不是 Variant/Dispatch 复用。因此同步后：

- 新 `variableKeys` getter 共享新 published Array；
- 新 published Array 与 current-label Array 对象身份不同，但内容在复制瞬间相同；
- 旧 getter Variant 继续持有旧 published Array，内容不会被新同步回写；
- 每次同步都更换 published Array 身份，即使 selector 状态没有实质变化。

`applyMetadata` 在构建全部控制器后调用该同步：Android arm64 `0x67AE6C`、Android
armv7 对应 `0x5603xx` 尾段、iOS arm64 对应 `0x1001B4Axx` 尾段、iOS armv7
`0x1B43C6`。公开 `selectorEnabled` setter 也无条件调用同步。

## 7. 析构与外部持有

正常 Engine 析构入口为 Android arm64 `0x67C898`、Android armv7 `0x5610E8`、
iOS arm64 `0x1001B8B4C`、iOS armv7 `0x1B814E`。三 Variant 逆声明释放为：

```text
destroy frame-list Dictionary Variant
destroy current-label Array Variant
destroy published/base Array Variant
```

当 reset 后两 Array Variant 仍别名时，current 先减引用，base 后减引用；getter
返回值还会额外持有一份 Dispatch 引用。若脚本保存了 getter 结果，则 Engine
析构只释放 Engine 自己的引用，脚本 Array 仍有效。这是普通引用计数所有权，不应
在 portable 实现中改成裸借用、深拷贝或 Engine 析构时强制失效。

## 8. 本地恢复与回归覆盖

本轮实现逻辑原本已符合四端；修正的是旧的单 ABI/“总是 Array”式注释，并新增
完整回归。测试覆盖：

- 构造后 C++ getter 与 NCB 属性均返回 `Void`；
- NCB 属性写入返回 `TJS_E_ACCESSDENYED`；
- reset 后 base/current 共享同一 Dispatch；
- 两次 getter 返回同一 backing Array，任一别名修改均可互相观察；
- current-label 替换及 selector 同步后，published Array 发生换代并复制内容；
- 旧 published getter 保留旧内容；
- 外部 getter Variant 在 Engine 析构后仍可读取。

四份 IDB 同时补入 getter 名称及 registrar、constructor、reset、builder、sync、
destructor 的数据流/生命周期注释。

## 9. 验证

- `cmake --build --preset "Web Debug Build"`：受影响 motionplayer 对象、静态库及
  最终 `index.html/index.wasm` 链接通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target
  krkr2_wasmtime_guest`：首次前台等待达到 64 秒超时，但底层 CMake/Ninja 继续完成；
  等待进程退出后立即重跑得到 `ninja: no work to do.`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用当前 Web Debug
  `EmoteEngine.cpp` 的真实 Emscripten 定义、include、ABI 参数，并加入既有
  `out/syntax-check` Catch2/test config，执行 `-fsyntax-only` 成功；唯一诊断为
  仓库既有 `_tss` literal-operator 弃用警告；
- `git diff --check`：通过；仅有工作区换行符提示；
- 四份改进后的 IDB 已原位保存。

当前工作区没有直接可运行的 `motionplayer-dll` Catch2 executable，因此这里准确
表述为完整测试翻译单元编译通过，不把 `-fsyntax-only` 冒充成运行时执行。
