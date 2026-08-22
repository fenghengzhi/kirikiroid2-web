# MotionPlayer camera query surface：四参考二进制联合证据（2026-08-13）

## 结论

本专题重新以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，追踪 Player 的
`cameraPosition`、`cameraTarget`、`cameraFOV` 和 `cameraAlive` 四个公开属性。

四端共同证明，本地原有的四份独立脚本侧状态是过时且错误的：

- `cameraPosition` 和 `cameraTarget` 不是可写、长期持有的 `tTJSVariant`；
- 两个 getter 每次调用都创建一个新的 TJS Array，按 X、Y、Z 顺序装入三个
  `tvtReal`，把 Array Variant 复制到返回槽，再释放函数内的临时 owner；
- `cameraFOV` 读取 CameraNode 更新链维护的 double，构造初值为精确的 `0.2`，
  不是旧本地字段的 `60.0`；
- `cameraAlive` 读取同一更新链每帧维护的 `hasCamera` byte，不是另一个永不更新的
  bool；
- 四个 NCB 属性都是 read-only，注册项的 setter 均为空。

本轮因此删除 `_cameraTarget`、`_cameraPosition`、`_cameraFOV`、`_cameraAlive`
四份重复状态以及两个错误的内部写接口，并把查询公开面接到既有的
`_cameraPos*`、`_cameraTarget*`、`_cameraFov`、`_hasCamera` 数据流。

## 四端函数与注册映射

### Getter

| 属性 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `cameraPosition` | `0x6C9C54` | `0x592174` | `0x10011CA9C` | `0x11B3C0` |
| `cameraTarget` | `0x6C9AF4` | `0x592100` | `0x10011CA20` | `0x11B2E8` |
| `cameraFOV` | `0x6D6B64` | `0x598FDA` | `0x1001256B8` | `0x1248DC` |
| `cameraAlive` | `0x6D6B6C` | `0x598FE4` | `0x1001256C0` | `0x1248E6` |

四端 IDB 中上述函数已分别命名为：

```text
Player_getCameraPosition_guess
Player_getCameraTarget_guess
Player_getCameraFOV_guess
Player_getCameraAlive_guess
```

### UTF-16 名称与 read-only 注册

