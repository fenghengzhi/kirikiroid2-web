# Player::isExistMotion 私有 hint、借用接收者与参数别名四参考恢复（2026-08-16）

## 1. 结论

四个参考二进制一致实现了同一个很窄、但边界可观察的脚本包装器：

1. 先构造局部路径 Variant：`motion/` + Player 的 `stealthChara` + `/` + 输入名；
2. 路径以及拼接过程中的临时字符串全部落定后，才把 Player 的 canonical
   ResourceManager Variant 严格解释为 Object；
3. 这里直接借用 canonical Variant 内的 dispatch，不复制 Variant，也不额外 `AddRef`；
4. 两个实参依次是 Player 持久 `_findMotionContextVariant` 的原址与局部路径 Variant 的原址；
5. 使用函数私有、进程期静态的 `isExistMotion` member-hint word，`flags=0`，result 非空，
   `objthis` 与 receiver 都是同一个借用 ResourceManager dispatch；
6. 普通 `FuncCall` HRESULT 完全不控制返回值；无论成功或普通失败，都对 result Variant 做
   `operator bool()`；
7. 正常尾部先销毁 result，再销毁 path。非 Object ResourceManager 在 path 已构造后抛出
   TJS conversion error；脚本调用或 bool conversion 抛异常时由落地清理销毁已构造局部对象。

这也闭合了 V163 留下的物理边界：四端共同的连续全局 hint 家族确实在 `piledCopy` 结束。
`isExistMotion` 的 backing word 只有该函数消费，但四个平台对它的物理放置并不一致；只有
iOS arm64 恰好把它紧排在 `piledCopy` 后。因此不能把 Intel64 的邻接偶然性提升为四端共同的
“第五个全局槽”。源码保留 function-local static，并只补充语义名。

## 2. 四端函数与字符串定位

精确 UTF-16LE byte pattern：

```text
69 00 73 00 45 00 78 00 69 00 73 00 74 00 4D 00
6F 00 74 00 69 00 6F 00 6E 00 00 00
```

四库各只有一个原始 byte hit。该 literal 同时被 ResourceManager/Player NCB 注册代码引用；
生产函数由 literal xref、两参数调用形状和 Player 字段访问共同筛出：

| 目标 | `Player_isExistMotion_guess` | size | UTF-16 literal |
|---|---:|---:|---:|
| Android arm64 | `0x6CDBD4` | `0x2BC` | `0x14D5A22` |
| Android armv7 | `0x5942F4` | `0xE4` | `0xD85542` |
| iOS arm64 | `0x10011F558` | `0x150` | `0x10195BED0` |
| iOS armv7 | `0x11E054` | `0x172` | `0x174E234` |

四个生产调用点分别为 `0x6CDD8C`、`0x5943A6`、`0x10011F658`、`0x11E172`。
Android AArch64/Thumb 的字符串显示曾被 IDA 错切成窄字符串前缀；raw-byte 搜索和生产 xref
消除了这个展示差异。本轮四库统一命名为 `str_isExistMotion_utf16_guess`。

## 3. 私有 member-hint storage

| 目标 | `isExistMotion` hint | `piledCopy` hint | 字节差 | 去重后 consumer |
|---|---:|---:|---:|---|
| Android arm64 | `0x1AB54B0` | `0x1AB54A4` | `0xC` | `Player_isExistMotion_guess` |
| Android armv7 | `0x111194C` | `0x1111940` | `0xC` | `Player_isExistMotion_guess` |
| iOS arm64 | `0x101B69970` | `0x101B6996C` | `0x4` | `Player_isExistMotion_guess` |
| iOS armv7 | `0x187D61C` | `0x187D610` | `0xC` | `Player_isExistMotion_guess` |

原始 data-xref 数为 2/3/1/2；AArch64 page materialization 与 Thumb literal 序列会给同一地址
产生多条引用，全部归属于表中的唯一函数。四库 fresh decompile 都把虚调用第四参数恢复为
`&g_Player_isExistMotionMemberHint_guess`。

Android arm64 在 `piledCopy` 后先有 `g_Player_defaultSyncActive_guess`，随后还有一个被另一条
load-request 路径送入字符串构造 helper 的零初始化单字节实体（按初值推断为空字符串输入）
及对齐空洞；两个 32 位目标也各留出八字节。因此 V163 的八槽声明必须在 `piledCopy` 截止，
不能把本函数的 local-static backing 搬进 `MotionDispatch.h` / `RuntimeSupport.cpp`。

## 4. 数据流与对象生命周期

四端共同伪代码为：

```cpp
bool Player::isExistMotion(ttstr name) {
    tTJSVariant path(
        TJS_W("motion/") + _stealthChara + TJS_W("/") + name);

    iTJSDispatch2 *rm = _resourceManager.AsObjectNoAddRef();
    tTJSVariant *params[] = { &_findMotionContextVariant, &path };
    tTJSVariant result;
    static tjs_uint32 isExistMotionMemberHint_guess = 0;

    (void)rm->FuncCall(
        0, TJS_W("isExistMotion"), &isExistMotionMemberHint_guess,
        &result, 2, params, rm);
    return result.operator bool();
}
```

关键 Player 字段偏移也按 ABI 一致地成组出现：

