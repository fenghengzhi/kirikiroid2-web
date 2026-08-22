# MotionPlayer variable-range Dictionary owner / return handoff 四参考二进制复原（2026-08-15）

## 结论

本轮重新从四个当前参考二进制检查 `EmotePlayer::getVariableRange`、其唯一的
Player fallback，以及两者共同调用的 Real 属性 setter，确认本地原先复用
`detail::makeDictionary` 会抹平原版可观察的 owner、hint、失败状态和隐藏返回对象
边界。

四端共同存在两条不同的 fresh Dictionary 构造管线：

1. Engine HM5 命中时，EmotePlayer 先持有局部 owning Dictionary，写完属性后才
   CopyRef 到 ABI 隐藏返回 Variant；
2. HM5 miss 后，Player 递归折叠真实 parameter range；仅当 `min < max` 时直接在
   ABI 隐藏返回 Variant 中建立 Dictionary，不再做末尾返回 CopyRef。

两条管线都用 copy/force/retained-accessor/early-Clear 取得属性 receiver，但它们
使用两对彼此独立的 mutable TJS member hint。所有四次 Real setter 的布尔结果都被
忽略；普通失败状态不会触发回滚、Void fallback 或第二次写入。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmotePlayer_getVariableRange_guess` | `0x670FCC` / `0x2C4` | `0x55AF8C` / `0xEE` | `0x1001AE454` / `0x124` | `0x1ADC6C` / `0x168` |
| `Player_getVariableRange_guess` | `0x6D3970` / `0x1DC` | `0x597C00` / `0xEA` | `0x1001241FC` / `0x12C` | `0x1234D8` / `0x16C` |
| `setDispatchRealProperty_guess` | `0x671290` / `0xE8` | `0x55B0E4` / `0x86` | `0x100113810` / `0xA4` | `0x1111E8` / `0xC2` |

表内地址和尺寸只用于对应 recovery IDB 的证据索引。剥离产物不能证明原始 C++
标识符拼写，因此恢复名保留 `_guess`。

## EmotePlayer HM5 hit

四端的正常路径顺序一致：

1. 用 label 查询 Engine HM5 `_variableRanges`；查询会按 `ttstr` 规则使用/更新
   label 自身的 hash hint；
2. 调用 Dictionary factory；把 `{dispatch, dispatch}` 构造成局部 owning Variant，
   随即释放 factory 返回的原始 dispatch 引用；
3. CopyRef owning Variant 到 accessor 输入；
4. `ToObject`，构造 retained dispatch accessor；
5. 在任何属性调用前 Clear accessor 输入副本；因此跨属性调用只保留原 owning
   Variant和 accessor 的 dispatch 引用，不保留第二个 Object closure；
6. 用 `flags=TJS_MEMBERENSURE`、字面量 `min`、EmotePlayer-min hint 和 Dictionary
   自身作为 `ObjThis` 写入 `frameMin`；
7. 用另一枚 EmotePlayer-max hint 同样写入 `frameMax`；
8. CopyRef 原 owning Dictionary 到 ABI 隐藏返回 Variant；
9. 释放 accessor；
10. 析构原 owning Dictionary，隐藏返回 Variant 成为最终 owner。

近似源结构是：

```cpp
Variant dictionary = own(TJSCreateDictionaryObject());
Variant receiverValue(dictionary);
receiverValue.ToObject();
Accessor receiver(receiverValue);
receiverValue.Clear();

(void)receiver.SetValue("min", frameMin, MEMBERENSURE,
                        &emotePlayerMinHint);
(void)receiver.SetValue("max", frameMax, MEMBERENSURE,
                        &emotePlayerMaxHint);
Variant returned(dictionary); // explicit CopyRef into hidden return
return returned;
```

它每次返回新的 Dictionary，不缓存脚本对象，也不把 HM5 node 暴露给 TJS。

## HM5 miss 与 label 按值边界

HM5 miss 后，EmotePlayer 不直接把现有 label 引用借给 Player。四端都先构造一份
额外 `ttstr` CopyRef，把它作为 `Player::getVariableRange(ttstr label)` 的按值参数，
等 Player 返回后再析构这份副本。Player 内部的递归 folder 则以 `const ttstr &`
借用该参数，不为每一层 child 再复制字符串。

因此源级 ABI 应为：

```cpp
Variant Player::getVariableRange_guess(ttstr label);
```

而不是 `const ttstr &label`。这个差异通常不改变数值结果，但会改变字符串引用计数、
分配失败位置和调用期间 label owner 的数目。

## Player fallback 的隐藏返回 owner

Player 先用 `DBL_MAX/-DBL_MAX` 初始化 extrema，再调用已恢复的递归 folder。只有
有序的 `minValue < maxValue` 才进入 Dictionary 路径；相等、反向或 unordered 时，
隐藏返回对象保持/成为 Void，完全不创建 Dictionary。

仅在 `minValue < maxValue` 时执行：

1. Dictionary factory 结果直接构造成 ABI 隐藏返回 Variant；
2. 释放 factory 原始 dispatch 引用；
3. CopyRef 隐藏返回 Variant 到 accessor 输入；
4. `ToObject`，建立 retained accessor，并 early Clear 输入副本；
5. 用 Player-min hint 写 `min`；
6. 用独立 Player-max hint 写 `max`；
7. 释放 accessor；隐藏返回 Variant 从创建时起一直是最终 owner。

近似源结构是：

```cpp
if (!(minValue < maxValue))
    return Void;

