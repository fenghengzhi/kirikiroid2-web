# motionplayer `global.D3D` ClassInfo、raw Factory、根 adaptor、容器与生命周期（四参考二进制）

日期：2026-08-17  
阶段：V206

## 结论摘要

本轮把 `global.D3D` 从“与 `DrawDeviceD3D` 表面相同的另一个根名”闭合成一个完整、独立的 NCBind concrete class：

- 它有独立的 `ncbClassInfo<D3D>` tuple、一次初始化 guard、class ID、native-class descriptor、raw factory descriptor vtable、concrete instance adaptor 和最终派生 vtable；
- 它与 `DrawDeviceD3D` 注册相同的 34-entry 公开表面，但没有 native ClassInfo 继承关系；保留下来的 `IsSubClass` 叶函数恒返回 false；
- exact one-Void 是 descriptor 私有的 empty-adaptor sentinel，不调用业务 factory；普通调用要求至少两个参数，只按 arg0、arg1 顺序转成 `tjs_int`，忽略 surplus；
- raw descriptor 在所有返回路径保持调用方 `result`，并且先调用业务 factory，随后才从 receiver 查询 D3D concrete adaptor；attach 失败会用 D3D primary deleting destructor 删除 fresh native；
- 根构造期间另注册一个 sticky `D3DLayerBase` adaptor，它只是借用视图；D3D concrete adaptor 为 non-sticky，是普通成功对象的唯一 native owner；
- 根内部是四棵有序红黑树：两个按实时 front/back index 比较、允许重复的 layer-pointer multiset，一个 non-owning `D3DImage*` set，一个拥有 mapped module value 的 `map<uint32,D3DModuleBase*>`；
- 完整析构先释放 targets 和 transition rule texture，再按 key 顺序 virtual-delete `Modules` 中每个非空 value，最后按成员逆序只销毁四棵树的节点；其余三棵树的 stored pointer 全部是 borrowed；
- 四端正常成功/attach 失败语义一致，但业务 factory 的异常清理并不一致。旧报告把 A64 的 phased landing pads 外推到所有目标，是过时且错误的。

源码逻辑已经能表达共同正常路径、容器类型和所有权；本轮没有为了模拟目标编译器异常表差异而引入平台条件分支，只在源码中明确记录二进制边界，并补充可执行公开 ABI 回归。

## 范围与方法

只把以下四份项目内参考作为事实来源，不再把历史 `libkrkr2.so` 注释当证据：

| 缩写 | 参考二进制 | ABI / STL |
|---|---|---|
| A64 | `reference/binaries/Kirikiroid2_1.3.9_Android_arm64-v8a.so` | AArch64 LP64 / libstdc++ |
| A32 | `reference/binaries/Kirikiroid2_1.3.9_Android_armabi-v7a.so` | ARM EABI ILP32 / libstdc++ |
| I64 | `reference/binaries/Kirikiroid2_1.3.9_iOS_arm64` | AArch64 LP64 / libc++ |
| I32 | `reference/binaries/Kirikiroid2_1.3.9_iOS_armv7` | ARMv7 ILP32 / libc++、SJLJ EH |

检查顺序遵守“一库一会话”：先完成四端只读映射和交叉验证，再逐库打开 recovery IDB 写回，保存后关闭，最后确认无遗留 IDA 会话。证据包括：

1. 从 `global.D3D` 注册链逆向到独立 ClassInfo tuple/guard；
2. 追踪 tuple 的所有读写、Set/Clear、registration/unregistration wrapper 和 public global publication；
3. 从 native-class allocator 追踪 empty concrete adaptor 的构造、invalidate 与 deleting destructor；
4. 从 class-name factory item 追踪 raw descriptor `FuncCall` 与 business factory；
5. 从最终 primary/secondary vtable 追踪 complete/deleting destructor；
6. 从 root ctor/dtor 的 STL helper 和字段偏移恢复四棵树的具体 ABI、所有权和销毁顺序；
7. 单独审计四端 business factory 全函数边界、landing pad、异常表和 I32 SJLJ call-site，不再用源码级常识替代产物证据。

地址仅保留在本分析映射中；编译源码注释只使用语义名和 ABI 描述。

## 一、`D3D` 是独立 concrete class，不是别名

`D3D` 与 `DrawDeviceD3D` 的 34-entry member surface 相同：factory 加 33 个 property/method item。相同表面来自同一注册宏形态，不意味着共享 ClassInfo 或 native type。

