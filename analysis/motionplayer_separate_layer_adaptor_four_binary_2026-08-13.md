# MotionPlayer `SeparateLayerAdaptor` 双 map、payload 与生命周期四参考复原

日期：2026-08-13

> 2026-08-17 V183 补充：本文的 `sourceLayer.member` 伪码表示
> `ncbPropAccessor_getIntValue_guess` 的两阶段读取，而不是一次普通 `PropGet`：先以
> `MEMBERMUSTEXIST`、null hint 探测并销毁临时值，仅负 HRESULT 回落为默认值 0；否则再以
> flags 0、null hint 读取，忽略第二次 HRESULT 后转换整数。`setSize` 调用则使用插件级共享
> `setSizeMemberHint_guess`。完整四端 helper、调用点与 10-call/7-function hint xref 见
> `motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。

## 1. 结论

四份当前参考二进制共同显示，`SeparateLayerAdaptor` 用两棵
`std::map<uint32_t, Payload>` 管理逐帧 Layer：ordinal 只保存在 pair key，一棵是本帧 active，一棵是上帧
retired。每次 pass 入口只交换整棵 map 并把 sequence 归零；解析一个 ordinal 时，
若 retired 中存在同键节点，就把其 Layer Variant 搬到本帧 payload 并擦除 retired
节点；若不存在，才调用 `Layer(owner, targetLayer)` 创建新 Layer。pass 正常尾部逐个
Invalidate 未复用的 retired Layer，再销毁 retired tree。

这轮四端联合证据纠正了本地早期 `libkrkr2.so` 推测中的几项关键错误：

- payload 不是 `type/visible/affine/vector<float>/color/origin`；它包含一个
  Layer Variant、两个整数、一个状态字节、`commandSrc`、四个 packed color、八个
  paint/viewport float、两组 `vector<MeshPoint>` 和八个 corner float；
- comparator 不比较 Layer Variant、四个 packed color 或最后八个 corner float；
- comparator 虽执行短路字段比较，但所有出口都返回 `true`，所以 reuse 时调用者仍会
  重刷图像；
- `assign` 入口不能清空交换后得到的 active map；
- map 的对象清理先复制完整 payload，以临时副本的 Layer Variant 调用
  `Invalidate`，再销毁副本与整棵 tree；不能先逐个清空 tree 节点内的 Variant；
- accurate SLA 的 retired 清理只在正常尾部调用。异常 landing pad 没有该调用，因而
  本地 RAII scope 会错误地在异常展开时清理 native 本应保留到下一次 swap 的状态。

本文地址只用于分析坐标。编译源码使用语义名，不继续携带旧单二进制地址。

## 2. 数据库与函数映射

本轮只把四份 recovery IDB 中原生 `mcp__idalib__*` 的 fresh decompile、disasm 和
xref 作为证据。原始 IDB 未被覆盖；四份 recovery 数据库分别是 Android arm64、
Android armv7、iOS arm64、iOS armv7 的当前参考二进制副本。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| clear | `0x6A965C` | `0x57C698` | `0x1001031AC` | `0x100590` |
| assign | `0x6A97F0` | `0x57C814` | `0x10010347C` | `0x100874` |
| payload comparator | `0x6D9F0C` | `0x59B7F0` | `0x1001299E0` | `0x1289F0` |
| resolve/reuse node | `0x6C3F28` | `0x58DCD4` | `0x100117E88` | `0x115B34` |
| clear retired map | `0x6C46C4` | `0x58E174` | `0x10011844C` | `0x116280` |
| normal render-command builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| accurate SLA renderer | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |

Android arm64 的 IDA 自动分析把 clear 和紧随其后的 assign 合并成从
`0x6A965C` 开始的一大函数；native disasm 在 `0x6A97F0` 显示独立 prologue，并在
`0x6AA060` 返回。因此表中保留真实 assign 入口，不把 IDA 的合并边界误当成源结构。

resolver 的正式 code xref 为：

| 调用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| assign | `0x6A993C` | `0x57C8A6` | `0x100103540` | `0x100930` |
| normal builder | `0x6C270C` | `0x58CABE` | `0x100116B30` | `0x114666` |
| accurate renderer | `0x6C7490` | `0x590912` | `0x10011AD70` | `0x119284` |

retired-map cleanup 在每份二进制都恰有四个调用者：assign、normal builder、accurate
renderer，以及 Player 的兼容 draw 路径。对应调用点为：

| 目标 | assign | normal builder | accurate | compat draw |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AA030` | `0x6C3798` | `0x6C8920` | `0x6D3860` |
| Android armv7 | `0x57CAAE` | `0x58D7E2` | `0x5918A4` | `0x5979F6` |
| iOS arm64 | `0x10010386C` | `0x100117904` | `0x10011BEF0` | `0x100123EB8` |
| iOS armv7 | `0x100C32` | `0x11539C` | `0x11A47E` | `0x12317A` |

