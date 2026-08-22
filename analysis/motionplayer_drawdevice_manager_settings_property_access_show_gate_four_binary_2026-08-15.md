# motionplayer DrawDevice manager settings、属性 accessor 与 Show 门控：四参考二进制对照

日期：2026-08-15

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7。本文延续 manager 挂接链，恢复 `DrawDeviceManagerItem::UpdateSettings`、索引树重排、`Show` 的更新/窗口门控，以及同一属性 accessor 在 `Draw` / `IsVisible` 中的边界。

## 四平台入口

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `DrawDeviceManagerItem::UpdateSettings` | `0x533030` | `0x496A48` | `0x100235664` | `0x2343B8` |
| `ncbPropAccessor::getIntValue` | `0x533228` | `0x496B84` | `0x1000F9468` | `0xF651C` |
| `D3DLayerObject::setFrontIndex` | `0x529E8C` | `0x49241C` | `0x1002302B4` | `0x22F304` |
| `D3DLayerObject::setBackIndex` | `0x52A0A0` | `0x4924E4` | `0x1002303D8` | `0x22F39A` |
| `DrawDeviceManagerItem::IsVisible` | `0x532BCC` | `0x4966A4` | `0x1002351E0` | `0x233EE8` |
| `DrawDeviceManagerItem::Draw` | `0x532CD4` | `0x496738` | `0x10023527C` | `0x233FC8` |
| `DrawDeviceObjectBase::Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| 独立 manager-settings 遍历 helper | 内联 | `0x496D1C` | `0x1002357E8` | `0x2345DE` |

恢复 IDB 中相应函数已经命名为带 `_guess` 的语义名；名称表达恢复语义，不声称取得了剥离前的链接符号。

## 宽字符串复核

IDA 的普通字符串渲染会把 UTF-16LE 地址错显成 `"d"`、`"f"`、`"b"` 或乱码。按 UTF-16LE 原始字节搜索，三个 settings 属性在四份镜像中各有唯一匹配：

| 属性 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `drawPlane` | `0x14BE6BC` | `0xD765B4` | `0x10196FA0A` | `0x1761DB6` |
| `frontIndex` | `0x14BE692` | `0xD7658A` | `0x10196F9E0` | `0x1761D8C` |
| `backIndex` | `0x14BE6A8` | `0xD765A0` | `0x10196F9F6` | `0x1761DA2` |

因此四平台属性名一致；不能把反编译器显示的首字符当成实际 key。

## `getIntValue` 不是一次 PropGet

四份参考的 source-level 等价行为正是仓库 `ncbPropAccessor::getIntValue` 的两段式实现：

```cpp
tjs_int getIntValue(const tjs_char *key, tjs_int defaultValue) {
    if(HasValue(key))
        return GetValue(key, ncbTypedefs::Tag<tjs_int>());
    return defaultValue;
}
```

展开后的精确调用为：

1. 构造一个 Void `tTJSVariant`。
2. 对同一 dispatch / objthis 执行 `PropGet(TJS_MEMBERMUSTEXIST, key, ...)`。
3. 销毁 probe Variant。
4. probe 返回任意负数：返回 caller default，不再读取。
5. probe 返回任意非负数：再构造一个 Void Variant。
6. 执行第二次 `PropGet(0, key, ...)`。
7. 不检查第二次 PropGet 的返回码，直接对第二个 Variant 执行整数转换。
8. 销毁第二个 Variant并返回结果。

可观察后果：

- 成功 getter 被调用两次，副作用也发生两次。
- probe 写入的值被丢弃；第二次 getter 可以返回另一个值。
- 第二次调用失败但写了值时，仍转换该值。
- 第二次调用失败且没写值时，初始 Void 转成整数 0；这时 caller default 不再生效。
- string/object/real 等类型如何转整数，完全委托 `tTJSVariant::AsInteger`；wrapper 不先做类型过滤。

旧的单次 `PropGet` helper 会漏掉这些边界，已从此调用链移除。

## `UpdateSettings` 的精确顺序

四份参考一致：

```text
ncbPropAccessor properties(PrimaryOwner)     // 无条件 PrimaryOwner->AddRef()
DrawPlane = properties.getIntValue("drawPlane", 0) & 3
setFrontIndex(properties.getIntValue("frontIndex", 0))
setBackIndex(properties.getIntValue("backIndex", 0))
properties.~ncbPropAccessor()                // PrimaryOwner->Release()
```

这里不是“先读三个值，再统一应用”。每个读取之后立即修改对象：

- `drawPlane` 成功后立刻按 `& 3` 写字段；之后 front/back getter 出错也不回滚。
- `frontIndex` getter 完成后立刻重排 front tree；随后 back getter 出错也不回滚 front。
- `backIndex` 最后读取并应用。
- `ncbPropAccessor` 在作用域内额外持有一次 owner 引用，正常退出和 C++ 异常展开都会 Release。
- accessor 的 `iTJSDispatch2 *` 构造器无 null guard。因此 owner helper 虽然条件式 AddRef null owner，完整构造会在这里对 null owner 解引用；manager item 实际要求有效 PrimaryOwner。

构造器首次调用 `UpdateSettings` 时 `Parent == nullptr`，front/back setter 只更新字段。`SetParent_guess(owner)` 随后才将 item 按已读取的两个索引插入 front/back 树。

## front/back setter 与树重排

两个 setter 除字段偏移和目标 tree 外完全对称：

```text
if(oldIndex == newIndex)
    return