四端都能同时观察到：

- 两套独立 ClassInfo storage 和 guard；
- 两个独立 class ID；
- 两套 native-class registration wrapper；
- 两套 empty-adaptor allocator / adaptor vtable；
- 两套 raw factory descriptor vtable，其 wrapper 查询各自 class ID；
- 两套最终 primary/secondary vtable；
- 一个 concrete shell 不能承载另一 concrete class 的 native pointer。

公开测试使用 `DrawDeviceD3D` one-Void shell 调用 `D3D` raw factory，能够让 root ctor 的共享 sticky `D3DLayerBase` 注册成功，却让随后 D3D concrete class-ID lookup 失败，稳定得到 `TJS_E_NATIVECLASSCRASH`。这直接区分“共享 root view”与“独立 concrete adaptor”。

## 二、独立 ClassInfo tuple 与 guard

### 2.1 ABI 布局

恢复出的逻辑类型为：

```cpp
template<class T>
struct ncbClassInfo_guess {
    bool initialized;
    // ABI padding to pointer alignment
    const tjs_char *name;          // borrowed
    tjs_int32 classID;
    // LP64 padding to pointer alignment
    iTJSDispatch2 *classObject;    // borrowed
};
```

| 目标 | tuple | `classID` | guard | tuple 大小 | guard 大小 |
|---|---:|---:|---:|---:|---:|
| A64 | `0x1AAF5F8` | `0x1AAF608` | `0x1AAF618` | `0x20` | `0x08` |
| A32 | `0x110E1D0` | `0x110E1D8` | `0x110E1E0` | `0x10` | `0x04` |
| I64 | `0x101AEE418` | `0x101AEE428` | `0x101AEE438` | `0x20` | `0x08` |
| I32 | `0x1838E4C` | `0x1838E54` | `0x1838E5C` | `0x10` | `0x04` |

LP64 tuple 是 32 B：`initialized@0x00`、`name@0x08`、`classID@0x10`、`classObject@0x18`。ILP32 tuple 是 16 B：对应偏移 `0x00/0x04/0x08/0x0C`。name/classObject 都是 raw borrowed slot；Clear 不做 Release，只把完整 tuple（包括 published flag 与 padding）清零。

### 2.2 静态初始化

| 目标 | static init |
|---|---:|
| A64 | `0x42CA58` |
| A32 | `0x2FEF14` |
| I64 | `0x10024C980` |
| I32 | `0x24E578` |

四端共同伪代码：

```cpp
if((guard & 1) == 0) {
    memset(&D3DClassInfo, 0, sizeof(D3DClassInfo));
    guard = 1;
}
```

检查的是 guard 的 bit 0/低字节，初始化时清整个 tuple，再发布 1。它不是与 `DrawDeviceD3D` 共用的 guard。

### 2.3 保留下来的叶函数

Android 两份仍保留可独立命名的叶函数：

| 操作 | A64 | A32 | 行为 |
|---|---:|---:|---|
| `GetName` | `0x52BB78` | `0x492EA0` | 返回 borrowed name |
| `GetID` | `0x52BB88` | `0x492EAC` | 返回 class ID |
| `GetClassObject` | `0x52BB98` | `0x492EB8` | 返回 borrowed class object |
| `IsSubClass` | `0x52BBA8` | `0x492EC4` | 恒 false |
| `Set` | `0x52BBB0` | `0x492EC8` | first-publication-wins |
| `Clear` | `0x52BBE8` | `0x492EF0` | 清完整 tuple |
| zero ctor | `0x52BC04` | `0x492F04` | 清完整 tuple |

A32 原数据库没有正确建立 `IsSubClass` 与 zero ctor，本轮先恢复函数边界再命名。iOS 两份把这些短叶内联进 generated registration code，但 tuple 读写仍给出同样语义。

`IsSubClass == false` 说明 native ClassInfo 层面不存在 `D3D : DrawDeviceD3D` 或反向继承。C++ 两者都继承共同 root base，不会在 NCBind concrete class-ID 查询中互相接受。

### 2.4 `Set`/`Clear` 的边界

`Set(name,classID,classObject)` 只有在 `initialized==false` 时才写入，并在三个 payload slot 写完后发布 `initialized=true`；重复发布保持第一组值。`Clear` 无条件把 initialized、name、classID、classObject 和 padding storage 一并归零。由此产生两个重要边界：

