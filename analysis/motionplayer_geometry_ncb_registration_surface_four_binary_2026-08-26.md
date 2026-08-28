# Point/Circle/Rect/Quad NCB 注册表面（MP-A09，四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合 Motion 下四个 geometry subclass 的 NCB member registrar：构造器
描述符、成员名称、顺序、descriptor 类型和 native binding 目标。`contains` 的
几何算法、构造后 record 内容以及 getter 的返回转换另行闭合，不能由本注册表面
报告替代。

## 2. 四端 registrar 映射

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Point | `Point_ncb_members@0x68E39C` | `Point_ncb_members@0x56E348` | `Point_ncb_members@0x1000F079C` | `Point_ncb_members@0xECA00` |
| Circle | `Circle_ncb_members@0x68E6E0` | `Circle_ncb_members@0x56E484` | `Circle_ncb_members@0x1000F08BC` | `Circle_ncb_members@0xECADA` |
| Rect | `Rect_ncb_members@0x68EA84` | `Rect_ncb_members@0x56E5CC` | `Rect_ncb_members@0x1000F09F4` | `Rect_ncb_members@0xECBC8` |
| Quad | `Quad_ncb_members@0x68EEB0` | `Quad_ncb_members@0x56E760` | `Quad_ncb_members@0x1000F0B7C` | `Quad_ncb_members@0xECD06` |

所有地址只属于表头对应的二进制。16 个 registrar 均在本轮以各自 `database`
fresh decompile。IDB 原先均为 `sub_*`，本轮以二进制中的精确类名/成员名和
registrar 控制流为依据改成上述语义名，四库分别保存。

## 3. 共同控制流与部分提交边界

四类使用同一个生成式 registrar 骨架：

```text
if !registering:
    return

publish zero-argument typed constructor descriptor under dynamic class name
if !registering: return

publish read-only property "type"
if !registering: return

publish typed method "contains"
if !registering: return

publish remaining read-only properties in class-specific order
after every descriptor publication:
    if registrar is no longer registering, stop immediately
```

Android arm64 内联展开每个 descriptor 的 allocation、native-function
construction、getter/method pointer 写入和 `addMember`；Android armv7 与 iOS
保留更多 typed helper。四端 source-level publication 顺序一致。

构造器的第一个描述符使用动态类名而不是字面成员名 `Factory`：

- Android arm64 在四个 member registrar 内直接 new constructor descriptor，
  native callback slot 为 0；
- Android armv7 分别调用 `0x56E3D4/0x56E52C/0x56E690/0x56E7D0`；
- iOS arm64 分别调用
  `0x1000F0854/0x1000F099C/0x1000F0AFC/0x1000F0C0C`；
- iOS armv7 分别调用 `0xECA98/0xECB94/0xECCA4/0xECD7C`。

本轮 fresh decompile Point 的三个 out-of-line constructor helper；它们共同以
`arg0=0` 创建 zero-argument typed constructor descriptor，并仅在 registering
为真时用 dynamic class name 发布。

## 4. 精确成员表

| 类 | descriptor 顺序 |
|---|---|
| Point | constructor, property-ro `type`, method `contains`, property-ro `x`, property-ro `y` |
| Circle | constructor, property-ro `type`, method `contains`, property-ro `x`, property-ro `y`, property-ro `r` |
| Rect | constructor, property-ro `type`, method `contains`, property-ro `l`, property-ro `t`, property-ro `w`, property-ro `h` |
| Quad | constructor, property-ro `type`, method `contains`, property-ro `p` |

四端的 `type` getter 和 `contains` body 在这四个 class descriptor 中共享同一
native binding；Point/Circle 还共享 `x/y` getters。

## 5. native binding 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| shared `type` getter | `sub_68E628@0x68E628` | `sub_56E3FA@0x56E3FA` | `sub_1000F08A4@0x1000F08A4` | `sub_ECABE@0xECABE` |
| shared `contains` method | `sub_68E1D0@0x68E1D0` | `sub_56E1B0@0x56E1B0` | `sub_1000F0670@0x1000F0670` | `sub_EC8D0@0xEC8D0` |
| shared `x` getter | `sub_68E630@0x68E630` | `sub_56E3FE@0x56E3FE` | `sub_1000F08AC@0x1000F08AC` | `sub_ECAC2@0xECAC2` |
| shared `y` getter | `sub_68E638@0x68E638` | `sub_56E408@0x56E408` | `sub_1000F08B4@0x1000F08B4` | `sub_ECACC@0xECACC` |
| Circle `r` getter | `sub_68E9DC@0x68E9DC` | `sub_56E552@0x56E552` | `sub_1000F09EC@0x1000F09EC` | `sub_ECBBA@0xECBBA` |
| Rect `l` getter | `sub_68EDE0@0x68EDE0` | `sub_56E6B6@0x56E6B6` | `sub_1000F0B4C@0x1000F0B4C` | `sub_ECCCA@0xECCCA` |
| Rect `t` getter | `sub_68EDE8@0x68EDE8` | `sub_56E6C0@0x56E6C0` | `sub_1000F0B54@0x1000F0B54` | `sub_ECCD4@0xECCD4` |
| Rect `w` getter | `sub_68EDF0@0x68EDF0` | `sub_56E6CA@0x56E6CA` | `sub_1000F0B5C@0x1000F0B5C` | `sub_ECCDE@0xECCDE` |
| Rect `h` getter | `sub_68EE00@0x68EE00` | `sub_56E6DC@0x56E6DC` | `sub_1000F0B6C@0x1000F0B6C` | `sub_ECCF0@0xECCF0` |
| Quad `p` getter | `sub_68F0D4@0x68F0D4` | `sub_56E7F8@0x56E7F8` | `sub_1000F0C5C@0x1000F0C5C` | `sub_ECDA4@0xECDA4` |

这些地址在本报告中只证明 descriptor 的 binding identity；上述 getter/method
函数体仍需各自四端 fresh decompile 才能标为 `EVIDENCED_4_4`。

## 6. 差异

- Android arm64 使用大段 inline descriptor construction；Android armv7 和
  iOS 使用较短的 typed helper 调用；这是模板/优化器展开差异。
- LP64 descriptor 分配大小可见为 constructor `0x38`、property `0x50`、method
  `0x40`；ILP32 的同一源码模板物理大小不同。它们是 ABI 产物，不进入共享 C++
  的对象 padding。
- iOS 与部分 Android 字面量在未补 helper 参数类型前显示成 `"t"`、`"x"`
  等 ASCII 首字符。完整 UTF-16LE 字节、共享 literal 地址和 registrar 顺序证明
  真实键为 `type/x/y/...`。显示差异不是脚本 API 差异。

## 7. 本地逐项对照

`cpp/plugins/motionplayer/main.cpp` 当前四个
`NCB_REGISTER_SUBCLASS_DELAY` block：

- constructor 数量与动态类名 publication 形态一致；
- `type/contains` 顺序一致；
- Point、Circle、Rect、Quad 的 class-specific property 名称和顺序逐项一致；
- 所有属性均为 `NCB_PROPERTY_RO`，`contains` 为普通 typed `NCB_METHOD`。

注册表面没有发现 C++ 偏差，因此本轮不修改运行语义。

## 8. 剩余纵切面

下一步分别闭合：

1. shared `type` getter 的 native record tag；
2. shared `contains` 的 Point/Circle/Rect/Quad 分支、比较边界和 NaN 行为；
3. `x/y/r/l/t/w/h/p` getter 的 Variant/Array owner 与 numeric conversion；
4. 四类 zero-argument constructor 产生的 record 默认内容与异常边界。
