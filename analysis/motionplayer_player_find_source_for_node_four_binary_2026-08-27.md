# Player findSourceForNode 四参考二进制联合恢复

日期：2026-08-27

## 1. 范围与结论

本报告闭合每次 timeline source refresh 进入的 `Player::findSourceForNode` root：ResourceManager
receiver/Variant owner、src backing-pointer 与 blank gate、spec-2 Win atlas、spec-1 KRKR atlas handoff、
generic `findSource` fallback、SourceState partial publication和异常边界。

KRKR atlas packer内部的大 record/vector/texture-cache state machine仍由独立 helper slice负责；本报告
精确闭合它在 resolver 中的调用条件、参数、成功/failure publication与 fallback handoff。Win atlas
cache/decode在四端 root 内联或等价展开，因此纳入本 root 数据流。

本地修改前还在三个成功出口增加 logo path trace。四端完整 root均无该旁路；删除必须覆盖入口
`AsStdString`、source/icon narrow和最终 logger，而不是只移除输出语句。

## 2. UTF-16 定位与四端完整函数

普通符号检索没有命中。按宽字符串流程搜索完整 UTF-16LE `blank` 原始字节，四端均唯一命中一个
字面量；其 xrefs 联合落到本 resolver 与 ResourceManager公开 `findSource` 两个函数族。resolver根为：

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x691CC8` | 1191 |
| Android armv7 | `0x570500` | 676 |
| iOS arm64 | `0x1000F316C` | 586 |
| iOS armv7 | `0xEF97C` | 952 |

四个 UTF-16 byte-search cursor 均 `done=true`；四个 root均 fresh decompile，并从 offset 0读取完整
disassembly，指令 cursor也全部 `done=true`。每端恰好 5 个 code xref，对应 absolute initializer、
parameterized seek、forward/rewind source refresh和 join restore路径；没有第六个 renderer/logger caller。

Android arm64的大函数把更多 Win texture/cache/cleanup模板内联；iOS arm64保留更多共享 helper，
所以指令数不能直接当算法差异。

## 3. 入口 owner 与故意泄漏

共同入口：

```text
rmDispatch = player.findSourceResourceManager.AsObject()   // strict, AddRef
context = copy(player.findMotionContextVariant)
rm = NCB GetNativeInstance(rmDispatch)
src = live node.activeSlot.src
icon = live node.activeSlot.icon
sourceHasBacking = src.internalStringPointer != null
spec = 0
```

`AsObject()` 的新 dispatch reference在四端正常尾和 unwind cleanup都没有 Release；每次非null调用故意
泄漏一份引用。typed-null Object不产生 AddRef，随后 nonblank route对 null native的直接解引用保持
malformed boundary。不能把这里替换成 RAII receiver owner。

context是独立 Variant CopyRef，活到完整函数退出；src/icon是 live node slot的 ttstr引用，不复制。
因此 getter/texture callback若 re-enter并改写节点，后续重新读取 live key的位置可观察到替换，而
入口 context保持 snapshot。

## 4. backing-pointer 与 spec 路由

条件不是 `src.Length()>0`，而是内部 backing pointer 非null且值不等于 `blank`。allocated-empty
string会进入 spec路由；null-backed empty与精确 blank直接进入 fallback。只有进入 nonblank route
才读取 `rm.spec`。

### 4.1 spec 2 / Win

共同顺序：

```text
dst.object.Clear()
moduleKey = ttstr(context)                 // potentially dispatching conversion
loaded = rm.loadedModules.find(moduleKey)
if miss:
    dst.valid = false
else:
    root = RawNode(loaded.file)
    sourceRoot = root["source"] strict
    group = sourceRoot.tryGet(utf8(src))
    if group hit:
        dst.texture = cached-or-built Win atlas texture keyed by live src
        iconRoot = group["icon"] strict
        iconNode = iconRoot[utf8(icon)] strict
        dst.valid = true
        commit originX, originY, width, height
        dst.blank = false
        dst.clip = {0,0,1,1}
        commit textureRect from left/top and width/height
        return
