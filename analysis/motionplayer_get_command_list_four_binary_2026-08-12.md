# motionplayer `getCommandList` 四二进制对照（2026-08-12）

## 结论

四个参考二进制共同证明，`Player::getCommandList()` 不是临时拼装一份互不关联的
TJS 数据树，而是一条两阶段序列化流水线：

1. 先建立两个仅借用指针的 render-item `std::vector`，并对 main list 排序；item
   本体仍由各 MotionNode 长期持有。
2. 第一遍遍历 **全部** main item，为每个 item 新建命令 Dictionary，并替换 item
   上持久保存的 `tTJSVariant`；此时还不做输出过滤。
3. 第二遍才过滤不可输出 item，补写 `stencilChain`，并把同一个持久命令对象的别名
   追加到一个每次调用都新建的结果 Array。

`EmotePlayer::getCommandList()` 只是从接收者中取出内嵌 Player，并原样返回上述结果。
两个类均通过普通的零参数、返回 `tTJSVariant` 的 typed `NCB_METHOD` 暴露；没有 raw
callback，也没有“必须恰好零个参数”的额外检查。

本轮证据全部来自当前会话中对下列四个 IDB 的宽字符串搜索、交叉引用、重新反编译、
指令/伪代码对照和 NCB 模板链追踪，不沿用旧 `libkrkr2.so` 地址注释：

- Android ARM64：`Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- Android ARMv7：`Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- iOS ARM64：`Kirikiroid2_1.3.9_iOS_arm64`
- iOS ARMv7：`Kirikiroid2_1.3.9_iOS_armv7`

## 名称、注册点和公开回调

普通 IDA 字符串搜索没有找到该宽字符串；本轮按 UTF-16LE 字节序列搜索
`getCommandList\0`，然后分别从 Player 和 EmotePlayer 注册器追到回调。

| 目标 | 方法名字符串 | Player 注册引用 / 回调 | EmotePlayer 注册引用 / 回调 |
|---|---:|---:|---:|
| Android ARM64 | `0x14D4204`（共享） | `0x6D60CC` / 共享 body `0x6D0E2C` | `0x67E93C`、`0x67E944` / `0x67F900` |
| Android ARMv7 | `0xD84B8A`（共享） | `0x59875A`、`0x598766` / `0x595FF0` | `0x561924`、`0x56192C` / `0x5623DE` |
| iOS ARM64 | Player `0x10195CF1E`；Emote `0x1019609B4` | `0x1001251B0` / `0x100121EB0` | `0x1001B5AF0` / `0x1001B65D4` |
| iOS ARMv7 | Player `0x174F282`；Emote `0x1752D18` | `0x1243F6`、`0x1243FC`、`0x124408` / `0x120CF8` | `0x1B56EC`、`0x1B56F2`、`0x1B56FE` / `0x1B644C` |

接收者到内嵌 Player 的 ABI 偏移为：Android ARM64 `+1064`、Android ARMv7
`+532`、iOS ARM64 `+696`、iOS ARMv7 `+348`。其共同源级关系是：

```cpp
tTJSVariant EmotePlayer::getCommandList() {
  return player().getCommandList();
}
```

### Android ARM64 的非连续函数块

Android ARM64 不是两个普通独立函数。`0x67F900` 只有：

```asm
LDR X0, [X0,#0x428]
B   loc_6D0E2C
```

IDA 将远端 `0x6D0E2C` Player 序列化器作为这个短 Emote 入口的 non-contiguous
chunk；Player 注册器则直接引用该远端块。该事实说明编译器合并了公共尾部，不能据此
推导源代码中存在共享的静态自由函数。本轮在 IDB 中保留该真实 chunk 拓扑，没有为了
获得整齐的函数列表而强行拆分。

## 两阶段容器和所有权模型

四端共同的源级轮廓如下：

```cpp
tTJSVariant Player::getCommandList() {
  std::vector<PreparedRenderItem *> main;
  std::vector<PreparedRenderItem *> aux;
  prepareRenderItems(main, aux);
  sort(main.begin(), main.end(), nativeComparator);

  for (PreparedRenderItem *item : main)
    item->commandVariant = buildFreshCommand(*item);

  tTJSVariant result = createFreshArray();
  for (PreparedRenderItem *item : main) {
    if (item->skipFlag0 || item->rawFlag16 || item->opacity == 0)
      continue;
    item->commandVariant["stencilChain"] = buildStencilChain(*item);
    result.push(item->commandVariant);
  }
  return result;
}
```

