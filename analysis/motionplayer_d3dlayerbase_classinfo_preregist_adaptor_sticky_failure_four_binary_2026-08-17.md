# motionplayer `D3DLayerBase` ClassInfo、预注册、adaptor sticky 与失败边界四端恢复

日期：2026-08-17  
纵切面：V208

## 一、范围

本纵切面只把 `reference/binaries/` 中四个当前发布物当作事实源：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

V203、V206 和 V207 已经证明 concrete `D3DLayer`、`D3D`、`DrawDeviceD3D` 与共同
root view 使用不同 native class ID，但此前没有独立闭合 `D3DLayerBase` 自身的：

- `ncbClassInfo<DrawDeviceObjectBase>` tuple、guard 与静态初始化；
- `DrawDeviceD3D_PreRegist` 中的直接 class-ID 注册、模块加载顺序和 prefix partial state；
- 无 global class object、无 Regist/Unregist、无 Clear 的内部身份；
- `SetAdaptorWithNativeInstance` 的 existing/empty/fresh 三态；
- sticky 晋升前后的 owner 转移、重复附着和失败状态；
- Invalidate、完整析构、删除析构和 concrete owner 的相对生命周期。

旧 `libkrkr2.so` 注释没有被当作证据。四端私有符号继续使用 `_guess`。

## 二、结论摘要

1. `D3DLayerBase` 不是脚本 class，却仍有完整 NCBind ClassInfo：LP64 `0x20`、ILP32
   `0x10`，字段为 `initialized/name/classID/classObject`；`classObject` 在当前插件中始终为 null。
2. 预注册直接调用 `TJSRegisterNativeClass("D3DLayerBase")`，再把 `{name,id,null}`
   first-publish 到 tuple；参考实现没有 `TJSFindNativeClassID` 的 lazy/fallback 路径。
3. 预注册顺序严格为 root ClassInfo publication → `LoadModule("emoteplayer.dll")` →
   注册一个独立的 `D3DLayerObjectNativeInstance` class-ID word。中间模块加载失败不会回滚
   已发布的 root tuple，后一个 ID 仍保持零值。
4. 两个内部 native class ID 都不会向 global object 发布成员；不存在
   `global.D3DLayerBase` 或 `global.D3DLayerObjectNativeInstance`。
5. root adaptor 为标准 `0x18/0x0C {vptr,native,sticky}`。唯一正常 producer 是两个
   common-root 构造器共用的 `DrawDeviceObjectBase` 主基类构造路径。
6. `SetAdaptorWithNativeInstance` 对 populated adaptor 先执行条件删除/清槽；对
   `native==null` 的 existing adaptor 则不调用 `_deleteInstance`，所以会保留其旧 sticky bit。
7. helper 总是在写入新 native 后再次 REGISTER。失败时不回滚 adaptor 或 native；当前唯一
   caller 传 `err=false`，忽略返回 bool，再严格 GET 并无条件写 sticky。
8. 因而“REGISTER 失败”本身不等于必崩：若 existing attachment 仍可 GET，构造仍会完成
   sticky 晋升；只有 owner==null 在 helper 内直接解引用空指针，或后续 GET 失败/返回 null，
   才会形成确定的空基址字段写。
9. 正常 fresh 路径在 REGISTER 与最终 sticky store 之间短暂 non-sticky；但人为制造的
   `native=null,sticky=true` existing adaptor 会保持 sticky，没有这个窗口。
10. adaptor Invalidate/析构仅在 `native!=null && sticky==false` 时删除 root。正常实例最终
    sticky，因此只是 borrowed view；concrete `D3D`/`DrawDeviceD3D` adaptor 才是 root owner。

## 三、内部 ClassInfo ABI

### 3.1 四端定位

