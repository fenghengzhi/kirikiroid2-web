# Motionplayer Layer frame 扩展四参考恢复（2026-08-13）

## 1. 结论

四个参考二进制都把一组 motionplayer 私有状态和九个 Layer 成员作为同一个
ncbind attached class 懒挂到脚本 `Layer` 实例上。成员注册顺序一致：

1. `debugMeshApp` 可读写属性；
2. `debugBezierApp` 可读写属性；
3. `meshCopy`；
4. `operateMesh`；
5. `drawMeshFrame`；
6. `bezierPatchCopy`；
7. `operateBezierPatch`；
8. `drawBezierPatchFrame`；
9. `drawBezierPatchMeshFrame`。

本纵切面恢复了前两个属性和后三个 frame 方法，并建立了独立的
`MotionLayerExtensions_guess` attached class。四个 copy/operate 包装仍留作下一纵切面：
本地 `LayerIntf` 目前直接注册同名方法，其来源仍是过时的 `libkrkr2.so` 结构；在四个
typed wrapper 的参数、face cache 更新和异常清理完整取证前，不把它们重复注册到新类。

## 2. 四文件映射

| 语义 | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Layer 扩展成员 registrar | `0x6A1204` | `0x578A6C` | `0x1000FE030` | `0xFAFB0` |
| `drawMeshFrame` | `0x69F5E4` | `0x577B50` | `0x1000FC9C0` | `0xF996C` |
| TJS 控制点解析 + patch tessellate | `0x69E9F8` | `0x577430` | `0x1000FBFF0` | `0xF9054` |
| `drawBezierPatchFrame` | `0x6A0210` | `0x578168` | `0x1000FD250` | `0xFA220` |
| `drawBezierPatchMeshFrame` | `0x6A0B3C` | `0x5786AC` | `0x1000FDAF8` | `0xFAAA4` |

四个 recovery IDB 已把上述五组函数分别改名为：

- `MotionLayerExtensions_registerMembers_guess`；
- `MotionLayerExtensions_drawMeshFrame_guess`；
- `MotionLayer_parseAndTessellateBezierPatch_guess`；
- `MotionLayerExtensions_drawBezierPatchFrame_guess`；
- `MotionLayerExtensions_drawBezierPatchMeshFrame_guess`。

registrar 使用的 UTF-16 名称地址如下。这里只记录用于复核的所属文件地址，不能跨目标
复用：

| 名称 | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `debugBezierApp` | `0x14D55DE` | `0xD851B6` | `0x10195B9DE` | `0x174DD42` |
| `operateMesh` | `0x14D560E` | `0xD851E6` | `0x10195BA0E` | `0x174DD72` |
| `drawMeshFrame` | `0x14D5626` | `0xD851FE` | `0x10195BA26` | `0x174DD8A` |
| `bezierPatchCopy` | `0x14D5642` | `0xD8521A` | `0x10195BA42` | `0x174DDA6` |
| `operateBezierPatch` | `0x14D5662` | `0xD8523A` | `0x10195BA62` | `0x174DDC6` |
| `drawBezierPatchFrame` | `0x14D5688` | `0xD85260` | `0x10195BA88` | `0x174DDEC` |
| `drawBezierPatchMeshFrame` | `0x14D56B2` | `0xD8528A` | `0x10195BAB2` | `0x174DE16` |

其中 `debugMeshApp`、`meshCopy` 位于同一连续 UTF-16 表的前两项；本轮通过整段 raw
bytes 和 registrar 消费顺序确认名称，没有用 IDA 截断后的单字符标注推导。

## 3. attached instance 布局与生命周期

Android arm64 属性回调直接证明：

- `debugMeshApp` Variant 位于 native object `+12`；
- `debugBezierApp` Variant 位于 `+32`；
- object `+0` 是目标 Layer dispatch 指针；
- `+8` 是 copy/operate 路径刷新使用的 `face` 缓存整数。

因此两种 ABI 的共同源布局为：

```text
64-bit pack(4): owner +0 (8), face +8 (4), debugMeshApp +12 (20),
                debugBezierApp +32 (20)
32-bit pack(4): owner +0 (4), face +4 (4), debugMeshApp +8 (12),
                debugBezierApp +20 (12)
```

