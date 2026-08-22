# motionplayer ordinary Canvas：source accessor temporary CopyRef、Object owner 与异常前缀（四参考二进制）

日期：2026-08-18  
阶段：V243

## 1. 结论

四个参考二进制的ordinary Canvas submitter在每个admitted item发布descriptor/color以后，调用共享source
resolver取得一个persistent owning source Variant。它没有先对该persistent Variant执行无引用转换；而是
按以下顺序建立source property accessor：

```text
sourceVariant = resolveRenderSource(item.sourceState.object)
temporary = CopyRef(sourceVariant)        // AddRef Object, then ObjThis
sourceObject = temporary.AsObject()       // AddRef Object only
destroy(temporary)                        // Release Object, then ObjThis
width  = sourceObject.PropGet("width")
height = sourceObject.PropGet("height")
```

因此width callback之前，temporary closure的ObjThis retain已经释放，只剩persistent source Variant与
accessor的raw Object retain。non-Object resolver结果也会先完成temporary CopyRef，随后才在`AsObject()`
conversion处抛异常；不能把type failure提前到CopyRef之前。

当前portable代码先调用`source.object.AsObjectNoAddRef()`，再从persistent lvalue直接构造
`ncbPropAccessor`。这同时少了一轮Object/ObjThis CopyRef/Release、改变了AddRef reentry与malformed-type
exception prefix。V243改为显式full-expression temporary，并令diagnostic raw pointer直接取自accessor
owner。

## 2. 四端地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| complete Canvas submitter | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| shared source resolver | `0x6C4E70` | `0x58E5C8` | `0x1001189FC` | `0x116DAE` |
| source temporary CopyRef | `0x6C4E7C` | `0x58E5D4` | `0x100118A08` | `0x116DC0` |
| source `AsObject` AddRef | `0x6C4EAC` | `0x58E5E0` | `0x100118A20` | `0x116DD4` |
| source temporary dtor | `0x6C4ED0` | `0x58E5EA` | `0x100118A2C` | `0x116DE0` |
| width read | `0x6C4EF4` | `0x58E60A` | `0x100118A4C` | `0x116E0A` |
| height read | `0x6C4F1C` | `0x58E624` | `0x100118A70` | `0x116E36` |

iOS arm64 recovery listing在本轮MCP rendered-text中显示出比entity EA小`0x10`的文本前缀，但xref/entity
addresses、既有四端映射与写回EA一致；表中继续使用数据库真实EA，不把renderer显示偏差写入源码。

## 3. descriptor/color与source之间的owner边界

本轮向前复核确认descriptor和color accessor已经使用正确的同族结构：persistent Player Variant先
CopyRef为temporary，accessor通过`AsObject()`取得raw Object retain，temporary在下一组property writes前
析构。source resolver发生在descriptor三字段与color四索引提交之后。

source与前两者的差异是producer：

- descriptor/color来自Player persistent Variants；
- source Variant由本item的resolver call返回并保持到primitive branch尾；
- source accessor额外拥有同一source Object的raw retain；
- source ObjThis只由persistent source Variant继续持有，accessor不retain ObjThis。

所以native owner树在dimension callbacks开始时为：

```text
descriptor raw Object accessor
color raw Object accessor
source persistent Variant(Object + ObjThis)
source raw Object accessor
```

不存在一个额外long-lived source temporary Variant，也不存在borrowed raw pointer取代source accessor owner。

## 4. 精确AddRef/Release前缀

对`Variant(Object=A, ObjThis=B)`且`A != B`，source accessor full expression发生：

```text
A.AddRef       // temporary CopyRef
B.AddRef
A.AddRef       // ncbPropAccessor(const Variant&) -> AsObject
A.Release      // temporary destructor
B.Release
```

随后width/height与direct/buffered transfer均在accessor raw `A`仍被retain的条件下执行。accessor phase结束
才再`A.Release`；persistent source Variant的`A/B` owners在更外层逆序cleanup中释放。

这与V242的accurate phase raw-owner primitive是同一种TJS基础机制，但source accessor通过现有
`ncbPropAccessor{tTJSVariant(...)}`表达，无需再造第二个helper。

## 5. width/height与failure边界

temporary dtor之后，四端严格先读width、后读height；两次都通过同一个raw source accessor Object作为
receiver与objthis。ordinary PropGet HRESULT不决定控制流，result Variant仍无条件转signed integer。

