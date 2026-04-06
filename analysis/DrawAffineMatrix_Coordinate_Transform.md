# drawAffineMatrix 设置和坐标变换分析

> Analysis target: libkrkr2.so (kirikiroid2 Android, arm64-v8a)
> Analysis method: IDA Pro MCP decompilation
> Date: 2026-04-06

---

## 1. drawAffineMatrix 布局

通过 `*(player + 1064)` 间接指针访问（指向 player 内部对象）：

| Offset | 字段 | 类型 | 含义 |
|--------|------|------|------|
| +808 | m11 | double | 缩放/旋转 |
| +816 | m12 | double | 旋转/倾斜 |
| +824 | m21 | double | 旋转/倾斜 |
| +832 | m22 | double | 缩放/旋转 |
| +840 | tx | **float** | X 平移 |
| +844 | ty | **float** | Y 平移 |

**注意**: m11-m22 是 double (8B)，但 tx/ty 是 **float** (4B)！

对应的标志位: `*(internal + 611)` = drawAffineMatrix_isNonIdentity (byte)

---

## 2. 所有写入 drawAffineMatrix 的位置

通过 IDA 搜索 STR 指令到 offset 0x328-0x348 确认：

| 写入位置 | 函数 | 地址 | 触发条件 | 值 |
|----------|------|------|----------|-----|
| Player_ctor | 0x6CED30 | 0x6CF12C-0x6CF158 | 构造时 | identity {1,0,0,1,0,0} |
| **Player_setDrawAffineTranslateMatrix** | **0x6D4F14** | 0x6D4F18-0x6D4F38 | **TJS 脚本调用** | 6个参数 |
| Player_DrawSLA | 0x6D5658 | 0x6D57C0 | SLA 渲染路径 | 部分字段 |
| sub_6F19B4 | 0x6F19B4 | 0x6F1A3C | 内部清理 | tx=0 |

**关键发现：没有任何 C++ 内部函数自动计算 drawAffineMatrix。它完全依赖 TJS 脚本调用。**

---

## 3. TJS 脚本设置 drawAffineMatrix 的链路

### 调用链
```
AffineLayer.onPaint()
  → _image.calcMatrix(mtx)              // KAG AffineMatrix 计算变换矩阵
  → _image.drawAffine(this, mtx)
    → AffineSourceMotion._drawAffine(mtx, revmtx, target)
      → _player.progress(interval)
      → ax = mtx.transformAreaX          // [x0, x1, x2]
      → ay = mtx.transformAreaY          // [y0, y1, y2]
      → ox = ax[0]; oy = ay[0]          // ★ tx/ty，包含居中偏移
      → dx = ax[1]-ox; ey = ay[2]-oy    // 缩放分量
      → ex = ax[2]-ox; dy = ay[1]-oy    // 旋转分量
      → _player.setDrawAffineTranslateMatrix(dx, ey, ex, dy, ox, oy)
      → _player.draw(target)
```

### 参数映射
```
setDrawAffineTranslateMatrix(a2, a3, a4, a5, a6, a7):
  internal+808 = a2  // m11 = dx
  internal+816 = a4  // m12 = ex
  internal+824 = a3  // m21 = ey
  internal+832 = a5  // m22 = dy
  internal+840 = a6  // tx  = ox (float)
  internal+844 = a7  // ty  = oy (float)
```

**`ox` 和 `oy` 是 AffineMatrix 的原点坐标，由 KAG 的 `calcMatrix()` 计算。
当 AffineLayer 居中在屏幕上时，ox ≈ canvas_width/2，oy ≈ canvas_height/2。**

---

## 4. sub_6C2334 (renderTree 构建) 中的 drawAffineMatrix 应用

地址: 0x6C2334

当 `internal+611` (isNonIdentity) 为 true 时，对渲染节点执行变换：

### paintBox 变换 (0x6C2808-0x6C2954)
```c
// 对 4 个角点 (left,top), (right,top), (left,bottom), (right,bottom)
newX = m12 * oldY + m11 * oldX + tx
newY = m22 * oldY + m21 * oldX + ty
// 然后取 min/max 重建 AABB
```