四端 xref 数量都只有四个；accurate 的函数尾 disasm 又显示 cleanup call 位于正常
return 之前，异常 landing pad 不再调用它。这是 normal-only 清理语义，而不是编译器
偶然隐藏的析构调用。

## 3. payload ABI 布局

### 3.1 64 位两端：168 字节

| 偏移 | 大小 | 源级含义 |
|---:|---:|---|
| `+0` | 20 | `tTJSVariant layerVariant` |
| `+20` | 4 | Player `completionType` |
| `+24` | 1 | outline/meshline 样式存在标志 |
| `+28` | 8 | `ttstr commandSrc` |
| `+36` | 4 | blend mode / normal builder 的中性 `0` |
| `+40` | 16 | `packedColors[4]` |
| `+56` | 32 | `paintBox[4]` 后接 `viewport[4]` |
| `+88` | 24 | 第一组 `vector<MeshPoint>` |
| `+112` | 24 | 第二组 `vector<MeshPoint>` |
| `+136` | 32 | `corners[8]` |

### 3.2 32 位两端：132 字节

| 偏移 | 大小 | 源级含义 |
|---:|---:|---|
| `+0` | 12 | `tTJSVariant layerVariant` |
| `+12` | 4 | Player `completionType` |
| `+16` | 1 | outline/meshline 样式存在标志 |
| `+20` | 4 | `ttstr commandSrc` |
| `+24` | 4 | blend mode / normal builder 的中性 `0` |
| `+28` | 16 | `packedColors[4]` |
| `+44` | 32 | `paintBox[4]` 后接 `viewport[4]` |
| `+76` | 12 | 第一组 `vector<MeshPoint>` |
| `+88` | 12 | 第二组 `vector<MeshPoint>` |
| `+100` | 32 | `corners[8]` |

`MeshPoint` 的元素步长在四端都是 8 字节，即连续两个 `float`。native comparator
disasm 在两组 vector 上都逐元素比较 x 和 y；32 位 Hex-Rays 曾把其中一段简化得像是
只比较第一个分量，但原始指令否定了该显示假象。

normal builder 与 accurate renderer 构造同一 payload 类型，但策略不同：

```cpp
// normal leaf builder
payload.layerVariant = Void;
payload.completionType = player.completionType;
payload.hasOutlineOrMeshline = false;
payload.commandSrc = item.commandSrc;
payload.blendMode = 0;
payload.packedColors = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
payload.paintAndViewport = concat(item.paintBox, item.viewport);
payload.meshVectors = selectByMeshType(item);
payload.corners = item.corners;

// accurate SLA
payload.layerVariant = Void;
payload.completionType = player.completionType;
payload.hasOutlineOrMeshline =
    player.outline.Type() != tvtVoid || player.meshline.Type() != tvtVoid;
payload.commandSrc = item.commandSrc;
payload.blendMode = item.blendMode;
payload.packedColors = item.packedColors;
payload.paintAndViewport = concat(item.paintBox, item.viewport);
payload.meshVectors = selectByMeshType(item);
payload.corners = item.corners;
```

