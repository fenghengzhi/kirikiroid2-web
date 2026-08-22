# `Motion.SourceCache.bufLayer` 四参考二进制闭环（2026-08-13）

## 结论

当前 C++ 的核心结构与四个参考实现一致：`SourceCache` 构造时分别持有
`owner`、`owner.primaryLayer` 和一次性创建的 `bufLayer` 三个
`tTJSVariant` 闭包；公开属性只返回第三个字段的 `CopyRef`。显式
`clearCache()` 只失效并释放 `std::list<Entry>` 中的缓存 Layer，随后清零缓存
字节数，既不失效也不替换 `bufLayer`。`ResourceManager` 公开继承这份状态，
它的成员表直接重注册同一个 getter 回调，并没有派生类转发函数或第二个缓冲层
字段。

继续追踪析构链后发现并修正了一处实质偏差：参考实现的对象析构不调用公开
`clearCache()`，而是由 `std::list` 析构函数直接释放 entry Layer，因此没有
脚本可观察的 `Invalidate` 调用。修改还包括移除过时的 Android 单端地址注释、
明确对象所有权，并增加只读性、闭包身份、清缓存保持、析构后外部别名存活和
“显式清缓存与析构不同”测试。

## 输入基线

| 参考文件 | SHA-256 |
| --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `05E2FF4C77F1561608AD7703153D2FB09855BF223237A85DC2267FFF1388564F` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `A15C238EC6F21C17D0889B064AE1AD47EC85B4F1530A3611F206B7190FF456AF` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3` |

iOS 两项指向同一个 fat Mach-O 文件，但以下证据分别来自其 arm64 与 armv7
slice 的独立 IDA 会话。

## 字符串与成员表

`bufLayer` 是宽字符串；四端唯一命中位置如下。

| ABI | 宽字符串地址 | `SourceCache` registrar | `ResourceManager` registrar |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x14D5866` | `0x6A5988` | `0x6A8C9C` |
| Android armv7 | `0xD853D2` | `0x57B0DC` | `0x57C3A8` |
| iOS arm64 | `0x10195BCC4` | `0x100100F90` | `0x100102E88` |
| iOS armv7 | `0x174E028` | `0xFE12A` | `0x1002FC` |

两个类表在各 ABI 中都登记完全相同的 getter：

| ABI | getter | `bufLayer` 字段 |
| --- | ---: | ---: |
| Android arm64 | `0x6A58DC` | `this + 0x28` |
| Android armv7 | `0x57B060` | `this + 0x18` |
| iOS arm64 | `0x100100F84` | `this + 0x28` |
| iOS armv7 | `0xFE11A` | `this + 0x18` |

四个 getter 都调用普通 `Variant CopyRef` 路径，而不是把裸 dispatch 指针借出。
64 位属性描述符中 getter 之后的 setter/调整量字均为零；两个 32 位实现也在
登记前显式把对应参数与存储槽清零。因此边界是 getter-only，写入必须返回
`TJS_E_ACCESSDENYED`。

`ResourceManager` 的 registrar 重复使用同一个函数地址尤其关键：如果原实现是
组合而非继承，或者派生类保存了第二份状态，这里至少需要一个不同的 this 调整量
或转发 callback；四端均不存在这种形状。

## 构造数据流与字段布局