- registration 在完整 member surface 建立前就可发布 ClassInfo；后续注册失败可能留下 prefix-visible/partially registered state；
- unregister Clear 后，旧 raw pointer 不再由 ClassInfo 可达，但 Clear 自身不管理引用计数。

## 三、registration / unregistration 生命周期

### 3.1 四端函数映射

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `Regist` wrapper | `0x538A80` | `0x49BD14` | `0x10023B1C0` | `0x23AC4C` |
| `Unregist` wrapper | `0x538BE4` | `0x49BD98` | `0x10023B228` | `0x23AD00` |
| `RegistBegin` | `0x538D3C` | `0x49BE14` | `0x10023B284` | `0x23ADB0` |
| begin EH cleanup | — | — | `0x10023B378` | `0x23AED2` |
| `CreateEmptyAdaptor` | `0x538E90` | `0x49BEFC` | `0x10023B39C` | `0x23AF0C` |
| adaptor success/no-op slot | `0x538EBC` | `0x49BF1C` | `0x10023B3C8` | `0x23AF2C` |
| adaptor invalidate | `0x538EC4` | `0x49BF20` | `0x10023B3D0` | `0x23AF30` |
| adaptor complete dtor | `0x538F04` | `0x49BF3C` | `0x10023B410` | `0x23AF4C` |
| adaptor deleting dtor | `0x538F60` | `0x49BF78` | `0x10023B470` | `0x23AF86` |
| dispatch/branch helper | — | — | `0x10023B4BC` / `0x10023B4E4` | `0x23AFB4` / `0x23B044` |
| `RegistEnd` | `0x538FAC` | `0x49BFA8` | `0x10023B4F8` | `0x23B054` |
| `UnregistEnd` | 内联/折叠 | `0x49C068` | `0x10023B5F4` | `0x23B188` |
| `RegistNCMIfNeeded` | 内联/折叠 | `0x49C0A8` | `0x10023B664` | `0x23B1C8` |
| NotImplemented item | `0x53910C` | — | — | — |
| register item helper | `0x539114` | — | — | — |

I64/I32 的 begin cleanup 只负责 native-class registration descriptor 构造失败；它不是 business factory 的 native allocation cleanup，不能混为一谈。

### 3.2 `RegistBegin`

`RegistBegin` 的共同顺序：

1. 分配 native-class descriptor：LP64 `0xB0`，ILP32 `0x70`；
2. 安装 class-specific empty-adaptor allocator；
3. first-publish 独立 D3D ClassInfo tuple；
4. 注册 finalize/termination callback；
5. 继续注册 factory 与其余 33 个公开 item。

这是非事务式 prefix publication：ClassInfo 先可见，随后每个 item 分别注册。中途异常的恢复不能被描述成“整个 class 原子出现”。

### 3.3 `RegistEnd` 与 `UnregistEnd`

`RegistEnd` 将 native class object 写到 `global.D3D`。若 global object 不存在，路径只记录错误，并不回滚已经发布的 ClassInfo 或 member prefix。

`UnregistEnd` 在 global 可用时移除 `D3D` member，然后清整个 ClassInfo tuple；缺失 global/member 的路径是容忍式的。generated `Unregist` 虚函数链确实存在，但当前集成式 module loader 仍没有恢复到实际 unload/registered-set erase caller，因此“有 Unregist 函数体”不能改写成“运行时正常卸载一定执行”。

## 四、concrete D3D adaptor

### 4.1 精确布局

| ABI | 大小 | 布局 |
|---|---:|---|
| LP64 | `0x18` | `{ vptr@0x00, native@0x08, sticky@0x10, padding }` |
| ILP32 | `0x0C` | `{ vptr@0x00, native@0x04, sticky@0x08, padding }` |

`CreateEmptyAdaptor` 设置 class-specific adaptor vptr、`native=null`、`sticky=false`。这就是 exact one-Void 创建的脚本壳；它不是 `(width=0,height=0)` 根对象。

### 4.2 invalidate / destructor

共同伪代码：

```cpp
void Invalidate() {
    if(native != nullptr && !sticky)
        delete native;       // primary virtual deleting destructor
    native = nullptr;
    sticky = false;
}
```

complete adaptor destructor 调用同一删除/清零语义；deleting destructor 随后释放 adaptor 自身。`delete nullptr` 路径不触发 native destructor。

### 4.3 owner topology

正常 D3D 根对象同时存在两个 native-instance view：

