# PreparedRenderItem camera-offset / stereovision post-prepare pass：四参考二进制联合证据（2026-08-13）

## 1. 结论与旧实现纠正

四个参考二进制共同证明：`prepareRenderItems` 完成递归构建和稳定排序后，普通
Layer、SeparateLayerAdaptor、SLA/D3D 以及 render-to-texture 的相应路线会调用同一个
post-prepare pass。它不是旧注释所称的单纯 `applyTranslateOffset`，而是一个不可拆散的
两阶段原地变换：

1. 对排序后的 **main pointer-vector** 中每个 item 始终执行 float camera-offset 平移；
2. 若 Player 的 `stereovisionActive` 为真，并且 item 的 `sortKey` 与立体相机 Z 不作
   ordered-equal，则对选定几何执行 double 透视计算、窄化回 float，并从投影后几何
   重建 `paintBox`。

这纠正了本地旧实现中的四个实质偏差：

- 旧实现把 float 加法提升为 double 后再窄化；参考实现直接执行 float 加法；
- 旧实现只在 `hasViewport` 为真时平移 viewport；参考实现从不读取这个 Web 辅助位，
  连 `{1,1,-1,-1}` 无效数值哨兵也照常平移；
- 旧实现完全缺少 stereovision 投影以及 `paintBox` 重建；
- 旧 `setStereovisionCameraPosition` 构造 TJS Array 并覆盖 `_cameraPosition` Variant；
  四端 setter 都只是把三个 double 直接写入 Player 的三个独立字段。

`cameraActive` 不参与本 pass。投影门控字段由 UTF-16 `stereovisionActive` 注册项、其
getter/setter 的直接 byte 读写、以及 pass 对同一偏移的读取三条独立证据闭合。

## 2. 四文件目标与映射

本轮重新核对了四个目标与四个已打开 IDB 的 `module`、`input_path` 和 imagebase；四者
均为 `reference/binaries/` 中当前要求的目标，Hex-Rays 可用。

| 目标 | prepare 外层 wrapper | post-prepare pass | `setStereovisionCameraPosition` |
|---|---:|---:|---:|
| Android arm64-v8a `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Player_prepareRenderItems_guess` `0x6D2544` | `sub_6D2644`, 1012 bytes；已重命名 `Player_applyPreparedRenderItemProjection_guess` | `sub_6CD420`；已重命名 `Player_setStereovisionCameraPosition_guess` |
| Android armv7 `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Player_prepareRenderItems_guess` `0x596DF0` | `sub_596EB0`, 1144 bytes；已重命名同上 | `sub_593F44`；已重命名同上 |
| iOS arm64 `Kirikiroid2_1.3.9_iOS_arm64` | `Player_prepareRenderItems_guess` `0x100122F68` | `sub_100123038`, 912 bytes；已重命名同上 | `sub_10011F54C`；已重命名同上 |
| iOS armv7 `Kirikiroid2_1.3.9_iOS_armv7` | `Player_prepareRenderItems_guess` `0x121FDC` | `sub_1220F0`, 1148 bytes；已重命名同上 | `sub_11E038`；已重命名同上 |

### `stereovisionActive` 注册/访问器闭环

| 目标 | UTF-16 字符串 | getter | setter | pass 所读 Player byte |
|---|---:|---:|---:|---:|
| Android arm64 | `0x14D6454` | `0x6D6AFC` | `0x6D6B04` | `+1095` / `+0x447` |
| Android armv7 | `0xD85D62` | `0x598F7E` | `0x598F84` | `+747` / `+0x2EB` |
| iOS arm64 | `0x10195CB56` | `0x100125638` | `0x100125640` | `+983` / `+0x3D7` |
| iOS armv7 | `0x174EEBA` | `0x124862` | `0x124868` | `+683` / `+0x2AB` |

四组 getter/setter 已分别重命名为
`Player_getStereovisionActive_guess` / `Player_setStereovisionActive_guess`。

### setter 注册字符串与直接存储

| 目标 | UTF-16 `setStereovisionCameraPosition` | 注册 xref | 三个 double 字段 |
|---|---:|---:|---:|
| Android arm64 | `0x14D66C6` | `0x6D649C` | `+120/+128/+136` |
| Android armv7 | 已由注册区定位 | `0x59883A` | `+88/+96/+104` |
| iOS arm64 | `0x10195CFB4` | `0x1001252F0` | `+96/+104/+112` |
| iOS armv7 | `0x174F318` | `0x124522/0x124528/0x124534` | `+72/+80/+88` |

四个 setter body 均没有 Array/Dictionary 创建、Variant CopyRef、释放或异常清理：只按 ABI
把 `x/y/z` 三个 double 写进上述连续字段。例如 Android arm64 为
`STP D0,D1,[X0,#0x78]` 后接 `STR D2,[X0,#0x88]`。

## 3. Player 与 PreparedRenderItem 字段对应

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| stereovision camera X/Y/Z | `+120/+128/+136` | `+88/+96/+104` | `+96/+104/+112` | `+72/+80/+88` |
| camera offset X/Y（float） | `+144/+148` | `+112/+116` | `+120/+124` | `+96/+100` |
| stereovisionActive | `+1095` | `+747` | `+983` | `+683` |

