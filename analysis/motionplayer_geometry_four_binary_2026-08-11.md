# Motion.Point / Circle / Rect / Quad 四参考二进制复原（2026-08-11）

## 范围与结论

本轮只使用 `reference/binaries/` 的四个目标及其 IDA 数据库：

| 目标 | 架构 | 二进制 |
|---|---|---|
| Android | arm64-v8a | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` |
| Android | armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` |
| iOS | arm64 | `Kirikiroid2_1.3.9_iOS_arm64` |
| iOS | armv7 | `Kirikiroid2_1.3.9_iOS_armv7` |

四端共同证明，公开的 `Motion.Point`、`Motion.Circle`、`Motion.Rect`、
`Motion.Quad` 不是四种尺寸各异的小对象；它们共享同一份几何记录：

```cpp
struct GeometryShape_guess {
    int32_t type;
    // 仅由目标 ABI 的 double 对齐规则产生隐式 padding
    double values[15];
};
```

`values` 的含义是：

| 下标 | 含义 |
|---|---|
| 0, 1 | point 的 x/y；circle 的 cx/cy |
| 2 | circle 的 r |
| 3, 4, 5, 6 | rect 的 left/top/right/bottom |
| 7..14 | quad 的 x0/y0、x1/y1、x2/y2、x3/y3 |

因此 64 位目标和 Android ARMv7 上 `values` 从 `+8` 开始，总大小
`0x80`；iOS ARMv7 的 `double` ABI 对齐为 4，`values` 从 `+4` 开始，
总大小 `0x7c`。不存在显式的源码级 `pad` 字段，也不存在第 16 个
`double`。

## 四文件函数映射

### NCB 类成员注册器

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x68E39C` | `0x56E348` | `0x1000F079C` | `0xECA00` |
| Circle | `0x68E6E0` | `0x56E484` | `0x1000F08BC` | `0xECADA` |
| Rect | `0x68EA84` | `0x56E5CC` | `0x1000F09F4` | `0xECBC8` |
| Quad | `0x68EEB0` | `0x56E760` | `0x1000F0B7C` | `0xECD06` |

四个类在同一目标中都把 `contains` 绑定到同一个原生回调：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x68E1D0` | `0x56E1B0` | `0x1000F0670` | `0xEC8D0` |

### 默认构造与 NCB 构造包装

真正分配原生对象、只写 `type` 的叶函数：

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6DCC98` | `0x59D8E8` | `0x10012CFFC` | `0x12BAD8` |
| Circle | `0x6DD810` | `0x59E41C` | `0x10012DDC8` | `0x12C9D8` |
| Rect | `0x6DE3D0` | `0x59F04C` | `0x10012ED6C` | `0x12D940` |
| Quad | `0x6DED5C` | `0x59FA1C` | `0x10012F9A4` | `0x12E614` |

相应的 NCB `FuncCall` 构造包装：

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6DCBC4` | `0x59D858`（vtable 中为 Thumb 地址 `0x59D859`） | `0x10012CF5C` | `0x12BA08` |
| Circle | `0x6DD73C` | `0x59E38C` | `0x10012DD28` | `0x12C96C` |
| Rect | `0x6DE2FC` | `0x59EFBC` | `0x10012ECCC` | `0x12D8D4` |
| Quad | `0x6DEC88` | `0x59F98C` | `0x10012F904` | `0x12E5A8` |

四端叶函数的共同控制流为：

```cpp
p = operator new(target_is_ios_armv7 ? 0x7c : 0x80);
p->type = point ? 0 : circle ? 1 : rect ? 2 : 3;
return p;
```

没有 `memset`，也没有任何坐标槽的初始化。因此脚本直接执行
`new Motion.Point/Circle/Rect/Quad()` 时，除 `type` 外的全部 15 个
`double` 都保留分配器返回内存中的不定值。NCB 包装在构造/装箱失败时
沿用原有错误返回；成功后由非 sticky adaptor 拥有并销毁该原生记录。

以 Android arm64 Point 为例，相关 adaptor 簇为：分配 `0x6F9D80`、
native reset/delete `0x6F9DB4`、complete destructor `0x6F9DEC`、deleting
destructor `0x6F9E40`。64 位 adaptor 的 native 指针在 `+8`，32 位在
`+4`。其余三类具有相同模板生成拓扑。

