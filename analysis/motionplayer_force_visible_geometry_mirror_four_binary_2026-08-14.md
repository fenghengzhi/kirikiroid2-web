# force-visible `emoteEdit` 几何镜像：四参考二进制恢复记录

日期：2026-08-14

本记录只闭合 `Player_updateLayers` 顶点阶段末尾的 force-visible TJS 写回块。它纠正了旧移植
中三项会完全改变行为的猜测：目标不是未赋值的 `tjsLayerObject`，第一个数组键不是 `c`，
异常也不会被吞掉。相邻的 render-item/material/stencil 生成不在本记录的完成声明内。

## 1. 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 顶点阶段函数 | `0x6B98D0` | `0x5866F8` | `0x10010F6AC` | `0x10CE30` |
| force gate / block 入口 | `0x6BA768` | `0x587208` | `0x1001102C0` | `0x10D748` |
| `coord` PropGet | `0x6BA7EC` | `0x587242` | `0x100110308` | `0x10D788` |
| `coord[0..1]` 写入 | `0x6BA860..0x6BA874` | `0x587264..0x587272` | `0x100110340..0x100110354` | `0x10D7B0..0x10D7C2` |
| `mtx` PropGet | `0x6BA8A0` | `0x587288` | `0x100110374` | `0x10D7E0` |
| `mtx[0..3]` 写入 | `0x6BA914..0x6BA950` | `0x5872AE..0x5872DE` | `0x1001103AC..0x1001103E8` | `0x10D808..0x10D83E` |
| 十个 named PropSet | `0x6BA970..0x6BAAB8` | `0x5872F6..0x5873F4` | `0x100110408..0x10011056C` | `0x10D868..0x10DA0E` |
| 逆序 Release 尾部 | `0x6BAAC4..0x6BAB08` | `0x5873FE..0x587426` | `0x10011057C..0x1001105CC` | `0x10DA12..0x10DA46` |

三个重复包装函数也在四份 recovery IDB 中统一命名：