这里各层所有权必须区分：

- `PreparedRenderItem` 是 node-owned 持久对象；连续调用会复用 item 地址。
- main/aux 是调用栈上的 `std::vector<PreparedRenderItem *>`，只借用指针，不释放
  item。
- 每次调用都会为 main 中每个 item 新建命令 Dictionary，并赋值替换
  `item.commandVariant`；旧命令对象按普通 Variant 引用计数释放。
- 即使 item 随后被过滤，它本轮新建的 `commandVariant` 仍留在持久 item 上。
- 结果 Array 中放入的是 `item.commandVariant` 的引用计数别名，不克隆 Dictionary。
- `stencilChain` 中的 `mesh` 同样引用父 item 或父 item 的 childItems 所持有的命令
  对象，不生成命令副本。
- 即使没有 motion/item，也仍创建并返回一个新的空 Array；相邻两次调用得到不同
  Array 对象。

## 命令 Dictionary 的精确构造顺序

第一遍对每个 main item 先创建主 Dictionary，然后按下列顺序写入属性。这个顺序不仅
影响可观察的成员插入过程，也决定中途异常时已经建立的对象集合、AddRef/Release
轨迹和局部对象逆序析构次序。

| 顺序 | 属性 | 值 / 对象来源 |
|---:|---|---|
| 1 | `key` | item 上持久命令 key Variant |
| 2 | `id` | layer id Integer |
| 3 | `src` | source path/string Variant |
| 4 | `coordinate` | coordinate mode Integer |
| 5 | `opacity` | 原始 opacity Integer |
| 6 | `blendMode` | blend mode Integer |
| 7 | `coord` | 新 Array，三个 `Real`，来自 command-coordinate 三元组 |
| 8 | `mtx` | 新 Array，四个 `Real`，来自 2×2 command matrix |
| 9 | `color` | 新 Array，四个 packed-color `Integer` |
| 10 | `originX` | Real |
| 11 | `originY` | Real |
| 12 | `triPriority` | Integer |
| 13 | `clipRect` | 有效 viewport 的新 Dictionary，或 Void |
| 14 | `meshTransform` | mesh type Integer |
| 15 | `bezierPatch` 或 `compositeMesh` | 仅相应 mesh 分支存在 |

`coord`、`mtx`、`color` 三个局部 Variant 一直活到单个 command 构造末尾，并按
`color → mtx → coord` 的逆序释放。主 Dictionary 在它们之前构造、在它们之后释放；
因此旧 Web 代码“先建所有数组/clip，再建主 Dictionary”虽然能产生近似相同的最终
内容，却不是相同的异常安全和引用计数时间线。

### `clipRect`

有效条件严格为：

```text
right >= left && bottom >= top
```

有效时创建分支局部 Dictionary，并依次写入：

```text
left, top, right, bottom, width=(right-left), height=(bottom-top)
```

随后把该对象设为 `clipRect`。无效时仍写入 `clipRect`，但值为一个分支局部 Void
Variant。两种分支的局部 Variant 都在写 `meshTransform` 之前析构；clip Dictionary
不会跨入 mesh payload 分支。

### mesh payload

- `meshType <= 1`：创建分支局部 `bezierPatch` Dictionary。
  - `patch` 是把控制点按 `x0,y0,x1,y1,...` 展平的新 Array。
  - `division = min(int64(meshDivisionRatio * commandPatchDivision), 50)`。
  - 只有上限 50，没有下限钳制；负 mesh type 也走此分支。
- `meshType == 2`：创建分支局部 `compositeMesh` Dictionary。
  - `vtx` 是按 `x0,y0,x1,y1,...` 展平的新 Array。
  - 随后写 `divx`、`divy`。
- 其他 mesh type：只保留 `meshTransform`，不创建上述任一 payload 属性。

payload Dictionary 和它的数组均为各自分支局部对象；完成对主命令的 SetValue 后按
普通 Variant/dispatch 引用计数释放局部所有者。

最后，主 Dictionary 的同一个 dispatch 同时作为 TJS Object 和 objthis 构造
Variant，并覆盖 item 上的持久 `commandVariant`。

## 第二遍过滤和 `stencilChain`

第二遍保持 main list 的排序顺序。过滤条件是三项 OR：

```text
skipFlag0 || rawFlag16 || opacity == 0
```

