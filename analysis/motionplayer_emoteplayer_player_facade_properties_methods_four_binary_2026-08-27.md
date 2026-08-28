# `EmotePlayer`→`Player` facade 属性与方法四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `analysis/motionplayer_emoteplayer_ncb_surface.tsv` 序号 23..41：

- `completionType/chara/motion/motionKey/project/maskMode/meshDivisionRatio/outline/priorDraw`；
- `frameLastTime/frameLoopTime/lastTime/loopTime/bounds/processedMeshVerticesNum`；
- `setDrawAffineTranslateMatrix/getCameraOffset/setCameraOffset/modifyRoot`。

19 个脚本成员在每个平台对应 24 个唯一 callback：`motionKey/project` 共用 getter/setter，
两组 frame property 也分别共用一个 getter。四端 96 个 callback body、六组共享 `Player`
helper 共 24 个 body 均完成 fresh decompile 与完整 disassembly。四个参考二进制共同构成权威。

## 2. callback 地址映射

| script member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `completionType get/set` | `0x67F02C/0x67F038` | `0x561E50/0x561E5A` | `0x1001B5F24/0x1001B5F30` | `0x1B5BD6/0x1B5BE0` |
| `chara get/set` | `0x67F044/0x67C750` | `0x561E64/0x561044` | `0x1001B5F3C/0x1001B4FD8` | `0x1B5BEA/0x1B4BFC` |
| `motion get/set` | `0x67F068/0x67F08C` | `0x561E88/0x561EAC` | `0x1001B5F60/0x1001B5F84` | `0x1B5C0E/0x1B5C34` |
| `motionKey/project get/set` | `0x67F128/0x67C6C0` | `0x561F18/0x560FD4` | `0x1001B5FEC/0x1001B4F68` | `0x1B5CF4/0x1B4B38` |
| `maskMode get/set` | `0x67F1A4/0x67F1B0` | `0x561F74/0x561F7E` | `0x1001B6048/0x1001B6054` | `0x1B5DA4/0x1B5DAE` |
| `meshDivisionRatio get/set` | `0x67F1BC/0x67F1C8` | `0x561F88/0x561F96` | `0x1001B6060/0x1001B606C` | `0x1B5DB8/0x1B5DC6` |
| `outline get/set` | `0x67F1D4/0x67F1E4` | `0x561FA4/0x561FB8` | `0x1001B6078/0x1001B6088` | `0x1B5DD4/0x1B5DE8` |
| `priorDraw get/set` | `0x67F258/0x67F264` | `0x562010/0x56201A` | `0x1001B60DC/0x1001B60E8` | `0x1B5E94/0x1B5E9E` |
| frame last/loop getters | `0x67F274/0x67F280` | `0x562024/0x562032` | `0x1001B60F4/0x1001B6100` | `0x1B5EA8/0x1B5EB6` |
| `bounds` | `0x67F28C` | `0x562040` | `0x1001B610C` | `0x1B5EC4` |
| `processedMeshVerticesNum` | `0x67F294` | `0x56204E` | `0x1001B6114` | `0x1B5ED2` |
| affine/camera get/camera set/root | `0x67F2C8/0x67F2D0/0x67F2D8/0x67F2EC` | `0x562068/0x562070/0x56207E/0x56209C` | `0x1001B6148/0x1001B6150/0x1001B6158/0x1001B616C` | `0x1B5EEC/0x1B5F26/0x1B5F34/0x1B5F56` |

Android arm64 `0x67C6C0` 是 `0x67C4AC` 的 internal entry；完整 169 条 containing-function
disassembly 与入口后的 37 条 callback path 均已读取，不创建重叠函数。其余 callback 均为
独立函数或 IDA 已建的共享 tail chunk。

## 3. shared `Player` helper 指令覆盖

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| set chara with flags | `0x6AFEC8`，37 | `0x5805FC`，39 | `0x100107AFC`，37 | `0x105140`，39 |
| play motion with flags | `0x6AF5C8`，39 | `0x5800EC`，40 | `0x1001074A4`，39 | `0x104A7C`，40 |
| bounds Dictionary | `0x6C9E64`，234 | `0x59226C`，188 | `0x10011CBD4`，141 | `0x11B53C`，263 |
| recursive processed count | `0x6CE3F8`，50 | `0x594710`，41 | `0x10011FDA8`，41 | `0x11EA6C`，76 |
| affine setter | shared tail from `0x67F2C8`，31 | `0x596C40`，43 | `0x100122D54`，27 | `0x121D90`，40 |
| camera Dictionary | shared tail from `0x67F2D0`，81 | `0x59441C`，68 | `0x10011F6EC`，50 | `0x11E220`，105 |