边界为：

- CopyRef的Object/ObjThis AddRef可重入，发生在AsObject与所有property callbacks之前；
- non-Object source在temporary已构造后由AsObject conversion抛出；temporary随后展开；
- Object-tag/null Object的AsObject返回null，temporary析构后在后续raw accessor使用处自然失败；
- width callback抛出时height不执行，source accessor raw owner与persistent source Variant按异常栈释放；
- width普通failure若仍写result则继续转换与height；未写result则Void按既有conversion边界处理。

portable没有添加type/null/HRESULT recovery。

## 6. 源码与测试

`PlayerRenderExecute.cpp`修改为：

```cpp
source.object = resolveRenderSource_guess(item.sourceState->object);
ncbPropAccessor sourceAccessor{tTJSVariant(source.object)};
source.layerObject = sourceAccessor.GetDispatch();
```

这删除了production路径中提前的`AsObjectNoAddRef()`。`source.layerObject`仍供headless/trace diagnostic读取，
但现在引用accessor已经retain的同一Object，不建立额外owner或conversion boundary。

V242的distinct Object/ObjThis owner probe扩展了一段真实`ncbPropAccessor` full-expression：

- accessor构造完成后立即观察
  `Object.AddRef, ObjThis.AddRef, Object.AddRef, Object.Release, ObjThis.Release`；
- accessor析构只产生一次`Object.Release`；
- 最后persistent Variant清理才产生`Object.Release, ObjThis.Release`。

## 7. IDB 写回与iOS armv7安全保存

四库各写回6条comment、4个bookmark，总计24 comment、16 bookmark；iOS armv7重建库另恢复
`Player_renderToCanvas_guess` semantic rename。覆盖resolver、temporary CopyRef、AsObject、temporary dtor、
width与height。

iOS armv7继续执行different-path compressed save → 独立`idat`重开退出码0 → live session
`save=false`关闭 → pre-V243 canonical及`.id0/.id1/.nam`逐文件移动到
`out/idb-recovery/v243-ios-armv7/pre-v243-canonical/` → 安装verified packed copy → MCP重开读回Canvas
function name与6条comments → `save=false`关闭。没有递归删除。

V243 canonical为376,109,264 bytes，SHA-256
`93FD4C4766E961CA7982091472AA8A7FB36FC08F230CED185812E56DBA4462E1`；pre-V243 canonical SHA-256为
`82EB884342C4131C0E9A7EC4A0998CFC324BD62827D1B86729E8D20621023449`。

## 8. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax：通过；
- Web Debug：`PlayerRenderExecute.cpp`、archive与主wasm增量链接通过；
- Wasmtime Headless Debug：portable/guest objects、archive与主wasm增量链接通过；
- guest重新链接并完成exnref转换；
- Node module construction通过；imports/exports仍为Web `539/69`、Wasmtime `538/69`；
- 两棵CTest当前配置明确`No tests were found`；
- Web/Wasmtime/guest final no-work通过；
- scoped diff check无whitespace error，仅既有LF/CRLF提示；
- final IDA audit为0 open sessions。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,655,386 | `0x1BD31` | `0x1A410DD` | `0x5A3E40` | `0x3185F7B` | `BC6F274BC1574F8FFEA9690C5351D2A83D6A5E1D17D7BD55F421EEE48509BE94` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,002,527 | `0x1BA50` | `0x19E908B` | `0x5A1090` | `0x3141E11` | `2BAE5181DCED06C22BA273C97026463705AA454673465AA9C09E429B9292DC06` |

相对V242，两端module与CODE各增70 bytes（`0x46`）；FUNCTION/GLOBAL/DATA/name/imports/exports全部不变。
增长只来自source temporary CopyRef/dtor与异常cleanup，不涉及新函数或persistent layout。

guest SHA-256为
`4D95A03ABAF7327CAAA0BE334C681B8E28F40F9BC5CB6242D5A6E4B18373A140`。

## 9. 下一边界

V244继续ordinary buffered branch的三层owner nesting：ResourceManager temporary/accessor → `bufLayer`
property result与persistent local Variant → buffer temporary/accessor。需要四端锁定propertyResult到bufLayer的
CopyRef/析构、各raw Object retain的phase范围、right<left early continue与callback exception时的逆序cleanup，
并确认direct branch完全不构造这三层owners。
