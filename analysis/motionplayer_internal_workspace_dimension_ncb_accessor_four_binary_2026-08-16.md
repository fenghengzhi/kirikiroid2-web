# Internal workspace dimension `ncbPropAccessor` source identity（四参考，2026-08-16）

## 范围与结论

本纵切面重新检查 `Player_materializeInternalRenderLayers_guess` 和
`Player_updateAccurateSLAAfterDraw_guess` 中四组 `height -> width` 尺寸读取。旧本地实现虽然已经
模拟出正确的两次 `PropGet`，却把它抽象成接收裸 `iTJSDispatch2 *` 的独立 helper，隐藏了参考
代码真实的 accessor 身份和跨调用 owner 生命周期。

四端共同证据显示：两个函数分别只构造一个 target `ncbPropAccessor`。同一对象从 target
Variant 取得并保有 dispatch，materializer 中还先用它读取 `window`，随后复用于 `height` 和
`width`，直到函数尾才执行虚析构/`Release`。每个尺寸的源级行为等价于：

```cpp
tjs_int readDimension(ncbPropAccessor &target,
                      const tjs_char *name,
                      tjs_uint32 *hint) {
    if(!target.HasValue(name, hint)) {
        return 0;
    }
    return target.GetValue(
        name, ncbTypedefs::Tag<tjs_int>(), 0, hint);
}
```

这不是 `getIntValue(name, 0)` 的无 hint 版本：四端的 probe 和 ordinary get 都明确收到同一个
process-wide mutable member-hint slot。因此本地恢复使用显式 `HasValue` + `GetValue<tjs_int>`，
而不是当前 `ncbind.hpp` 中不接受 hint 的 convenience overload。

## Materializer 四端映射

| 目标 | function | accessor vptr store | height probe / get | width probe / get | accessor Release |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6CB57C` | `0x6CB5D8` | `0x6CB790` / `0x6CB7C0` | `0x6CB7FC` / `0x6CB828` | `0x6CBA4C` |
| Android armv7 | `0x592F7C` | `0x592FAC` | `0x593012` / `0x59302E` | `0x593046` / `0x593066` | `0x593160` |
| iOS arm64 | `0x10011E2BC` | `0x10011E310` | `0x10011E3AC` / `0x10011E3D4` | `0x10011E3F8` / `0x10011E420` | `0x10011E584` |
| iOS armv7 | `0x11CAC8` | `0x11CB50` | `0x11CBEA` / `0x11CC16` | `0x11CC42` / `0x11CC70` | `0x11CDA6` |

accessor 在上表的 vptr store 前先 copy-construct target Variant，再用 `AsObject()` 取得 owning
dispatch。它在尺寸读取前已经执行 `GetValue<tTJSVariant>("window", ...)`，并跨过 primary
Layer 创建/发布。两个尺寸读取后，它仍跨过两次 `setSize`、secondary work Layer 创建/发布，
最后才释放。这说明尺寸 helper 只借用 accessor 引用，不能独立 AddRef/Release，也不能用临时
Variant 重新构造第二个 accessor。

## Accurate-SLA 四端映射

| 目标 | function | accessor vptr store | height probe / get | width probe / get | accessor Release |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6CBD18` | `0x6CBD78` | `0x6CBE44` / `0x6CBE70` | `0x6CBEB4` / `0x6CBEE4` | `0x6CBFF0` |
| Android armv7 | `0x593344` | `0x593378` | `0x5933B6` / `0x5933D2` | `0x5933EC` / `0x59340A` | `0x5934D2` |
| iOS arm64 | `0x10011E808` | `0x10011E860` | `0x10011E8BC` / `0x10011E8E4` | `0x10011E908` / `0x10011E930` | `0x10011EA44` |
| iOS armv7 | `0x11D078` | `0x11D0FE` | `0x11D15C` / `0x11D188` | `0x11D1B4` / `0x11D1DE` | `0x11D2C4` |

accurate-SLA helper 在 producer flag 为 false 时不会构造 accessor。true path 构造 target
accessor，调用 materializer，再构造 internal Layer owner；随后仍由最初的 target accessor 读取
height 和 width。`piledCopy(0,0,target,0,0,width,height)` 完成并清理参数/internal owner 后，
target accessor 最后释放。materializer 内部自己的 accessor 是一次嵌套调用的独立 owner；它与
外层 accurate-SLA accessor 生命周期重叠，但二者不互相转移所有权。

## 模板 helper 与精确边界

| 模板实例 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `ncbPropAccessor::HasValue(name,hint,type)` | 两 caller 内联 | `0x496BE8` | `0x10010905C` | `0x10686C` |
| `ncbPropAccessor::GetValue<tjs_int>(name,Tag,flags,hint)` | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |

`HasValue` 的共同边界：

- 对 accessor 的 retained `_obj` 调 `PropGet(TJS_MEMBERMUSTEXIST, name, hint, ...)`；
- receiver 与 `objthis` 都是同一个 `_obj`；
- `TJS_S_OK`、`TJS_S_TRUE`、`TJS_S_FALSE` 等所有非负状态都视为存在；
- probe Variant 总会销毁，其值不参与最终整数转换；
- 可选 type 指针仅在成功时接收 probe Variant type；这里 caller 均传 null。

`GetValue<tjs_int>` 随后再次对同一 `_obj` 调
`PropGet(0, name, hint, ...)`，不检查第二次返回码，直接把第二个临时 Variant 转成 integer 后
销毁。因此“probe 写 111，ordinary failing get 写 -909”必须返回 -909；missing probe 则只调用
一次并返回默认 0。第二次失败且没有写值时，默认 Void Variant 的 integer 转换自然得到 0，
不是由显式错误分支提供的 fallback。

## 本地恢复

- `PlayerRenderTargets.cpp` 删除接收裸 dispatch 的复制 helper；恢复为接收调用者
  `ncbPropAccessor&` 的 `HasValue`/`GetValue<tjs_int>` 源级抽取；
- materializer 和 accurate-SLA 的四个调用点直接传各自已有的 `targetAccessor`，不再调用
  `GetDispatch()`；
- `PlayerRenderInternal.h` 声明该 `_guess` helper，供生产 caller 与精确边界回归共用；
- 单元探针锁定同一个 objthis、同一个 hint、`MEMBERMUSTEXIST -> flags 0` 双读、非零成功状态、
  第二次失败状态忽略，以及 missing 只 probe 一次；
- 四份 recovery IDB 将可见模板实例命名为
  `ncbPropAccessor_HasValueNamed_guess` / `ncbPropAccessor_GetValueNamedInteger_guess`，补充
  prototype、function comment、两个 caller bookmark，并全部原位保存。

绝对地址仅保留在本文和 recovery IDB；编译源继续只使用带 `_guess` 的语义名。

## 验证

- ordinary motionplayer syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` motionplayer syntax-only：通过；
- Web Debug 完整增量构建/最终链接：`11/11`，通过；
- Wasmtime Headless Debug 完整增量构建：`20/20`，通过；
- 两个 syntax-only 只报告仓库既有 `_tss` literal-operator warning；Web 链接只报告既有
  pthread memory-growth、JSPI 与 JS-library warning；
- scoped source scan 与 `git diff --check`：通过。并行初始化两个 Emscripten 环境时其中一份
  `emsdk.ps1` 在删除共享临时 `emsdk_set_env.ps1` 时报告文件已被另一份先删除，但三个命令
  各自 exit code 均为 0，全部编译/链接步骤已经成功，不属于源码或构建目标失败。
