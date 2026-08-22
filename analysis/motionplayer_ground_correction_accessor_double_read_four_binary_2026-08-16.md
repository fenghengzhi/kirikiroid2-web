# ground-correction 返回 accessor 与 `getRealValue` 双读取：四参考二进制恢复记录

日期：2026-08-16

本纵切面复审 `Player_applyGroundCorrection_guess` 在 callback 返回之后的解析链。旧端口已经恢复
current/parent 参数顺序、callback owner、结果 Object 转换、missing-index 默认零和 x/y/z 增量
提交，但把每个坐标简化成了一次 `PropGetByNum(0)`。四份参考共同使用
`ncbPropAccessor::getRealValue(index, 0.0)`：先以 `TJS_MEMBERMUSTEXIST` 探测，再在探测成功时
执行第二次 flags-0 读取和转换。脚本 getter 的调用次数、重入值、状态码判定和异常位置都可观察。

## 1. worker 与结果 accessor 映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| ground worker | `0x6B7DF0` | `0x585230` | `0x10010DFF4` | `0x10B8FC` |
| result Variant copy / accessor construction | `0x6B808C..0x6B80E0` | `0x5852F8..0x58530E` | `0x10010E124..0x10010E148` | `0x10BA60..0x10BA82` |
| first coordinate read | `0x6B80E4..0x6B8138` | `0x58532A` | `0x10010E158` | `0x10BAA4` |

callback result 先 copy-construct 成临时 `tTJSVariant`；随后栈槽写入前一纵切面已经识别的
`ncbPropAccessor` vptr，`AsObject()` 取得独立 dispatch 引用，再销毁 Variant 临时。源码等价为：

```cpp
ncbPropAccessor corrected{tTJSVariant(result)};
```

这纠正了端口中第二个手写 `RetainedVariantDispatch_guess` 用途；callback 的 borrowed raw
dispatch 仍是独立 AddRef/Release owner，不应和 result accessor 混为同一源码对象。

## 2. `getRealValue`、`HasValue` 与 `GetValue` 映射

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `getRealValue(index,default)` | worker 内联 | `0x4BFBD8` | `0x1000FBB5C` | `0xF8BC0` |
| `HasValue(index)` | worker 内联 | `0x4C76C4` | `0x1001271CC` | `0x126724` |
| `GetValue<double>(index,Tag,flags)` | `0x66699C` | `0x4C7734` | `0x1000F2FF8` | `0xEF66C` |

recovery IDB 现分别使用：

- `ncbPropAccessor_getRealValueByNum_guess`
- `ncbPropAccessor_HasValueByNum_guess`
- `ncbPropAccessor_GetValueArrayReal_guess`

Android arm64 把前两层内联到 worker，但保留第三层共享实例；另外三端保留完整三层调用。共同
实现逐项匹配仓库 `ncbind.hpp`：

```cpp
double ncbPropAccessor::getRealValue(IndexT index, double defaultValue) {
    if(HasValue(index)) {
        return GetValue(index, ncbTypedefs::Tag<double>());
    }
    return defaultValue;
}
```

## 3. 第一次探测

`HasValue(index)` 对一个初始 Void Variant 调用：

```cpp
_obj->PropGetByNum(
    TJS_MEMBERMUSTEXIST, index, &probeValue, _obj);
```

四端 flags 都是 `0x400`。它以 signed `status >= 0` / `TJS_SUCCEEDED(status)` 判断存在，不是
`status == TJS_S_OK`：

- `TJS_S_OK`、`TJS_S_TRUE`、`TJS_S_FALSE` 都进入第二次读取；
- 任意负状态直接返回 caller 提供的 `0.0`；
- probe Variant 的值不参与坐标转换，并在判断返回前销毁；
- 本调用没有请求 type 输出，所以第一次值的 Variant type 也不会改变路由。

这与上一纵切面的 setter exact-zero 形成有意的非对称：`SetValue` 只认零，`HasValue` 接受所有
非负状态。

## 4. 第二次读取

probe 成功后，`GetValue<double>` 重新构造一个 Void Variant，调用：

```cpp
_obj->PropGetByNum(0, index, &value, _obj);
return convertToReal(value);
```

第二次调用的 `tjs_error` 被忽略，转换和 Variant 析构照常执行。因而一个 reentrant/custom
dispatch 可以在两次调用之间返回不同值；参考消费第二次 flags-0 调用写入的值。第一次返回的
值只证明 member probe 成功。第二次 getter 抛出的异常传播；x/y/z 是三个独立调用，前一坐标
已经提交时后一坐标失败不会回滚。

missing index 只有一次 `MEMBERMUSTEXIST` 调用，不发生第二次 flags-0 读取，并使用独立默认
`0.0`。三个 index 的完整正常顺序是：

```text
(0, MEMBERMUSTEXIST), (0, 0),
(1, MEMBERMUSTEXIST), (1, 0),
(2, MEMBERMUSTEXIST)          // 若 2 missing
```

## 5. 本地恢复与回归

`PlayerUpdateLayersInternal.h` 现在用 `ncbPropAccessor corrected{tTJSVariant(result)}`，并依次调用
三次 `corrected.getRealValue(tjs_int32(index), 0.0)`。结果专用
`RetainedVariantDispatch_guess::fromOwnerVariant` 已删除；callback raw dispatch 的
`fromRawDispatch` owner 保持不变。

测试中的 callback 现在返回一个记录型 result dispatch：

- index 0 的 probe 返回正状态 `TJS_S_TRUE` 和值 `111.0`，第二次返回 `401.25`；最终 x 必须是
  `401.25`，证明 positive status 被接受且第一次值被丢弃；
- index 1 的两次值为 `222.0`、`-502.5`；最终 y 必须消费第二次值；
- index 2 的 probe 返回 `TJS_E_MEMBERNOTFOUND`；必须只有一次调用并得到 z=0；
- 精确断言 flags/index 序列为 `0x400/0` 的两阶段顺序；
- 结果 accessor 与 callback result Variant 在函数退出时共同释放 dispatch，析构计数恰为 1。

## 6. IDB 与验证

四份 recovery IDB 已更新上述 helper 命名、result accessor 构造和双读取注释，加入
`ground correction accessor double-read (2026-08-16)` 书签并全部原位保存。

验证结果：

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer 测试翻译单元 syntax-only
  通过，仅有仓库既有 `_tss` 弃用警告；
- Web Debug 与 Wasmtime Headless Debug 增量构建通过；
- 定向 `git diff --check` 无新增内容级 whitespace error，仅有换行转换提示。