### getter 与 Quad.p

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| type | `0x68E628` | `0x56E3FA` | `0x1000F08A4` | `0xECABE` |
| x | `0x68E630` | `0x56E3FE` | `0x1000F08AC` | `0xECAC2` |
| y | `0x68E638` | `0x56E408` | `0x1000F08B4` | `0xECACC` |
| r | `0x68E9DC` | `0x56E552` | `0x1000F09EC` | `0xECBBA` |
| rect l | `0x68EDE0` | `0x56E6B6` | `0x1000F0B4C` | `0xECCCA` |
| rect t | `0x68EDE8` | `0x56E6C0` | `0x1000F0B54` | `0xECCD4` |
| rect w | `0x68EDF0` | `0x56E6CA` | `0x1000F0B5C` | `0xECCDE` |
| rect h | `0x68EE00` | `0x56E6DC` | `0x1000F0B6C` | `0xECCF0` |
| quad p | `0x68F0D4` | `0x56E7F8` | `0x1000F0C5C` | `0xECDA4` |

`w`/`h` 并非存储字段，而是分别返回 `right-left` 与 `bottom-top`。
`Quad.p` 每次构造一个新的 TJS `Array`，按顶点顺序放入四个新字典，
每个字典含 `x`、`y` 两项。

### LayerGetter.shape 与整记录复制

`LayerGetter.shape` 的属性回调：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x699F28` | `0x574F34` | `0x1000F8C60` | `0xF5B38` |

它分别把节点中 `+1664`、`+1424`、`+1680`、`+1392` 的地址交给同一
形状装箱辅助函数：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x68F2C0` | `0x56E914` | `0x1000F0DC4` | `0xECF54` |

四端共同伪代码：

```cpp
switch (source->type) {
case 0: case 1: case 2: case 3:
    copy = operator new(sizeof(GeometryShape_guess));
    memcpy(copy, source, sizeof(GeometryShape_guess));
    return box_with_corresponding_Point_Circle_Rect_or_Quad_adaptor(copy);
default:
    return Void;
}
```

这条路径复制完整记录，而不是只挑当前形状使用的槽位。装箱失败时，
四端路径不回收刚分配的记录；本地保留这个可观察的失败边界。

## contains 的共同源码结构与边界行为

四端共享上述单一回调。用记录槽位写出的共同控制流为：

```cpp
switch (shape.type) {
case 1: {
    dx = x - values[0];
    dy = y - values[1];
    return dx*dx + dy*dy <= values[2]*values[2];
}
case 2:
    if (values[3] > x || values[5] <= x || values[4] > y)
        return false;
    return values[6] > y;
case 3: {
    orientation =
        (y2-y0)*x1 + (x0-x2)*y1
        - ((y2-y0)*x0 + y0*(x0-x2));
    direction = orientation < 0 ? -1 : +1;
    for (edge cur->next, including vertex3->vertex0) {
        edgeExpr = (nextY-curY)*x + (curX-nextX)*y
                 - (curX*(nextY-curY) + curY*(curX-nextX));
        if (direction * edgeExpr > 0)
            return false;
    }
    return true;
}
default:
    return false;
}
```

有限数值下的边界为：point 永远 false；circle 圆周包含，负半径因平方而
与正半径相同；rect 左/上闭、右/下开；quad 四条边都包含，并支持顺、逆
两个绕向。

不得把矩形改写成正向 `left <= x && ...`，也不得把 quad 改写成四个
`cross >= 0` 的合取，因为 IEEE NaN 下不等价：

- rect 的原始否定式使 NaN x 不会触发前两个拒绝比较；只要有限 y 通过，
  结果可为 true。
- quad 只在 `direction * edgeExpr > 0` 时拒绝。查询坐标为 NaN 时，
  edgeExpr 为 NaN，四条边都不拒绝，结果为 true。
- circle 相关算术出现 NaN 时，最终 `<=` 为 false。

唯一逐目标差异是 quad 的 orientation 为 NaN 时方向符号：Android arm64
代码生成先置 `-1`，仅在 `orientation >= 0` 时改为 `+1`，所以 NaN 得
`-1`；Android armv7、iOS arm64、iOS armv7 都先置 `+1`，仅在
`orientation < 0` 时改为 `-1`，所以 NaN 得 `+1`。有限值完全一致。
三端共同形式也更直接对应源码条件表达式，因此移植采用 `< 0 ? -1 : +1`，
并在这里保留 Android arm64 的 NaN-only 编译器差异。

