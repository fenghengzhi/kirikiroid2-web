# Player 根 flip/opacity/visible/slant/zoom #51–#63（四参考二进制，2026-08-26）

## 1. 范围与表面复用

闭合 13 个脚本表面、19 个不同 native callback：

- `setFlip`、`flipX`、`flipY`；
- `setOpacity`、`opacity`；
- `setVisible`、`visible`；
- `setSlant`、`slantX`、`slantY`；
- `setZoom`、`zoomX`、`zoomY`。

`setOpacity` 方法与 `opacity` property setter 是同一 callback；
`setVisible` 方法与 `visible` property setter 也是同一 callback。二进制没有为
property 额外生成薄 wrapper。

## 2. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| setFlip | `0x6BE2FC` | `0x58A4D8` | `0x100113910` | `0x111326` |
| get/setFlipX | `0x6D6CA0 / 0x6CA448` | `0x59902E / 0x5926AE` | `0x100125738 / 0x10011D14C` | `0x12496C / 0x11BB02` |
| get/setFlipY | `0x6D6CAC / 0x6CA46C` | `0x599038 / 0x5926C6` | `0x100125760 / 0x10011D194` | `0x124994 / 0x11BB3A` |
| get/setOpacity | `0x6D6CB8 / 0x6BE408` | `0x599042 / 0x58A60A` | `0x100125788 / 0x100113AC0` | `0x1249BC / 0x1114E6` |
| get/setVisible | `0x6D6CC4 / 0x6BE428` | `0x59904C / 0x58A622` | `0x1001257B0 / 0x100113B08` | `0x1249E4 / 0x11151E` |
| setSlant | `0x6BE3D8` | `0x58A5C8` | `0x100113A64` | `0x11147C` |
| get/setSlantX | `0x6D6CD0 / 0x6CE73C` | `0x599056 / 0x594884` | `0x1001257D8 / 0x100120024` | `0x124A0C / 0x11ED34` |
| get/setSlantY | `0x6D6CDC / 0x6CE75C` | `0x599068 / 0x5948AA` | `0x100125800 / 0x10012006C` | `0x124A3C / 0x11ED7A` |
| setZoom | `0x6BE334` | `0x58A4FE` | `0x10011396C` | `0x111372` |
| get/setZoomX | `0x6D6CE8 / 0x6CE6FC` | `0x59907A / 0x594838` | `0x100125828 / 0x10011FF94` | `0x124A6C / 0x11ECA8` |
| get/setZoomY | `0x6D6CF4 / 0x6CE71C` | `0x59908C / 0x59485E` | `0x100125850 / 0x10011FFDC` | `0x124A9C / 0x11ECEE` |

Android armv7 的 `setSlant/slantX-set/zoomX-set` 起点原被 IDA 误标成 data。
本轮分别按 `0x58A5C8..0x58A60A`、`0x594884..0x5948AA`、
`0x594838..0x59485E` 恢复为三个独立 Thumb 函数并 fresh decompile。

## 3. root delta 字段坐标

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| dirty | `+0x630` | `+0x540` | `+0x640` | `+0x520` |
| visibleOverride | `+0x632` | `+0x542` | `+0x642` | `+0x522` |
| flipX / flipY | `+0x633/+0x634` | `+0x543/+0x544` | `+0x643/+0x644` | `+0x523/+0x524` |
| scaleX / scaleY | `+0x658/+0x660` | `+0x568/+0x570` | `+0x668/+0x670` | `+0x548/+0x550` |
| slantX / slantY | `+0x668/+0x670` | `+0x578/+0x580` | `+0x678/+0x680` | `+0x558/+0x560` |
| opacity Int32 | `+0x678` | `+0x588` | `+0x688` | `+0x568` |

所有 getter 直接读取 synthetic root，没有 size/null guard。

## 4. 共同 setter 模式

单字段：

```cpp
if (root.field != value) {
    root.dirty = true;
    root.field = value;
}
```

双字段：

```cpp
if (root.x != x || root.y != y) {
    root.dirty = true;
    root.x = x;
    root.y = y;
}
```

编译器在无可抛调用的块内重排部分 store：例如 Android arm64 的双 slant/zoom
可见 `x -> dirty -> y`，Android armv7 的 setFlip 可见 `y -> x -> dirty`，
其余目标多为 `dirty -> x -> y`。共同源结构和单线程脚本结果一致；不能把这些
机器 store 顺序误建成会抛异常的 source statements。

## 5. 精确边界

- 所有 equality gate 都是普通 `!=`；
- double 的 NaN 每次触发写/dirty；+0/-0 相等时单字段 setter 不写；
- 双字段 setter 任一轴不同就写两轴，所以“比较相等”的另一轴仍可能被新 bit pattern
  覆盖；
- slant 与 zoom 不做有限性或范围验证；zoom 接受 0、负数、NaN 和无穷；
- Boolean 值已由 typed NCB 转换为 false/true；AArch64 leaf 额外显式 `&1`；
- opacity 保存完整 signed Int32，不 clamp 到 0..255，不做 unsigned cast；
- visible 访问 `visibleOverride`，不是 LayerGetter 的 branch/layer composite visible；
- setFlip 比较 X 后短路比较 Y；变化时同时写两轴；
- 方法/property 共享的 opacity/visible setter 具有完全相同副作用和错误边界。

## 6. 本地对照

`PlayerLayerQuery.cpp` 当前实现与四端共同源结构逐项一致：

- getter/单轴/双轴的字段选择正确；
- dirty gate 正确；
- opacity 不 clamp；
- visible 使用 root override；
- slant/zoom 接受任意 double；
- 共享 method/property 由 registrar 绑定到同一 C++ 函数。

本纵切面未发现新的运行 C++ 偏差，状态为 `EVIDENCED_4_4`。四个 IDB 已统一
函数名、签名、注释并保存；正式 build/test 仍因工具链缺失不可运行。