iOS arm64 accurate renderer 读取的 Player `+936` 与 `+956` 正是分别位于
`outline + 16`、`meshline + 16` 的 Variant type tag；Android arm64 对应
`+1048/+1068`，32 位两端也按 12 字节 Variant 布局落在相应 type tag。故该 byte
不是 `visible`，也不是新的独立 Player Boolean。

mesh vector 选择由 `meshType` 控制：type 2 填第一组 composite points，type 1 填
第二组 bezier patch points；其他类型两组为空。vector 是拥有型深拷贝，不是 item
缓冲区的借用 view。

## 4. comparator 的反直觉边界

四端共同伪代码为：

```cpp
bool SeparateLayerPayload::requiresRefresh_guess(const Payload &rhs) const {
    if(commandSrc == rhs.commandSrc &&
       completionType == rhs.completionType &&
       hasOutlineOrMeshline == rhs.hasOutlineOrMeshline &&
       blendMode == rhs.blendMode &&
       paintAndViewport == rhs.paintAndViewport &&
       meshPointVector0 == rhs.meshPointVector0 &&
       meshPointVector1 == rhs.meshPointVector1) {
        return true;
    }
    return true;
}
```

比较顺序以 `commandSrc` 开始；它故意跳过 payload 开头的 Layer Variant。四个 packed
color word 与最后八个 corner float 同样完全不参与比较。每个不等分支直接汇入
`return 1`，全部相等也返回 `1`。因此函数名只能是带 `_guess` 的语义描述，不能把
它“修正”为传统 equality 或 compatibility predicate。保留实际短路比较仍然重要：
`ttstr`/vector 比较可能有可观察的读取、性能与异常边界，不能直接简化成字面
`return true`。

## 5. pass、resolver 与 tree 迁移

共同伪代码：

```cpp
beginPass(adaptor):
    swap(adaptor.activeMap, adaptor.retiredMap)
    adaptor.sequence = 0

resolve(adaptor, ordinal, sourcePayload):
    retired = adaptor.retiredMap.find(ordinal)
    out = Void

    if(retired != end):
        createdOrChanged = sourcePayload.requiresRefresh_guess(retired.value)
        activePayload = adaptor.activeMap.find_or_insert_default(ordinal)
        activePayload = sourcePayload
        activePayload.layerVariant = retired.value.layerVariant
        out = activePayload.layerVariant
        adaptor.retiredMap.erase(retired)
    else:
        createdOrChanged = true
        activePayload = adaptor.activeMap.find_or_insert_default(ordinal)
        activePayload = sourcePayload
        created = new Layer(ownerOrObjThis, adaptor.targetLayer)
        activePayload.layerVariant = created
        out = created

    layer = strictLayerAccess(out)
    layer.absolute = adaptor.absolute + adaptor.sequence
    ++adaptor.sequence
    layer.hitThreshold = 256
    return out

endPass(adaptor):
    clearRetiredLayers(adaptor.retiredMap)
```

resolver 先查询 retired，再按分支复制 payload。复用分支只继承 retired 的 Layer
Variant；新建分支无条件构造 Layer，不从 `sourcePayload.layerVariant` 复用对象。
四端都没有本地移植中“若 Layer 无效则尝试创建，否则静默返回”的友好兜底结构；
Layer 访问与属性写入位于共同尾，保持 native 的错误传播边界。

Android 使用 libstdc++ 红黑树 helper，iOS 使用 libc++ `std::map` helper；node header、
sentinel 和旋转/erase 机器码因此不同。源级共同容器仍是排序 map，而不是 hash map 或
vector。`ordinal` 的 signedness 不影响相等查找，但 native 比较形态与 `uint32_t` 键
一致。

## 6. clear 与临时 payload 生命周期

private target 与 map Layer 是两个独立 owner 集合。clear 的共同顺序为：

```cpp
if(adaptor.privateTarget is Object)
    Invalidate(adaptor.privateTarget)
adaptor.privateTarget.Clear()

for(node : adaptor.activeMap) {
    Payload temporary = node.payload
    if(temporary.layerVariant is Object)
        Invalidate(temporary.layerVariant)
    temporary.~Payload()
}
destroy_and_reset(adaptor.activeMap)
// retiredMap untouched
```