```text
script receiver
├─ D3D concrete adaptor: non-sticky, native = complete D3D root, owns
└─ D3DLayerBase adaptor: sticky, native = same root, borrows
```

root common constructor先向 `objthis` 注册 `D3DLayerBase` view，再把它标 sticky。raw descriptor 只有在 business factory 完成后，才把 root 指针写入预先存在的 D3D concrete adaptor。

V208 已把这条内部 view 独立闭合：它有 `name/ID/null-classObject` 的完整 ClassInfo，PreRegist
直接 Register 而不是 lazy Find fallback；`SetAdaptorWithNativeInstance` 对 native-null existing
adaptor 保留 sticky，REGISTER failure 不回滚且 caller 忽略 bool，最终是否崩溃取决于后续严格
GET 是否仍取得 adaptor。详见
`motionplayer_d3dlayerbase_classinfo_preregist_adaptor_sticky_failure_four_binary_2026-08-17.md`。

四端都没有为 D3D 找到“已有 native -> CreateAdaptor” producer；普通脚本 `CreateNew` 和 exact one-Void shell + raw attach 是 concrete adaptor 的实际生产路径。

### 4.4 边界

- one-Void shell 的 adaptor 存在但 `native==null`；普通 typed property/method 会得到 `TJS_E_NATIVECLASSCRASH`；
- ordinary raw factory 成功 attach 时直接写 native；若对已 populated shell 重入，旧 native 不会先删，产生旧 root 泄漏；
- attach 到错误 concrete shell 时，root ctor 已可能在该 shell 注册 sticky `D3DLayerBase` view；wrapper 随后删除 fresh root，但 sticky borrowed view 留下 dangling pointer；
- null `objthis` 加两个普通参数不是稳定的 `-1008` 路径：root ctor 对 `D3DLayerBase` 注册/二次查询是严格的，会在 concrete lookup 之前失败；
- adaptor 销毁时若 `sticky=true`，只清槽不删 native；D3D concrete adaptor 的正常实例保持 non-sticky。

## 五、raw two-argument Factory

这里的 `Factory(&D3D::factory)` 是 `ncbNativeClassFactory<D3D>` raw descriptor，不是 typed Factory。

### 5.1 descriptor wrapper

I32 的 D3D raw wrapper 位于 `0x23B524`；其他三端及两 root class 的映射已经在旧 root-factory 报告中闭合。四端 D3D wrapper 共同顺序为：

```cpp
tjs_error FuncCall(membername, result, argc, argv, objthis) {
    if(membername != nullptr)
        return TJS_E_MEMBERNOTFOUND;

    if(argc == 1 && argv[0]->Type() == tvtVoid)
        return TJS_S_OK; // empty-adaptor sentinel

    D3D *native = nullptr;
    tjs_error hr = D3D::factory(&native, argc, argv, objthis);
    if(TJS_FAILED(hr))
        return hr;

    auto *adaptor = GetD3DConcreteAdaptor(objthis);
    if(adaptor != nullptr) {
        adaptor->native = native; // no old-native cleanup
        return TJS_S_OK;
    }

    if(native != nullptr)
        delete native;
    return TJS_E_NATIVECLASSCRASH;
}
```

`flag`、`hint`、`result` 不参与流程；result 在 member-not-found、sentinel、business error、success 和 attach failure 全部保持原值。

### 5.2 精确边界表

| 调用 | business factory | native | receiver | 返回 | result |
|---|---|---|---|---:|---|
| named member，任意 argv | 不调用 | 无 | 不检查 | `-1001` | 保持 |
| exact one Void | 不调用 | 无；保留 empty shell | 不检查 | `0` | 保持 |
| zero args | argc gate | 无 | 不检查 | `-1004` | 保持 |
| one non-Void | argc gate | 无 | 不检查 | `-1004` | 保持 |
| 两个参数 + 正确 empty D3D shell | 调用 | 构造并 attach | 成功 | `0` | 保持 |
| 两个参数 + 错误 concrete shell | 调用 | 构造后 deleting-dtor rollback | concrete lookup 失败 | `-1008` | 保持 |
| 两个参数 + null receiver | 会先进入 root ctor | 可能在共同 root 注册中失败 | 不构成安全 lookup 边界 | 非普通可恢复路径 | 不应依赖 |
| 三个以上参数 | 只读前两项 | 与两参数相同 | 与两参数相同 | 同 ordinary path | 保持 |

