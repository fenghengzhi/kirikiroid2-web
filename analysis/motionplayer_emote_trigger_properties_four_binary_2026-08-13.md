# Motion.EmotePlayer 四个触发型 Boolean 属性的四端复原（2026-08-13）

## 结论

`Motion.EmotePlayer` 的 `debugPrint`、`queuing`、`directEdit`、
`selectorEnabled` 并不是通常意义上的可逆 Boolean 属性。四份当前参考二进制
完全一致：getter 读取 EmoteEngine 内的一个无符号字节；setter 不读取传入值，
只把对应字节写成 `1`。因此脚本赋 `false` 也会得到 `true`。其中
`selectorEnabled` setter 写 `1` 后还会无条件执行一次完整 selector 同步。

这四个字节属于 `EmoteEngine`，不属于内层 `Player`。尤其要避免把
`directEdit` 与 `Player::_directEdit`、把 `queuing` 与 `Player::_queuing` 合并。

## 注册入口与访问器

属性按 `debugPrint`、`queuing`、`directEdit`、`selectorEnabled` 的顺序注册：

| 构建 | EmotePlayer 注册函数 | 四个属性名引用 |
|---|---:|---:|
| Android arm64 | `0x67CEA8` | `0x67E008 / 0x67E080 / 0x67E0F8 / 0x67E170` |
| Android armv7 | `0x5612E8` | `0x561750 / 0x56176C / 0x561782 / 0x56179C` |
| iOS arm64 | `0x1001B5130` | `0x1001B57A8 / 0x1001B57D4 / 0x1001B5800 / 0x1001B582C` |
| iOS armv7 | `0x1B4DE0` | `0x1B53CA / 0x1B53F6 / 0x1B5422 / 0x1B544E` |

访问器与 Engine 字节偏移：

| 构建 | debugPrint get/set | queuing get/set | directEdit get/set | selectorEnabled get/set | Engine offsets |
|---|---|---|---|---|---|
| Android arm64 | `0x67F330 / 0x67F338` | `0x67F344 / 0x67F34C` | `0x67F358 / 0x67F360` | `0x67F36C / 0x67F374` | `+1163 / +1161 / +1159 / +1160` |
| Android armv7 | `0x5620E8 / 0x5620EE` | `0x5620F6 / 0x5620FC` | `0x562104 / 0x56210A` | `0x562112 / 0x562118` | `+595 / +593 / +591 / +592` |
| iOS arm64 | `0x1001B61CC / 0x1001B61D4` | `0x1001B61E0 / 0x1001B61E8` | `0x1001B61F4 / 0x1001B61FC` | `0x1001B6208 / 0x1001B6210` | `+795 / +793 / +791 / +792` |
| iOS armv7 | `0x1B5FC0 / 0x1B5FC6` | `0x1B5FCE / 0x1B5FD4` | `0x1B5FDC / 0x1B5FE2` | `0x1B5FEA / 0x1B5FF0` | `+411 / +409 / +407 / +408` |

所有 getter 都只有一次 `LDRB` 和返回。前三个 setter 都是“装入常数 1、
`STRB`、返回”。`selectorEnabled` setter 也是先写常数 1，但随后跳到：

| 构建 | selector 同步函数 |
|---|---:|
| Android arm64 | `0x66E0FC` |
| Android armv7 | `0x559A8C` |
| iOS arm64 | `0x1001AC8A4` |
| iOS armv7 | `0x1AC0D0` |

访问器不比较旧值，所以 `selectorEnabled=true` 的重复赋值仍会同步；
`selectorEnabled=false` 也先写成 true 再同步。

## 构造默认值和相邻布局

