# MotionPlayer common-mesh 软件 repeat bitmap 交接四端复原（2026-08-15）

## 1. 结论

四份当前 `reference/binaries/` 都把软件 source-repeat 分成两个源码级阶段：

1. `RenderMesh_makeRepeatedSoftwareBitmap_guess` 只归一化坐标、计算 copy 数、分配并
   填充 fresh 32-bpp `tTVPBitmap`，返回 bitmap 指针；
2. common-mesh 主函数看到非空 bitmap 后，先 `Release` 原 source texture，再取得当前
   render manager，调用 `CreateTexture2D(bitmap)`，把原样返回值作为新的 source。

portable 实现此前把 `CreateTexture2D` 提前放进 helper，使 render-manager factory 在
旧 source 的临时引用释放之前执行。正常像素结果大多相同，但对象生命周期、可重入观察
顺序和 factory 抛异常时的边界不同。本轮恢复为四端共同结构。

另一个看似错误的行为反而是真实 native 边界：第一 band 按扫描线做水平重复，后续
vertical band 却从原 source base 连续复制 `sourcePitch * textureHeight` 字节，而不是
复制已经横向展开的第一 band。它会在 destination pitch 更宽时把多条原扫描线紧密塞进
一个目标扫描线的前部；本轮明确保留，不把它“修好”。

## 2. 四端函数与调用顺序

| 目标 | repeat helper | common mesh | helper call | old-source Release | manager getter | bitmap factory |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x69AE08` | `0x69AFE4` | `0x69B094` | `0x69B0AC` | `0x69B0B0` | `0x69B0C0` |
| Android ARMv7 | `0x575688` | `0x575800` | `0x57586C` | `0x575878` | `0x57587A` | `0x575886` |
| iOS ARM64 | `0x1000F9570` | `0x1000F974C` | `0x1000F9800` | `0x1000F9814` | `0x1000F9818` | `0x1000F982C` |
| iOS ARMv7 | `0xF6650` | `0xF685C` | `0xF6920` | `0xF6930` | `0xF6934` | `0xF6942` |

四端 helper 的参数顺序都是：

```cpp
tTVPBitmap *makeRepeatedSoftwareBitmap_guess(
    iTVPTexture2D *sourceTexture,
    int &sourceTop,
    int &sourceLeft,
    int sourceWidth,
    int sourceHeight);
```

`sourceTop` 在 `sourceLeft` 之前不是反编译显示偶然：A64 caller 明确以 source-rect
packed pair 的 `+4` 地址送入 `X1`、base 地址送入 `X2`；其余三端的 helper 内部也分别
用 texture height/width 对第二、第三参数做 floor-mod 归一化。

## 3. repeat helper 的精确算法

四端共同伪代码：

```cpp
top  -= textureHeight * floor(double(top)  / textureHeight);
left -= textureWidth  * floor(double(left) / textureWidth);

verticalCopies =
    (sourceHeight + textureHeight + top - 1) / textureHeight;
horizontalCopies =
    (sourceWidth + textureWidth + left - 1) / textureWidth;

if (verticalCopies == 1 && horizontalCopies == 1)
    return nullptr;

bitmap = new tTVPBitmap(horizontalCopies * textureWidth,
                        verticalCopies * textureHeight, 32);

for (int y = 0; y < textureHeight; ++y) {
    for (int x = 0; x < horizontalCopies; ++x) {
        memcpy(dstRow + x * textureWidth * 4,
               sourceRow,
               textureWidth * 4);
    }
}

for (int band = 1; band < verticalCopies; ++band) {
    memcpy(dstBase + band * textureHeight * destinationPitch,
           sourceBase,
           sourcePitch * textureHeight);
}
return bitmap;
```

四端两个关键 `memcpy` 的位置：

| 目标 | 水平逐行 copy | 后续 vertical band copy |
| --- | ---: | ---: |
| Android ARM64 | `0x69AF44` | `0x69AF9C` |
| Android ARMv7 | `0x5757A8` | `0x5757E4` |
| iOS ARM64 | `0x1000F96AC` | `0x1000F9704` |
| iOS ARMv7 | `0xF67E0` | `0xF6822` |

helper 不读取 texture format，固定按四字节像素分配/copy。texture width/height 为零、
整数加法/乘法溢出、negative source extent、bitmap 分配失败等路径都没有额外净化。
fresh bitmap 的 construction reference 在可见调用链中没有 `Release`；factory 返回空也
不检查，后续路径原样接收空 source。

## 4. GPU warning 的宽字符串来源

普通 string 搜索四端均为零命中，因为该字面量是 UTF-16LE。按宽编码 byte 搜索得到：

| 目标 | UTF-16LE 字面量 | common-mesh 引用 |
| --- | ---: | ---: |
| Android ARM64 | `0x14D541E` | `0x69B0CC` |
| Android ARMv7 | `0xD84FD2` | `0x57588A` / `0x57588E` / `0x5761C0` |
| iOS ARM64 | `0x10195B76A` | `0x1000F9838` |
| iOS ARMv7 | `0x174DACE` | `0xF6946` / `0xF6950` |

文本四端一致：`Repeat texture for opengl is not implemented yet.`。non-software
路径以 important=true 记录后继续使用原 texture 和未归一化的 source coordinates。

## 5. portable 落地

`MotionRenderBackend.cpp` 的内部 helper 现返回 `tTVPBitmap*`，并恢复
`(sourceTop, sourceLeft, sourceWidth, sourceHeight)` 的参数顺序。common-mesh caller
在非空 bitmap 上：

1. 通过临时 source owner 立即 Release 旧 source 并解除旧 owner；
2. 调用 `TVPGetRenderManager()->CreateTexture2D(repeatedBitmap)`；
3. 把 raw factory return 写成新 source，并重新武装最终 Release owner。

这样 factory 抛异常时不会对已经释放的旧 source 再做一次 unwind Release；factory
返回空时也不加入防御性替代。fresh bitmap 仍不主动 Release，保留四端可见所有权边界。

新增 unit case 使用 2×2 source 与 `{-1,-1,5,5}` source rect，验证：

- top/left floor-mod 后均为 1，替代 bitmap 为 8×8；
- 第一、第二行分别水平重复四次；
- 第一个后续 band 的开头四像素是原两行连续打包，锁定不对称 native copy；
- callback 收到归一化后的六个 source 顶点；
- 使用替代 texture，callback 一次，原 source 临时引用恢复平衡。

验证结果：

1. 复用真实 `motionplayer_test_args.rsp` 的 Emscripten `-fsyntax-only` 通过，仅既有
   `_tss` warning；
2. `cmake --build out/web/debug -j 8` 重新编译 `MotionRenderBackend.cpp`，成功归档
   `libmotionplayer.a` 并链接 `index.html/index.wasm`；仅既有 Emscripten warning；
3. 四份 recovery IDB 已写入 helper 参数/局部变量语义、helper/caller 注释与 bookmark，
   并全部原位保存。