two Void 不匹配 sentinel；它进入 ordinary path，两项分别按 Variant 整数转换规则成为 0。

### 5.3 business factory

| 目标 | 函数 | complete object size |
|---|---:|---:|
| A64 | `0x52CC54` | `0x200` |
| A32 | `0x49337C` | `0x13C` |
| I64 | `0x10023156C` | `0x1A0` |
| I32 | `0x230300` | `0x10C` |

四端正常路径精确顺序：

1. `argc < 2`：返回 `TJS_E_BADPARAMCOUNT`，发生在 allocation 之前；
2. 分配 complete D3D root；
3. 把 arg0 转为 `tjs_int`；
4. 把 arg1 转为 `tjs_int`；
5. 以 `(width,height,objthis)` 调用共同 root ctor；
6. 安装 D3D-specific final primary/secondary vtable；
7. 最后写 `*out`，返回成功。

surplus 从不读取。`objthis` 是借用的 script owner，不 AddRef。

## 六、business factory 异常清理矩阵：旧结论订正

这是本轮最重要的过时注释修正。不能把 A64 的 landing-pad 形态解释成四端共同源码级保证。

| 目标 | factory EH 形态 | arg0/arg1 conversion escape | root ctor escape |
|---|---|---|---|
| A64 | 两个 phased landing pad | raw-delete allocation | primary base 已完成时先调用 root-base dtor，再 raw delete；更早阶段只 delete |
| A32 | factory 仅 `0x5C`，无 landing pad/EH cleanup | 泄漏 `0x13C` allocation | 泄漏 `0x13C` allocation |
| I64 | factory 仅 `0x98`，无 landing pad/EH cleanup | 泄漏 `0x1A0` allocation | 泄漏 `0x1A0` allocation |
| I32 | SJLJ landing `0x2303DA` | call-site 1/2 raw-delete `0x10C` 后 resume | call-site 3 进入 terminate/trap |

A64 D3D 的两个 cleanup 入口为：

- `0x52CDC8`：root primary base 已完成，先调共同 root-base dtor，再释放 allocation；
- `0x52CDD8`：base 尚未完成，只释放 raw allocation。

I32 的三处有风险调用分别写入 SJLJ call-site 值 1、2、3；landing jump-table 原始字节为 `02 02 02 00`。case 0..2 汇入 raw-delete + SJLJ resume，case 3 落到 termination/trap 形态，Hex-Rays 将它显示成 abort 类路径。

这组差异描述的是四份已发布产物的真实边界，不要求 portable Web 源码人为制造泄漏或终止。移植实现应保持正常 ABI 和显式 attach-failure rollback；异常矩阵作为兼容性/取证记录，避免今后再次用泛化的“new-expression 一定清理”注释覆盖二进制事实。

## 七、最终 vtable 与 concrete destructor

### 7.1 地址映射

| 目标 | class | primary vtable | complete dtor | deleting dtor |
|---|---|---:|---:|---:|
| A64 | `DrawDeviceD3D` | `0x19FA908` | `0x531410` | `0x531438` |
| A64 | `D3D` | `0x19FACB8` | `0x531410` | `0x533370` |
| A32 | `DrawDeviceD3D` | `0x10AAEA0` | `0x495744` | 独立 slot/body |
| A32 | `D3D` | `0x10AB078` | `0x495744` | `0x496DC8` |
| I64 | `DrawDeviceD3D` | `0x101AEE568` | `0x100233F54` | `0x100233F7C` |
| I64 | `D3D` | `0x101AEE9A8` | `0x10023590C` | `0x100235934` |
| I32 | `DrawDeviceD3D` | `0x1838EF4` | `0x232C74` | `0x232C8C` |
| I32 | `D3D` | `0x1839110` | `0x234714` | `0x23472C` |

Android 两端把字节完全相同的 complete destructor 链接折叠到同一地址，但 deleting destructor、class vtable 和 class ID 仍各自独立。本轮 A64 因此采用 `DrawDeviceD3D_D3D_shared_complete_dtor_guess`，避免把 linker folding 错命名成某一个 concrete class 的专属函数。

iOS 两端没有折叠：两类的 complete/deleting destructor 都是独立地址，虽然 complete bodies 结构相同。

### 7.2 析构顺序

所有 complete root destructor 的结构一致：