| 目标 | ClassInfo tuple | classID 字段 | guard | static init |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AAF6E8` | `0x1AAF6F8` | `0x1AAF708` | `0x42CB78` |
| Android armv7 | `0x110E248` | `0x110E250` | `0x110E258` | `0x2FF034` |
| iOS arm64 | `0x101AEE508` | `0x101AEE518` | `0x101AEE528` | `0x10024CAA0` |
| iOS armv7 | `0x1838EC4` | `0x1838ECC` | `0x1838ED4` | `0x24E680` |

LP64：

```cpp
struct D3DLayerBase_NCB_ClassInfo_LP64_guess {
    bool initialized;                 // +0x00
    unsigned char pad0[7];
    const tjs_char *name;             // +0x08, borrowed
    tjs_int32 classID;                // +0x10
    unsigned char pad1[4];
    iTJSDispatch2 *classObject;       // +0x18, always null here
};                                    // 0x20
```

ILP32：

```cpp
struct D3DLayerBase_NCB_ClassInfo_ILP32_guess {
    bool initialized;                 // +0x00
    unsigned char pad0[3];
    const tjs_char *name;             // +0x04, borrowed
    tjs_int32 classID;                // +0x08
    iTJSDispatch2 *classObject;       // +0x0C, always null here
};                                    // 0x10
```

### 3.2 guard 与初始化

四个 static initializer 都先读 guard 的低 bit；未初始化时清整个 tuple，最后整宽写 1：

| ABI | guard data item | 最终 store |
|---|---:|---|
| LP64 | `0x08` | 64-bit `1` |
| ILP32 | `0x04` | 32-bit `1` |

这再次说明不能因为入口只用 byte/bit test 就把 guard 错建成一个字节。四库 readback 均已按
最终写宽恢复 typed data。

### 3.3 exact string 证据

IDA 的普通窄字符串显示只会把 UTF-16LE 名称渲染成 `"D"`。V208 用宽字节模式分别找到唯一
完整 `D3DLayerBase`：

| 目标 | UTF-16LE string |
|---|---:|
| Android arm64 | `0x14BF29E` |
| Android armv7 | `0xD77012` |
| iOS arm64 | `0x101970626` |
| iOS armv7 | `0x17629D2` |

四处都匹配完整字节序列 `D 3 D L a y e r B a s e \0`，不是从 Web 源码名称反推。

## 四、PreRegist 的真实发布顺序

### 4.1 四端入口

| 目标 | `DrawDeviceD3D_PreRegist_guess` | D3DLayerObject ID word |
|---|---:|---:|
| Android arm64 | `0x53101C` | `0x1AAF484` |
| Android armv7 | `0x49516C` | `0x110E0EC` |
| iOS arm64 | `0x1002335C8` | `0x10256A0A4` |
| iOS armv7 | `0x2323C0` | `0x218E054` |

共同源级顺序是：

```cpp
void DrawDeviceD3D_PreRegist_guess() {
    const tjs_char *name = TJS_W("D3DLayerBase");
    const tjs_int32 id = TJSRegisterNativeClass(name);
    if(!D3DLayerBaseClassInfo.initialized) {
        D3DLayerBaseClassInfo.name = name;
        D3DLayerBaseClassInfo.classID = id;
        D3DLayerBaseClassInfo.classObject = nullptr;
        D3DLayerBaseClassInfo.initialized = true;
    }

    ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));

    D3DLayerObjectClassID =
        TJSRegisterNativeClass(TJS_W("D3DLayerObjectNativeInstance"));
}
```

关键边界：

- `TJSRegisterNativeClass` 在 initialized test 之前调用；tuple 已发布时仍先执行注册调用，只是
  `Set` 不覆盖旧 payload；
- 参考实现没有先调用 `TJSFindNativeClassID`；
- ClassInfo publication 只保护自己的 first-write，不是整个 callback 的事务；
- `LoadModule` 抛出时，root tuple 已经 sticky-published，后一个 ID word 尚未写；
- 该 callback 没有生成的 Unregist 对偶，也没有任何 Clear caller；
- `GetID()` 在 callback 之前只会读到静态零值，不会 lazy-register 一个更友好的 ID。

### 4.2 不存在 global class

完整 xref 闭包没有 native-class descriptor、`TJSCreateNativeClassForPlugin`、RegistEnd、global
PropSet、UnregistEnd 或 tuple Clear。`classObject` 唯一写入是零初始化和 first-publish 时的 null。

因此这两个身份只供 `NativeInstanceSupport(GET/REGISTER, classID, ...)` 使用。它们不应被恢复成
第八/第九个公开脚本 class，也不应有 one-Void shell constructor。

## 五、ClassInfo consumer 闭包

root class-ID 的业务 consumer 只有共同 root view 路径：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `D3DLayer` arg0 root unbox | `0x52D308` | `0x49361C` | `0x1002317E8` | `0x230594` |
| `D3DImage` arg0 root unbox | `0x52D98C` | `0x4939F8` | `0x100231BE8` | `0x2309DC` |
| root primary ctor | `0x531274` | `0x495618` | `0x100233C88` | `0x23295C` |
| SetAdaptor helper | `0x5322AC` | `0x495F90` | `0x100234964` | `0x2336CA` |

两个 typed factory 只读取 adaptor 的 native root；它们不会接受 concrete D3D/DrawDeviceD3D
class ID，也不会 fallback 到 `D3DLayerObjectNativeInstance` borrowed-view ID。

## 六、root adaptor ABI 与虚表

### 6.1 布局

```text
LP64  0x18: vptr@0x00, DrawDeviceObjectBase* native@0x08,
              bool sticky@0x10, padding
