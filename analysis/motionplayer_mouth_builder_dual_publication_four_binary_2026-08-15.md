# Mouth metadata builder：双标签发布、稀疏索引与 deque 原始 ownership 边界

日期：2026-08-15

本轮没有沿用旧 `libkrkr2.so` 注释作结论，而是在四个当前参考恢复库中分别重新反编译 `EmoteEngine_buildMouthControl_guess`，并继续进入三个未内联的 Mouth deque raw-emplace helper。四份实现的 source-level 顺序一致；差异只来自 Android libstdc++ deque 与 iOS libc++ deque 的 ABI/块布局。

## 1. 四参考函数与 controller allocation

| 参考 | builder | 大小 | `EmoteMouthController` allocation |
|---|---:|---:|---:|
| Android ARM64 | `0x66A39C` | `0x508` | `0x70` |
| Android ARMv7 | `0x557894` | `0x21A` | `0x48` |
| iOS ARM64 | `0x1001A988C` | `0x294` | `0x50` |
| iOS ARMv7 | `0x1A8ED0` | `0x2A8` | `0x38` |

Controller 大小的 ABI 差异来自它起始处的裸 `std::deque<12-byte keyframe>` 实现，不改变本轮 builder 的记录语义。

## 2. 恢复出的严格操作顺序

四份代码都执行下列顺序：

1. 在循环前只读取一次 metadata count。
2. 以原始 metadata 下标读取 element。
3. `enabled == false` 时直接进入下一个原始下标；不构造 controller，也不压缩下标。
4. 为 enabled element 分配并构造 `EmoteMouthController`。
5. 向 deque #6 原始追加 `{controller, null, null}`。这里 controller 以 raw pointer 传入，helper 不清空 source raw slot。
6. 从 metadata 读取 `label`，引用计数复制到刚追加记录的第二个 pointer-width 字段。
7. 再读取 `talkLabel`，复制到第三个字段。
8. 对 `label` 做 get-or-insert，写 `{type=6,index=原始 metadata 下标}`。
9. 对 `talkLabel` 做第二次 get-or-insert，写同一 `{6,原始 metadata 下标}`。
10. 增加原始 metadata 下标。

关键指令位置如下：

| 阶段 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| count snapshot | `0x66A434` | `0x5578CC` | `0x1001A98E4` | `0x1A8F4E` |
| enabled test | `0x66A508` | `0x557944` | `0x1001A996C` | `0x1A8FC4` |
| controller allocate/ctor | `0x66A514..0x66A520` | `0x55794E..0x557954` | `0x1001A9978..0x1001A9984` | `0x1A8FD2..0x1A8FDC` |
| raw deque append | `0x66A528..0x66A5C8` | `0x557960..0x55799C` | `0x1001A9994` | `0x1A8FEC` |
| label getter/slot write | `0x66A600..0x66A64C` | `0x5579B4..0x5579E2` | `0x1001A99DC..0x1001A9A1C` | `0x1A9042..0x1A9090` |
| talkLabel getter/slot write | `0x66A688..0x66A6D4` | `0x5579FC..0x557A2C` | `0x1001A9A48..0x1001A9A84` | `0x1A90BC..0x1A90F2` |
| label map publication | `0x66A6EC..0x66A6F4` | `0x557A3A..0x557A40` | `0x1001A9A98..0x1001A9AA0` | `0x1A9102..0x1A910E` |
| talkLabel map publication | `0x66A700..0x66A70C` | `0x557A48..0x557A4C` | `0x1001A9AAC..0x1001A9AB0` | `0x1A9118..0x1A9122` |
| metadata index increment/test | `0x66A73C` | `0x557A74..0x557A7A` | `0x1001A9AD8..0x1001A9AE0` | `0x1A9142..0x1A9146` |

## 3. `talkLabel` 的宽字符串证据

iOS 两份数据库把同一 UTF-16LE 数据的首字节重叠识别成 ASCII `"t"`。因此不能把反编译窗口中的一字符字符串当成属性名。用宽字符串字节序列

`74 00 61 00 6C 00 6B 00 4C 00 61 00 62 00 65 00 6C 00`

在四份参考中各得到唯一命中：

| 参考 | `talkLabel` UTF-16LE |
|---|---:|
| Android ARM64 | `0x14D399C` |
| Android ARMv7 | `0x557B48` |
| iOS ARM64 | `0x10195FDB2` |
| iOS ARMv7 | `0x1752116` |

所以两个 iOS call site 仍然是完整的 `talkLabel` 查询，不存在一字符 `t` 属性分支。

## 4. deque #6 的真实元素与内部块边界

元素是恰好三个 pointer-width 字段：

```cpp
struct MouthEntry {
    unique_ptr<EmoteMouthController> controller;
    ttstr label;
    ttstr talkLabel;
};
```