item 深度字段就是稳定排序所用的 `sortKey`，不是另一个 projection-only 副本：

- Android arm64 builder 在 `0x6C080C` 从 node `+0x5F8` 取 double，并在
  `0x6C0810` 写 item `+0x40`；pass 同样读取 item `+0x40`；
- Android/iOS armv7 的对应 item 字段为 `+0x28`；
- iOS arm64 对应 item 字段同为 `+0x40`；
- 64 位 command coordinate 在物理布局上不是连续 triple：z/sortKey 位于 `+64`，而
  command x/y 位于 `+104/+112`。这与
  `motionplayer_get_command_list_four_binary_2026-08-12.md` 的序列化证据一致。

## 4. 调用链与容器边界

四端调用链的共享源码形状是：

```text
render route
  -> prepareRenderItems(mainList, auxList)
       -> recursive prepared-item builder
       -> stable sort(mainList, item.sortKey)
  -> post-prepare camera/projection pass(mainList)
  -> route-specific draw / render-to-canvas / render-to-texture
```

已核对的调用点包括：

- Android arm64 `Player_renderToSeparateLayerAdaptor_guess` `0x6D2A38`，调用 pass 于
  `0x6D2A84`；
- Android armv7 对应 wrapper `0x597328`，调用于 `0x597368`，另有
  `0x597654/0x59773E/0x597A06`；
- iOS arm64 对应 wrapper `0x1001233C8`，调用于 `0x10012340C`，另有
  `0x100123880/0x100123A40/0x100123ED0`；
- iOS armv7 pass xrefs 包括 `0x1225FA/0x122B26/0x122D12/0x123194`。

函数参数只携带 main pointer-vector；aux 不是空 vector 参数，而是根本不存在于该调用
边界。pass 借用 persistent `PreparedRenderItem` 指针并原地修改，不分配、删除或保留
item；main/aux vector 的栈上所有权与 item 的 node 持久所有权均不改变。
`getCommandList` 只 prepare/sort/serialize，不调用此 pass，这也是四端独立的一条调用链
边界。

## 5. 四端共同控制流

联合反编译可抽象为以下源码级伪代码：

```cpp
if (player.stereovisionActive) {
    originX = player.stereovisionCameraX + double(player.cameraOffsetX);
    originY = player.stereovisionCameraY + double(player.cameraOffsetY);
    projectionZ = player.stereovisionCameraZ;
} else {
    originX = originY = projectionZ = 0.0;
}

for (PreparedRenderItem *item : mainList) {
    float-translate item.corners;
    float-translate item.commandCompositeMeshPoints;
    if (item.meshType == 1)
        float-translate item.meshPoints;
    float-translate item.paintBox;
    float-translate item.viewport; // unconditional

    if (!player.stereovisionActive || item.sortKey == projectionZ)
        continue;

    item.paintBox = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
    for each point in corners,
        then commandCompositeMeshPoints,
        then meshPoints iff meshType == 1:
    {
        double x = floatPoint.x;
        double y = floatPoint.y;
        double z = item.sortKey;
        double denominator = z - projectionZ;
        floatPoint.x = float(x - z * (x - originX) / denominator);
        floatPoint.y = float(y - z * (y - originY) / denominator);

        if (floorf(floatPoint.x) < paintBox.left)   update left;
        if (floorf(floatPoint.y) < paintBox.top)    update top;
        if (ceilf(floatPoint.x)  > paintBox.right)  update right;
        if (ceilf(floatPoint.y)  > paintBox.bottom) update bottom;
    }
}
```

`commandBezierPatchPoints` 是 raw command payload，两个阶段均故意不访问。viewport 只
参与 float 平移，不参与 perspective，也不参与投影后的 paintBox union。

## 6. 数值与边界行为

### 6.1 平移次序和精度

- camera offset 的存储类型与每个几何分量均为 float；第一阶段用 `FADD S`、
  `VADD.F32` 或相应 vector float 指令完成，不能改写成 double addition；
- 平移先发生，projection 才把结果 float 扩展为 double；这个中间窄化对大数、subnormal、
  infinity 和舍入边界可观察；
- origin X/Y 使用 `camera double + double(cameraOffset float)`。因此正常有限数下 offset
  在 `(x-origin)` 中常会代数抵消，但第一阶段 float 舍入使实际计算顺序仍不可删减。

### 6.2 depth equality、NaN 与除零

- 四端都是 ordered equality 分支（如 arm64 `FCMP ...; B.EQ`）；
- `+0.0 == -0.0`，故两者跳过 perspective；
- NaN 不相等，故进入 perspective，而不是跳过；
- 唯一 denominator 保护就是此前的 ordered equality。NaN、infinity 及其他 IEEE 异常值
  继续进入普通 double 运算，不另做 finite 检查。

### 6.3 paintBox 哨兵与增长

- 投影前重置值是精确的 `{+FLT_MAX,+FLT_MAX,-FLT_MAX,-FLT_MAX}`；不是旧分析曾误记的
  `{1,1,-1,-1}`。Android arm64 常量原始字节为两项 `0x7F7FFFFF` 后接两项
  `0xFF7FFFFF`；