ILP32 0x0C: vptr@0x00, DrawDeviceObjectBase* native@0x04,
              bool sticky@0x08, padding
```

### 6.2 四端函数身份

| 目标 | adaptor vtable | Invalidate | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x19FAB78` | `0x532588` | `0x5325C8` | `0x532624` |
| Android armv7 | `0x10AAFD8` | `0x49613E` | `0x49615C` | `0x496198` |
| iOS arm64 | `0x101AEE7D8` | `0x100234B20` | `0x100234B60` | `0x100234BC0` |
| iOS armv7 | `0x183902C` | `0x2337CC` | `0x2337EA` | `0x233824` |

Android arm64 的旧 IDB 把一个前置 `BRK #1` 与 Invalidate 吞成同一函数，导致 Hex-Rays 把
函数错误标成 noreturn trap。V208 已把前置 trap 保留为独立 code item，并把真实函数边界恢复为
`0x532588..0x5325C8`；vtable xref 也重新闭合到新入口。

## 七、`SetAdaptorWithNativeInstance` 三态

### 7.1 四端入口与唯一 caller

| 目标 | helper | caller |
|---|---:|---:|
| Android arm64 | `0x5322AC` | `0x531274` |
| Android armv7 | `0x495F90` | `0x495618` |
| iOS arm64 | `0x100234964` | `0x100233C88` |
| iOS armv7 | `0x2336CA` | `0x23295C` |

完整 xref 证明没有第二个 producer；两个 concrete root factory 都汇入同一个主基类构造器。

### 7.2 共同伪代码

```cpp
bool SetAdaptorWithNativeInstance(
    iTJSDispatch2 *owner, DrawDeviceObjectBase *fresh, bool err) {
    Adaptor *adaptor = GetAdaptor(owner, false);

    if(adaptor != nullptr) {
        if(adaptor->native != nullptr) {
            if(!adaptor->sticky)
                delete adaptor->native;   // primary virtual deleting dtor
            adaptor->native = nullptr;
            adaptor->sticky = false;
        }
        // native==null: deliberately skip _deleteInstance; sticky is retained
    } else {
        adaptor = new Adaptor;            // native=null, sticky=false
    }

    adaptor->native = fresh;              // publication precedes REGISTER
    iTJSNativeInstance *base = adaptor;
    tjs_error hr = owner->NativeInstanceSupport(
        TJS_NIS_REGISTER, D3DLayerBaseClassID, &base);

    if(TJS_FAILED(hr)) {
        if(err)
            TVPThrowExceptionMessage(TJS_W("Adaptor registration failed."));
        return false;                     // no rollback/delete
    }
    return true;
}
```

### 7.3 状态矩阵

