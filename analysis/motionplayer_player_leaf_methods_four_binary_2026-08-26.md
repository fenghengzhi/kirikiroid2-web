# Player 低依赖方法组（四参考二进制，2026-08-26）

覆盖 member #69、#74–#77、#87–#90、#92。40 个 endpoint 均已 fresh
decompile + disassemble，统一命名/注释并保存。

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| processedMeshVerticesNum | `0x6CE3F8` | `0x594710` | `0x10011FDA8` | `0x11EA6C` |
| stop | `0x6D6E10` | `0x599118` | `0x100125938` | `0x124B2E` |
| setCameraOffset | `0x6D6E18` | `0x599120` | `0x100125940` | `0x124B36` |
| getCameraOffset | `0x6CDE90` shared tail | `0x59441C` | `0x10011F6EC` | `0x11E220` |
| releaseSyncWait | `0x6D6E28` | `0x59913A` | `0x100125954` | `0x124B54` |
| onAction | `0x6D6E30` | `0x599142` | `0x10012595C` | `0x124B5C` |
| onSync | `0x6D6E34` | `0x599144` | `0x100125960` | `0x124B5E` |
| onGroundCorrection | `0x6D6E38` | `0x599146` | `0x100125964` | `0x124B60` |
| onFindMotion | `0x6D6E40` | `0x599152` | `0x10012596C` | `0x124B6C` |
| setStereovisionCameraPosition | `0x6CD420` | `0x593F44` | `0x10011F54C` | `0x11E038` |

## 2. processedMeshVerticesNum

```cpp
uint32_t getProcessedMeshVerticesNum() const {
    uint32_t result = localProcessedCount;
    visitChildPlayerDispatches([&](Player *child) {
        result += child->getProcessedMeshVerticesNum();
        return true;
    });
    return result;
}
```

local counter 坐标：Android arm64 `+0x480`、Android armv7 `+0x328`、
iOS arm64 `+0x410`、iOS armv7 `+0x2E4`。

该方法复用已闭合的 shared visitor，而非独立树遍历：

- flat node 顺序；type 4 particle 先处理，type 3 nested motion 后处理；
- type 4 先持有一个 Array Dispatch，再读 count，并错误地读取 numeric index 0
  共 count 次，因此第一个 particle child 的递归计数可能被重复累加；
- type 3 child 是从 persistent Variant 借用的 native Player；没有 null guard；
- 每层都是 `uint32_t` 加法，overflow 按模 `2^32` wrap；没有 saturation；
- 无 cycle/recursion-depth guard。

Android 两端的捕获闭包使用 8/4-byte heap allocation；iOS 两端将引用 capture
放在 libc++ `std::function` small buffer。allocation 或递归异常不发布部分
result，但子调用此前产生的外部副作用仍不 rollback。

## 3. stop / releaseSyncWait

四端 body 都只有一次 byte store：

```cpp
void stop()            { playing = false; }
void releaseSyncWait() { syncWaiting = false; }
```

| 端 | playing | syncWaiting |
|---|---:|---:|
| Android arm64 | `+0x44B` | `+0x44A` |
| Android armv7 | `+0x2EF` | `+0x2EE` |
| iOS arm64 | `+0x3DB` | `+0x3DA` |
| iOS armv7 | `+0x2AF` | `+0x2AE` |

`stop` 不清 label、motion owners、cursor、pending stealth 或 sync state；
`releaseSyncWait` 不改 syncActive。两者都不递归 child。

## 4. cameraOffset

字段为两个 float32：

| 端 | x / y |
|---|---|
| Android arm64 | `Player+0x90/0x94` |
| Android armv7 | `Player+0x70/0x74` |
| iOS arm64 | `Player+0x78/0x7C` |
| iOS armv7 | `Player+0x60/0x64` |

`setCameraOffset(double x, double y)` 依次把两个 binary64 参数窄化为 float32 后
直接写入；无 equality gate、dirty 或 cameraActive gate。`getCameraOffset` 每次
创建 fresh Dictionary，按 x、y 顺序把 float32 扩为 TJS Real，使用
`TJS_MEMBERENSURE` 和进程级 x/y hint；没有返回字段借用。

Android arm64 的优化形状特殊：`0x67F2D0` 是 EmotePlayer forwarding thunk，
先从 `+0x428` 取 embedded Player，再 tail-branch 到 `0x6CDE90`；Player registrar
本身直接以 `0x6CDE90` 为 callback。IDA 因这条 tail branch 把共享体建成 thunk
的 tail chunk，本轮保留该真实共享代码形状，以 bookmark/line comment 标记
Player 入口，而没有伪造两份函数。

## 5. stereovision camera position

```cpp
void setStereovisionCameraPosition(double x, double y, double z) {
    stereoX = x; stereoY = y; stereoZ = z;
}
```

| 端 | x / y / z |
|---|---|
| Android arm64 | `+0x78/0x80/0x88` |
| Android armv7 | `+0x58/0x60/0x68` |
| iOS arm64 | `+0x60/0x68/0x70` |
| iOS armv7 | `+0x48/0x50/0x58` |

三个字段保持 binary64 原值；无转换、验证或 dirty。它们与 float cameraOffset、
camera-node position/target 是三个独立状态族。

## 6. 四个默认 callback

- `onAction()` / `onSync()`：四端都是单条 return 的真正 no-op。
- `onGroundCorrection(currentPosition)`：CopyRef 第一个 Variant 参数到返回值；
  默认不读取 parent-position extra argument。
- `onFindMotion(request)`：CopyRef by-value request Variant 到返回值；不把它转换
  为 String/Dictionary，也不克隆 Object。

Variant identity callback 对 Object 增加返回 owner 引用，因此调用方清理输入
临时值后返回对象仍存活。它们只是 native default；运行时仍可由脚本在 TJS
dispatch 上 override。

## 7. 本地与验证

本地实现与四端共同形状一致；本轮仅把 Player.h 中 processed count 的注释修正
为 shared visitor 的真实 particle-index-zero 行为，没有改 compiled semantics。
既有测试覆盖 stop wrapper、releaseSyncWait、cameraOffset fresh/float/non-finite、
onFindMotion Object identity，以及 shared visitor 的 particle owner/重复访问边界。

正式 CMake/Emscripten 工具链不可用，未执行正式测试。普通 body/owner/container
路径均已闭合；2026-08-27 的
`motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md` 已继续
闭合 processedMesh 的 `std::function` allocation、manager/vtable、normal destroy 与
target local-landing 差异。Dictionary publish 仍由独立 cameraOffset EH 条目承接。

该 cameraOffset EH 条目随后已由
`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 闭合：
Android arm64、iOS arm64 LSDA cold ordinary path 与 iOS armv7 SjLj path 在 x/y insertion
或 result publication 抛出时 Release fresh Dictionary，iOS 两端的 destructor-throw state
terminate；只有 Android armv7 的完整函数/相邻 catalog 无本帧 cleanup。Dictionary 只在
两次 SetValue 完成后写入返回槽，因此不存在 partial result publication。cameraOffset
coverage row 现为 `IMPLEMENTED`。