每个 callback 和 helper 的 disassembly 均读至 `cursor.done=true`。Android arm64 affine/camera
callback 是两条 wrapper 指令加 distant shared chunk，工具给出的总数包含两部分。

## 4. facade owner 与直接 scalar/Variant 字段

每个 callback 先从 `EmotePlayer`/Engine shell 取得同一 owning `Player*`：偏移为 Android
arm64 `+0x428`、Android armv7 `+0x214`、iOS arm64 `+0x2B8`、iOS armv7 `+0x15C`。
shell 不复制 Player，也不新建临时 facade。

直接字段共同伪代码：

```text
completionType get/set = Player signed Int32 raw load/store
maskMode get/set       = Player signed Int32 raw load/store
meshDivision get/set   = Player double raw load/store
priorDraw get/set      = Player Boolean byte load/store
frameLast getter       = Player cachedTotalFrames double
frameLoop getter       = Player loopTime double
```

`completionType`、`maskMode` 不做范围验证；负数与极值原样保存。`meshDivisionRatio` 不 clamp
且保留 NaN、无穷和负零。`priorDraw` 是 Player 级 render gate，不是节点内同名标志；typed
Boolean 输入在进入成员前已经归一为 0/1，Android arm64 仍显式 `AND #1`。

两组时间 alias 尤其容易写错：

```text
frameLastTime == lastTime == raw cachedTotalFrames
frameLoopTime == loopTime == raw loopTime
```

这里没有 `frames * 1000 / 60`。该换算只属于另一组 `Motion.Player` 脚本 property 语义，
不能搬到 `EmotePlayer`。

`outline` getter 对持久 Variant 做 owning CopyRef；setter 先拥有 by-value Variant，再执行
copy assignment，因此任何 TJS type（Void/Integer/Real/String/Object）和 Object/ObjThis owner
均原样保留。新 owner 先 retain、旧 owner 后 release；异常不把字段 move 成 Void。

## 5. `chara` 与 `motion` 的 flags=0 调用链

getter 对 Player 的 `ttstr` owner 做原子 CopyRef。setter 数据流：

```text
temp = CopyRef(converted ttstr argument)
chara:  Player.setCharaWithFlags(flags=0, temp)
motion: Player.play(flags=0, temp)
destroy temp after normal return
```

flags=0 使 shared helper 走普通 live path，而不是“仅缓存 stealth pending”入口；但 helper 仍会
处理此前遗留的 pending 字段：

1. 先更新普通 live chara/motion 状态；相同 UTF-16 value 的 chara 变更是 complete no-op；
2. 若 pending owner 非空，再以 flags=16 把该 owner送入 stealth live path；
3. nested 调用正常返回后才 Release 并清空 pending 字段。

因此 nested live writer 抛异常时 pending owner 保留，不能用 scope guard 提前清空。setter
输入临时 owner 则按外层 C++ unwind 释放。chara 与 motion 拥有独立 pending 字段和 live
更新 helper。

## 6. `motionKey` / `project` 精确 alias

两名共用完全相同的 getter和 setter 地址：

```text
get:
    return VariantCopyRef(Player.findMotionContextVariant)

set(convertedTtstr):
    temp.type = String
    temp.stringOwner = CopyRef(convertedTtstr.owner)
    Player.findMotionContextVariant = temp   # Variant copy assignment
    destroy temp
```

setter 不是把 String 写进 motion label，也不是调用 ResourceManager lookup；它只替换持久
motion-context Variant。getter 返回 owning CopyRef，旧 getter 结果可在后续 setter 或 Player
析构后继续持有自己的 String/Object owner。

Android arm64 的 callback 恰好与一个 `vector<ttstr>` copy-assignment body 共用 containing
function，因此 Hex-Rays 只显示前一入口；`0x67C6C0` 的原始指令明确建立 type=String 的
Variant、CopyRef输入、copy-assign Player context、析构临时并独立返回。

## 7. bounds Dictionary 的顺序和 IEEE-754 边界

每次 getter 都创建 fresh Dictionary。共同伪代码：

```text
dict = new Dictionary
if maxY >= minY && maxX >= minX:       # Y test first
    dict.left   = minX
    dict.top    = minY
    dict.right  = maxX
    dict.bottom = maxY
    dict.width  = maxX - minX
    dict.height = maxY - minY
    dict.isValid = scalarValid(minX) && scalarValid(maxX) &&
                   scalarValid(minY) && scalarValid(maxY)
else:
    dict.isValid = false
return owning Object Variant(dict, dict)
```

任一排序比较 unordered（NaN）即进入 else，只发布 `isValid=false`，不发布六个几何 key。
排序成立后仍由 native sign/special classifier 判断四值是否可标 valid；已经发布的几何值即使
含特殊边界也不撤回。width/height 用原始 double subtraction，不有限化、不 clamp。

