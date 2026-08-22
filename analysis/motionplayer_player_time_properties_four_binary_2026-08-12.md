# MotionPlayer Player 时间属性四参考二进制对照（2026-08-12）

## 1. 结论与本地偏差

`Motion.Player` 的四个只读时间属性是同一对 raw-frame 字段的两套视图：

| 属性 | 字段来源 | 返回域 |
| --- | --- | --- |
| `frameLastTime` | `motion["lastTime"]` | 原始 frame double |
| `frameLoopTime` | `motion["loopTime"]` | 原始 frame double |
| `lastTime` | `motion["lastTime"]` | 正值换算为毫秒 |
| `loopTime` | `motion["loopTime"]` | 正值换算为毫秒 |

四端共同伪代码为：

```cpp
double frameLastTime() { return rawLast; }
double frameLoopTime() { return rawLoop; }
double lastTime() {
    return rawLast > 0.0 ? rawLast * 1000.0 / 60.0 : rawLast;
}
double loopTime() {
    return rawLoop > 0.0 ? rawLoop * 1000.0 / 60.0 : rawLoop;
}
```

本地纵向前的 `lastTime` 与 `loopTime` 都绑定同一个读取 `_loopTime` 的换算 getter，
所以 `lastTime` 可观察地返回了错误字段。修订后显式拆成两个 raw getter 和两个
converted getter，并保持 EmotePlayer 的四个同名属性仍全部返回 raw frame 值。

## 2. 宽字符串与 Player registrar

四个名称均通过 UTF-16LE 字节模式定位。iOS 二进制各有 Player 与 EmotePlayer
两份字符串，Android 由两张 registrar 共享一份字符串。

| 字符串 | Android arm64 | Android armv7 | iOS arm64 Player / EmotePlayer | iOS armv7 Player / EmotePlayer |
| --- | --- | --- | --- | --- |
| `frameLastTime` | `0x14D3E9E` | `0xD84854` | `0x10195CCB8` / `0x1019604B6` | `0x174F01C` / `0x175281A` |
| `frameLoopTime` | `0x14D3EBA` | `0xD84870` | `0x10195CCD4` / `0x1019604D2` | `0x174F038` / `0x1752836` |
| `loopTime` | `0x14D3ED6` | `0xD8488C` | `0x10195C810` / `0x1019604EE` | `0x174EB74` / `0x1752852` |
| `lastTime` | `0x14D3A60` | `0xD844A2` | `0x10195C822` / `0x10195FE9A` | `0x174EB86` / `0x17521FE` |

Player 注册/绑定如下；所有调用都传空 setter 成员函数指针对，属性为只读。

| 属性 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| `lastTime` 注册点/getter | `0x6D3FFC` / `0x6D6800` | `0x597F3E` / `0x598D50` | `0x100124590` / `0x100125454` | `0x1238D6` / `0x124650` |
| `loopTime` 注册点/getter | `0x6D4060` / `0x6D6828` | `0x597F58` / `0x598D88` | `0x1001245B8` / `0x10012547C` | `0x1238F8` / `0x124684` |
| `frameLastTime` 注册点/getter | `0x6D508C` / `0x6D6B84` | `0x598360` / `0x598FF6` | `0x100124BBC` / `0x1001256D8` | `0x123E7C` / `0x1248F8` |
| `frameLoopTime` 注册点/getter | `0x6D50F0` / `0x6D6B8C` | `0x59837A` / `0x599000` | `0x100124BE4` / `0x1001256E0` | `0x123E9E` / `0x124902` |

这也否定了旧注释中“`loopTime` 绑定名为 `getLastTime` 的单一 getter”的表述。
源代码层面存在两套独立换算 getter；旧 IDB 名称和本地 C++ 命名把它们混在了一起。

## 3. 字段布局与初始化数据流

| 证据 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| raw last 字段 | `+1128` | `+784` | `+1016` | `+716` |
| raw loop 字段 | `+1136` | `+792` | `+1024` | `+724` |
| ordinary initializer | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| loop 读取/写入 | call `0x6B0AE8`, store `0x6B0AEC` | call `0x580C6C`, store `0x580C78` | call `0x1001082C0`, store `0x1001082C4` | call `0x10598C`, store `0x1059A0` |
| last 读取/写入 | call `0x6B0B08`, store `0x6B0B0C` | call `0x580C84`, store `0x580C8C` | call `0x1001082E0`, store `0x1001082E4` | call `0x1059B2`, store `0x1059C6` |

