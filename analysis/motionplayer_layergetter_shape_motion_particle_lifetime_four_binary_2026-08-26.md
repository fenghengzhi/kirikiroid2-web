# LayerGetter shape/motion/particle 返回生命周期（四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合 LayerGetter 最后三个对象/Variant getter：

- `shape`：从 node 内嵌完整 `HitData` 复制出新的 Point/Circle/Rect/Quad facade，
  经各自 ClassInfo/CreateAdaptor 生成脚本对象；
- `motion`：仅 type-3 node 返回持久 child-player Variant 的 CopyRef；
- `particle`：仅 type-4 node 返回持久 particle-Array Variant 的 CopyRef。

这三者不是快照同一类 owner：shape 每次分配一个新的 native geometry copy 和新的
脚本 shell；motion/particle 只复制 node 已有 Variant owner，不创建新 child/Array。

## 2. 四端映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `shape` getter | `0x699F28` | `0x574F34` | `0x1000F8C60` | `0xF5B38` |
| shape dispatch builder | `LayerGetter_buildShapeDispatch_guess@0x68F2C0` | `...@0x56E914` | `...@0x1000F0DC4` | `...@0xECF54` |
| `motion` getter | `0x699FB0` | `0x574F7E` | `0x1000F8CE8` | `0xF5C0C` |
| `particle` getter | `0x699FD4` | `0x574F9A` | `0x1000F8D0C` | `0xF5C28` |

12 个 getter/builder 主体均 fresh decompile；四端通用 Variant copy helper也 fresh
decompile（含 iOS thunk 的真实 body）。builder 和三个 getter 已写入 IDB 语义名/注释。

## 3. `shape` 数据流

四端 node 内嵌 geometry record 起点：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `node + 1664` | `node + 1424` | `node + 1680` | `node + 1392` |

共同伪代码：

```text
switch embeddedHitData.type:
    case 0: native = new Point(bitwise_copy(full HitData)); class = Point
    case 1: native = new Circle(bitwise_copy(full HitData)); class = Circle
    case 2: native = new Rect(bitwise_copy(full HitData)); class = Rect
    case 3: native = new Quad(bitwise_copy(full HitData)); class = Quad
    default: return Void

dispatch = ncbInstanceAdaptor<class>::CreateAdaptor(native,
                                                    sticky=false,
                                                    err=false)
if dispatch == null:
    return Void
return ObjectVariant(dispatch, dispatch), balancing local dispatch reference
```

复制大小在 Android arm64/armv7/iOS arm64 为 `0x80`，iOS armv7 为 `0x7c`，严格
复制 type 和全部 15 个 double（包括未初始化/NaN/padding 字节）。它不是按 shape
种类只复制已用坐标，也不重新计算 Rect width/height 或 Quad orientation。

type 不在 0..3 时，在任何分配前返回 Void；负值和大正值行为相同。返回 Void 的机器
路径只写 Variant type tag 0，不需要清理 payload owner，因为这是未构造的隐藏返回槽。

## 4. `shape` CreateAdaptor 所有权与泄漏边界

有效 type 路径先分配 native copy，再检查类状态。四端 ClassInfo 使用 geometry
构造器报告中的四组独立 class object/class ID；没有一个通用 geometry class shell。

CreateAdaptor 的精确边界：

1. ClassInfo class object 为空：返回 null；已经分配的 native copy 不删除，泄漏；
2. class object 用“恰好一个 Void”调用 `CreateNew`，让脚本构造器只创建空的非 sticky
   adaptor shell，不再分配第二个 native geometry；
3. 获取全局 script dispatch 作 ObjThis/调用上下文，`CreateNew` 返回后平衡 Release；
4. `CreateNew` 返回错误或 null object：返回 null；native copy 不删除，泄漏；
5. shell 创建成功后，以类专属 ID 调 `TJS_NIS_GETINSTANCE`：
   - 找到 adaptor：写入 native copy；adaptor 成为 owner；
   - 找不到/类型不兼容：不写入、不删除 native copy，但仍返回非 null shell；