| ABI | `SourceCache` ctor | `ResourceManager` ctor | owner | primaryLayer | bufLayer | current bytes | limit | list sentinel |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6A4CD4` | `0x6A5CAC` | `+0x00` | `+0x14` | `+0x28` | `+0x3C` | `+0x40` | `+0x48` |
| Android armv7 | `0x57AADC` | `0x57B1EC` | `+0x00` | `+0x0C` | `+0x18` | `+0x24` | `+0x28` | `+0x2C` |
| iOS arm64 | `0x10010071C` | `0x100101158` | `+0x00` | `+0x14` | `+0x28` | `+0x3C` | `+0x40` | `+0x48` |
| iOS armv7 | `0xFD824` | `0xFE254` | `+0x00` | `+0x0C` | `+0x18` | `+0x24` | `+0x28` | `+0x2C` |

四端构造顺序一致：

1. `owner` 以普通 `Variant` CopyRef 进入 `this + 0`。
2. `primaryLayer` 与 `bufLayer` 槽先初始化为 Void，缓存字节数置零，保存
   `cacheSize` 的低 32 位，并建立自环 list sentinel。
3. 对 `owner` 做严格对象转换，以动态 `PropGet("primaryLayer")` 取值，并
   CopyAssign 到独立的 `primaryLayer` 字段；没有 native-Layer fallback。
4. 从全局对象解析 `Layer`，以恰好两个参数 `{owner, primaryLayer}` 调用
   `CreateNew`。Android arm64 在 `0x6A4DE0`，Android armv7 helper
   `0x57AC1C` 在 `0x57AC3C`，iOS arm64 helper `0x1001008A8`，iOS armv7
   helper `0xFDA14` 均可见相同调用形状。
5. 新 Layer 返回为 Object/ObjThis 相同的闭包，CopyAssign 到 `bufLayer` 字段。
6. 派生构造函数首先直接调用上述基类构造函数。`SourceCache` ctor 的唯一正常
   代码引用也正是对应的 `ResourceManager` ctor。

这解释了源码里三个 `tTJSVariant` 成员为何不能合并：它们是三个独立引用所有者，
构造临时量释放后仍各自持有闭包；getter 结果又增加新的外部所有者。

iOS 两端还显示了明确的构造异常清理段：已经初始化到哪个阶段，就逆序释放相应
临时 `Variant`、list sentinel、`bufLayer`、`primaryLayer` 与 `owner`。Android
arm64 的 IDA 将若干 landing-pad/chunk 合并进构造函数，故本轮没有为了好看的
函数边界强行拆分它；这不影响正常路径和字段所有权结论。

## `clearCache()` 的容器与边界行为

| ABI | callback/entry | list sentinel | current bytes |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6A5818` | `this + 0x48` | `this + 0x3C` |
| Android armv7 | `0x57B018` | `this + 0x2C` | `this + 0x24` |
| iOS arm64 | `0x100100F10` | `this + 0x48` | `this + 0x3C` |
| iOS armv7 | `0xFE0D4` | `this + 0x2C` | `this + 0x24` |

算法是标准 `std::list` sentinel 遍历：从 sentinel.next 开始，抵达 sentinel
结束。每个节点若 `entry.layer.Type() == tvtObject`，就通过 Layer dispatch 的
`Invalidate` 虚表槽调用 `Invalidate(0, nullptr, nullptr, layer)`；随后逐节点
析构/释放，恢复空自环，最后把 current bytes 写零。

该路径完全不读取或写入：

- `owner`；
- `primaryLayer`；
- `bufLayer`；
- cache limit；
- `ResourceManager` 自己的 PSB module map。

所以空列表调用仍是合法幂等操作，并且 `clearCache()` 前后 `bufLayer` 的 Object
和 ObjThis 身份保持不变。Android arm64 的 `0x6A5818` 被当前 IDA 数据库错误地
识别为 `SourceCache` ctor 的一个 function chunk；本轮保留这个真实入口并加注释/
bookmark，没有冒险重切可能影响异常 CFG 的函数范围。

## 析构与异常回滚不是 `clearCache()`

| ABI | `ResourceManager` destructor | constructor unwind |
| --- | ---: | ---: |
| Android arm64 | `0x6A5F74` | landing pads 分散在构造函数 chunks |
| Android armv7 | `0x57B2E4` | SJLJ landing pads 分散在构造路径 |
| iOS arm64 | `0x10010126C` | `0x10010120C` |
| iOS armv7 | `0xFE3B4` | `0xFE344` |

正常析构的四端序列一致：

1. `ResourceManager` 析构体先清 module `unordered_map`；
2. 自动析构 used-layer-id `std::set`、random-generator Variant 与 map 自身存储；
3. 进入 `SourceCache` 基类 teardown，由 list destructor 直接逐节点析构 `Entry`；
4. 再按 `bufLayer -> primaryLayer -> owner` 顺序释放三个 Variant。

关键反证是该路径完全没有调用四端的 public `clearCache` callback。list 节点中的
Layer Variant 会正常 Release，但不会先经 Layer vtable 的 `Invalidate` 槽。这与
显式 `clearCache()` 的可观察语义不同。旧端口的
`SourceCache::~SourceCache(){ clearCache(); }` 因而多发了脚本回调，现已改为
默认析构，让普通成员析构完成原生顺序。

iOS 构造异常回滚同样显示派生成员与基类成员的逆序释放，但也不调用 public
`clearCache()`；这是“析构/回滚使用 C++ 成员 teardown，而非复用脚本方法”的第二
条独立证据。

## 渲染消费链

