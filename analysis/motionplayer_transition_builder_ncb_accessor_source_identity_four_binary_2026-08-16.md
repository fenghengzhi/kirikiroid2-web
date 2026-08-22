# MotionPlayer Transition builder `ncbPropAccessor` 源码身份与元素生命周期（四参考，2026-08-16）

## 结论

四份当前参考的 `EmoteEngine_buildTransitionControl_guess` 都具有与相邻 leaf builder 相同、但已
独立重新验证的 accessor/source owner 骨架：

```cpp
ncbPropAccessor controlObject{tTJSVariant(transitionControl)};
const int count = controlObject.GetArrayCount();
for (int metadataIndex = 0; metadataIndex < count; ++metadataIndex) {
    const tTJSVariant element =
        controlObject.GetValue(metadataIndex, Tag<tTJSVariant>());
    ncbPropAccessor elementObject{tTJSVariant(element)};
    if (!elementObject.GetValue("enabled", Tag<bool>(), 0, enabledHint))
        continue;
    // raw controller allocation -> deque owner
    entry.label =
        elementObject.GetValue("label", Tag<ttstr>(), 0, labelHint);
    // type-7 publication
}
```

循环外 root accessor 持有 copied control dispatch。每轮 indexed getter 返回独立 source element
Variant，第二份 Variant copy 构造 element accessor；构造 copy 随即析构，source element 保留到
本轮公共尾部。disabled 与 enabled 路径都先释放 element accessor、再析构 source element；循环
完成或 count 为零时才释放 root accessor。

Transition 不把 metadata element 传给 `EmoteVarController(1)` constructor，但 source Variant
仍真实存在到 iteration tail，因此不能把反编译中的 source storage 与 accessor storage合并。
既有 raw controller→deque ownership、sparse index、flag=1 与 partial publication 边界不变。

## 函数映射

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x66A8A4` | `0x557B84` | `0x1001A9C9C` | `0x1A9314` |

函数名继续带 `_guess`，表示 stripped binary 的语义恢复名。

## Root accessor 与 count

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| input Variant copy | `0x66A8D8` | `0x557B9C` | `0x1001A9CC4` | `0x1A9336` |
| root accessor vptr/AsObject | `0x66A8F0..0x66A928` | `0x557BA6..0x557BAC` | `0x1001A9CD4..0x1001A9CE0` | `0x1A9358..0x1A937E` |
| copied input temp dtor | `0x66A92C` | `0x557BB0` | `0x1001A9CE8` | `0x1A9382` |
| `GetArrayCount()` | `0x66A938` | `0x557BB6` | `0x1001A9CF4` | `0x1A9390` |

`GetArrayCount()` 对 root `_obj` 执行 `PropGet(0,"count",nullptr,&temporary,_obj)`，忽略
HRESULT 后把 Variant 转为 integer，并只在循环前读取一次。失败但写入 count 的 dispatch 因此
仍能驱动循环；这里没有 `GetCount()` 或 `TJS_SUCCEEDED` gate。

## Indexed source 与 element accessor copy

| 阶段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| indexed source getter | `0x66A988` | `0x557C00` | `0x1001A9D3C` | `0x1A93BA` |
| second Variant copy | `0x66A9A8` | `0x557C08` | `0x1001A9D48` | `0x1A93C6` |
| accessor vptr/AsObject | `0x66A9B0..0x66A9EC` | `0x557C0C..0x557C16` | `0x1001A9D4C..0x1001A9D58` | `0x1A93CC..0x1A93D8` |
| accessor-construction temp dtor | `0x66A9F0` | `0x557C1A` | `0x1001A9D60` | `0x1A93DC` |

Android ARM64 把 indexed Variant getter 展开为 `PropGetByNum`、source copy 和 temporary dtor；
另外三端调用的 helper 分别是 `0x5334E0 / 0x1000691F8 / 0xED9A8`。共同模板对 root
accessor `_obj` 执行 `PropGetByNum(0,index,&temporary,_obj)`，忽略 HRESULT，再 copy 成返回
Variant。

## enabled/label hint 与 typed getter

Transition 使用 V133 已恢复出的两个 Engine-wide identity：

| hint | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `label` | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |
| `enabled` | `0x1AB4F20` | `0x11114B8` | `0x101B69FD0` | `0x187D9F0` |

| getter | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| enabled | `0x66AA0C` | `0x557C34` | `0x1001A9D7C` | `0x1A9406` |
| label | `0x66AB04` | `0x557CA8` | `0x1001A9DF0` | `0x1A9484` |

两次调用 flags 都是 0，receiver 与 `objthis` 都是 element accessor retained `_obj`。bool 与
string template 均忽略 `PropGet` HRESULT；只要失败 getter 写入 Variant，转换/复制仍继续。
Transition 的 enabled/label hint 与 Eye、Eyebrow、Mouth 的对应 pointer identity完全相同，不是
builder-local cache。

## 清理顺序

| owner cleanup | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| element accessor | `0x66AB74..0x66AB8C` | `0x557D02..0x557D0A` | `0x1001A9E58..0x1001A9E6C` | `0x1A94EE..0x1A94FC` |
| source element Variant | `0x66AB94` | `0x557D0E` | `0x1001A9E74` | `0x1A9502` |
| root accessor | `0x66ABA4..0x66ABBC` | `0x557D28..0x557D30` | `0x1001A9E88..0x1001A9E9C` | `0x1A9514..0x1A9522` |

disabled gate 直接汇入 element cleanup；enabled publication 完成后也汇入同一点。因此 portable
源码按 source element→element accessor 的声明顺序，依赖 C++ 逆序析构，精确表达共同路径。

## Portable 源码与回归

`cpp/plugins/motionplayer/EmoteEngine.cpp` 的 Transition builder 已从四个 plugin-local raw getter
改成真实 root/element `ncbPropAccessor`，复用 `engineEnabledHint_guess` 与
`engineLabelHint_guess`。controller allocation、raw-pointer emplace、entry flag=1、label-after-
emplace 和 type-7 map publication 顺序未变。

既有 leaf builder dispatch probe 已扩展 Transition case：

- root count 与 indexed getter均写有效 Variant后返回 `TJS_E_FAIL`；
- outer array 在 indexed getter 内立即放弃 element owner，返回 source Variant仍保持 element
  存活；
- element enabled/label 同样失败但写值；
- 测试锁定 flags=0、index=0、`objthis`、read order、flag=1 和 element 恰好一次析构；
- Transition 的 enabled/label hint pointer 必须分别等于 Eye/Eyebrow/Mouth 的共享 identity。

## Recovery IDB 写回与验证

四份 recovery IDB 的 builder 入口、indexed source、enabled、label、element accessor cleanup、
source Variant dtor 和 root cleanup 均已加注，入口增加 V134 书签；四个函数 force-recompile 后
逐点 disassembly readback 成功，并已原位保存。

验证包括：

1. 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer test TU syntax-only 通过，仅有
   仓库既有 `_tss` warning；
2. Web Debug 与 Wasmtime Headless Debug 增量构建通过；
3. 两份最终 wasm 可由 `llvm-objdump -h` 解析；
4. Transition builder 的旧 raw getter 定向扫描为零；
5. 本专题源码、测试、文档和 `plan.md` 的限定 `git diff --check` 通过。