尽管源数据读取顺序是 loop 后 last，字段内存顺序是 last 后 loop。四端均先从
motion content dispatch 读取 `loopTime` 写 raw-loop，再读取 `lastTime` 写
raw-last；此后才读取 `tag`、`priority` 和 root content。两个 double 是立即写入，
后续 tag/priority/构树失败不会回滚它们。

## 4. 换算和浮点边界

换算 getter 先做有序 `raw > 0.0` 比较；只有成功时才依次执行乘 `1000.0`、除
`60.0`。不能把表达式重排成 `raw / 60 * 1000`，也不应预折叠成一个近似常量，
因为这会改变部分有限值的最后几位和溢出边界。

- 正有限值：`raw * 1000.0 / 60.0`；
- `+∞`：比较为真，换算后仍为 `+∞`；
- 正零：比较为假，原样返回 `+0`；
- 负零：比较为假，保留负零符号位；
- 负有限值和 `-∞`：比较为假，原样返回；
- NaN：有序比较为假，原 NaN payload 通过返回路径。

raw getter 只是一个 double load，不做 clamp、整数化或单位换算。

## 5. EmotePlayer 的不同公开语义

EmotePlayer 的四个名称并不复用 Motion.Player 的 converted getter。其 registrar
把 `frameLastTime` 与 `lastTime` 绑定到同一个嵌套 Player raw-last wrapper，把
`frameLoopTime` 与 `loopTime` 绑定到同一个 raw-loop wrapper。

| raw wrapper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| last/frameLast | `0x67F274` | `0x562024` | `0x1001B60F4` | `0x1B5EA8` |
| loop/frameLoop | `0x67F280` | `0x562032` | `0x1001B6100` | `0x1B5EB6` |

因此以下差异是原始插件 API 的一部分：同样名为 `lastTime` / `loopTime`，
Motion.Player 返回正值的毫秒换算，EmotePlayer 返回 raw frames。

## 6. 本地修订

- `Player.h` 新增 `getFrameLoopTime()`，让 `getFrameLastTime()` 与它只返回 raw
  pair；`getLastTime()` 改读 `_cachedTotalFrames`，`getLoopTime()` 改为读取
  `_loopTime` 的 converted getter；
- `main.cpp` 把四个 Motion.Player 属性分别绑定到正确 getter，移除旧
  `libkrkr2.so` 单端地址和历史回滚注释；
- `EmotePlayer.h` 明确其四个属性均路由 raw getter，避免新 Player converted
  getter意外改变 EmotePlayer ABI；
- 单元测试直接从真实 fixture motion dispatch 读取 `lastTime`/`loopTime` 作
  oracle，验证四个 C++ getter、四个脚本属性、四个只读 setter 边界，以及
  loop converted getter 对负零、负值与 NaN 的 passthrough。

## 7. IDB 改进

四份 IDB 均命名六个 getter：四个 Player getter 与两个 EmotePlayer raw wrapper，
共 24 个函数。Android armv7 原本未把两个 converted getter 定义成函数，本轮
补齐 `0x598D50..0x598D74` 与 `0x598D88..0x598DAC` 函数边界后重新反编译。
四个 Player 注册点和 ordinary initializer 的字段读取点均加了语义注释，并在
强制重新编译后 fresh-decompile 验证名称、字段和分支。

## 8. 验证

- `git diff --check`：通过；
- 使用 Web `compile_commands.json` 的实际 motionplayer Emscripten 参数，对完整
  `motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；仅有仓库既有 `_tss`
  literal-operator 弃用警告；
- `cmake --build out/web/debug --target motionplayer -j 1`：通过；长增量编译在
  工具调用超时后由 Ninja 正常完成，重跑确认 `ninja: no work to do`；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest -j 1`：通过；
  `wasm-opt` 正常产出 guest wasm，重跑确认无待办工作；
- `cmake --build out/web/debug --target krkr2 -j 1`：通过，成功链接
  `index.html`。输出只有仓库既有的 pthread/memory-growth、JSPI 与 JS library
  警告。