| ABI | render function | `bufLayer` 属性取值点 |
| --- | ---: | ---: |
| Android arm64 | `0x6C4820` | `0x6C4FE8` / `0x6C4FFC` |
| Android armv7 | `0x58E2CC` | `0x58E866` / `0x58E874` |
| iOS arm64 | `0x1001186E0` | `0x100118C34` |
| iOS armv7 | `0x11653C` | `0x1170AA` / `0x1170B6` |

四端不是直接按 native 偏移读取 `bufLayer`，而是保留脚本可观察的动态边界：

1. 从 Player 中取得 ResourceManager/SourceCache 闭包并 CopyRef 到临时 Variant；
2. 解包该临时量的 dispatch；
3. 对它执行 `PropGet("bufLayer")`；
4. 把属性结果 CopyRef 到第二个 Variant，并释放属性临时量；
5. 再把第二个 Variant 严格转换/解包为 Layer dispatch；
6. 此后才读取目标 `width`/`height` 并执行缓冲区合成；
7. 离开路径时按声明的逆序释放 raw-dispatch owner、`bufLayer` Variant 和资源管理器
   Variant。

这支持 `PlayerRenderExecute.cpp` 保留“三层 owner”而不简化成借用裸指针。它也说明
`bufLayer` getter 的闭包身份和生命周期是渲染数据流的一部分，而不仅是脚本 API
表面兼容。

## IDA 数据库改进

四份 IDB 均已保存。每端新增或确认以下 `_guess` 名称：

- `SourceCache_ctor_guess`
- `SourceCache_clearCache_guess`（Android arm64 保持为带注释的 chunk entry）
- `SourceCache_getBufLayer_guess`
- `SourceCache_ncb_registerMembers_guess`
- `ResourceManager_ctor_guess`
- `ResourceManager_ncb_registerMembers_guess`

同时在构造、清缓存、getter、两个 registrar 和渲染取值点加入了字段/所有权注释与
bookmark。没有把尚未由符号证实的名字去掉 `_guess`。

## 端口与测试映射

- `SourceCache.h`：保留 `owner -> primaryLayer -> bufLayer -> counters -> list`
  源码顺序，记录三个闭包的独立所有权。
- `SourceCache.cpp`：构造一次 `bufLayer`，getter 按值返回，`clearCache()` 只处理
  entry list；析构改为默认成员析构，避免额外 `Invalidate`。
- `ResourceManager.h/.cpp`：明确同 callback 重注册代表公开基类，而非转发对象。
- `PlayerRenderExecute.cpp`：保留资源管理器 Variant、属性结果 Variant、Layer
  dispatch owner 的嵌套生存期。
- `motionplayer-dll.cpp`：新增 `SourceCache` 与 `ResourceManager` 两个类表下的
  read-only、重复 getter 身份、空列表 clear 保持，以及外部别名越过
  `SourceCache` 析构仍存活测试；另以可观测 Layer probe 区分显式 clear（一次
  `Invalidate`）和对象析构（零次 `Invalidate`），并删除旧的单端构造地址注释。

## 验证

- 复用 Web Debug `compile_commands.json` 中 `EmoteEngine.cpp` 的完整 Emscripten
  定义与头路径，并加入 `out/syntax-check` 的 Catch2/test-config 头，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；唯一
  诊断为仓库既有 `_tss` literal-operator 弃用 warning。
- `cmake --build out/web/debug --target motionplayer --parallel 1`：通过，最终复核
  `ninja: no work to do`。
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel 8`：
  guest wasm 完成链接及 `wasm-opt` 后处理；随后 `--parallel 1` 复核为
  `ninja: no work to do`。
- `cmake --build out/web/debug`：首次最终链接因 Windows 暂时占用现有
  `index.wasm` 返回 `permission denied`；确认编译进程结束后触发干净重链接，
  成功生成 `index.html/index.wasm` 并同步 shell prealloc 页数；最终复核为
  `ninja: no work to do`。该短暂文件锁不是源码诊断，也未被误计为一次成功。
- `git diff --check`：没有空白错误；输出仅为工作树现有 LF/CRLF 转换提示。

在继续审计析构并将 `SourceCache::~SourceCache()` 改回普通成员 teardown 后，上述
完整测试翻译单元、Web 默认目标和 Wasmtime guest 又各执行了一遍：Web 重新编译
`SourceCache.cpp`/`ResourceManager.cpp` 并成功链接；Wasmtime guest 完成重新链接与
`wasm-opt`；最后两者均再次返回 `ninja: no work to do`。

当前构建树没有可直接执行该 Catch 测试翻译单元的 native runner，因此上述测试被
完整编译验证，但没有把 syntax-only 误报为运行时已执行。