retired cleanup helper使用同样的“完整 payload 临时拷贝 -> Invalidate 临时 Variant ->
析构临时 payload -> 最后销毁 tree”模式，只是目标为 retired map。这个顺序会先对
Layer 增加一次临时引用，再 Invalidate；tree 节点仍持有原 Variant，直到整棵 tree
销毁才释放。直接修改/清空节点中的 Variant 会改变引用计数峰值、Invalidate 时 owner
集合以及异常时序，不能视为等价实现。

普通 map 析构只释放 payload owner，不调用对象 `Invalidate`；主动 clear/retired
cleanup 才执行 Invalidate。

四端的 PrivateMotionGLL target helper 进一步证明，这个专用 target 只存在于
`privateTarget` Variant 槽：helper 检查该槽的 type，必要时构造并 CopyAssign，获取
PrivateMotionGLL native 后设置 absolute、visible 与 size。它不访问 active/retired
map，也不构造 SLA payload。因此旧本地实现在创建 PrivateMotionGLL 后又以 ordinal 0
插入 active map 是虚构的所有权边，会让 clear 对同一对象走两次 Invalidate；该插入与
对应的 `trackManagedTarget` helper 已删除。

## 7. assign 数据流与属性顺序

四端共同顺序：

```cpp
assign(destination, source):
    swap(destination.activeMap, destination.retiredMap)
    destination.sequence = 0

    for((ordinal, sourcePayload) : source.activeMap) {
        targetVariant = destination.resolve(ordinal, sourcePayload)
        sourceLayer = strictLayerAccess(sourcePayload.layerVariant)
        targetLayer = strictLayerAccess(targetVariant)

        targetLayer.assignImages(sourcePayload.layerVariant)

        height = getIntValueTwoStage(sourceLayer, "height", default=0) // both hints null
        width  = getIntValueTwoStage(sourceLayer, "width",  default=0) // both hints null
        ignore targetLayer.FuncCall(flags=0, "setSize",
                                    hint=&setSizeMemberHint_guess,
                                    args={width, height}, objthis=targetLayer)

        absolute = sourceLayer.absolute
        visible  = sourceLayer.visible
        opacity  = sourceLayer.opacity
        type     = sourceLayer.type
        left     = sourceLayer.left
        top      = sourceLayer.top

        targetLayer.absolute =
            destination.absolute + absolute - source.absolute
        targetLayer.visible = visible
        targetLayer.opacity = opacity
        targetLayer.type = type
        targetLayer.left = left
        targetLayer.top = top
    }

    clearRetiredLayers(destination.retiredMap)
```

交换后获得的 active map 不能立即 clear：它是 resolver 本帧插入/覆盖的目标 tree，
而交换前的 active 已成为 retired 供 ordinal 复用。属性读取明确是 height 先、width
后；两者都走“`MEMBERMUSTEXIST` 探测、负值回落 0、flags 0 二次读取”的 null-hint helper，
但 `setSize` 实参仍为 `(width, height)`，并传入跨 SourceCache/SLA/Player 调用链共享的
`setSizeMemberHint_guess`。resolver 调用先于 source/target Layer
access；本地提前解析 source 并在失败时 `continue` 会跳过 resolver 的 sequence 与
map 副作用，改变 malformed-object 边界。

`absolute` 写回覆盖 resolver 刚按 sequence 写入的暂时 absolute，公式保留 source 与
destination adaptor 的基准差；随后属性写入顺序严格为 visible、opacity、type、left、
top。

## 8. accurate pass 的异常边界

accurate renderer 在完成 render-command 构造后执行 begin swap，然后逐 item resolve。
正常尾部在日志/返回前清理 retired map。四端异常 landing pad 只析构当前 item 局部
Variant/vector/临时对象，没有 retired cleanup 调用；因此异常时 retired map 被保留。
下一次 pass 的入口仍会整树 swap，这个状态转移是原生行为的一部分。