Variant result = own(TJSCreateDictionaryObject()); // hidden return owner
Variant receiverValue(result);
receiverValue.ToObject();
Accessor receiver(receiverValue);
receiverValue.Clear();

(void)receiver.SetValue("min", minValue, MEMBERENSURE,
                        &playerMinHint);
(void)receiver.SetValue("max", maxValue, MEMBERENSURE,
                        &playerMaxHint);
return result; // no second returned CopyRef
```

这与 EmotePlayer hit 的关键区别是隐藏返回 Variant 的提交时机：前者在两个属性写
之前已经拥有 Dictionary，后者在两个属性写之后才从局部 owner CopyRef。

## Real setter 与四枚 hint

三组函数的交叉检查确认共享 setter helper 的精确语义是：

```cpp
bool setDispatchRealProperty_guess(Accessor &receiver,
                                   const tjs_char *name,
                                   tjs_uint32 *hint,
                                   double value) {
    tTJSVariant realValue(value);
    const tjs_error hr = receiver.PropSet(
        TJS_MEMBERENSURE, name, hint, &realValue, receiver.dispatch());
    realValue.~tTJSVariant();
    return hr == TJS_S_OK;
}
```

两条调用路径各传两枚不同的进程级 mutable hint：

- EmotePlayer `min` hint；
- EmotePlayer `max` hint；
- Player `min` hint；
- Player `max` hint。

所以不能用只持有一对 hint 的通用 Dictionary builder，也不能为动态方便传
`nullptr`。helper 在 `PropSet` 返回后先销毁 Real temporary，再把严格
`hr == TJS_S_OK` 转成 bool；四个 caller 全部丢弃该 bool。

## 失败状态、部分内容与异常前缀

普通 `tjs_error` 失败不是 C++ 异常。四端的可观察边界为：

- `min` 失败仍继续尝试 `max`；
- `min` 成功而 `max` 失败时不删除 `min`；
- `min` 失败而 `max` 成功时不把结果改回 Void；
- 两次都失败也仍沿各自正常 owner 路径返回 fresh Dictionary；
- 没有 retry、fallback setter、事务、回滚或错误码传播。

因此返回 Dictionary 可以只含一个属性，甚至两个属性都没有；这取决于 dispatch
在两个 `PropSet` 时实际接受了什么。当前 factory 创建的是普通 Dictionary，但
恢复代码仍保留原调用契约，避免未来替换 factory/dispatch 时错误地收紧边界。

对真正的 C++ 异常，owner 建立时机决定已构造前缀：

- EmotePlayer hit 在 factory 成功后由局部 owning Variant负责清理；隐藏返回对象要到
  两次 setter 之后才获得 CopyRef；
- Player valid-range 在 setter 之前已经把 Dictionary 建进隐藏返回 Variant；accessor
  只是额外短命 owner；
- accessor 输入都在第一次 setter 前 early Clear，所以异常或脚本重入期间不存在
  跨调用的第二份 Object closure；
- Real temporary 由共享 helper 的清理路径销毁。

本轮没有把反编译无法证明的分配器内部行为或异常类型写入源注释。

## 为什么不能继续使用 `detail::makeDictionary`

原通用 helper 的结构是直接在 factory dispatch 上逐项 `PropSet`，使用 null hints，
并在属性写完后才构造返回 closure。它至少产生以下偏差：

- 缺少 copy/force/retained-accessor/early-Clear owner 链；
- 丢失四枚函数族专属 mutable hint；
- 无法表达 EmotePlayer 局部 owner与 Player 隐藏返回 owner 的提交时机差异；
- 字符串 widening 和 initializer 容器临时量引入参考二进制中不存在的生命周期；
- 失败状态和异常前缀不同。

因此本轮在两个 caller 中分别展开原版结构，没有扩写通用 helper。

## 源码与 IDB 回写

源码修改：

- `EmotePlayer::getVariableRange`：HM5 hit 改为局部 owning Dictionary、
  copy/force/accessor/early-Clear、EmotePlayer hint 对、显式返回 CopyRef；
- `Player::getVariableRange_guess`：参数恢复为 `ttstr` 按值，valid-range 改为直接
  hidden-return owner、短命 accessor 和 Player hint 对；
- 两条路径继续忽略 setter bool，并保持 `min >= max` 返回 Void 的原边界。

四份 recovery IDB 均已在 EmotePlayer 与 Player 入口补充 owner/handoff 注释和书签；
共享 Real setter 保留已恢复的语义名。精确地址只保存在本分析文件和 IDB 中，未写入
编译源注释。

## 验证

- 使用单元测试 translation unit 的真实 Emscripten response file 执行 syntax-only，
  通过；唯一输出是仓库既有 `_tss` literal-operator 弃用警告；
- `cmake --build --preset "Web Debug Build"` 完成 33 个增量步骤并成功链接最终
  `index.html`；输出仅含既有编译器和 Emscripten 警告；
- 对三个源码文件、旧变量查询总览、本文和 `plan.md` 执行定向 diff 检查；
- 四份 recovery IDB 在文档与验证完成后保存。
