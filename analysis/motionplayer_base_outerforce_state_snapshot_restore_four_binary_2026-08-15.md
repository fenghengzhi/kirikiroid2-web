# Base / OuterForce state snapshot 与 restore 四端复原（2026-08-15）

## 范围与结论

本纵切面闭合顶层 state pipeline 最后的两个单对象 child：Base 与 OuterForce。覆盖
serialize/restore 入口、七字段 schema、member-hint 共享、outer dispatch 生命周期、flags-0
getter、child controller 传参和缺字段/异常前缀。

两个 serializer 都创建新 Dictionary，并按固定顺序逐字段发布：Base 是
`coord -> scale -> color -> rotate`，OuterForce 是 `bust -> hair -> parts`。对应 restore 先只
检查输入 Variant 的 Type 是否为 Object；通过后复制 closure、取得并 retain 一份 dispatch，
销毁临时 Variant，然后在同一 accessor 上按相同顺序执行 flags 0 的 PropGet。getter 的
HRESULT 被忽略，返回的 Variant 立即按值交给对应 controller restore，后续失败不回滚已
恢复的前序 controller。

Base 的缺字段行为不对称：前三项进入 `EmoteVarController_restoreState_guess`，该函数对
非 Object/Void 静默返回；最后 `rotate` 进入无 outer type gate 的 Angle restore，因此缺失
`rotate` 会在前三个 controller 已处理后抛出 Object 转换异常。OuterForce 三项都是 Var
controller，单纯缺字段都会静默略过。

## 四端入口映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| serialize Base | `0x674F88` | `0x55CC70` | `0x1001B0DC4` | `0x1B073C` |
| serialize OuterForce | `0x675208` | `0x55CDF0` | `0x1001B0F98` | `0x1B0980` |
| restore Base | `0x67846C` | `0x55EAC0` | `0x1001B24DC` | `0x1B1F8C` |
| restore OuterForce | `0x67872C` | `0x55EC4C` | `0x1001B26CC` | `0x1B21DC` |

restore 使用的 flags-forwarding named Variant getter 在 Android ARM64 内联；另外三端入口
分别是 Android ARMv7 `0x55218C`、iOS ARM64 `0x1000F1860`、iOS ARMv7 `0xEDBF0`，四端
语义均为：初始化 Void probe、PropGet(flags/name/hint)、忽略 HRESULT、copy-construct 返回
Variant、销毁 probe。

## 七字段、宽字面量与共享 hint

普通 IDA string rendering 在多处只显示 `c/r/b/h/p`，或把相邻 UTF-16LE 当作错误的宽
字符串。按 `ida-search-string` 的 UTF-16LE raw-byte 流程恢复到完整边界后，七个 key 为：

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `coord` | `0x14D3BC6` | `0xD845F8` | `0x101960010` | `0x1752374` |
| `scale` | `0x14C6A22` | `0xD7B54C` | `0x10196001C` | `0x1752380` |
| `color` | `0x14C6982` | `0xD7B4DC` | `0x101960028` | `0x175238C` |
| `rotate` | `0x14D3BD2` | `0xD84604` | `0x101960034` | `0x1752398` |
| `bust` | `0x14D3BE0` | `0xD84612` | `0x101960042` | `0x17523A6` |
| `hair` | `0x14D3BEA` | `0xD8461C` | `0x10196004C` | `0x17523B0` |
| `parts` | `0x14D3BF4` | `0xD84626` | `0x101960056` | `0x17523BA` |

每个字段的 serialize PropSet 与 restore PropGet 复用同一个 mutable hint：

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `coord` | `0x1AB4FCC` | `0x1111564` | `0x101B6A07C` | `0x187DA9C` |
| `scale` | `0x1AB4FD0` | `0x1111568` | `0x101B6A080` | `0x187DAA0` |
| `color` | `0x1AB4FD4` | `0x111156C` | `0x101B6A084` | `0x187DAA4` |
| `rotate` | `0x1AB4FD8` | `0x1111570` | `0x101B6A088` | `0x187DAA8` |
| `bust` | `0x1AB4FDC` | `0x1111574` | `0x101B6A08C` | `0x187DAAC` |
| `hair` | `0x1AB4FE0` | `0x1111578` | `0x101B6A090` | `0x187DAB0` |
| `parts` | `0x1AB4FE4` | `0x111157C` | `0x101B6A094` | `0x187DAB4` |

这些槽与 metadata 的 `scale/bustControl/hairControl/partsControl` hints 不同；相同或相近的
key 文本不能作为合并 hint identity 的依据。

## serialize 数据流与返回 closure

Base 的四端共同伪代码：