### 顶点坐标变换 (0x6C2B90-0x6C2CD0)
```c
// 对 4 个顶点 (+136/140, +144/148, +152/156, +160/164)
newX = m12 * vtxY + m11 * vtxX + tx
newY = m22 * vtxY + m21 * vtxX + ty
```

### bezier 控制点变换 (0x6C2CD4+)
```c
// 对 mesh 控制点同样应用 drawAffineMatrix
```

**当 drawAffineMatrix 是 identity 时，isNonIdentity=false，跳过整个变换——节点保留 PSB 原始坐标。**

---

## 5. sub_6D5264 (Player_applyTranslateOffset) — cameraOffset 应用

地址: 0x6D5264

### cameraOffset 来源 (player+144/148)

**来源 1: TJS 方法 setCameraOffset** (0x6D9A38)
```c
player+144 = x;  // cameraOffset.x
player+148 = y;  // cameraOffset.y
```

**来源 2: camera node 自动计算** (sub_6BDA28, 0x6BDA28)
```c
// 当存在 nodeType=5 (camera) 节点时
dx = -(cameraPosX - rootPosX)
dy = -(cameraPosY*zFactor + cameraPosZ - (rootPosY*zFactor + rootPosZ))
// 用 drawAffineMatrix 变换（但不加 tx/ty！）
cameraOffsetX = (int)(m11*dx + m12*dy + 0.5)
cameraOffsetY = (int)(m21*dx + m22*dy + 0.5)
```

### applyTranslateOffset 逻辑
```c
// 遍历所有渲染节点
for each node in renderList:
    // 加 cameraOffset 到所有坐标字段
    node+136 += cameraOffsetX; node+140 += cameraOffsetY;  // vertex0
    node+144 += cameraOffsetX; node+148 += cameraOffsetY;  // vertex1
    node+152 += cameraOffsetX; ...  // vertex2
    node+160 += cameraOffsetX; ...  // vertex3
    node+184 += cameraOffsetX; node+188 += cameraOffsetY;  // paintBox
    ...
    // 当 stereovisionActive (player+1095):
    //   额外做透视缩放
```

---

## 6. sub_6C7440 (renderToCanvas/Layer) 中的 affineCopy

地址: 0x6C7440

每个渲染节点生成 affineCopy 调用参数：
```c
// v57 = fmaxf(paintBox.left, 0)  即裁剪左边界
// v61 = fmaxf(paintBox.top, 0)   即裁剪上边界
// affineCopy 参数 (6 个顶点坐标):
affineCopy(layer, 0, "affineCopy", ...,
    vertex0.x - 0.5 - v57,  // src 左上 X
    vertex0.y - 0.5 - v61,  // src 左上 Y
    vertex1.x - 0.5 - v57,  // src 右上 X
    vertex1.y - 0.5 - v61,  // src 右上 Y
    vertex2.x - 0.5 - v57,  // src 左下 X
    vertex2.y - 0.5 - v61,  // src 左下 Y
    project_value,           // 插值模式
    1                        // opaque flag
);
```

**坐标已经过 drawAffineMatrix 变换（在 renderTree 中）和 cameraOffset 偏移（在 applyTranslateOffset 中），affineCopy 不做额外偏移。**

---

## 7. 问题诊断总结

### 根因
drawAffineMatrix 的 tx/ty 为 0（identity 矩阵），导致 PSB 原始坐标（以 motion 中心 (0,0) 为原点，大量负坐标）直接映射到 Layer 像素坐标。

### 正确流程
TJS 脚本层应该在 `_drawAffine()` 中调用 `setDrawAffineTranslateMatrix(dx, ey, ex, dy, ox, oy)`，其中 `ox`/`oy` 由 KAG 的 `AffineMatrix.calcMatrix()` 计算，包含居中偏移。

### 检查点
1. `setDrawAffineTranslateMatrix` 是否被 TJS 脚本调用？（加日志确认）
2. 如果被调用，传入的 ox/oy 值是否正确？
3. 如果没被调用，`AffineSourceMotion._drawAffine` 执行链路是否完整？
4. `draw()` 是否从正确的 TJS 调用路径进入，而非 C++ 直接调用？