6. `err=false`，上述普通失败不主动抛 ncbind 的辅助异常；若底层 `CreateNew` 自身抛
   C++ 异常，函数也没有 catch/delete native，异常传播且 native copy 泄漏。

因此 `shape` getter 的“非 null dispatch”不能证明 native 已附着：不兼容 shell 仍会
被包装成 object Variant返回，而 unattached native copy 泄漏。这一反直觉行为已由
Android arm64 inline body和三端 class-specific CreateAdaptor helper共同证明，本地
`makeShapeVariant` 注释/调用也保留它。

getter 对返回 dispatch 构造 object Variant 时，Object 与 ObjThis 各 AddRef 一次；
随后 Release CreateNew 的局部 dispatch 引用，最终 Variant持有两条引用边。正常
adaptor 为 non-sticky，脚本对象 Invalidate/析构时 scalar-delete geometry copy。

## 5. `motion` 与 `particle`

共同伪代码：

```text
motion(node):
    if node.type == 3: return Variant CopyRef(node.childPlayerVariant)
    return Void

particle(node):
    if node.type == 4: return Variant CopyRef(node.particleArrayVariant)
    return Void
```

持久 Variant 的 ABI 坐标：

| owner | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| child motion Variant | `+1912` | `+1668` | `+1928` | `+1632` |
| particle Variant | `+2296` | `+1968` | `+2312` | `+1932` |

匹配路径调用通用 Variant copy-constructor：先复制 payload/type，然后按 type 分类
增加所有权（object 对 Object/ObjThis 各 AddRef、string 原子增 ref、octet 增加普通
refcount）。虽然正常字段预期持有 object/Array，getter 没有强制 type gate；若 node
内部被其他 native 路径写成 string/int/Void，它仍按实际 Variant type 完整 CopyRef。

不匹配路径只构造 Void，不读取持久 Variant，也不清空/改变 node 内 owner。返回的
CopyRef 是 live owner 的独立引用，但后续 node 替换自己的 Variant 不会改变已返回
Variant 所指的旧 dispatch/value。

## 6. 本地逐行对照

`PlayerLayerQuery.cpp` 当前：

- `buildShapeVariant_guess` 按 0/1/2/3 分配对应 derived facade，并从同一完整
  `HitData` 构造；默认返回 Void；
- `makeShapeVariant` 使用 class-specific `ncbInstanceAdaptor<T>::CreateAdaptor`，
  构造 Object+ObjThis Variant并平衡 dispatch Release；失败不主动 delete 传入 copy；
- `getMotion` 只在 `nodeType == 3` 返回 `childPlayerVar`；
- `getParticle` 只在 `nodeType == 4` 返回 `particleArrayVar`；
- 两个字段按值返回 `tTJSVariant`，使用通用 CopyRef，而非裸 borrowed dispatch。

语义与四端逐项一致，无需修改运行 C++。

## 7. 结构与剩余边界

embedded HitData、child Variant、particle Variant 在 reference node 中的相对位置是
完整 `MotionNode` source-order 账本的新约束；当前 portable 声明按逻辑分组，尚不能
宣称声明顺序 1:1。

shape helper 的普通失败/泄漏边界已经闭合；各目标 C++ exception table 对底层
CreateNew 抛出时的精确 landing-pad metadata 仍未逐 call-site 展开，但源层无 catch
且没有 native delete 的事实由四端控制流和 ncbind helper共同确定。

## 8. 2026-08-27 CreateNew EH 闭包

`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md` 已展开四类
CreateAdaptor 的完整异常表面：Android arm64合并 body landing、iOS arm64四个相邻
LSDA-only cold cleanup与 iOS armv7四个 SjLj cleanup都只析构一枚 Void参数 Variant；
Android armv7四个完整 adaptor body无本帧 cleanup。四端都不删除预分配 geometry copy，
也不补偿 CreateNew抛出前取得的 global dispatch引用，因此共同泄漏边得到精确支持。
shape wrapper的 active return Variant cleanup也已闭合。该 row 现为 `IMPLEMENTED`。
