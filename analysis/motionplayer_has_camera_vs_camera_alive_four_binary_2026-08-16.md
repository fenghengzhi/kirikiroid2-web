# Motion.Player `hasCamera` / `cameraAlive` 双状态四参考审计（2026-08-16）

## 结论

`Motion.Player.hasCamera` 与 `cameraAlive` 不是同一 Boolean 的两个名字。
四份当前参考二进制共同给出两条独立数据流：

- `hasCamera` 是只读的结构查询；每次调用从头到尾遍历 Player 的完整节点 deque，
  只要任意节点的 `nodeType == 5` 就返回 true。它不检查节点是否 active，也不读取
  CameraNode 阶段维护的状态 byte。
- `cameraAlive` 是只读的帧状态查询；getter 只读取 Player 内的一个 byte。每次
  CameraNode 更新先把该 byte 清零，随后只在找到第一条 active type-5 节点时置一。

旧本地 `getHasCamera()` 直接返回 `_hasCamera`，并额外公开一个未注册、无调用的
`setHasCamera(bool)`，把资源结构与帧存活状态错误合并。本轮将 `getHasCamera()` 恢复为
deque 扫描，删除伪 setter，并保留 `_hasCamera` 作为 `cameraAlive` 的唯一数据源。

## 四端函数映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `hasCamera` getter | `0x6CA378` | `0x5925E0` | `0x10011CFA8` | `0x11B972` |
| `cameraAlive` getter | `0x6D6B6C` | `0x598FE4` | `0x1001256C0` | `0x1248E6` |
| CameraNode producer | `0x6BAE08` | `0x587748` | `0x1001108C4` | `0x10E048` |
| Player registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |

recovery IDB 中四个结构扫描函数统一命名为
`Player_getHasCamera_guess`。现有 direct-byte getter 保持
`Player_getCameraAlive_guess`，生产者保持 `Player_updateCameraNode_guess`。

## `hasCamera` 的容器与边界行为

四端反编译虽展开成不同的 deque ABI，但共同源码语义为：

```text
for node in nodes.begin() .. nodes.end():
    if node.nodeType == 5:
        return true
return false
```

关键边界：

1. 扫描从真实 `begin()` 开始，不人为跳过根节点，也不假设尾 sentinel。
2. 空 deque 返回 false。
3. 只比较有符号/无符号表示相同的整型常量 5；不读取 active、slot、done、visible、
   cameraActive 或 stereovisionActive。
4. 第一条 type-5 节点立即短路返回 true；重复相机节点不计数。
5. 查询没有缓存，节点 deque 后续变化会在下一次 getter 调用中立即反映。

四端的节点步长分别为 `2632 / 2272 / 2648 / 2228` 字节；Android 使用旧
libstdc++ deque block/map 游标，iOS 使用 libc++ begin-index/size 算术。两种展开均覆盖
完整半开范围，没有可移植的额外成员或专用 camera index cache。

## `cameraAlive` 的独立生产链

四个 scalar getter 分别只读取 Player 中对应 ABI 的单个 byte。四个 CameraNode
producer 则共同执行：

```text
cameraAlive = false
scan non-root nodes in order
if first node with nodeType == 5 and accumulated.active:
    cameraAlive = true
    continue camera focus/offset/query publication
```

因此存在稳定且脚本可见的组合：

| 节点状态 | `hasCamera` | `cameraAlive` |
|---|---:|---:|
| 没有 type-5 节点 | false | false（producer 运行后） |
| 存在 inactive type-5 节点 | true | false |
| 存在 active type-5 节点且 producer 已运行 | true | true |

`cameraActive` 只门控后续 FOV/position/target/angle 发布；它不改变上述两个查询的
定义。`stereovisionActive` 属于更晚的 prepared-item 投影通道，同样不参与。

## 注册面

精确 UTF-16LE `hasCamera` 只命中一次：

| 目标 | 字符串 | registrar xref |
|---|---:|---:|
| Android arm64 | `0x14D6576` | `0x6D5154` |
| Android armv7 | `0xD85E84` | `0x598394` / `0x5983A0` |
| iOS arm64 | `0x10195CCF0` | `0x100124C0C` |
| iOS armv7 | `0x174F054` | `0x123EC0` / `0x123EC6` / `0x123ED2` |

四个 descriptor 都填入上述结构扫描 getter，并把 setter member pointer 保持为零。
相邻的 `frameLoopTime` 同样为只读 descriptor，而后续 `angleDeg` 明确同时填入 getter
和 setter；这进一步排除 `setHasCamera` 是隐藏 typed setter 的可能。

## 本地修复与验证目标

- `Player.h`：删除无注册、无 caller 的 `setHasCamera`，把 `getHasCamera` 改为
  out-of-line 结构查询，并明确 `_hasCamera` 只属于 `cameraAlive`。
- `PlayerLayerQuery.cpp`：按 deque 顺序扫描所有节点，type-5 首命中短路。
- `motionplayer-dll.cpp`：覆盖无相机、inactive type-5、active type-5 三种状态，尤其锁定
  `hasCamera == true && cameraAlive == false` 的分离边界。

所有绝对地址仅记录在本分析文件；compiled source 注释只保留可移植语义。