if(Parent == nullptr)
    index = newIndex
    return
Parent->EraseFront/Back(this)  // 返回值被忽略，只删首个匹配节点
index = newIndex
Parent->FrontItems/BackItems.insert(this)
```

边界：

- 相等值是纯 no-op；即便 tree 节点已经丢失或节点位置与对象的实时 index 不一致，也不会尝试修复。
- 两张树不是 `multimap<int, pointer>`，而是只保存 pointer 的 `multiset`；比较器每次都
  解引用对象的 front/back index。erase 用当前对象指针做 `equal_range`，再在等价区间中
  查找相同指针，只删除首个匹配节点。
- erase miss 不阻止新插入；若旧节点仍在树中，随后原位写 index 会立即改变该节点的比较
  key 而不重排，从而破坏红黑树有序不变量，然后还会再增加一个新节点。
- 先 erase、再写字段、最后分配/插入新红黑树节点。`operator new` 或插入失败时，对象保留新 index，但从该目标 tree 缺席。
- setter 不调用 `OnDetached`、parent changed hook 或 window update。
- front 成功、back 失败时两棵树和两个字段可处于不同代次；没有事务回滚。
- 同一对象的重复节点尤其危险：setter 只删一个节点，剩余节点都引用同一字段；字段写入会
  同时改变所有剩余节点的比较结果并使树失序。

Android arm64 将红黑树查找/插入更多地内联在 setter 内；Android armv7 和 iOS 通过独立 tree helper，但上述状态机一致。
四端节点载荷和 ABI 尺寸的独立证据见
`motionplayer_drawdevice_front_back_pointer_multiset_four_binary_2026-08-15.md`。

## `Show` 的真实门控顺序

四份参考都不是入口先检查 window。精确高层顺序是：

```text
snapshot = UpdateState
UpdateObjects_guess(snapshot)         // 一轮共享一个 Variant，遍历 live front tree
UpdateState = 0                       // 仅 helper 正常返回后提交
if(Window == nullptr)
    return