```text
concrete D3D destructor body
  -> secondary tTVPDrawDevice destructor
  -> primary DrawDeviceObjectBase/root-base destructor
     -> primary-root member/body teardown
```

deleting destructor 在 complete chain 后调用 `operator delete`。non-sticky D3D concrete adaptor 和 raw wrapper attach-failure path 都通过 primary vtable 的 deleting slot 进入这条链。

## 八、根对象四棵内部容器

### 8.1 source-level 类型

```cpp
std::multiset<D3DLayerObject*, FrontItemLess_guess> FrontItems;
std::multiset<D3DLayerObject*, BackItemLess_guess>  BackItems;
std::set<D3DImage*>                                  ManagedObjects;
std::map<tjs_uint32, D3DModuleBase_guess*>           Modules;
```

两个 layer set 的 comparator 不在 node 中保存整数 key，而是在比较时读取 pointed object 的 live front/back index。插入不拒绝 equivalent value，因此是 multiset。修改 index 时，代码必须先从旧等价范围中按 pointer identity 擦除恰好一个 node，再更新 index 并重新插入。

`ManagedObjects` 按 pointer 排序且唯一；D3DImage ctor 插入自己，dtor 删除自己。root 不拥有 image。

`Modules` 按 `uint32` class ID 排序；map node 拥有 key/pointer pair，但 C++ map 本身不会删除 pointer，因此 root dtor 显式 virtual-delete mapped value。

### 8.2 根内偏移与 tree-object ABI

| 目标 | Front | Back | Managed | Modules | 单个 tree header |
|---|---:|---:|---:|---:|---:|
| A64/libstdc++ | `+0x48` | `+0x78` | `+0xA8` | `+0xD8` | `0x30` |
| A32/libstdc++ | `+0x2C` | `+0x44` | `+0x5C` | `+0x74` | `0x18` |
| I64/libc++ | `+0x48` | `+0x60` | `+0x78` | `+0x90` | `0x18` |
| I32/libc++ | `+0x2C` | `+0x38` | `+0x44` | `+0x50` | `0x0C` |

相同源代码在 Android/iOS 上出现不同 header 大小和字段步长，是 libstdc++ `_Rb_tree` 与 libc++ `__tree` ABI 差异，不是插件有两套容器设计。

### 8.3 node teardown 算法

Android/libstdc++ 的 tree erase helper 使用递归右子树、迭代左链的 `_M_erase` 形态；iOS/libc++ helper 对 node 递归 left、递归 right、再释放当前 node。遍历形态不同，但这四棵树的 teardown 都只销毁 node/pair，不会自动 delete stored pointer。

I32 单独保留下来的四个 tree helper：

| helper | 地址 | value ownership |
|---|---:|---|
| Modules node cleanup | `0x233852` | values 已在前置有序 pass 中删除；此处只删 node |
| ManagedObjects node cleanup | `0x233878` | stored image pointer borrowed |
| BackItems node cleanup | `0x23389E` | stored layer pointer borrowed |
| FrontItems node cleanup | `0x2338C4` | stored layer pointer borrowed |

### 8.4 完整 root teardown 顺序

共同逻辑顺序：

1. 释放 render targets；iOS 通过 `ReleaseTargets` helper，Android 产物可见 front/back 的直接释放；
2. 若非空，Release transition-rule texture；
3. 从 `Modules.begin()` 到 end，按 key 顺序对每个非空 mapped value 调用 virtual deleting destructor；
4. 销毁 transition Variant；
5. 销毁 Modules tree nodes；
6. 销毁 ManagedObjects tree nodes，不删除 images；
7. 销毁 BackItems tree nodes，不删除 layers；
8. 销毁 FrontItems tree nodes，不删除 layers。

I32 root teardown 入口为 `0x232B14`。A64/I64/A32 的结构由对应 complete root dtor 与 tree helper 交叉确认。

`CurrentTarget`、borrowed ScriptOwner 和 sticky adaptor 不在 root teardown 中被清理/通知。由此保留参考边界：如果 D3DImage、D3DLayer 或 script-side sticky view 活得比 root 更久，它们可保留 dangling owner/native pointer；root 不进行反向 detach sweep。

## 九、对象生命周期与失败路径

### 9.1 正常创建

```text
native class CreateNew
  -> 创建 script object
  -> 创建 D3D concrete empty adaptor {native=null, sticky=false}
  -> raw descriptor ordinary path
     -> business factory allocate/convert/construct
        -> root ctor 在同一 receiver 注册 sticky D3DLayerBase borrowed view
     -> 查询 D3D concrete adaptor
     -> adaptor.native = root
  -> 返回 script object
```