```text
dictAccessor = fresh native Dictionary
child = serializeVar(position); dict.PropSet(MEMBERENSURE, "coord", child, coordHint)
child = serializeVar(scale);    dict.PropSet(MEMBERENSURE, "scale", child, scaleHint)
child = serializeVar(color);    dict.PropSet(MEMBERENSURE, "color", child, colorHint)
child = serializeAngle(angle);  dict.PropSet(MEMBERENSURE, "rotate", child, rotateHint)
return Object closure whose Obj and ObjThis are dictAccessor.dispatch
```

OuterForce 完全同构，仅替换为：

```text
serializeVar(bustOuterForce)  -> "bust"
serializeVar(hairOuterForce)  -> "hair"
serializeVar(partsOuterForce) -> "parts"
```

每个 child Variant 在对应 PropSet 返回后立即销毁；下一个 child serialize 不与前一个临时
重叠。Dictionary accessor 一直活到结果 closure 完成双槽 AddRef/发布，随后释放自己的引用。
若 child serialize 或 PropSet 抛出，当前 child 与 dictionary accessor 都清理，未完成的
Dictionary 不返回；不存在部分 Dictionary 对调用者的发布。

## restore outer accessor、getter 和提交顺序

共同骨架为：

```text
if input.Type != Object: return

temporary = copy input
force/check Object
objectAccessor = AddRef(temporary.Object dispatch)
destroy temporary before first child lookup

for field in fixed order:
    childController = engine.fieldOwner.get()
    probe = Void Variant
    objectAccessor.PropGet(flags=0, field.name, field.hint,
                           &probe, objectAccessor.dispatch)
    childArg = copy(probe)                  // regardless of HRESULT
    destroy probe
    restore matching controller(childController, childArg)
    destroy childArg

Release objectAccessor
```

这里的 retained accessor 是跨全部字段的单一 owner，不是每次 PropGet 都从输入 Variant
借用 dispatch。getter 脚本若重入、改写输入持有者或释放外部引用，原 Object dispatch 在整次
Base/OuterForce restore 完成前仍存活。源码此前逐字段调用通用 `motionPropGet(value, ...)`，
只借用原 Variant 的 dispatch；本次已恢复一次 copy/force/retain、临时 early-destroy 与共享
accessor 的生命周期。

flags 0 与“忽略 HRESULT”也很关键：缺属性不会在 wrapper 层变成 early return，而是产生
Void childArg 并照常调用 controller restore。其后果由 child 类型决定：

| wrapper 字段 | child restore | missing field |
|---|---|---|
| Base `coord` | Var | 静默返回 |
| Base `scale` | Var | 静默返回 |
| Base `color` | Var | 静默返回 |
| Base `rotate` | Angle | Object 转换失败并抛出 |
| OuterForce `bust` | Var | 静默返回 |
| OuterForce `hair` | Var | 静默返回 |
| OuterForce `parts` | Var | 静默返回 |

若属性存在但 getter 自己抛出，异常立即传播，后续字段不处理。若 getter 返回 failed HRESULT
但把 probe 留为 Void，行为如表；wrapper 不检查错误码。若某 child restore 在字段中途抛出，
前序 controller 的逐字段提交保留，outer accessor 和当前 Variant 临时量仍按 unwind 释放。

输入 Type 是 Object 但 closure dispatch 为 null 时，四端仍进入 accessor/PropGet 路径；没有
额外 null-friendly guard。源码同样不把这种无效 closure 转成“缺少 Base/OuterForce”。

## 本地修正、IDB 回写与验证

- 源码新增七个 Base/OuterForce state hint，serialize 与 restore 按字段复用同一槽。
- 两个 serializer 的七个 `PropSet` 接回真实 hint，字段顺序和 MEMBERENSURE 保持不变。
- 两个 restore 改为一次 copy/force/accessor retain/temporary early-destroy，并通过 accessor
  flags-0 Variant getter 按固定顺序立即调用 child restore。
- UTF-16LE raw search 恢复七个完整 key 边界；四份 recovery IDB 已建立语义数据名，并为
  四个入口、literal、hint、顺序和缺字段边界补注释/bookmark，随后原位保存。
- 真实 response-file `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有 `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，成功重编
  `EmoteEngine.cpp`、生成 `libmotionplayer.a` 并链接最终 `index.html`；仅有既有 `_tss`、
  pthread memory-growth 与 Emscripten/JSPI warnings。
- 定向 `git diff --check` 通过；换行转换提示不属于 whitespace error。

本页闭合 Base/OuterForce 子树；各 Var/Angle controller 字典内部字段与异常前缀由 controller
state family 和 Angle lifecycle 证据页单独记录。

2026-08-16 的 source-identity 复审把该 flags-0 Variant getter 收紧为
`ncbPropAccessor::GetValue<tTJSVariant>`：Android arm64 内联模板，其余三端调用同一 standalone
实例；源码已经删除手写 raw-dispatch 展开并直接调用模板。详见
`analysis/motionplayer_engine_state_ncb_getvalue_source_identity_four_binary_2026-08-16.md`。