- left/top 使用 floorf 后的 ordered `<`，right/bottom 使用 ceilf 后的 ordered `>`；
- 相等不写回，NaN 比较 unordered 也不写回；若所有被访问点都投影成 NaN，完整极值
  哨兵保持不变；
- depth ordered-equal 或 stereovision 关闭时，paintBox 只有第一阶段平移，绝不重建。

### 6.4 viewport、mesh 类型和 raw Bezier

- viewport 的四个 float 无条件平移；reference item 没有 Web `hasViewport` 判定；
- `commandCompositeMeshPoints` 无条件平移/投影；
- processed `meshPoints` 仅在 `meshType == 1` 时平移/投影；其他 mesh type 的 vector 即使
  非空也不访问；
- raw `commandBezierPatchPoints` 始终保持原值。

## 7. 四目标代码生成差异

四端共享控制流、字段角色、容器选择、计算公式、分支默认值和生命周期一致。观察到的
差异属于 ABI/编译器代码生成：

- 64 位 item 指针和 vector 三指针布局扩大，使 sortKey、corners、paintBox、viewport、
  三个 point vector 的物理偏移不同于 32 位；
- arm64 广泛以 `D`/SIMD pair 执行两个相邻 float 的平移和 paintBox 常量写入；armv7
  展开为 VFP scalar/paired 指令与显式 `floorf`/`ceilf` 调用；
- iOS arm64 用 `FRINTM`/`FRINTP` 完成 floor/ceil 语义，Android armv7 调用导入函数；
- armv7 需要额外寄存器保存与循环计数，因而函数体约 1144/1148 bytes；iOS arm64 的
  vectorized leaf body 只有 912 bytes。这些不是第二套源码算法。

## 8. 本地复刻

- `Player.h`
  - 删除错误的 `_rootOffsetX/Y/Z` 解释，建立三个独立 stereovision camera double；
  - private pass 改名为 `applyPreparedRenderItemProjection_guess`。
- `PlayerLayerQuery.cpp`
  - `setStereovisionCameraPosition` 改为三次直接 double 存储，不再创建 Array，也不触碰
    `_cameraPosition`。
- `PlayerRenderItems.cpp` / `PlayerRenderInternal.h`
  - 新增可独立验证的 `applyPreparedRenderItemProjectionCore_guess`；
  - 完整保留 native 两阶段顺序、float/double 边界、main-only 容器边界、mesh type gate、
    raw Bezier 排除、viewport 无条件平移、ordered equality 以及 paintBox 极值重建；
  - member wrapper 只把 Player 字段传给该 core。
- `PlayerDrawDispatch.cpp` / `PlayerRenderTargets.cpp`
  - 既有五个 post-prepare 调用点统一改用新 member 名；没有把 pass 错误加入
    `getCommandList`。
- Wasmtime trace 的函数/事件名同步改为 `apply_prepared_projection`，删除仍把这条调用边界
  描述成单纯 translate 的过时命名和旧单库地址注释。
- Frida oracle staged-diagnostic 挂点由陈旧的 `0x6D5264` 改为当前 Android arm64
  reference 的 `0x6D2644`，阶段名同步为 projection，并删除旧探针中并不存在的第三参数。
  对应 render-step comparator 的预期 kind 也同步更新。

## 9. 测试、构建与 IDB 保存

- Catch2 回归覆盖：
  - active projection 对 corners、composite points、type-1 processed mesh 的精确结果；
  - raw Bezier 不变、无效 viewport 仍平移且不投影；
  - `sortKey=-0.0` 与 `projectionZ=+0.0` ordered-equal，只执行 float translation；
  - meshType 非 1 的 processed vector 不访问；
  - NaN depth 进入 projection，所有几何 NaN 时 paintBox 保留完整 `±FLT_MAX` 哨兵；
  - stereovision camera setter 不再覆盖独立 `_cameraPosition` Variant。
- `Web Debug Build`：成功编译并完成最终链接；超时的前台等待对应后台 Ninja 正常完成，
  随后复核为 `ninja: no work to do`。
- `Wasmtime Headless Debug Build`：成功完成 motionplayer/guest objects 与最终链接；只有
  仓库既有 `_tss`、pthread memory-growth、JSPI 和 Emscripten JS library warning。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp`：复用 Web Debug 的真实 Emscripten
  defines/includes/ABI 参数并加入 `out/syntax-check` Catch2、test config 与既有 syntax
  stub，执行 `-fsyntax-only` 成功；唯一诊断为既有 `_tss` warning。当前配置没有可直接
  运行该 Catch2 executable 的原生目标，因此这里只声明完整翻译单元编译验证。
- Frida oracle agent 通过 Node `--check`；render-step comparator 通过 Python
  `py_compile`。
- `git diff --check`：通过；仅有工作树既有 LF→CRLF 提示。
- 四个 IDB 均已加入 pass/setter/accessor 的 `_guess` 名称和关键语义注释，并全部原位
  保存成功。