销毁时 concrete adaptor non-sticky 删除 root；sticky root view 随 script object 销毁，只清 adaptor 自身，不再次删除 root。

### 9.2 exact one-Void shell

```text
CreateNew(Void)
  -> 创建 script object + empty D3D adaptor
  -> raw descriptor sentinel 直接成功
  -> native remains null
```

该 shell 可供内部 raw attach 使用；在填充前调用普通实例 member 会返回 native-class crash。

### 9.3 错误 concrete receiver

```text
DrawDeviceD3D empty shell
  -> D3D business factory 可在 shell 上注册共同 D3DLayerBase view
  -> D3D concrete class-ID lookup 失败
  -> raw descriptor 用 D3D deleting destructor 删除 fresh root
  -> 返回 -1008，result 保持
  -> shell 上的 sticky root view 仍借用已删除地址
```

这条路径证明 wrapper rollback 与 business-factory exception cleanup 是两个完全不同的阶段。

### 9.4 populated shell 重入

wrapper attach 是 raw pointer overwrite，不先 invalidate adaptor。对同一 D3D receiver 再次 ordinary 调用 factory，会让旧 root 失去 concrete owner；旧 root 注册的 sticky view、children、targets、modules 等资源都随之泄漏或悬挂。这是参考边界，不能用“安全替换”注释掩盖。

## 十、源码与回归同步

### 10.1 `cpp/plugins/DrawDeviceD3D.cpp`

本轮保持执行逻辑不变，补充无绝对地址的四参考注释：

- 四棵 tree 的 libstdc++/libc++ ABI 大小、pointer ownership 和 reverse member teardown；
- root destructor 中 target/texture/module-value/tree-node 的精确顺序；
- D3D 独立 ClassInfo/guard/class ID/descriptor/adaptor；
- 0x18/0x0C concrete adaptor 与 sticky borrowed root view；
- Android complete-dtor folding 与 iOS distinct destructor；
- 四端 D3D business-factory exception cleanup matrix，明确旧的统一 cleanup 推断已失效。

没有加入按目标模拟 allocation leak/terminate 的逻辑：这些差异来自参考工具链产物，portable Web 代码应保持共同的正常调用和显式 attach rollback。

### 10.2 `tests/unit-tests/plugins/motionplayer-dll.cpp`

现有 `DrawDeviceD3D exposes the seven-class reference surface` 已覆盖：

- D3D 34-entry surface；
- raw descriptor named-member precedence；
- exact one-Void + null receiver；
- zero args 参数门；
- wrong concrete shell 的 post-construction delete / `-1008`；
- correct empty shell + three args 的 surplus ignore；
- attach 后 width/height 可见。

V206 新增：

- one non-Void 同样在 allocation 前返回 `TJS_E_BADPARAMCOUNT`，raw result 保持；
- one-Void empty D3D shell 在填充前访问 `primaryWidth` 返回 `TJS_E_NATIVECLASSCRASH`，并验证 ordinary typed-property result-clear。

异常 leak/terminate 矩阵不在单元测试中主动触发，以免把参考目标的不可恢复行为变成测试进程崩溃或人为要求 portable 实现泄漏。

## 十一、recovery IDB 固化

四份 recovery IDB 均已原位保存并关闭：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`；
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`；
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`；
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`。

| IDB | 新 local layout types | 语义 rename | 新 comment | bookmark |
|---|---:|---:|---:|---:|
| A64 | 2 | 25 | 21 | 4 |
| A32 | 2 | 25 | 19 | 4 |
| I64 | 2 | 21 | 18 | 4 |
| I32 | 2 | 22 | 19 | 4 |
| 合计 | 8 | 93 | 77 | 16 |

两类 layout type 分别是 target-specific `ncbClassInfo_D3D_*_guess` 和 `ncbInstanceAdaptor_D3D_*_guess`。另对 tuple、guard、registration/adaptor/destructor 函数应用了对应 data/function type；I32 本轮有 10 项显式 type application，并新恢复原数据库缺失的 Android armv7 ClassInfo 短叶函数边界。

关键命名策略：