## 修改前本地逐行差异与复刻计划

1. `HitTestInternal.h` 当前显式声明 `pad`、使用 16 个 double，并给成员加
   默认零初始化；改为 `int32_t + std::array<double, 15>`，由 ABI 自动
   padding，并以 `offsetof`/`sizeof` 静态断言锁住四端两种布局。
2. 同文件 circle 计算次序已相符；rect 当前用正向合取，quad 当前用抽象
   `cross >= 0` 合取，两者都会改变 NaN 边界；改成上述拒绝式和原始运算
   次序。
3. `SourceCache.h` 当前四类各自保存小型、零初始化字段，Point/Quad 的
   `contains` 还是 stub；改成共享同一完整基类记录，构造函数只写 `type`，
   所有 getter/contains 从共同槽位读取。
4. `MotionNode.h` 当前拆成 `shapeGeomType + shapeVertices[16]`；改为单一
   `HitData shapeGeometry`，精确表达节点内连续记录。
5. `PlayerUpdateGeometry.cpp` 只机械地把现有 type/槽位写入改指向
   `shapeGeometry.type/values`，不改变已经复原的几何生产公式。
6. `PlayerLayerQuery.cpp` 当前按类型只挑选使用中的字段再构造不同小对象，
   并在 hit-test 前重建临时记录；改为完整记录复制构造相应 facade，hit-test
   直接读取节点记录。保留未知 type 返回 Void、adaptor 失败不删除新记录。
7. `RuntimeSupport.cpp` 的节点赋值当前分开复制 type 与数组；改成一次复制
   `shapeGeometry`，与参考记录的源码级整体成员一致。
8. Android arm64 oracle 适配器当前人为构造 136 字节、16 double 的记录；
   改成带 ABI padding 的 `<i4x15d>` 128 字节布局。现有 JSON case 不需要
   改格式或改预期。

## 落地与验证

本轮已按上节计划落地：

- `HitData`、节点记录和四个 NCB facade 统一为 15-double 整记录；
- 默认脚本构造只写 type，整记录复制路径保留全部槽位；
- rect/quad 恢复参考比较方向及运算顺序；
- Android arm64 oracle 适配器恢复为 128 字节记录；
- 未修改 JSON cases 或 `EXPECTED_HITS`。

验证结果：

1. `cmake --build out/web/debug` 成功完成 21 个增量步骤并链接
   `index.html`；只有仓库既有的 `_tss`、pthread memory growth、JSPI 等
   warning。
2. `cmake --build out/wasmtime/debug --target geometry_hit_test_wasm`
   成功重新生成 guest。
3. 标准 LLDB runner 在当前 Windows 环境先后受缺少 `wasm-objdump` 和
   host Python `wasmtime` 包阻挡；没有为此安装或修改外部依赖。
4. 使用 Node 24 的原生 `WebAssembly` API 直接实例化同一 guest，现有
   10 个 JSON case 全部与 `EXPECTED_HITS` 一致。
5. 同一 guest 的额外只读探针验证：finite rect 数据下 x=NaN 返回 true；
   finite quad 数据下查询 x=NaN 返回 true；finite circle 数据下查询
   x=NaN 返回 false，三项均与本轮四端反编译结论一致。
6. `git diff --check` 无 whitespace error；仅报告工作树既有的 LF→CRLF
   转换提示。

## IDA 数据库改善

四份 IDB 各完成 24 个函数命名（共 96 个），覆盖四类 registrar、共同
contains/getter、四个默认构造叶函数、四个 NCB 构造包装、`Quad.p`、
`LayerGetter.shape` 和整记录装箱辅助函数。所有尚无精确源码名的标识均以
`_guess` 结尾。

此外建立 `MotionGeometryShape_guess` 类型，并针对目标 ABI记录正确布局：

| 目标 | values offset | sizeof |
|---|---:|---:|
| Android arm64 | `0x8` | `0x80` |
| Android armv7 | `0x8` | `0x80` |
| iOS arm64 | `0x8` | `0x80` |
| iOS armv7 | `0x4` | `0x7c` |

给共同 contains、八个简单 getter 和四个默认构造叶函数应用了该类型的函数
签名。重新反编译确认三端使用 `orientation < 0`，Android arm64 使用
`orientation >= 0` 的 NaN-only 差异仍清晰可见。四份数据库均已通过原生
`idb_save` 成功保存回各自 `.i64`。