因此它在 64-bit 参考中为 24 字节，在 32-bit 参考中为 12 字节。raw-emplace 不是从一个完整临时元素 move：它只读取 caller raw slot 中的 controller pointer，并在 destination 原地把两个字符串 slot 清零。

| ABI | helper/内联位置 | 每块元素数 | 有效 element bytes |
|---|---:|---:|---:|
| Android ARM64 libstdc++ | builder 内联 `0x66A528..0x66A5C8` | 21 | `21 * 24 = 504 (0x1F8)` |
| Android ARMv7 libstdc++ | `0x5677BA` (`0x4E`) | 42 | `42 * 12 = 504 (0x1F8)` |
| iOS ARM64 libc++ | `0x1001A9BEC` (`0xB0`) | 170 (`0xAA`) | `170 * 24 = 4080` |
| iOS ARMv7 libc++ | `0x1A9280` (`0x94`) | 341 (`0x155`) | `341 * 12 = 4092` |

Android 的 504-byte block 是 512-byte deque buffer 规则容纳完整元素后的结果；iOS 的除数/余数寻址明确使用 170/341 个元素。不能把这些不同的 header、map 或 block 常量误写成 motionplayer 自己的手工环形队列。

记录析构按 C++ 逆成员顺序释放 `talkLabel`、`label`，最后 delete controller。engine deque 析构因此完整承担成功 append 后的 ownership。

## 5. 覆盖与异常/边界行为

- disabled element 不进入 deque，但循环下标仍增加。因此 map 中的 `index` 是稀疏 metadata index，不是 deque index。
- `label` 和 `talkLabel` 都不做非空检查。空字符串会成为合法 map key。
- 同一 element 的两个 key 相等时，第二次 `talkLabel` publication 再写同一 mapped object，结果仍是该 element 的 `{6,index}`。
- 不同 element 复用任一 key 时，后处理的 publication 覆盖旧 mapped value；旧 controller record 仍保留在 deque 中。
- 第二个 key 可以与前一个 element 的第一个或第二个 key 交叉碰撞，没有别名表或专门冲突分支。
- controller 构造成功后，caller 把 raw pointer 交给 deque append；source raw slot 没有 RAII owner。若 deque growth allocation 在 destination element 构造前失败，原实现会泄漏该 controller。Web 源码现在直接保留这一 raw-pointer 边界，不再制造一个反编译中不存在的临时 `unique_ptr`。
- append 成功以后，deque 已拥有 controller。后续 label/talkLabel 或 map 操作失败时，engine 仍会在销毁 deque 时释放 controller，但可以留下已经追加的 element、已写的第一个字符串或已经发布的第一个 map key；操作不是事务性的。

## 6. 源码、测试与恢复库落点

源码修改：

- `EmoteEngine::buildMouthControl_guess` 的 `v5/elem/ctl/back/ref1/ref2` 已迁移为 `metadataIndex/element/controller/entry/labelRef/talkLabelRef`。
- 去掉临时 `unique_ptr` 再立即 `release()` 的伪结构，改成参考生成代码对应的 raw allocation -> destination ownership 顺序。
- `EmoteMouthControlEntry_Deque6` 增加三 pointer-width 字段的静态断言。
- header 注释明确记录双 key 的顺序、稀疏 index、重复/相等/空 key 与四 ABI controller allocation size。

新增单元测试覆盖：

- index 0 disabled、后续 enabled 从 index 1 开始，验证稀疏 index；
- `talkLabel` 与后一 element 的 `label` 交叉碰撞，验证后写覆盖；
- 同一 element 的 `label == talkLabel`；
- 两个 key 都为空；
- disabled element 的两个 key 均不发布；
- 所有 enabled controller owner 仍按顺序保留在 deque。

四份 IDB 已把核心 locals 迁移为 `metadataCount/metadataIndex/controller/labelSlot/talkLabelSlot/labelRef/talkLabelRef`，并在 builder、第二属性读取与第二次 map publication 添加注释和书签。

当前验证：定向 `git diff --check` 通过；Emscripten 单元测试 TU 语法检查通过；最终 `Web Debug Build` 完整编译和链接通过。构建只报告仓库已有的 `_tss`、imagepacker `nodiscard` 与 Emscripten pthread/JSPI 类警告。

## 7. 2026-08-16 accessor/source owner 补完

本页双 label publication、稀疏 metadata index、raw owner leak 与 partial-commit 边界均保持不变。
新的四端 fresh 复核补齐了 builder 的源码类型：copied `mouthControl` 构造循环外 root
`ncbPropAccessor`；每轮 indexed `GetValue<tTJSVariant>` 的 source element 独立存活，第二份
Variant copy 只用于构造 element accessor。controller ctor 接收前一份 source element，label 与
talkLabel 则由 element accessor 读取。公共尾部先释放 accessor、再析构 source，最后才释放 root。
三个 builder 共享 `enabled`/`label` hint，`talkLabel` 使用 Mouth-only hint。完整证据见
`analysis/motionplayer_leaf_controller_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