- stripped/private template/helper 名统一保留 `_guess`；
- A64/A32 linker-folded complete destructor 明确命名为 shared，而不伪造单一 concrete owner；
- I64/I32 的 D3D complete/deleting destructor 保留独立 concrete 名；
- registration begin cleanup 与 business factory exception cleanup 使用不同名称和注释，防止再次混淆。

每库四个 bookmark 分别覆盖 ClassInfo init、registration/adaptor 生命周期、factory exception boundary 和 root tree teardown。保存后 `idb_list` 为零会话。

## 十二、旧假设修正表

| 旧说法 | 四参考闭环 |
|---|---|
| `D3D` 只是 `DrawDeviceD3D` 的脚本别名 | 两者只有 surface/root base 相同；ClassInfo、guard、class ID、adaptor、descriptor、final vtable 独立。 |
| 相同 complete-dtor 地址证明同一 concrete type | 只发生在 Android 的 identical-code folding；deleting dtor/vtable/class ID 仍独立，iOS complete dtor 也独立。 |
| ClassInfo 有 native parent | 保留的 `IsSubClass` 恒 false；共享的是 C++ root base，不是 NCBind concrete inheritance。 |
| one-Void 调用 `(0,0)` constructor | descriptor 在 business factory 前成功返回，只留下 null-native concrete shell。 |
| raw Factory 像 typed wrapper 一样清 result | 所有 raw descriptor 路径都保持 result。 |
| ordinary wrapper 先验证 receiver | 它先完成 business factory；root ctor 甚至先在 receiver 注册共同 sticky view。 |
| concrete adaptor 在 C++ ctor 中创建 | native class 先建 empty concrete adaptor；root ctor 只建 sticky D3DLayerBase view；wrapper 最后填 concrete native。 |
| sticky D3DLayerBase view 拥有 root | 它 borrowed；non-sticky D3D concrete adaptor 才拥有正常 root。 |
| 四棵树都拥有 stored pointer | 只有 Modules mapped value 由 root 显式 delete；其他三棵只拥有 node。 |
| STL node dtor 会 delete module pointer | module values 在独立有序 pass 中 virtual-delete；map tree helper随后只清 node/pair。 |
| 四平台 new-expression 异常都会清理 allocation | 仅 A64 D3D 有 phased cleanup；A32/I64 leak，I32 conversion cleanup但 ctor terminate/trap。 |
| null ordinary receiver 稳定返回 `-1008` | root ctor 的严格 D3DLayerBase 注册发生在 concrete lookup 前，null 不是安全可恢复边界。 |

## 十三、验证状态

四库交叉检查、IDB 写回/保存/关闭、源码注释、回归补充与工程验证均已完成：

- ordinary Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 `motionplayer-dll.cpp` syntax-only 均通过；两次都只有既有 `_tss` literal-operator deprecated warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；
- Node 对两份 Wasm 的 `WebAssembly.validate`/`WebAssembly.Module` parse 均成功；
- Web：539 imports、69 exports；Wasmtime：538 imports、69 exports；
- 两个 build tree 的 CTest 都以 0 退出，并明确报告当前没有发现 CTest test；
- `git diff --check` 通过。

产物审计：

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| `out/web/debug/index.wasm` | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` |
| `out/wasmtime/debug/index.wasm` | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2F` | `0x1BA4E` |
| GLOBAL | `0xD5B2` | `0xD5DA` |
| CODE | `0x1A427D0` | `0x19EA77E` |
| DATA | `0x5A4017` | `0x5A1267` |
| name custom | `0x3185E3C` | `0x3141CD2` |

两份大小、哈希、imports/exports 和上述 section 均与 V205 精确一致，符合本轮只改变源码注释与未链接测试 TU 断言、没有改变插件执行代码的预期。

## 十四、未闭合边界

- `D3D`/`DrawDeviceD3D` 的具体公开 surface 已闭合，但 common root 内仍有若干构造后从未访问的 pointer/state slot，只能继续保留 `_guess`；
- 四端异常矩阵是实际产物边界，尚不能仅凭反编译证明原始源码使用了哪套异常开关、宏或 placement helper；
- generated Unregist 链已恢复，但当前 loader 无可达 unload caller；未来若发现外部入口，需重新审计 ClassInfo/global member 的实际终止时序；
- root teardown 不 detach borrowed images/layers/adaptors，相关 UAF 窗口已经确认，但其所有脚本触发组合仍需在各具体对象纵切面继续闭合；
- 本轮完成的是一个高价值纵切面，不代表整个 motionplayer 已达到 100% 一比一。
