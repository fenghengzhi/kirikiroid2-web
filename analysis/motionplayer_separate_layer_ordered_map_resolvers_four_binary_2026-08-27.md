# SeparateLayerAdaptor ordered-map 与两个 resolver（四参考二进制，2026-08-27）

## 1. fresh evidence 总表

### 1.1 类构造器、payload comparator 与resolver

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | total instructions |
|---|---:|---:|---:|---:|---:|
| `SeparateLayerAdaptor` ctor | `0x6C3DB4`, 92 | `0x58DBDC`, 67 | `0x1001298C4`, 50 | `0x128890`, 101 | 310 |
| payload refresh predicate | `0x6D9F0C`, 120 | `0x59B7F0`, 105 | `0x1001299E0`, 93 | `0x1289F0`, 109 | 427 |
| payload resolver | `0x6C3F28`, 387 | `0x58DCD4`, 229 | `0x100117E88`, 190 | `0x115B34`, 329 | 1135 |
| payload-free ordinal resolver | `0x6C90C4`, 295 | `0x591DEC`, 129 | `0x10011C628`, 98 | `0x11AE24`, 174 | 696 |

### 1.2 红黑树helper与normal-tail清理

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| whole-tree swap | build入口内联 `0x6C225C..0x6C22F4` | `0x59B6E8`, 50 | `0x100129858`, 27 | `0x128848`, 26 |
| find | resolver内联 | resolver内联 | `0x100129B54`, 21 | `0x128B3A`, 27 |
| ensure/insert default node | `0x6DA0EC`, 60 | `0x59B764`, 55 | `0x100129BA8`, 56 | `0x128B74`, 53 |
| erase/destroy | destroy node `0x6D8BE8`, 20；rebalance为libc++ helper | `0x59BB42`, 24 | `0x100129CE0`, 48 | `0x128C24`, 43 |
| invalidate-and-clear | `0x6C46C4`, 87 | `0x58E174`, 109 | `0x10011844C`, 130 | `0x116280`, 190 |

上述独立函数合计3594条指令，全部在本轮fresh decompile并完整disassemble；A64内联swap及
normal-only调用点另由已完整读取的 `buildRenderCommands`主体确认。iOS ARMv7小函数内含
SJLJ注册、call-site state与注销；A64函数尾的landing pads也包含在完整disassembly内，不能
只按Hex-Rays正常路径计生命周期。

本地对应实现位于 `SeparateLayerPayload_guess`、`SeparateLayerOrderedMap_guess`和
`SeparateLayerAdaptor`。这里恢复的是源码级 `std::map<uint32_t,payload>`及其wrapper，而
不是把目标libc++/libstdc++的header padding硬编码到Web对象。

## 2. 四端对象与树节点形状

四端Adaptor声明顺序一致：

1. owner Variant；
2. target Layer Variant；
3. private target Variant；
4. active ordered map；
5. retired ordered map；
6. signed 32-bit absolute base；
7. signed 32-bit per-pass sequence。

ABI宽度与STL实现导致offset不同：Android使用libstdc++风格tree header，iOS使用libc++
`__tree`风格header；但两者都是header sentinel + root/leftmost/rightmost + node count的
红黑树。

节点共同布局：

- 64位节点分配 `0xD0`，key在node `+0x20`，payload从 `+0x28`开始；
- 32位节点分配 `0x98`，key在node `+0x10`，payload从 `+0x14`开始；
- ordinal只存于pair key，payload前没有第二份ordinal；
- key比较是unsigned 32-bit strict less；遍历、clear和assign都按key升序。

payload声明顺序为Layer Variant、completionType、outline/meshline byte、commandSrc、blend、
四colors、paint+viewport八float、composite mesh vector、Bezier vector、corners。节点析构按
反向成员顺序释放两个vector、command string，最后释放Layer Variant。局部
`std::map<tjs_uint32, SeparateLayerPayload_guess>`准确表达源结构和销毁依赖；目标树旋转与
rebalance交给当前标准库，而不是移植参考STL私有ABI。

## 3. 构造器owner与初始状态

构造器接收by-value target Variant。共同顺序是：

1. 再复制一份call-local target closure；
2. 对副本执行strict `AsObject`，取得额外Object-only AddRef；
3. 以target Object自身作为receiver/objthis读取 `window`，flags=0、hint=null，HRESULT忽略；
4. 把结果Variant复制到Adaptor owner；
5. 销毁结果临时，Release Object-only owner，再销毁target closure副本；
6. 复制constructor参数到持久target字段；
7. private Variant设Void，两棵树初始化为空，absolute设0。

per-pass sequence没有在构造器初始化；它只能在begin-pass的whole-tree swap之后写0。若
`AsObject`、`window` getter或Variant复制抛异常，C++部分构造规则负责按已构造字段逆序回滚。

本地原先只让target closure持有Object，缺少第2步的独立Object owner。本轮改成完整closure
与Object-only owner同时跨越 `window` getter，并用测试记录getter时的引用balance。

## 4. whole-tree pass 状态机

begin-pass不是逐节点迁移，而是O(1)交换active与retired的完整tree header：root、leftmost、
rightmost和size全部交换；非空root的parent重新指向新的header sentinel，空树恢复自指针。
交换完成后sequence写0。

这个状态机故意保留异常后的树：如果上一pass在中途异常，下一次begin仍然交换整棵active/
retired，而不是merge、清空或事务回滚。重复ordinal也没有dedupe保护；同一pass第二次解析
同一key时retired已经没有该节点，ensure返回已存在active payload，随后新Layer可以覆盖旧
Layer Variant而不先Invalidate。