| 构建 | EmoteEngine 构造函数 | directEdit | selectorEnabled | queuing | debugPrint |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x67B76C` | 0 | 1 | 0 | 0 |
| Android armv7 | `0x560948` | 0 | 1 | 0 | 0 |
| iOS arm64 | `0x1001B7FB0` | 0 | 1 | 0 | 0 |
| iOS armv7 | `0x1B7788` | 0 | 1 | 0 | 0 |

Android arm64 在 `0x67BB38` 对从 selector 字节开始的四字节写入整数 1，
即字节序列 `01 00 00 00`；directEdit 已由前一段清零覆盖。Android armv7
在 `0x560B9A` 做同样的四字节写入，前面的 `__aeabi_memclr4` 覆盖 directEdit。
iOS arm64 在 `0x1001B8118` 以八字节零写覆盖 directEdit，随后
`0x1001B812C..0x1001B8134` 分别写 selector=1、queuing=0、debug=0。
iOS armv7 的两个连续八字节零写覆盖 directEdit，随后
`0x1B796E..0x1B797E` 写其余三个字节。相邻 dirty 字节最终单独置 1，不能把
四字节初始化误读成“整个属性簇中只有 selector 有意义”。

## 数据流

### debugPrint

在四个当前二进制的 Motion/Emote 代码区内，以对应 Engine 偏移扫描全部指令，
只得到构造初始化和这对属性访问器；没有日志、渲染或进度路径读取该字节。
因此当前可证行为是“可观察、可单向置位，但运行期无消费者”。这不等于原始
源码一定没有条件编译掉的调试代码。

### queuing

该字节是 Engine 控制器调用的 append/replace 参数。四端读取集合一致，覆盖：

- 通用 `setVariable` 中 Blink、Eyebrow、Mouth、Transition、Selector 控制器；
- timeline seek/window；
- `setCoord`、`setScale`、`setRotate`、`setColor`；
- outer-force 与 timeline-blend 控制器；
- 构造阶段给初始控制值排队的路径。

false 表示替换已有工作，true 表示追加。它与内层 Player 控制帧游标的
`_queuing` 是不同对象中的不同字节。D3DEmotePlayer 的历史拼写 `queing`
访问同一个 Engine 字节，但不是本文件四属性注册簇的一员。

### directEdit

该名称容易误导。Engine 字节的两类消费者是：

1. 通用 `setVariable`：HM6 命中的类型 0/1/2 在字节为 false 时由专用
   spring/constant feed 处理并提前返回；字节为 true 时落入 HM7。
2. `EmoteEngine_progressCore_guess`：原始 dt 非零且字节为 false 时才推进三个
   physics-target controller、hair/parts spring 与两条 bust chain。置 true 后
   跳过整个 physics-only pass。

四端 progress 判定点分别为 `0x67A7F4`、`0x55FFB6`、`0x1001B43E8`、
`0x1B3EE0`。这与 Player 自己决定角度编辑/Emote motion 的
`Player::_directEdit` 完全不同。

### selectorEnabled

setter 每次都调用同步函数。同步过程：

1. 新建 Array，赋给公开 `variableKeys` backing Variant；
2. 内容复制当前 variable-label Array；
3. 置 Engine dirty；
4. 遍历 selector deque，把每项 gate 写成 selectorEnabled；
5. enabled 时清 selector command queue、把状态置零并立即应用选项 0；
6. 遍历每项保存的非 owning transition-target 指针并更新公开变量标签。

由于公开 setter 永远先写 true，脚本不能通过该属性进入同步函数的 disabled
分支；该分支仍可能由内部字段写入后调用同步而存在，故本地实现保留完整分支。

## 本地落地

- `EmotePlayer.h` 保留四个忽略参数并强制置 true 的 setter；
- `selectorEnabled` 保留无条件同步调用；
- `EmoteEngine.h` 注释明确四个字段的实际消费者，并去掉只指向旧 Android
  单端地址的说明；
- 单元测试覆盖四个构造默认值、false/Void/true 经 typed ncbind setter 后都
  读回 true，以及 selector 的重复 false 赋值仍发布新 Array 并置 dirty；
- 四份 IDB 的 32 个访问器均改为带 `_guess` 的语义名，并在注册器、构造器、
  progress 判定点加入四端校准注释。

## 验证

- Web debug 完整构建通过最终 `index.html`/Wasm 链接和
  `sync_prealloc_memory`。
- Wasmtime-headless debug 构建通过；构建进程在工具时限后退出，但紧接着的
  同一构建命令报告 `ninja: no work to do.`，确认输出已完成且一致。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用当前 Web 目标的真实
  Emscripten 定义、include 和 ABI 参数，并加入仓库准备好的 Catch2/test config，
  执行 `-fsyntax-only` 成功；唯一诊断为既有 `_tss` literal-operator 弃用警告。
- 两个构建树的最终即时重建均报告 `ninja: no work to do.`。
- `git diff --check` 成功；只有仓库现有的 LF/CRLF working-copy 警告。
- 四份改进后的 IDB 均已保存，包含本纵切面的 32 个访问器语义名及注册、构造、
  progress 注释。