| 目标 | `cameraTarget` 注册 | `cameraPosition` 注册 | `cameraFOV` 注册 | `cameraAlive` 注册 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D4D64` | `0x6D4DC8` | `0x6D4E2C` | `0x6D4E98` |
| Android armv7 | `0x598290` | `0x5982AA` | `0x5982C4` | `0x5982DE` |
| iOS arm64 | `0x100124A7C` | `0x100124AA4` | `0x100124ACC` | `0x100124AF4` |
| iOS armv7 | `0x123D6C` | `0x123D8E` | `0x123DB0` | `0x123DD2` |

对应 UTF-16 字符串地址如下，均由宽字符串搜索后沿 xref 回到同一连续 NCB 注册区：

| 名称 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `cameraTarget` | `0x14D64E4` | `0xD85DF2` | `0x10195CC08` | `0x174EF6C` |
| `cameraPosition` | `0x14D64FE` | `0xD85E0C` | `0x10195CC22` | `0x174EF86` |
| `cameraFOV` | `0x14D651C` | `0xD85E2A` | `0x10195CC40` | `0x174EFA4` |
| `cameraAlive` | `0x14D6530` | `0xD85E3E` | `0x10195CC54` | `0x174EFB8` |

每个属性的注册对象都含 getter function pointer 和 null setter。这里不存在“脚本层
只读但 C++ 内部仍有同名 setter”的第二条状态通道；旧本地 setter 是从先前不完整
单端推断中虚构出来的。

## Array getter 的对象拓扑与生命周期

四个 `cameraPosition` getter 与四个 `cameraTarget` getter 虽因 ABI 展开方式不同，
但具有相同的源码级形状：

```cpp
tTJSVariant Player::getCameraPosition() const {
    auto array = createTJSArray();
    array.items.push_back(tTJSVariant(tvtReal, cameraX));
    array.items.push_back(tTJSVariant(tvtReal, cameraY));
    array.items.push_back(tTJSVariant(tvtReal, cameraZ));
    copyVariantToReturnSlot(array.value);
    destroyLocalArrayVariantOwner(array);
}
```

`cameraTarget` 只把数据源换成 target X/Y/Z。关键边界为：

1. Array 在 getter 内无条件创建；没有“无相机则返回 Void/null”的分支。
2. 元素严格按 X、Y、Z 插入；不是 Dictionary，也没有 `x/y/z` 字符串 key。
3. 三项都按 TJS real 构造，即使值是整数形式的 `0.0`，类型仍为 `tvtReal`。
4. 每次调用都获得不同的 Array dispatch identity；Player 不缓存该 Array。
5. 返回值通过 Variant 引用计数保留 Array；函数退出时只销毁局部 owner，不使已返回
   Array 失效。
6. 返回 Array 与 Player 后续状态没有 live view 关系；以后 CameraNode 改写 Player
   doubles 时，先前返回的 Array 不会跟着变化。

Android arm64 的 getter 读取 Player `+72/+80/+88` 和 `+96/+104/+112`；
Android armv7 为 `+40/+48/+56` 和 `+64/+72/+80`；iOS arm64 为
`+48/+56/+64` 和 `+72/+80/+88`；iOS armv7 为 `+24/+32/+40` 和
`+48/+56/+64`。这些偏移只用于反编译核验，不进入 portable C++ 注释。

## Scalar getter 与 producer

`cameraFOV` getter 是一条直接 double load/return：

| 目标 | Player 字段 |
|---|---:|
| Android arm64 | `+1104` |
| Android armv7 | `+760` |
| iOS arm64 | `+992` |
| iOS armv7 | `+692` |

`cameraAlive` getter 是一条 byte load/return：

| 目标 | Player 字段 |
|---|---:|
| Android arm64 | `+1100` |
| Android armv7 | `+752` |
| iOS arm64 | `+988` |
| iOS armv7 | `+688` |

四端 producer 分别为：

| 目标 | CameraNode producer | Player 构造函数 |
|---|---:|---:|
| Android arm64 | `0x6BAE08` | `0x6CC110` |
| Android armv7 | `0x587748` | `0x5935C4` |
| iOS arm64 | `0x1001108C4` | `0x10011EC04` |
| iOS armv7 | `0x10E048` | `0x11D488` |

构造链共同设置：

```text
hasCamera = false
cameraFOV = 0.2                 // exact double 0x3FC999999999999A
cameraPosition = {0.0, 0.0, 0.0}
cameraTarget   = {0.0, 0.0, 0.0}
```

CameraNode producer 每次进入先清 `hasCamera`，再按节点顺序选择第一条 active type-5
节点。命中时立即置 `hasCamera=true`，且二维 camera offset 与 `cameraActive` gate 无关；
只有 `cameraActive` 开启时才写 FOV 和 position XYZ，target 查找成功时才写 target XYZ。
没有 active CameraNode 时，本帧 getter 返回 `cameraAlive=false`，其余数值继续保留旧值。
完整 target lookup、float 窄化和跨帧保持细节见
`motionplayer_camera_node_four_binary_2026-08-12.md`。

因此四个公开 getter 并不是独立模型：它们是同一 CameraNode 状态机的只读投影。

## 与 stereovision camera position setter 的分离

`setStereovisionCameraPosition(x,y,z)` 的三 double 属于 post-prepare perspective pass
的投影原点/平面，四端物理位置均与 CameraNode position/target 六 doubles 不同。

```text
CameraNode position XYZ  -> cameraPosition getter
CameraNode target XYZ    -> cameraTarget getter
stereovision origin XYZ  -> prepared-item perspective pass
```

setter 只直接写第三组字段，不创建 Array，也不触碰前两组字段。因此调用
`setStereovisionCameraPosition` 后，`cameraPosition`/`cameraTarget` 仍返回 CameraNode
状态；它们绝不能被 setter 参数覆盖。详细投影 pass 见
`motionplayer_prepared_render_item_camera_projection_four_binary_2026-08-13.md`。

## 边界行为

- Player 刚构造且尚未运行 CameraNode 时，position/target 各返回三个 `tvtReal 0.0`，
  FOV 返回 `0.2`，alive 返回 false。
- 没有 camera 时仍创建三元素 Array，不返回空 Array、Void、null 或上一份 Array。
- `cameraActive` 关闭但存在 active CameraNode 时，alive 为 true，二维 offset 更新；
  FOV、position、target 仍可能是此前保留状态或构造初值。这是 producer 的明确 gate，
  不应在 getter 中追加 alive/cameraActive 判定。
- target 空或 miss 时 target XYZ 跨帧保留；getter 原样暴露保留值，不用 position、根
  坐标或零替代。
- getter 不做 finite 检查或正规化；NaN、infinity、signed zero 都以 `tvtReal` 原样进入
  Array。
- getter Array 的引用计数和临时对象销毁遵守普通 TJS Variant 所有权；不得用借用的
  栈对象、静态共享 Array 或 Player-owned mutable Array 近似。

## 本地修复与验证点

源码修复：

- `Player.h` 删除两个错误 setter 及四份重复字段；两个 Array getter 改为 out-of-line，
  scalar getter 改读 `_cameraFov`/`_hasCamera`；
- `PlayerLayerQuery.cpp` 分别实现两个 fresh Array getter，明确逐项追加三个
  `tjs_real`；
- `motionplayer-dll.cpp` 新增默认值、三元素计数、元素类型、fresh identity、
  stereovision setter 不串写和 CameraNode 的 cameraActive/stereovisionActive 独立
  gate 确定性测试；旧的“持久字符串 cameraPosition”断言已删除。

四份 IDB 已写入 getter 名称、注册 read-only 边界、producer/constructor 数据流注释，
并保存到各自数据库。

验证结果：

- `Web Debug Build` 完整编译并链接成功；
- `Wasmtime Headless Debug Build` 完整编译并链接成功；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实
  Emscripten defines、include、ABI 参数，并加入既有 `out/syntax-check` Catch2 与
  `test_config.h`，执行 `-fsyntax-only` 成功；唯一诊断为仓库既有 `_tss`
  literal-operator 弃用警告。当前配置不提供可直接运行的原生 Catch2 executable，
  因此这里只声明完整测试翻译单元的编译验证，不把它冒充成运行时执行。
- cameraActive gate 纠正后的并行复核一度超过外层等待窗口；在旧 Ninja 尚未退出时
  错误地再次启动同一输出目录链接，分别触发临时 wasm metadata assertion 和
  `index.wasm` 写竞争。原始构建随后正常完成；进程退出后对 Web Debug、Wasmtime
  Headless 串行复核均为 `ninja: no work to do`。这是验证调度竞争，不是源码失败。