```

module miss只写 `valid=false`，然后继续 generic fallback；group miss也继续 fallback。`valid=true`早于
icon metadata读取，因此后续 strict getter异常留下 valid与已提交字段前缀。Win texture cache
miss按 type `RGBA8`或`A8L8`构造 BGRA，未知格式先free buffer再抛；texture先插入 cache holder再
Release factory reference。cache/operator[]异常的 raw texture owner和 partial map边界保持原状。

### 4.2 spec 1 / KRKR

spec 1首先无条件 `dst.path = src`。只有 persistent `useD3D` byte为true才把 context严格转ttstr并调用
KRKR atlas helper；helper成功后 resolver重复写 `dst.valid=true`并立即返回。helper disabled/failure
均继续 generic fallback，保留 helper已产生的 partial SourceState/cache mutations。spec 0和未知 spec
不写 path，直接进入 fallback。

resolver本身不将 moduleKey转成 narrow string。修改前的 `moduleKey.AsStdString()`只为trace服务，
参考函数没有该额外转换/分配/异常前沿。

## 5. generic fallback

```text
dst.texture = null
fallbackPath = copy(src)
if icon.internalStringPointer != null:
    fallbackPath += "/"
    fallbackPath += icon

status = rmDispatch.findSource(context, fallbackPath, out=dst.object)
if status != TJS_S_OK or dst.object.Type == Void:
    dst.valid = false
    return

dst.valid = true
sourceOwner = strict owning accessor(copy(dst.object))
dst.width   = getDouble("width")
dst.height  = getDouble("height")
dst.originX = getDouble("originX")
dst.originY = getDouble("originY")
dst.blank   = getBool("blank")
clip = getVariant("clip")
if clip.Type == Object:
    clipOwner = strict owning accessor(copy(clip))
    dst.clip = getDouble("left","top","right","bottom")
else:
    dst.clip = {0,0,1,1}
dst.textureRect = {0,0,int(width),int(height)}
```

icon判定仍是 backing pointer；null src加backed icon形成 `/icon`。dispatch status按“精确等于0”判断，
不是 `TJS_FAILED`；nonzero status即使已写入object也强制 valid=false。non-Void错误类型先写valid=true，
再在strict Object conversion抛出。

所有 property helper忽略普通 PropGet status，转换当前 Variant并在目标store前销毁临时owner。clip的
Object type包含typed-null，仍进入strict accessor；非Object才整组写默认quartet。width/height向
signed int转换使用各ISA FP conversion，finite in-range为toward-zero；NaN/out-of-range是平台机器边界。

## 6. SourceState partial publication

该函数不是事务：

- spec-2入口只Clear object；其它旧字段先保留；
- spec-1在atlas attempt前写path；
- fallback先清texture，再逐项提交；
- valid在generic对象属性前写true；
- clip四项和textureRect都有明确后置顺序；
-任何 callback、转换、map allocation、raw metadata、texture API异常都保留已写前缀。

这也是为什么不能把SourceState改成先构造local、最后整体move的value object。

## 7. 本地差异与证据后修改

主路由、owner、泄漏、cache、strict/try lookup、partial stores和fallback均与四端共同结构一致。
确认的不匹配只有diagnostic chain：

1. function local `tracePath`；
2. spec-2对module ttstr额外 `AsStdString`；
3. Win成功时narrow live src/icon并格式化atlas详情；
4. spec-1 atlas attempt前额外 `AsStdString`，成功时narrow path并记录；
5. generic成功后narrow fallbackPath并记录。

四端root没有trace-enable check、logger获取、string formatting或这些narrow conversion。它们会增加
分配、TJS/string conversion、re-entrancy和partial-publication后的throw点。完整证据固化后已从
`PlayerResource.cpp`删除整组旁路；三个native成功出口现在直接return/结束。

四个IDB已统一命名 `Player_findSourceForNode_guess`，添加注释/bookmark并保存。

## 8. 验证与剩余边界

实施后执行 `rg`、`git diff --check`、coverage严格12列与duplicate-ID检查。当前环境缺
CMake/Ninja/Emscripten正式工具链，不能声称unit/Web build通过。

KRKR atlas builder/decoder/packer record生命周期随后已由
`motionplayer_krkr_atlas_imagepacker_four_binary_2026-08-27.md` 独立完整闭合；该报告与
D3D source getter 的第二 caller、ObjSource共享decoder和ResourceManager map销毁链交叉后，
本resolver的spec-1下游不再保留开放helper。