UpdateManagerSettings_guess()
EnsureTargets()
... render ...
Window->GetWindowDispatch()->...present...
```

因此 window-null 时仍然：

- 迭代 front tree；
- 调用可见对象的 `OnUpdate`；
- 传入入口 snapshot 和全轮共享的同一个 Variant 地址；
- helper 正常返回后把 UpdateState 清零。

window-null 时不会：

- 遍历 Managers 刷新 manager item settings；
- 创建/扩容 render targets；
- 绘制或 present。

当前源码先前把 `if(!Window) return` 放在最前面，已经按四参考证据下移到对象更新和状态清零之后。单测用 windowless root 的 D3DLayer `onUpdate` 验证状态 37 仍被消费。

V269 又收紧了这里的“清零”：callback 重入 `update(newState)` 并正常返回时，success-only 零写
覆盖新值；若 callback 在重入 update 后抛出，helper 只析构共享 Variant并向外传播，零写不到达，
新值留给下一次 Show。Window-null 只在这个提交之后生效，不改变异常边界。完整四端 lowering
见 `motionplayer_root_updateobjects_shared_variant_tree_iterator_updatestate_commit_four_binary_2026-08-21.md`。

## Show 中的 manager-settings 遍历

window 存在时，Show 按共享 Managers vector 的插入顺序逐项执行：

```text
for(manager in Managers) {
    item = manager->GetDrawDeviceData();
    if(item != nullptr)
        item->UpdateSettings();
}
```

没有 manager null guard、data 类型检查或去重。结合重复 Add 边界：

- 同一个 manager 在 vector 中出现两次时，Show 会读取同一个当前 data 槽两次，并对最新 item 调用两次 UpdateSettings。
- 被第二次 Add 覆盖的旧 orphan item 不再能由 manager data 找回，所以不会被刷新；它仍可能留在 front/back tree 中按旧 settings 绘制。
- 第一次 UpdateSettings 若重排 tree，第二次通常因索引相等而 no-op，但六次 getter 仍会再次发生，脚本副作用可见。
- manager data 在 getter 副作用中被修改时，当前迭代已经取得的 item 指针不会重新读取。

## Draw 使用同一个 accessor 生命周期

`DrawDeviceManagerItem::Draw` 在四份参考中按以下前置顺序：

1. `Parent == nullptr` 立即返回。
2. `Manager->GetDrawBuffer()`；null 立即返回，没有 Manager null guard。
3. 以 `PrimaryOwner` 构造一个 `ncbPropAccessor`，无条件临时 AddRef。
4. 依次读取：
   - `type`，默认 0；
   - `drawOpacity`，默认 255；
   - `offsetX`，默认 0；
   - `offsetY`，默认 0。
5. 每个成功属性均执行前述 probe + ordinary-get 两次调用。
6. 取得 render method；null 时不绘制，但 accessor 仍在作用域退出时 Release。
7. 调用渲染链后释放 accessor 的临时 owner 引用。

一个 accessor 覆盖四次读取和后续 render 调用；不是每个属性单独 AddRef/Release。当前源码已改为相同结构。

## `IsVisible` 是另一套单读边界

`IsVisible` 不使用 `ncbPropAccessor::getIntValue`，也不双读：

1. 先检查保存的 PrimaryLayer 字段；null 返回 false。
2. 对 PrimaryOwner 执行一次 `PropGet(TJS_MEMBERMUSTEXIST, "drawvisible", &staticHint, ...)`。
3. 返回码精确等于 `TJS_E_MEMBERNOTFOUND`（四份均为 `-1001`）时返回 true。
4. 其他任何返回码，包括其他负值，都对返回 Variant 做普通 bool 转换。

所以“失败即 false”只在 dispatch 没写输出、Variant 仍为 Void 时碰巧成立。如果 dispatch 写 true 后返回另一个失败码，原版仍返回 true。当前源码已保留 function-static hint，并按这个精确判定转换；回归测试覆盖了“写 true + 返回 `TJS_E_FAIL`”路径。

## 当前源码和测试改动

- `cpp/plugins/DrawDeviceD3D.cpp`
  - `UpdateSettings` 改为单个 `ncbPropAccessor`，三个属性按原顺序双读并立即应用；
  - `Draw` 改为单个 accessor 覆盖四个整数属性；
  - `IsVisible` 使用 static hint，仅特判 `TJS_E_MEMBERNOTFOUND`；
  - `Show` 将 window-null 门控移到 `UpdateObjects` 和 `UpdateState = 0` 之后。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 自定义 primary layer dispatch 让 probe 返回 12345、ordinary get 返回实际值，验证六次读取的 key/flags 顺序和只采用第二次值；
  - 验证 `drawPlane 6 & 3 == 2`、front 17、back -4；
  - 验证 `drawvisible` 写 true 后返回 `TJS_E_FAIL` 仍可见，且 hint 非空；
  - 验证无 window 的 Show 仍把 update state 37 送入 D3DLayer `onUpdate`。

## 恢复 IDB 状态

四份恢复库均已：

- 命名 `DrawDeviceManagerItem_UpdateSettings_guess`、front/back setter；
- 命名 A64 内联附近独立识别出的 `ncbPropAccessor_getIntValue_guess`；其他三份此前已命名；
- 命名三份非 A64 的 manager-settings 遍历 helper；
- 注释 accessor 双 PropGet、串行应用、tree 半更新异常边界、Show 门控顺序和重复 manager 行为；
- 为 manager settings 和 Show window gate 增加书签。