过滤只控制是否进入返回 Array，不撤销第一遍已经构造并保存的命令对象。负 opacity
和大于常规 8-bit 范围的 opacity 均不因这里的比较而被过滤。

未过滤 item 的 `stencilChain` 行为：

- 没有 `parentItem`：把 Void 写入命令的 `stencilChain`。
- 有父链：新建 Array，从直接父 item 开始沿 `parentItem` 一直向上。
  - 每一层新建 link Dictionary，先写 `type = parent.stencilComposite`。
  - 若 `type & 4` 非零，再新建 `mesh` Array，把
    `parent.childItems[0..n)` 的 `commandVariant` 按 vector 原始顺序全部追加进去；
    不做输出过滤，也不克隆命令。
  - 否则 `mesh` 直接别名到 `parent.commandVariant`。
  - link 依次追加到 chain，因此 chain 顺序是近父到远祖。
- 把 chain/Virtual Variant 写回当前 item 的持久命令 Dictionary，再把该同一命令
  Variant 追加到结果 Array。

因此引用关系可概括为：

```text
MotionNode --owns--> PreparedRenderItem --owns Variant--> command Dictionary
                         ^       ^                         ^
                         |       |                         |
                   main/aux borrow pointers        result/stencil alias
```

## PreparedRenderItem 的 ABI 证据

以下偏移仅用于对应四端反编译，不应复制进可编译源码注释。64-bit 两端共同观察到的
关键布局：

| 字段 | 偏移 |
|---|---:|
| filter bytes (`rawFlag16` / `skipFlag0`) | `+16` / `+17` |
| `commandSrc` | `+8` |
| `childItems` begin/end | 约 `+24` / `+32` |
| `blendMode` / `id` | `+48` / `+52` |
| 2×2 command matrix | `+72,+80,+88,+96` |
| command coordinate | `+104,+112,+64`（投影/参数次序） |
| origin | `+120,+128` |
| packed colors | `+168,+172,+176,+180` |
| viewport | `+200,+204,+208,+212` |
| opacity / coordinate / triPriority | `+232/+236/+240` |
| stencil composite / command key | `+244/+248` |
| `parentItem` | `+264` |
| mesh divs / mesh type | `+272/+276/+280` |
| persistent `commandVariant` | `+284` |
| composite mesh points | `+344` |
| patch division / patch points | `+368/+376` |

32-bit 两端共同的关键边界为：filter bytes `+8/+9`、opacity `+208`、stencil
composite `+220`、command key `+224`、parentItem `+232`、mesh divY `+240`、
mesh type `+244`、persistent command Variant `+248`。child vector 的 begin/end
位于其 ABI 对应的 `+16/+20` 一带。其余数组/字符串/vector 的内部布局会受
libc++/目标 ABI 影响，不能把某个端的字节偏移当作跨端源结构名称。

## typed NCB 调用链

| 目标 / 类 | create | allocate | function ctor | `FuncCall` | no-arg Variant invoke |
|---|---:|---:|---:|---:|---:|
| Android ARM64 / Player | Player 注册器内联；vtable `0x1A1E178` | 同左 | 同左 | `0x6F7860` | `0x6F4A2C` |
| Android ARM64 / EmotePlayer | `0x67EF34`；vtable `0x1A16588` | 内联 | 内联 | `0x68B870` | `0x68B988` |
| Android ARMv7 / Player | `0x5B3DF4` | `0x5B3E28` | `0x5B3E64` | `0x5B3ECC` | `0x5B1100` |
| Android ARMv7 / EmotePlayer | `0x56BB38` | `0x56BB6C` | `0x56BBA8` | `0x56BC10` | `0x56BCBC` |
| iOS ARM64 / Player | `0x10014A5B4` | `0x10014A608` | `0x10014A66C` | `0x10014A704` | `0x100146EA0` |
| iOS ARM64 / EmotePlayer | `0x1001C7EA4` | `0x1001C7EF8` | `0x1001C7F5C` | `0x1001C7FF4` | `0x1001C80A8` |
| iOS ARMv7 / Player | `0x14B8CC` | `0x14B8F4` | `0x14B9B4` | `0x14BAB4` | `0x1476CC` |
| iOS ARMv7 / EmotePlayer | `0x1C57A8` | `0x1C57D0` | `0x1C5890` | `0x1C5990` | `0x1C5A14` |

四端模板行为一致：

