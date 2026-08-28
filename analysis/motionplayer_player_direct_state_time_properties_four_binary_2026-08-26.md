# Player 直接状态/时间属性 #23/#24/#27/#35/#36/#38/#40–#42（四参考二进制，2026-08-26）

## 1. 范围与结论

本纵切面闭合九个低依赖属性的 12 个 native callback：

- read/write：`cameraActive`、`stereovisionActive`、`maskMode`；
- read-only：`cameraFOV`、`cameraAlive`、`playing`、`syncWaiting`、
  `frameLastTime`、`frameLoopTime`。

四端都只执行一个字段 load 或 store。没有递归、状态重算、单位转换、dirty 标记、
容器访问、Variant CopyRef 或 renderer 调用。

## 2. callback 与字段坐标

| 属性 | 角色 | Android arm64 | 偏移 | Android armv7 | 偏移 | iOS arm64 | 偏移 | iOS armv7 | 偏移 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cameraActive | get/set | `0x6D6AE8 / 0x6D6AF0` | `+0x446` | `0x598F72 / 0x598F78` | `+0x2EA` | `0x100125628 / 0x100125630` | `+0x3D6` | `0x124856 / 0x12485C` | `+0x2AA` |
| stereovisionActive | get/set | `0x6D6AFC / 0x6D6B04` | `+0x447` | `0x598F7E / 0x598F84` | `+0x2EB` | `0x100125638 / 0x100125640` | `+0x3D7` | `0x124862 / 0x124868` | `+0x2AB` |
| maskMode | get/set | `0x6D6B38 / 0x6D6B40` | `+0x47C` | `0x598FB6 / 0x598FBC` | `+0x324` | `0x100125670 / 0x100125678` | `+0x40C` | `0x12489A / 0x1248A0` | `+0x2E0` |
| cameraFOV | get | `0x6D6B64` | `+0x450` | `0x598FDA` | `+0x2F8` | `0x1001256B8` | `+0x3E0` | `0x1248DC` | `+0x2B4` |
| cameraAlive | get | `0x6D6B6C` | `+0x44C` | `0x598FE4` | `+0x2F0` | `0x1001256C0` | `+0x3DC` | `0x1248E6` | `+0x2B0` |
| playing | get | `0x6D6B74` | `+0x44B` | `0x598FEA` | `+0x2EF` | `0x1001256C8` | `+0x3DB` | `0x1248EC` | `+0x2AF` |
| syncWaiting | get | `0x6D6B7C` | `+0x44A` | `0x598FF0` | `+0x2EE` | `0x1001256D0` | `+0x3DA` | `0x1248F2` | `+0x2AE` |
| frameLastTime | get | `0x6D6B84` | `+0x468` | `0x598FF6` | `+0x310` | `0x1001256D8` | `+0x3F8` | `0x1248F8` | `+0x2CC` |
| frameLoopTime | get | `0x6D6B8C` | `+0x470` | `0x599000` | `+0x318` | `0x1001256E0` | `+0x400` | `0x124902` | `+0x2D4` |

四库已统一命名、签名和注释；所有地址在修改 IDB 类型后重新保存。

## 3. 共同伪代码与边界

```cpp
bool getCameraActive() const { return cameraActive; }
void setCameraActive(bool v) { cameraActive = v; }

bool getStereovisionActive() const { return stereovisionActive; }
void setStereovisionActive(bool v) { stereovisionActive = v; }

int getMaskMode() const { return maskMode; }
void setMaskMode(int v) { maskMode = v; }

double getCameraFOV() const { return cameraFov; }
bool getCameraAlive() const { return cameraAlive; }
bool getPlaying() const { return playing; }
bool getSyncWaiting() const { return syncWaiting; }
double getFrameLastTime() const { return cachedTotalFrames; }
double getFrameLoopTime() const { return loopTime; }
```

精确语义：

- `cameraActive` 只控制后续 camera-node publication 的 gate；setter 本身不重算相机；
- `stereovisionActive` 只写独立字节；setter 不移动 stereo camera；
- `maskMode` 是未经验证的 signed Int32，真正 alpha-mask 消费发生在后续渲染链；
- `cameraAlive` 是每帧结果字节，不等价于扫描资源里是否存在 camera node；
- `playing` 只读本 Player 的本地播放字节，不检查任何 child；
- `syncWaiting` 公开表面只读，其他状态机路径可以在内部修改该字节；
- 两个 `frame*` 属性返回原始 frame-domain double，不执行毫秒换算；
- `cameraFOV` 和两个 frame double 原样保留 NaN、无穷与 signed zero。

## 4. 发现并修复的本地偏差

修改前，本地 `Player::getPlaying()` 在诊断开关启用时还会：

1. 读取 `_findMotionContextVariant`；
2. 构造 `ttstr/std::string`；
3. 调用 logger 写 I/O；
4. 最后才返回本地播放字节。

四端 leaf 都是纯 byte load，因此额外路径会引入参考不存在的分配、异常和 I/O
边界。本轮已删除该诊断分支，使 getter 恢复为纯字段读取。没有删除其他调用者所需的
`matchedMotionPath()` 辅助函数。

现有单元用例
`Player playing and allplaying preserve local and recursive boundaries` 已覆盖
`playing` 只看本地字节、`allplaying` 才递归 child 的核心边界。当前机器缺少
CMake/Ninja/Emscripten，无法运行正式 test/build；`git diff --check` 仍需在本轮
最终机械验收中通过。

## 5. 本地其他对照

其余八项与参考一致：

- Boolean 与 Int32 直接字段访问；
- `getMaskMode/setMaskMode` 没有额外变换；
- `getCameraFOV/getCameraAlive/getSyncWaiting` 读取对应字段；
- `getFrameLastTime/getFrameLoopTime` 返回 raw frame 值。

下一组应把这些 leaf 与非 leaf 的 `allplaying/hasCamera/bounds` 明确分离；后三者
不能因为名字相邻而按直接字段实现。