| 入口状态 | attach 前动作 | 新 native 的临时 sticky | 旧 root |
|---|---|---|---|
| owner 无 adaptor | 分配 `0x18/0x0C` | false | 无 |
| existing，native 非空，sticky=false | 删除旧 root、清槽 | false | 被删 |
| existing，native 非空，sticky=true | 不删旧 root、清槽 | false | 泄漏/仍由外部 owner 管理 |
| existing，native=null，sticky=false | 不清 sticky | false | 无 |
| existing，native=null，sticky=true | 不清 sticky | true | 无 |
| owner=null | GET 返回 null，先分配 adaptor；REGISTER 解引用 null | 未完成 | adaptor/fresh 留在崩溃现场 |

对 populated sticky adaptor 的重复 root 构造会丢弃旧 borrowed pointer；随后 concrete factory
wrapper 还会覆盖旧 concrete owner slot而不删除旧 root，所以 normal constructor re-entry 的旧 root
泄漏同时出现在 root view 和 concrete view 两层。

## 八、主基类构造器的严格 sticky 晋升

helper 返回后四端共同执行：

```cpp
(void)SetAdaptorWithNativeInstance(owner, this, false);

Adaptor *adaptor = GetAdaptor(owner, false);
adaptor->sticky = true; // no null check
```

这形成精确的失败分类：

- `owner==null`：helper 在 REGISTER 虚调用处直接崩溃；不会走到第二次 GET；
- fresh REGISTER 失败且 receiver 未保留 adaptor：helper 返回 false，第二次 GET 失败/null，sticky
  store 对零基址偏移字段写入；
- existing adaptor 的重复 REGISTER 失败，但旧 attachment 仍可 GET：false 被忽略，第二次 GET
  仍返回同一 adaptor，sticky 晋升成功，构造继续；
- REGISTER 部分发布后返回失败：同样由后续 GET 的实际结果决定；没有显式 rollback；
- fresh 正常路径：REGISTER 后到 sticky store 前，adaptor 短暂表现为 non-sticky owner；
- existing `native=null,sticky=true`：helper 保留 sticky，新 root 从覆盖开始就是 borrowed；
- 第二次 GET 自身抛出时，异常机制按各平台 ctor/new-expression 边界处理 native root storage，
  但 helper 已发布的 adaptor/native 没有配套事务回滚。

因此旧的“注册失败总是返回 false”或“REGISTER 失败必然马上崩溃”都不精确；真正控制最终行为的是
第二次 GET 是否还能取得非空 adaptor。

## 九、Invalidate、析构与 owner topology

### 9.1 共同删除门

```cpp
void deleteInstance() {
    if(native != nullptr && !sticky)
        delete native;
    native = nullptr;
    sticky = false;
}
```

- Invalidate 执行上述逻辑并保留 adaptor storage；
- complete dtor 先安装 derived vptr，执行同一条件删除，再清字段并进入 base teardown；
- deleting dtor 条件删除 native 后直接 `operator delete(adaptor)`，不需要先清即将释放的字段；
- native 删除走 root 的 primary virtual deleting-dtor slot，能正确分派到 concrete D3D 或
  DrawDeviceD3D；
- 正常 root 构造最终 sticky=true，所以这些路径只清借用槽，不删除 root。

### 9.2 正常双身份

```text
script receiver
├─ concrete D3D / DrawDeviceD3D adaptor
│  ├─ native = complete root
│  ├─ sticky = false
│  └─ owns root
└─ D3DLayerBase adaptor
   ├─ native = same root
   ├─ sticky = true
   └─ borrows root
```

concrete adaptor 先销毁时，root view 暂时 dangling，但其后续 Invalidate/dtor 只清槽，不解引用或
删除 native。root view 先销毁时也只断开借用，不影响 concrete owner。错误 concrete shell 上的
factory rollback 会删除 fresh root，却把 shell 中已经 sticky-published 的 root view 留成 dangling，
直到 shell 销毁时静默清槽。

## 十、no-unload 与边界行为