1. function dispatch 收到非空 membername 时返回 `TJS_E_MEMBERNOTFOUND`（`-1001`）。
2. objthis 为空时返回 `TJS_E_NATIVECLASSCRASH`（`-1008`）。
3. 在上述两项通过后，若 result 非空，先调用 Variant clear。
4. 随后才检查 argc；`argc < 0` 返回 `TJS_E_BADPARAMCOUNT`（`-1004`），因此这个
   人为可达的负计数失败会留下 Void result。
5. 任意 `argc >= 0` 都被接受；额外参数完全不读取。
6. 通过 NCB class ID 从 objthis 取原生实例，然后调用
   `tTJSVariant (Class::*)()`。
7. 成员函数返回值先形成一个临时 Variant，再 copy-construct 第二个临时；仅当
   result 非空时 copy-assign 给 result；最后依次析构两个临时。

Android ARM64 的 invoke helper 返回一个布尔式成功值，外层 `FuncCall` 再规格化成
TJS status；其余 ABI 的 helper 直接返回零式成功。这个机器级差异没有改变 TJS
可观察结果。

注册阶段构造的是普通堆 NCB Function dispatch，内部嵌入 method facade 并检查成员
函数指针。没有 raw-callback 专用闭包，也没有由脚本调用参数拥有的额外对象。

## 与本地旧实现的差异及修正

本地 serializer 的字段内容、两遍遍历、过滤、持久 item 别名和 stencil chain 已与
参考实现基本一致，但构造顺序存在一处实质偏差：旧代码先创建 `coord`、`mtx`、
`color` 和 `clipRect`，之后才创建主 command Dictionary。四端则一致地先创建主
Dictionary、写六个标量，再逐个创建三组数组；clip 更是 `triPriority` 之后的分支
局部对象，并在 mesh 分支之前释放。

本轮已在 `cpp/plugins/motionplayer/PlayerLayerQuery.cpp` 按四端顺序重排这些局部对象，
并把相关说明改为无地址的源级生命周期注释。另同步清理：

- `cpp/plugins/motionplayer/EmotePlayer.cpp` 中 `getCommandList` 的旧单二进制回调地址；
- `cpp/plugins/motionplayer/MotionDispatch.h` 中这一组 member-hint 的旧地址说明。

新增单元测试锁定：

- 无 item 时每次直接调用都返回不同的新空 Array；
- EmotePlayer wrapper 返回同类空 Array 结果；
- typed NCB 在 `argc == 1` 时忽略额外参数并成功返回 Array；
- typed NCB 在人为传入 `argc == -1` 时返回 `TJS_E_BADPARAMCOUNT`，且 result 已先被
  清成 Void。

## IDB 回填

四个 IDB 均已回填 Player/EmotePlayer 回调、typed NCB create/allocate/constructor/
`FuncCall`/invoke 链的 `_guess` 名称和语义注释，并对全部已改名函数强制刷新 Hex-Rays
缓存。Android ARM64 的远端共享 body 以 chunk 行注释记录。四个数据库随后均原位
保存成功。

## 验证

- Web Debug `motionplayer` 静态库：通过。
- Wasmtime Debug `motionplayer` 静态库：通过。首次高并发构建出现一次
  `PlayerMotionLoad.cpp.o` 子进程失败且并行日志未保留诊断；同一单目标立即以 `-j1`
  重编成功，随后整个静态库以 `-j1` 完整通过，故没有把首次失败掩盖为成功。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用当前 Web Debug 的真实
  Emscripten 定义/头路径执行 `-fsyntax-only`：通过；唯一诊断是仓库既有的 `_tss`
  deprecated-literal-operator warning。该项目配置没有生成可直接执行的 motionplayer
  单测程序，因此这里只声明翻译单元编译，不把 syntax-only 冒充成运行时测试。
- Web Debug `index.html` 完整链接：通过；只有既有 pthread/memory-growth、JSPI 和
  JS library warning。
- Wasmtime Debug `krkr2_wasmtime_guest` 全量构建、链接与 exnref 异常转换：通过；
  只有既有 `_tss`、`imagepacker.h` `nodiscard` 和 Emscripten warning。
- 本轮旧 `libkrkr2.so` 的 getCommandList 地址/符号扫描无命中；同时移除了相关测试
  中残留的 ABI 字节偏移注释。
- `git diff --check`：退出码 0；输出只有工作树 LF/CRLF 转换提醒，没有 whitespace
  error。