normal end-pass只对retired执行invalidate-and-clear。`buildRenderCommands`、accurate SLA及
其他caller的异常landing pads都绕过这一步；因此本地必须保留显式normal-flow调用，不能用
scope guard自动补清理。

## 5. ensure、复用与部分提交

ensure查找uint32 key；缺失时先分配并链接默认/零payload节点，然后才由resolver把source
payload赋进去。结果是：

- 插入成功而后续payload copy抛异常时，默认或部分赋值节点留在active树；
- 新Layer factory抛异常时，source payload节点已发布，Layer Variant保留source传入值
  （普通caller通常是Void）；
- reuse分支先计算refresh byte，再ensure active、复制完整source payload；
- 只有完整source赋值成功后才用retired节点Layer Variant覆盖active payload首字段；
- output Variant复制成功后才rebalance/erase retired节点。

因此reuse中任何更早的复制异常都会保留retired节点；erase之后的属性回调异常则只留下
active节点。local wrapper使用 `map::operator[]`、payload copy assignment和最后erase，保持
这些commit点。

## 6. shipped comparator：读很多字段，但永远返回true

refresh predicate按固定短路顺序读取：

1. commandSrc；
2. completionType；
3. outline/meshline byte；
4. blendMode；
5. paint+viewport八float，逐项比较；
6. composite MeshPoint vector的byte length、再逐点x/y；
7. Bezier MeshPoint vector的byte length、再逐点x/y。

它不读取Layer Variant、packedColors或corners。任一mismatch直接到共同 `return 1`；两vector
也完全相等时仍落到同一个 `return 1`。这不是“比较相等”的合理语义，而是四端共同的已发布
行为，所以每次retired payload reuse都把created-or-changed报告为true。本地保留全部
短路读取和永真结果，并把八float改成显式索引循环，固定读取顺序。

## 7. payload resolver

payload resolver按第5节解析或创建Layer后，四端固定执行：

1. 复制已发布的output Layer Variant；
2. strict `AsObject`额外保留其Object；
3. 立即销毁临时closure，因此ObjThis owner不会跨属性回调；
4. 直接向该Object写 `absolute = wrapping_i32(base + sequence)`；
5. absolute写正常返回后，sequence按32-bit自然回绕自增；
6. 写 `hitThreshold = 256`；
7. Release Object-only owner，返回先前已拥有的Layer Variant。

两次PropSet均为MEMBERENSURE、使用共享member hint、HRESULT忽略。`AsObject`或PropSet异常
传播；absolute写抛异常时sequence不变，hitThreshold写抛异常时sequence已经增长。Layer
Variant是sharp Object边界，没有null过滤和Adaptor/private-target fallback。

本地原先调用assign专用的 `resolveAssignableLayerStrict_guess`，可能把返回对象若恰好是
SeparateLayerAdaptor时解包到private/target/owner，而且没有额外Object-only owner。本轮改为
direct Object receiver和完整owner envelope；同时把signed加法与自增改为显式uint32回绕，
避免C++ signed-overflow UB。

## 8. payload-free ordinal resolver

ordinal重载只搬移/创建Layer Variant：复用时ensure active、复制retired Layer、复制output、
erase retired；新建时ensure后调用共享Layer factory。它不复制source payload。

属性owner和direct receiver与第7节完全相同，absolute仍按32-bit回绕相加，hitThreshold仍为
256；唯一关键差异是它不增长sequence。准确SLA的optional mask Layer及shared-D3D路径依赖
这个行为，同一pass多次ordinal-only解析会发布相同absolute值。

本轮同步修正了它的Adaptor解包和缺失Object-only owner问题，并用同一个引用balance断言
覆盖新建ordinal路径。

## 9. retired invalidate-and-clear

normal clear按key升序处理每个retired节点：

1. 复制整份payload到栈临时；两个vector会各自分配/复制；
2. 仅当临时Layer Variant类型为Object时，对direct Object调用
   `Invalidate(0,null,null,self)`；非Object直接跳过；
3. Invalidate HRESULT忽略；
4. 临时payload先析构Bezier/composite vectors与command string，最后才析构Layer Variant；
5. 所有节点都成功走完后，才一次性销毁原树并重置空header/count。

任何payload复制、分配、Invalidate或析构异常都会让原树完整保留。已经成功Invalidate的较小
key不会被从树中删掉，重试会从首key开始再次Invalidate它。现有异常重试测试覆盖这个边界。

本地原先在Invalidate返回后立刻 `payloadCopy.layerVariant.Clear()`，把临时Layer owner提前到
vector/string之前释放。本轮拆分private-target显式clear与payload-copy invalidate：map路径不
再提前Clear，Layer Variant随整份临时payload最后析构。

## 10. 验证与剩余范围

四端ctor/comparator/payload resolver/ordinal resolver、所有独立find/ensure/erase/swap helper及
invalidate-and-clear共3594条指令已完整读取。相关IDB函数已统一命名、注释、bookmark并保存。

本轮源码变化：

- 恢复constructor `window` getter的额外Object-only owner；
- 两个resolver改回direct Layer Object receiver与完整owner envelope；
- absolute/sequence使用显式32-bit回绕；
- payload comparator逐float固定短路顺序；
- retired payload临时Layer Variant延后到完整payload析构时释放；
- 扩展现有测试，记录constructor getter与两个resolver属性回调期间的引用balance。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机缺少CMake、
Ninja、Emscripten且没有既有build/out目录而未运行。本报告当时保留的public `assign`、public
clear/destructor和NCB constructor/attach范围，现已分别由
`motionplayer_separate_layer_assign_four_binary_2026-08-27.md`、
`motionplayer_separate_layer_clear_destructor_four_binary_2026-08-27.md`与
`motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md`关闭。