ncbind hook 在第一次调用该 attached class 的成员时查 native instance；不存在则
`new ClassT(objthis)` 并装回 Layer adaptor。Layer 在失效时删除 adaptor，adaptor 删除这个
native object。`owner` 是非 owning 指针：它的生命周期由拥有 attached adaptor 的同一个
Layer 支配，native object 不对 owner 单独 `AddRef`，也不形成引用环。两个 Variant 字段则
执行普通 `tTJSVariant` copy assignment，独立持有它们的对象/字符串内容。

## 4. `drawMeshFrame` 共同控制流

```text
pointsObject = flatPoints.AsObject()/ncbPropAccessor
count = pointsObject.GetCount()
adjusted = count >= 0 ? count : count + 1
pointCount = adjusted >> 1                  // C++ signed /2 trunc toward zero
if unsigned(count + 1) >= 3:
    reserve(pointCount)
    if count >= 2:
        for i in [0, pointCount):
            x = MUSTEXIST probe(2*i) failed ? 0 : second indexed read AsReal
            y = MUSTEXIST probe(2*i+1) failed ? 0 : second indexed read AsReal
            points.push_back({x,y})

LayerClass = global.Layer
for y = 0 .. divisionY inclusive:
    app = boundary(y) ? outline : meshline
    if app is not Void:
        line = TJS Array native Items deque
        append fresh [x,y] Arrays for x = 0 .. divisionX inclusive
        LayerClass.drawLines.call(ownerLayer, app, line)
for x = 0 .. divisionX inclusive:
    同样生成列并调用 drawLines
```

共同边界行为：

- odd coordinate 尾项静默忽略；
- 缺失坐标先 probe，失败变为 `0.0`；成功坐标会触发两次 indexed dispatch；
- 自定义对象若返回负 `GetCount`，unsigned gate 仍可能进入 reserve，而 signed 循环不填点；
- division 只控制循环，不验证 point vector 的实际大小；短输入会落入 native 越界/UB；
- 某个 style 是 Void 时跳过那条线，其他线继续；
- TJS point 与 outer line 均通过 `TJSCreateArrayObject` 后直接写原生
  `tTJSArrayNI::Items`（`std::deque<tTJSVariant>`），没有脚本 `add()` 调用；
- `drawLines` 返回值被忽略。

## 5. TJS patch parse+tessellate helper

`drawBezierPatchMeshFrame` 的共享 helper 顺序在四端一致：

```text
control.reserve(16)                         // 不 clear
result.reserve(int32((divY+1)*(divX+1)))    // signed 32-bit 乘法后再扩宽
pointsObject = flatControlPoints.AsObject()/ncbPropAccessor
for i = 0 .. 15:
    control.push_back({readReal(2*i), readReal(2*i+1)})
basisX = cachedCubicBasis(divX)
basisY = cachedCubicBasis(divY)
for y = 0 .. divY inclusive:
    for x = 0 .. divX inclusive:
        p = {0,0}
        for controlIndex = 0 .. 15:
            weight = basisY[y][controlIndex/4] * basisX[x][controlIndex%4]
            p.x = p.x + weight * control[controlIndex].x
            p.y = p.y + weight * control[controlIndex].y
        result.push_back(p)
```

这个 helper 不查 count，也不先做 MUSTEXIST probe；它固定请求索引 `0..31`。普通失败产生
default Void Variant，real conversion 为 `0.0`。注意这是调用次数差异，不可仅以最终零值
为由和 `drawMeshFrame` 共用 parser。

capacity 表达式先在 32 位 signed int 中运算。复原代码用显式 32 位 unsigned 乘法和
bit-copy 取得相同的二补码结果，避免在 C++ 层引入有符号 overflow UB；负结果转为
`size_t` 后触发 `vector::reserve` 的长度错误。helper 不 clear 两个输出 vector；frame caller
每次传入新空 vector。

## 6. `drawBezierPatchMeshFrame`

共同顺序是：先构造 global `Layer` accessor，暂持目标 Layer class；再创建 control/result
vectors，调用上一节 helper；然后以与 `drawMeshFrame` 相同的 inclusive row/column 方式，
从 row-major result 生成嵌套 TJS Arrays 并调用 `Layer.drawLines`。函数退出时按局部对象逆序
释放 result、control 与 Layer accessor；局部 TJS Arrays 每次调用后立即释放。

## 7. `drawBezierPatchFrame` 的三点曲线怪癖

这个三参方法不是 patch 网格绘制：它固定取 cubic division 3 的四行 basis，然后发出两组
各四次 `Layer.drawBeziers`。输入 parser 固定读 16 对坐标；与 patch-mesh helper 不同，
四端这里会对每个坐标先做 MUSTEXIST 检查，失败为 `0.0`，成功再读取转换一次。

