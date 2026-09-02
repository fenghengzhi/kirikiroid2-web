# MotionLayer 网格数组长度读取偏差（四端，2026-09-02）

## 现象与运行时证据

`NEKOPARA 0` 通过 `Motion.EmotePlayer` 加载 `e-mote3.0*.psb`。Web 端人物的
网格部件会碎裂成长三角形。进入 `MotionLayerExtensions` 之前，PreparedItem 中的
复合网格完整：例如躯干 `6x10` 网格有 `77` 个点，另一躯干 `8x8` 网格有 `81`
个点，头发 `4x11` 网格有 `60` 个点。

进入 `buildAndSubmitMeshTriangles_guess` 后，同一批对象却统一只剩 `12` 个点；网格
划分数仍保持 `6x10`、`8x8`、`4x11`。后续按 `(divisionX + 1) *
(divisionY + 1)` 索引顶点，因此越过 12 点向量，日志中的包围盒随即出现
`INT_MIN/INT_MAX`，画面表现为随机的大三角形和长条。

本地 `ncbPropAccessor::GetCount()` 调用 TJS dispatch 的 `GetCount` 虚函数。
`tTJSArrayObject` 没有覆盖该虚函数，继承的 `tTJSCustomObject::GetCount` 返回对象
成员表数量；本次运行中是固定的 `24`，于是平面坐标数组被解析成 `24 / 2 = 12`
个点。这不是 PSB 网格数量。

## 本轮 fresh 四端函数映射

| 参考二进制 | `renderMesh` | 数组 count helper | 状态 |
|---|---:|---:|---|
| Android arm64-v8a | `0x69E0D0` | `0x56CA74` | 已定位并重新反编译 |
| Android armeabi-v7a | `0x576E08` | `0x4BEB84` | 已定位并重新反编译 |
| iOS arm64 | `0x1000FB660` | `0x1000F30F4` | 已定位并重新反编译 |
| iOS armv7 | `0xF86B0` | `0xEF8B4` | 已定位并重新反编译 |

四个 count helper 都不是调用 dispatch `GetCount`：它们共同调用
`PropGet(0, L"count", nullptr, &value, receiver)`，再把 Variant 转成整数。Android
arm64 的 helper 将 Variant 类型分支内联展开；其余三端调用各自的 Variant-to-int
helper。这只是编译器展开差异，属性名、flags、hint、receiver 和返回转换一致。

## 四端共同控制流

```text
pointAccessor = ncbPropAccessor(flatPoints)
coordinateCount = integer(pointAccessor.PropGet("count"))
pointCount = coordinateCount / 2
reserve(meshPoints, pointCount)

for pointIndex in [0, pointCount):
    x = indexed MEMBERMUSTEXIST probe succeeds
        ? real(pointAccessor[pointIndex * 2]) : 0.0
    y = indexed MEMBERMUSTEXIST probe succeeds
        ? real(pointAccessor[pointIndex * 2 + 1]) : 0.0
    meshPoints.push_back({x, y})

read owner clip properties
resolve source/target Layer and render method
submitLayerMesh(sourceRect, meshPoints, divisionX, divisionY, ...)
if submitted: owner.update(clipRect)
draw optional mesh debug grid
```

逐端编译形态差异：Android arm64 显式保留 `(uint32_t)(count + 1) >= 3`
门控；Android armv7 和两份 iOS 代码把 reserve/循环门控重新排列或内联。但对 TJS
Array 的非负 `count`，四端都只遍历 `count / 2` 个点，且均从 `"count"` 属性取得
这个数值。

## 本地实现对照与修复

`MotionLayerExtensions.cpp::readMeshPoints_guess` 的构造、两坐标一组、缺失索引补
零、vector reserve、循环和后续 submit 顺序均与四端一致。唯一偏差是
`points.GetCount()`：它走 dispatch `GetCount`，而四端明确读取 `"count"` 属性。

本地 `ncbPropAccessor::GetArrayCount()` 正好实现四端 helper 的
`PropGet("count") + integer conversion`。因此修复仅把该调用替换为
`GetArrayCount()`；不修改 TJS Array 核心、不改变网格算法，也不引入 Web 专用渲染
分叉。

## 已排除方向

- 两份 PSB 经 mtndump 导出的各 114 张源图尺寸和透明 alpha 正常；
- PreparedItem 的 source rect、四角、父子矩阵、网格点顺序与完整点数正常；
- 强制 root rotation 为零、停用 physics、修改 physics substep 均不能修复；
- 强制软件渲染单线程、替换逐三角形采样器均不能修复；
- 躯干无 parent，故其碎裂不是 alpha mask 或父层裁剪造成；
- 同一部件跨帧几何稳定，排除时间线状态累积。

这些实验中的代码改动均不保留；它们只用于把故障边界收敛到平面网格数组进入
`MotionLayerExtensions` 后的长度读取。

## 修复后验证

- `cmake --build out/web/debug` 成功；仅有项目原有的 deprecated/JSPI 警告；
- 以完整 `NEKOPARA 0.zip` 和 `启动游戏.xp3` 启动，不使用单独 XP3；
- 同屏两名角色的头发、脸、衣服与透明边缘恢复完整，无三角形/长条碎裂；
- 间隔 4 秒的两帧中头部、发梢和身体姿态继续变化，确认 E-mote 动画未停帧；
- 浏览器运行时 error 日志为空。
