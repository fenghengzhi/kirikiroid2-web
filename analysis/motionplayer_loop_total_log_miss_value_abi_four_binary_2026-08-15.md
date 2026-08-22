# MotionPlayer loop / total-frame log-miss 与 value ABI 四参考二进制复原（2026-08-15）

## 结论

本轮 fresh 检查四端 Engine query、D3D facade和 miss-path共享 callee，修正了旧
`analysis/motionplayer_d3d_loop_total_four_binary_2026-08-11.md` 中一项重要错误：

`getLoopTimeline` 在 label miss 时不会抛异常。它构造
`timeline label not found '<label>'.`，调用普通单参数、非重要级别的 TVP log wrapper，
销毁两个字符串临时量，然后返回 `false`。`getTimelineTotalFrameCount` 对同一 miss既不
记录日志也不抛异常，直接返回 `0.0`。

此外，四端 D3D wrapper都在调用 Engine 前 CopyRef label，调用后释放，证明两个 Engine
query 的源级参数是 `ttstr` 按值而非 `const ttstr &`。EmotePlayer注册的 loop query直接
返回 native bool；本地此前显式构造 `tTJSVariant(bool)` 虽脚本值接近，但源结构不符。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `D3DEmotePlayer_isLoopTimeline_guess` | `0x530B10` / `0xA4` | `0x494E18` / `0x58` | `0x100233284` / `0x5C` | `0x231EC8` / `0x98` |
| `D3DEmotePlayer_getTimelineTotalFrameCount_guess` | `0x530BB4` / `0xAC` | `0x494E88` / `0x68` | `0x1002332F4` / `0x5C` | `0x231F8C` / `0xA0` |
| `EmoteEngine_getLoopTimeline_guess` | `0x67260C` / `0x1C4` | `0x55B6B0` / `0x76` | `0x1001AF02C` / `0x84` | `0x1AE89C` / `0xCE` |
| `EmoteEngine_getTimelineTotalFrameCount_guess` | `0x6727D0` / `0xD4` | `0x55B750` / `0x2A` | `0x1001AF0D4` / `0x30` | `0x1AE9A4` / `0x2A` |
| one-argument TVP log wrapper | `0xA16CA4` | `0x76483A` | `0x1002591D4` | `0x25A52E` |

Android ARMv7/iOS ARMv7注册描述符中的函数指针带 Thumb bit；普通偶地址 xref缺失时需
查询奇地址。剥离产物不能证明原始标识符拼写，插件内部函数继续使用 `_guess`。

## Engine HM3 lookup

两个 query均对同一 timeline-state HM3执行非插入查询：

| 项目 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| HM3 成员偏移 | `+936` | `+468` | `+584` | `+292` |
| `loopBegin` | node/value `+56` | `+40` | `+64` | `+36` |
| `lastTime` | node/value `+72` | `+56` | `+80` | `+52` |

64 位 Android body明确展示 `ttstr` embedded hash hint：string object `+68`；hint为0时
计算 TJS wide-string hash，0结果正规化为 `0xFFFFFFFF`，再写回 label对象。其他三端把
同一逻辑封装进 HM3 find helper。空 `ttstr` 使用 hash 0。lookup不调用 `operator[]`，
miss不会创建 timeline state。

四端共同判断为 ordered double比较：

```cpp
const bool loop = state.loopBegin >= 0.0;
```

所以 `-0.0` 通过，任意 NaN失败。没有读取或检查 `timelineData`、active vector、flags、
blend状态或 controller。

## loop hit / miss

hit只读取 `loopBegin`并返回 bool。miss的精确正常路径是：

1. 构造 `prefix + label` 的第一份 owning `ttstr`；
2. 再与后缀 `'.` 拼接成第二份 owning `ttstr`；
3. 把第二份字符串传给单参数 TVP log wrapper；
4. wrapper把 line和整数 `0` 传给共享日志核心并正常返回；
5. 先析构完整 line，再析构 prefix+label临时量；
6. 返回 `false`。

近似源结构：

```cpp
bool getLoopTimeline_guess(ttstr label) {
    const auto found = timelineStates.find(label);
    if (found != timelineStates.end())
        return found->second.loopBegin >= 0.0;
    TVPAddLog(ttstr(L"timeline label not found '") + label + L"'.");
    return false;
}
```

字符串分配或日志内部真正抛出的 C++ 异常仍可传播，但“label not found”这个业务分支本身
没有 throw helper、异常对象或错误码。

## total-frame value边界

total query的共同路径：

```cpp
double getTimelineTotalFrameCount_guess(ttstr label) {
    const auto found = timelineStates.find(label);
    if (found != timelineStates.end() &&
        found->second.loopBegin >= 0.0)
        return found->second.lastTime;
    return 0.0;
}
```

`loopBegin` gate通过后，`lastTime` 原样返回。没有：

- 非负 clamp；
- integer rounding；
- finite/NaN检查；
- `timelineData` null检查；
- 与 loop begin/end重新计算；
- 日志或异常。

因此 EmotePlayer 的 double API可观察到负数、分数、NaN和无穷 `lastTime`。

## D3D owner与整数转换

两个 D3D body共同执行：

1. 从主 EmoteObject槽解析 Engine；
2. CopyRef输入 label handle为下游按值 `ttstr` 参数；
3. 调用 Engine query；
4. 在正常路径释放该 CopyRef；异常清理路径也释放后继续 unwind；
5. loop原样返回 bool；total把 double转换为 signed 32-bit integer。

四端 total facade的转换指令是：

- Android/iOS ARM64：`FCVTZS Wn, Dn`；
- Android/iOS ARMv7：`VCVT.S32.F64 Sn, Dn`。

对可表示的有限值，它们向0截断；不会先 round、floor或clamp source field。原始 C++
浮点到整数转换对NaN或超出目标范围没有可移植语义，因此本轮保留源级
`static_cast<tjs_int>`，只在 IDB/分析中记录参考机器指令，不为 Web 自创一套未被原源码
证明的饱和策略。

## 源码修正

- `EmoteEngine::getLoopTimeline_guess`：miss从 `TVPThrowExceptionMessage` 改回精确字符串
  拼接、`TVPAddLog`、`false`；
- 两个 Engine query参数从 `const ttstr &` 恢复为按值 `ttstr`；
- `EmotePlayer::getLoopTimeline` 返回类型从显式 Variant改为 bool；
- EmotePlayer facade以 move把自身按值参数交给同对象的 Engine helper，避免本地额外
  facade层引入参考直接注册体不存在的第二次 AddRef；
- D3D facade继续以 lvalue调用 Engine，保留四端明确存在的额外 CopyRef；
- 回归从 `REQUIRE_THROWS` 改为 false，并增加 HM3 miss不插入、raw负数/NaN/Inf
  `lastTime` 覆盖。

## IDB 回写与验证

四份 recovery IDB 的八个 Engine/D3D入口补充 hit/miss、日志、按值 owner和整数转换注释；
loop与total入口添加 bookmark。四个 TVP wrapper保留 `TVPAddLog_guess` 语义名，并修正为
通用“单参数、importance=0、正常返回”的注释，避免再被误读为 throw helper。

真实 Emscripten测试 TU syntax-only通过，唯一输出是仓库既有 `_tss` literal-operator
弃用警告；Web Debug完成 10 个增量步骤并成功链接最终 `index.html`。随后对四个源码/
头文件、测试、旧总览、本文和 `plan.md` 执行定向 `git diff --check`，最后保存四份
recovery IDB。