所以 portable 源码应显式：

```cpp
sla.beginPass();
// work that may throw
sla.endPass();       // only on normal control flow
return true;
```

不能用 destructor 自动调用 `endPass()` 的 scope guard。

## 9. 与本地实现逐项比较及落地方案

| 本地旧实现 | 四端证据 | 落地 |
|---|---|---|
| payload 含 `type/visible/affine/vector<float>/color/origin` | ABI 与 builder 均否定 | 改为本文件第 3 节布局与 `MeshPoint` vector |
| comparator 比较 Variant、colors、corners，遇不等返回 false | 三类字段被排除，所有出口 true | 保留 native 短路比较但固定 true |
| normal builder 零填 payload | native 填完整中性描述符 | 从 item 与 Player 构造完整 payload |
| accurate 填 layer type/visible/sourceKey | native 填 completion/style/commandSrc/render descriptor | 按 accurate 策略重建 |
| resolver 先覆盖 active、清空 Layer，再条件创建 | native 先分 retired/new 分支；new 分支无条件创建 | 重排 resolver |
| clear 直接 Invalidate/clear node Variant | native 使用完整 payload 临时副本 | `clear(true)` 改成 copy-invalidate-destroy-tree |
| assign swap 后清空 active | native 不清空 | 删除该 clear |
| assign width 后 height | native height 后 width，均为两阶段 null-hint 读取；`setSize` 使用共享 hint | 调整读取顺序与 helper 边界，保持 `setSize(width,height)` |
| assign 先解析 source，失败即 continue | native resolver 先执行 | resolver 移到首位 |
| accurate scope guard 析构清理 | native 仅正常尾清理 | 改成显式 begin/end |
| map mapped value 再包一层 `{ ordinal, payload }` | ordinal 只在 pair key；mapped value 直接从 node `+40/+20` 开始 | 删除重复 ordinal 与 `.payload` 间接层 |

精确 C++ 私有类型名未出现在符号中，因此源码名继续带 `_guess`。与 NCB 对外属性、
Layer 方法名和已由宽字符串证明的成员名不同，不能冒充已恢复的原始 identifier。

## 10. IDB 落地与验证

四份 recovery IDB 均已执行 rename dry-run，再正式命名并保存：

- clear、assign（Android arm64 为 IDA 合并边界）、payload comparator、resolver、
  retired cleanup 使用统一语义名；
- 在上述函数及 Android arm64 的独立 assign 指令入口记录双 map、固定 true、临时
  payload Invalidate 和 normal-only cleanup 注释；
- Android arm64 没有为了得到漂亮函数名强拆 IDA 已合并的 CFG；真实 assign 入口以
  line comment 保留，其他三端保留独立函数名。

源码验证：

- `cmake --build out/web/debug -j 8` 完整编译并链接成功；
- 首次验证曾因前一次超时命令仍在后台写 archive，使每个 object 在生成的
  `libmotionplayer.a` 中重复两次而链接失败。核实无残留进程、验证目标绝对路径位于
  `out/web/debug` 后，只删除该可再生 archive 并由单一 Ninja 重建，随后完整链接成功；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数，并加入既有 `out/syntax-check` Catch2、
  `test_config.h` 与 syntax stubs，执行 `-fsyntax-only` 成功；唯一诊断为仓库既有的
  `_tss` literal-operator 弃用 warning；
- 新测试覆盖 comparator 的全等、excluded packed-color/corner 差异、第二个
  `MeshPoint` 的 y 分量差异和 `commandSrc` 差异；四种情况都要求返回 true；
- 2026-08-15 的节点 ABI 纵切进一步删除 mapped value 中重复的 ordinal；精确节点头、
  分配大小、默认 zero-fill、提交/回滚和逆序析构见
  `motionplayer_separate_layer_payload_map_node_abi_four_binary_2026-08-15.md`；
- `git diff --check` 无 whitespace error；输出只有仓库行尾策略产生的
  LF-to-CRLF 提示。