九个宽 key `left/top/right/bottom/width/height/isValid/x/y` 已以 UTF-16LE bytes 在四端搜索
到完整 cursor；Hex-Rays 的 `"i"/"r"` 等只是宽字面量截断显示。每次 SetValue 使用
`TJS_MEMBERENSURE` 与独立 process-wide hint，status 被忽略；脚本 setter 异常时 Dictionary
可能只含前缀字段，随后按 RAII release，不做事务回滚。

## 8. `processedMeshVerticesNum` 的两层整数语义

shared Player core：

```text
uint32 result = Player.localProcessedCount
visitChildPlayerDispatches(type4/type3 traversal):
    result += child.getProcessedMeshVerticesNum()   # uint32 wrap
return result
```

visitor 的 type-4/type-3 容器边界、type-4 particle-index-zero 重复行为和 end 每轮重读由既有
Player child visitor 恢复保持不变。本轮 outer callback 的关键额外语义是：

```text
uint32 core = Player.getProcessedMeshVerticesNum()
int32 published = bit-preserving low32 cast(core)
return IntegerVariant(sign_extend(published))
```

所以 `0x80000000..0xFFFFFFFF` 在脚本层显示为负 Integer；不能按无符号 64 位发布。四端
Variant type tag均为 Integer，32/64 位 hidden-return 形状不同但值相同。

## 9. affine、camera 与 root 修改

### 9.1 affine

参数源级顺序为 `m11,m21,m12,m22,m14,m24`。写入顺序恢复为矩阵字段顺序：

```text
M11 = double(m11)
M12 = double(m12)
M21 = double(m21)
M22 = double(m22)
M14 = float(m14)
M24 = float(m24)
nonIdentity = m11!=1 || m21!=0 || m12!=0 || m22!=1 || m14!=0 || m24!=0
return nonIdentity
```

predicate 使用六个原始 double，而不是已压缩的两个 float。极小非零平移即使转成 float 0，
仍可使返回值为 true；NaN 比较也使 nonIdentity 为 true，正负零均等于零。函数总是先写完
六个字段再写 flag；不设置 root dirty。

### 9.2 camera

setter 独立把 x、y 从 double 转为 float并直接存储，不 clamp、无变更比较、不 dirty。
getter 每次创建 fresh Dictionary，按 x 后 y 写两个 Real；值来自 retained float，先扩大为
double再装 Variant。Dictionary getter 结果互不 alias，但都拥有自己的 dispatch。

### 9.3 root

`modifyRoot` 直接执行 `Player.nodes[0].delta.dirty=true`。四端都展开 deque map/block 索引，
没有 empty/null检查；正常 Player constructor 保证 root 存在，人工破坏该前提时是原生未定义
访问，而不是安全 no-op。

## 10. 本地逐行对照与测试

对应实现：

- `cpp/plugins/motionplayer/EmotePlayer.h:171`：序号 23..37 property/alias；
- `cpp/plugins/motionplayer/EmotePlayer.cpp:742`：四个 facade method；
- `cpp/plugins/motionplayer/Player.h:132`：scalar、String、Variant 和 raw frame字段；
- `cpp/plugins/motionplayer/PlayerCore.cpp:330`：bounds；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:639`：recursive processed count；
- `cpp/plugins/motionplayer/PlayerDrawDispatch.cpp:8`：affine；
- `cpp/plugins/motionplayer/PlayerCore.cpp:979`：camera/root。

现有测试覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:12778`：typed aliases、raw frame和 signed count；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:13725`：bounds key顺序、fresh owner、NaN/Inf/负零；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16988`：motionKey/project 持久 Variant owner；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:31577`：camera fresh Dictionary 与 float storage；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:31739`：affine 六原始 double identity 边界和
  EmotePlayer forwarding。

逐行对照未发现新的 C++ 运行语义偏差；本 slice 不修改 C++。

## 11. 状态结论与验证边界

`EmotePlayer` 序号 23..41 共 19 行从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。全局 NCB pending 从 58 降为 39，`IMPLEMENTED` 从 72 增为 91；注册面仍为
316/316、`UNMAPPED=0`。

四份 IDB 已为 callback 与 Android arm64 两个原先匿名 shared helper统一命名，internal
entry 单独注释，添加五组关键书签并原位保存。生成器确定性、strict TSV 与
`git diff --check` 在台账回填后复核。当前环境缺少 CMake、Ninja、Emscripten，独立 syntax
check 受缺失第三方头文件阻塞，因此不宣称正式 build/unit runtime。剩余 39 行为
`EmotePlayer` 序号 4..22 与 54..73；完整 root-reachable helper/object/container 分母仍待闭合。