| 旧/简化叙事 | 四端恢复结果 |
|---|---|
| `D3DLayerBase` 只有一个 lazy static ID | 它有完整 ClassInfo tuple、独立 guard、name/ID/null object。 |
| 先 Find，找不到再 Register | PreRegist 直接 Register；不存在 Find fallback。 |
| 它是隐藏的 global native class | 只注册 class ID；无 global member、descriptor 或 script constructor。 |
| tuple 会随 DrawDeviceD3D Unregist 清理 | 没有该内部身份的 Unregist/Clear 路径，process-lived。 |
| SetAdaptor 总把 sticky 清 false | existing native-null adaptor跳过 deleteInstance，保留 sticky。 |
| REGISTER 失败会删除 fresh adaptor/root | 失败不回滚、不删除；caller 还忽略 bool。 |
| REGISTER 失败必崩 | 只有 helper null owner或后续 GET 失败/null确定崩；existing attachment 可掩盖失败。 |
| root view 是共同 owner | 正常 sticky borrowed；concrete non-sticky adaptor 是唯一 owner。 |

## 十一、源码与测试修正

### `cpp/plugins/DrawDeviceD3D.cpp`

- 把 `GetD3DLayerBaseClassID` 的 lazy `FindNativeClassID`/register fallback 改回真正的
  `ncbClassInfo<DrawDeviceObjectBase>::GetID()`；
- PreRegist 直接 `TJSRegisterNativeClass` 并 first-publish `{name,id,null}`；
- 把 `D3DLayerObjectNativeInstance` 改回 PreRegist 写入的单一 class-ID word；
- 保留 root helper 的三态、partial publication 和严格后续 GET；
- 删除 helper 对 null owner 的提前可恢复返回，使 source boundary 与参考一致；
- compiled source 注释不包含参考绝对地址。

### `tests/unit-tests/plugins/motionplayer-dll.cpp`

注册级回归新增：

- 两个内部名称都已取得 native class ID；
- global object 上两个同名成员都返回 `TJS_E_MEMBERNOTFOUND`；
- 已有 wrong-shell 测试继续验证 root view 能注册而 concrete ID lookup 失败；
- 已有 null-receiver 注释继续明确它不是安全的 `TJS_E_NATIVECLASSCRASH` oracle。

## 十二、恢复库写回

四份 recovery IDB 共完成并原位保存：

- 8 个 ABI layout type（每库 ClassInfo + adaptor）；
- 36 个函数/data/vtable rename；
- 32 次 global/function type application；
- 36 条证据注释；
- 16 个书签；
- 4 次 UTF-16LE exact-byte name 验证；
- Android arm64 一处真实 Invalidate 函数边界修复。

保存后 `idb_list` 为零 session。

## 十三、验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 两者仅保留既有 `_tss` literal-operator warning；
- Web build：通过；
- Wasmtime build：通过；
- 两个产物均 `WebAssembly.validate=true`；imports/exports 分别保持 Web `539/69`、
  Wasmtime `538/69`；
- CTest 两端均 exit 0，当前配置仍报告 `No tests were found`；
- `git diff --check` exit 0，仅输出工作树既有 LF→CRLF warning；
- Web Wasm：`85,660,946` bytes，SHA-256
  `1706B037BCE4DC375992B2D2C63039CBAD3620C7C9BDA4993AE15F38CAAEBA9E`；
- Wasmtime Wasm：`85,008,087` bytes，SHA-256
  `139430AA13C51C78B16B957E22140E22D485288A2C05B8C10C137E5D835BFDBA`；
- 两个产物相对 V207 都精确减少 `449` bytes。section 变化同构：FUNCTION `+1`、
  GLOBAL `+0x10`、CODE `-0x27A`、name `+0xA8`、DATA 零变化；这是移除两个 lazy
  local-ID guard、恢复完整 root ClassInfo publication 后的预期代码/符号重排，不是随机漂移；
- Web sections：FUNCTION `0x1BD30`、GLOBAL `0xD5C2`、CODE `0x1A42556`、
  DATA `0x5A4017`、name `0x3185EE4`；
- Wasmtime sections：FUNCTION `0x1BA4F`、GLOBAL `0xD5EA`、CODE `0x19EA504`、
  DATA `0x5A1267`、name `0x3141D7A`。