| wrapper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ncbPropAccessor_SetValueArrayReal_guess` | `0x6BE0E8` | `0x58A39C` | `0x100113758` | `0x1110F0` |
| `ncbPropAccessor_SetValueNamedReal_guess` | `0x671290` | `0x55B0E4` | `0x100113810` | `0x1111E8` |
| `ncbPropAccessor_SetValueNamedIntegerByte_guess` | `0x5A2540` | `0x4E2568` | `0x100102BD0` | `0xFFFF8` |

## 2. 字段身份与进入条件

四端都只检查节点的 force-visible 整数字段；进入后立即复制同一节点持有的
`emoteEdit` Variant：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `emoteEditVariant` | node `+1980` | node `+1708` | node `+1996` | node `+1672` |
| `forceVisible` | node `+1996` | node `+1716` | node `+2012` | node `+1680` |

这与节点初始化中从 raw layer 读取 `emoteEdit` 并保存 Variant owner 的链一致。旧字段
`tjsLayerObject` 在当前项目中没有赋值点，因此旧 gate `forceVisible && tjsLayerObject` 会让整个
原生块永久失效。参考实现没有这个额外 null/type guard：void Variant 会走普通 Variant→Object
转换异常；Object 内部 raw pointer 为 null 则继续进入原生的不安全调用边界。

本块仍位于顶点 materialization 内层 gate 之内。因此它必须先满足外层
`forceVisible || type-mask` 与 `source.valid`，再满足内层
`forceVisible || (5185/5193 type-mask && !source.blank)`；本记录没有把它提升到函数级无条件
写回。

## 3. UTF-16 键名

四端内存字节均直接解码为：

```text
coord: 63 00 6f 00 6f 00 72 00 64 00 00 00
mtx:   6d 00 74 00 78 00 00 00
```

对应地址为：

| 键 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `coord` | `0x14D3BC6` | `0xD845F8` | `0x10195B2F8` | `0x174D65C` |
| `mtx` | `0x14D53E8` | `0xD84F90` | `0x10195B71E` | `0x174DA82` |

A64 的旧 IDA item 只显示 `"c"`，iOS 两端的 `mtx` item 只显示 `"m"`，都是宽字符串数据
类型识别错误，不是不同平台的短键。四份 IDB 现已将这两个 item 分别命名为
`aCoord_forceVisible_utf16_guess` 和 `aMtx_forceVisible_utf16_guess`。

## 4. 共同数据流

四端共同伪代码如下：

```cpp
if(node.forceVisible) {
    retained base = retainObject(copy(node.emoteEditVariant));

    Variant coordResult;
    base->PropGet(0, L"coord", nullptr, &coordResult, base);
    retained coord = retainObject(copyThenClear(coordResult));
    setArrayReal(coord, 0, mappedAnchorX, 0x200);
    setArrayReal(coord, 1, mappedAnchorY, 0x200);

    Variant matrixResult;
    base->PropGet(0, L"mtx", nullptr, &matrixResult, base);
    retained matrix = retainObject(copyThenClear(matrixResult));
    setArrayReal(matrix, 0, accumulatedM11, 0x200);
    setArrayReal(matrix, 1, accumulatedM12, 0x200);
    setArrayReal(matrix, 2, accumulatedM21, 0x200);
    setArrayReal(matrix, 3, accumulatedM22, 0x200);

    setReal(base, L"width",  sourceWidth,  0x200, widthHint);
    setReal(base, L"height", sourceHeight, 0x200, heightHint);
    setReal(base, L"originX", sourceOriginX + activeSlotOriginX,
            0x200, originXHint);
    setReal(base, L"originY", sourceOriginY + activeSlotOriginY,
            0x200, originYHint);
    setIntegerByte(base, L"flipX", accumulatedFlipX, 0x200, flipXHint);
    setIntegerByte(base, L"flipY", accumulatedFlipY, 0x200, flipYHint);
    setReal(base, L"zoomX",  accumulatedZoomX,  0x200, zoomXHint);
    setReal(base, L"zoomY",  accumulatedZoomY,  0x200, zoomYHint);
    setReal(base, L"slantX", accumulatedSlantX, 0x200, slantXHint);
    setReal(base, L"angle",  accumulatedAngle,  0x200, angleHint);
} // release matrix, coord, base
```

`mappedAnchorX/Y` 是已经经过 mesh ancestor 两阶段映射后的 anchor，不是 quad 左上角、
source origin 或写入 `vertexPosX/Y` 的旧值。`coord` 和 `mtx` 必须已存在并且可转为 Object；
原版原地覆写它们的 Array 数字成员，不创建或替换 Array。

## 5. wrapper ABI 与 Variant 类型

三个 wrapper 在四端的虚调用槽一致：

- Array real wrapper：构造 `tvtReal`，调用 vtable `+56`（32 位 `+28`）的
  `PropSetByNum(flags,index,value,objthis)`；
- named real wrapper：构造 `tvtReal`，调用 vtable `+48`（32 位 `+24`）的
  `PropSet(flags,name,hint,value,objthis)`；
- byte wrapper：读取一个 unsigned byte，构造 `tvtInteger`，再调用同一 named PropSet；
- 每次 `flags` 都是 `0x200 == TJS_MEMBERENSURE`；dispatch 自身同时作为 objthis；
- wrapper 返回 `tjs_error == TJS_S_OK` 的 Boolean，但顶点函数忽略所有返回值。

2026-08-16 的四端 caller/xref 复审进一步闭合了这一返回 ABI：三类 wrapper 均以 exact-zero
比较 materialize `bool`，正状态 `TJS_S_TRUE/TJS_S_FALSE` 也返回 false；当前四个链接产物中的
所有可见调用者都以 statement 形式忽略返回值。指令点、xref 计数和本地 ABI 回归见
`analysis/motionplayer_dispatch_setter_boolean_return_four_binary_2026-08-16.md`。

因此 flipX/flipY 不能写成 Real，也不能误以为 TJS 有独立 Boolean Variant 类型。

named property 的 member-hint 指针不是 null。width/height/originX/originY 使用插件中已有的
共享全局槽；angle 与 timeline content 的 angle reader 共用一个全局槽；force-visible 的
flipX、flipY、zoomX、zoomY、slantX 各有独立全局槽。A64 对应地址依次为：

```text
width  0x1AB520C   height 0x1AB5210
originX 0x1AB5214  originY 0x1AB5218
flipX  0x1AB5428   flipY 0x1AB542C
zoomX  0x1AB5430   zoomY 0x1AB5434
slantX 0x1AB5438   angle 0x1AB5158
```

其中 `0x1AB5158` 同时有 timeline slot content merge 与本块两个函数族的交叉引用，排除了
为镜像另建 angle cache 的实现。

## 6. 所有权、异常与部分写入边界

原生每一层 Object 都遵循相同骨架：Variant copy → Object AddRef → Variant clear；因此
base、coord、mtx 在后续脚本回调中分别拥有独立 dispatch 引用。正常尾部按 mtx、coord、base
逆序 Release。iOS armv7 的 SjLj call-site 标号覆盖每一步，异常 landing pad 会销毁已经构造
成功的 owner；四端都没有 catch-and-ignore 语义。

边界结果是：

1. base Variant 为非 Object、缺少 `coord`/`mtx`、或成员结果不能转 Object时，普通转换异常
   向上传播；
2. PropGet/PropSet 的 `tjs_error` 返回值本身被忽略；失败且没有抛异常时继续执行；
3. 写入是严格增量的。若在任意 callback 抛异常，之前的 Array 索引或 named property 保留，
   没有事务回滚；
4. `mtx` 在两个 coord 索引之后才读取，因此 coord 写回 callback 可以影响随后 mtx 查找；
5. 三个 owner 的独立 AddRef 保证 callback 删除/替换 dictionary 成员时，当前正在写的 dispatch
   仍存活到对应逆序清理点。

旧移植使用 `try { ... } catch (...) {}`，既吞掉上述异常，又跳过 `coord`/`mtx` 实际写入，
与四端均不相符，现已删除。

## 7. 本地实现与验证

实现变更：

- `PlayerUpdateGeometry.cpp`：force gate 直接消费 `emoteEditVariant`，调用完整镜像 helper；
- `PlayerUpdateLayersInternal.h`：恢复三类 setter、三层 retained dispatch owner、UTF-16
  `coord`/`mtx` PropGet、精确写入顺序和异常传播；
- `MotionDispatch.h`、`RuntimeSupport.cpp`：补齐共享 angle hint 和五个 mirror-only hint；
- `PlayerUpdateLayerEval.cpp`：timeline angle read 接回与镜像共用的 angle hint；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：验证原地 Array 更新、矩阵/标量顺序对应值、
  flip 的 `tvtInteger` 类型、void owner 与 missing coord 的异常边界；后续 exact-zero 回归还验证
  三个 setter 对 `TJS_S_OK`、正状态和负错误码的 Boolean 返回，以及 flags/index/member/hint/
  objthis 和 Variant 类型。

2026-08-16 的后续源码身份复审进一步确认三层 owner 实际都是 `ncbPropAccessor`，named getter
是 `GetValue<tTJSVariant>` 模板，三类 setter 是 `SetValue<T>` 模板实例；本地手写复制层已替换
为 ncbind 原类型。完整布局、临时链和 IDB 命名见
`analysis/motionplayer_force_visible_ncb_prop_accessor_source_identity_four_binary_2026-08-16.md`。

验证结果：

- motionplayer 完整测试翻译单元 Emscripten syntax check 通过；
- `Web Debug Build` 34 个增量目标完整编译、静态库链接和最终 `index.html`/Wasm 链接通过；
- 2026-08-16 复审后，普通/headless 两种 syntax-only、Web Debug 和 Wasmtime Headless Debug
  增量构建均通过；
- 四份 recovery IDB 的 wrapper 函数、宽字符串和 block 生命周期注释均已写入并成功保存；
- `git diff --check` 仅保留仓库既有 LF/CRLF 转换提示，无新增内容级 whitespace error。

## 8. 未在本纵切面宣告闭合的内容

- force-visible block 前面的完整 vertex materialization 已由 mesh-chain 记录覆盖，但两份记录
  合起来仍不等于相邻 render-item/material/stencil 构造已恢复；
- `tjsLayerObject` 的 ground-correction 残留已在
  `motionplayer_ground_correction_four_binary_2026-08-11.md` 的 2026-08-14 复审中闭合：真实链是
  `Player.rootPlayer.currentDispatch`，与本块的 `emoteEditVariant` 无关；
- wrapper 的全部当前 code xref 已在 2026-08-16 纵切面审计：四端所有可见调用者都忽略返回
  Boolean；共享 accessor/template 的源码归属也已闭合为 `ncbPropAccessor::SetValue<T>`。
  未批量改写的只是其他业务调用点各自的上层源码结构，不再是本 force-visible 路径的缺口。