| 目标 | stealthChara | canonical ResourceManager Variant | persistent context Variant |
|---|---:|---:|---:|
| Android arm64 | `this+968` | `this+992` | `this+1012` |
| Android armv7 | `this+672` | `this+684` | `this+696` |
| iOS arm64 | `this+856` | `this+880` | `this+900` |
| iOS armv7 | `this+608` | `this+620` | `this+632` |

### 4.1 路径先于 receiver conversion

四端都先完成字符串拼接、把最终字符串复制进局部 `tTJSVariant path`，并清理中间 `ttstr`
引用，之后才检查 ResourceManager Variant 的 type。严格转换的错误 helper 调用点为
`0x6CDD54`、`0x594382`、`0x10011F620`、`0x11E13C`。

旧源码虽然已经采用严格 `AsObjectNoAddRef()`，但把它写在 path 构造之前。正常路径相同，
异常前缀却不一致；本轮把 path 提前，恢复参考实现的构造/展开顺序。

### 4.2 canonical receiver 是借用，不是独立 owner

转换成功后机器码直接读 canonical Variant 内的 Object 指针并发起虚调用。四端在这里都没有
Variant copy、Object `AddRef` 或配对 `Release`。这与 old-node reset/requireLayerId 等必须跨
重入保活接收者的路径不同；不能复用 `retainVariantObject_guess`。

若 canonical Variant 的 Object 指针本身为空，参考实现没有额外 null gate；后续虚调用仍按
原始指针边界执行。若 canonical Variant 不是 Object，则严格转换抛异常而不是返回伪造的
`false`。

### 4.3 两参数的别名与可变性

`params[0]` 不是 `_findMotionContextVariant` 的保护性副本，而是成员地址本身。自定义 dispatch
可以在回调中替换它，修改会在 `Player::getProject()` 等后续读取中持续可见。`params[1]` 则只
指向本次调用的局部 path；参数顺序固定为 context、path。

### 4.4 HRESULT、result 与销毁顺序

调用返回的普通失败 HRESULT 被丢弃。只要 dispatch 在 result 中写入 truthy 值，即使返回
`TJS_E_FAIL`，包装器仍返回 `true`；若普通失败未写 result，默认 Void 的 bool conversion 为
`false`。正常尾部四端都先完成 bool conversion，再按 result、path 的逆构造顺序销毁。

虚调用直接抛异常时不会执行 bool conversion；已构造的 result/path 由异常清理释放。bool
conversion 自身抛异常时同样清理二者。由于 receiver 只是借用，该函数没有额外 manager-owner
析构动作。

## 5. 源码与回归探针

本轮源码变更：

- `PlayerResource.cpp`：把 path 构造移到 strict ResourceManager conversion 之前；保留
  `AsObjectNoAddRef()`；把匿名 `hint` 改成准确的
  `isExistMotionMemberHint_guess`；
- `MotionDispatch.h`：明确八槽全局家族在 `piledCopy` 截止，避免沿 Intel64 邻接关系继续
  扩张；
- `motionplayer-dll.cpp`：把现有 aliased-context probe 扩展为两次调用，锁定路径、context
  的持续变更、同一私有 hint 的复用、与前八个全局 hint 的地址隔离，以及两次回调前后
  ResourceManager `AddRef/Release` 计数完全不变；保留 ordinary-failure truthy result 与
  non-Object strict throw 检查。

测试观察的是 dispatch 收到的真实 hint 指针，而没有把 function-local static 暴露成新的公共
header symbol；这与四端 storage 范围更接近。

## 6. IDB 回写

四个 recovery IDB 均完成：

- `Player_isExistMotion_guess` 函数级行为注释；
- exact UTF-16 literal 语义名与注释；
- private backing word 重建为 size-4
  `g_Player_isExistMotionMemberHint_guess`；
- strict-conversion、虚调用、bool conversion/normal destruction 注释；
- bookmark：`V164 complete isExistMotion private hint and borrowed receiver boundary`；
- force-recompile 后 fresh pseudocode readback，四端都直接显示新 hint 实体；
- entity readback 均为独立 4-byte global；
- 四库原位保存成功。

Android arm64 原 IDB 把 `0x1AB54A9` 起的一段零区误并成大 byte item，使新 hint 第一次
`make_data` 只得到零尺寸别名。本轮先只 undefine `0x1AB54A9..0x1AB54B3`，再恢复原单字节
`byte_1AB54A9` 并在 `0x1AB54B0` 建立独立 4-byte word；没有修改任何二进制字节。

## 7. 验证

- ordinary syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- Web Debug 最终链接：通过；
- Wasmtime Headless Debug 最终链接：通过；
- Web Wasm：`85,648,294` bytes，539 imports / 69 exports；
- Headless Wasm：`84,995,435` bytes，538 imports / 69 exports；
- 两份 Wasm 均通过 Node `WebAssembly.Module` 解析与 LLVM section 审计；
- 两份产物都只比 V163 增加 7 bytes；
- scoped `git diff --check`：无 whitespace error，仅工作区既有 LF/CRLF 提示；
- 两个配置的 CTest：均未注册测试；相关 Catch2 TU 由 ordinary/headless 两次 syntax-only
  完整编译检查覆盖。