第一组：

```text
for sample = 0 .. 3:
    app = sample in {0,3} ? outline : meshline
    if app is not Void:
        curve = []
        for row = 0 .. 2:                    // 恰好三行，不是四行
            curve.push(eval horizontal cubic control row at basis[sample])
        if sample == 0: reverse(curve.Items)
        Layer.drawBeziers(app, curve)
```

第二组：

```text
for sample = 0 .. 3:
    app = sample in {0,3} ? outline : meshline
    if app is not Void:
        curve = []
        for column = 0 .. 2:                 // 恰好三列，不是四列
            curve.push(eval vertical cubic control column at basis[sample])
        if sample == 3: reverse(curve.Items)
        Layer.drawBeziers(app, curve)
```

特殊 helper 在 Android arm64 是独立函数，在 32 位优化结果中内联；两种形态都明确是
`std::reverse(deque<tTJSVariant>::begin(), end())`。它不补第四个点。因此每次调用都传恰好
三个二维点；即使底层 GDI+ Bezier 对该数量返回错误、上层忽略 status，这个历史怪癖也应
保留，不能按“合理曲线”修成四点。

## 8. 平台/编译器差异

- AArch64 `tTJSVariant` 是 pack(4) 的 20 字节，32 位是 12 字节；因此同一字段布局产生
  两组不同 offset，语义不变。
- AArch64 libstdc++ 的 deque block 与 32 位 libc++ 的具体 iterator/块尺寸不同；32 位
  反编译中 `0x155 * 12 = 4092` 的跨块逻辑只是 STL ABI 展开，共同源码仍是
  `std::reverse`。
- registrar 和方法控制流、成员次序、Void gate、inclusive loops、三点怪癖在四端一致；
  未发现产品版本差异。
- `drawBezierPatchFrame` 的 per-coordinate MUSTEXIST probe 与 patch-mesh helper 的直接读取
  是方法间真实差异，而不是平台差异。

## 9. 本地逐项对照

- `MotionLayerExtensions.h` 以 owner、face cache、两个 Variant 的顺序复刻 native layout；
- `main.cpp` 使用 `NCB_GET_INSTANCE_HOOK` + `NCB_ATTACH_CLASS_WITH_HOOK` 实现按 Layer 懒建、
  随 Layer adaptor 销毁，并已按参考顺序注册完整九成员 surface；
- 两个 property 用 typed `NCB_PROPERTY`，保留 Variant copy ownership；
- `readMeshPoints_guess` 保留 `GetCount`、unsigned gate、truncating `/2`、probe + second read；
- `parseAndTessellateBezierPatch_guess` 保留两个 reserve 的先后、直接 32-coordinate reads、
  两次 basis lookup 和固定 16 项 row-major 求和；
- `drawGridFrame_guess` 为两个 grid frame 方法复用共同的 row/column TJS Array 构造流；
- `drawBezierPatchFrame` 独立保留 probe parser、两个 4-sample groups、每组 3 点以及两个
  reverse 条件；
- 全部嵌套脚本 Array 都复用 `createTJSArrayWithItems_guess`，直接操作
  `std::deque<tTJSVariant>`；
- 所有 Layer draw call 都以 attached owner Layer 作为 `objthis`，并忽略返回 status。

## 10. 验证与剩余缺口

- `cmake --preset "Web Debug Config"`：通过；
- `cmake --build out/web/debug`：通过；
- `git diff --check`：通过，仅有工作树既存 CRLF 提示；
- 当前没有包含真实 GDI+/LayerExDraw appearance 与 malformed dispatch 的既存 fixture；按项目
  约束没有从零捏造物料。本纵切面的验证是四端静态控制流复核和全量构建。

紧邻的 `meshCopy`、`operateMesh`、`bezierPatchCopy`、`operateBezierPatch`、face cache 与
common render 数据流现已在
`motionplayer_layer_copy_operate_four_binary_2026-08-13.md` 闭合。特别纠正：`face==128`
读取的是 Layer 的 `type` 属性，不是旧 IDB 符号暗示的 `engineType`。四方法已迁入 attached
class，核心 Layer 不再直接注册这四个脚本名称，完整九成员序列现已恢复。另一个必须区分
的容器边界是：公开 `drawMeshFrame` 每条实际绘制的线各自创建外层 Array；common render
后的 mesh debug overlay 则在整个 helper 中复用同一个外层 Array，每轮只清空其 deque。
