# EmoteObject raw owner / constructor-failure 生命周期（四参考，2026-08-13）

本纵切面重新判断 `EmoteObject` 的两个 pointer 字段应当恢复为 `unique_ptr` 还是保留
raw owner。旧文档只凭正常析构中的显式 destructor + `operator delete` 把它们称为 raw；
这种证据本身不充分，因为优化后的 `unique_ptr` 也可能展开成同样指令。fresh 四端
constructor unwind 给出了决定性差异：已写入 member slot 的 ResourceManager 和 Engine
在随后构造失败时都不回收，所以两者必须保留 raw owner。

## 1. 四端函数、对象尺寸与字段

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteObject` constructor/init | `0x67AF8C` | `0x5604B8` | `0x1001B4984` | `0x1B4500` |
| ctor unwind body | ctor 尾部内联 | 无独立 cleanup body | `0x1001B4D20` | `0x1B489E` |
| normal destructor | `0x67C800` | `0x5610BE` | `0x1001B5058` | `0x1B4CCE` |
| ResourceManager destructor | `0x6A5F74` | `0x57B2E4` | `0x1001012D4` | `0xFE408` |
| Engine destructor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |

对象布局没有平台额外字段：

```text
+0                   ResourceManager *rawOwner
+pointerSize         EmoteEngine *rawOwner
+2 * pointerSize     vector<ttstr> modulePaths
```

所以两份 64-bit 对象是 40 bytes，两份 32-bit 对象是 20 bytes。四份 constructor 都先
把两个 pointer 和 vector header 清零；不存在隐藏的 constructed-bit 或第三个 owner
wrapper state。

## 2. 发布时点

四端共同构造顺序为：

```cpp
ResourceManager *pendingRm = operator new(sizeof(ResourceManager));
ResourceManager_ctor(pendingRm, globalKag, 20_MiB);
self->rm = pendingRm;                         // immediately published

Variant temp = makeStickyAdaptor(self->rm);
EmoteEngine *pendingEngine = operator new(sizeof(EmoteEngine));
EmoteEngine_ctor(pendingEngine, temp);
self->engine = pendingEngine;                 // immediately published
destroy(temp);

self->modulePaths = inputPaths;
load every path and extract metadata;
seed Player project/chara/motion;
self->engine->applyMetadata(metadata);
```

四端的 store 都在对应子对象 constructor 成功后紧接发生，不会等到整个 EmoteObject
初始化成功才一次性提交两个 pointer。这个发布时点决定了不同异常发生时的对象状态。

## 3. pending allocation 与 member owner 是两套清理边界

若 `ResourceManager_ctor` 或 `EmoteEngine_ctor` 自身抛出，当前 `new` expression 仍拥有
尚未发布的 allocation，landing path 会调用 `operator delete`：Android arm64 的内联
cleanup 在 ctor 尾部执行 pending delete；iOS arm64 的 cleanup body 也对当前 pending
Engine allocation执行 delete；iOS armv7 SjLj cases 3/8 分别 delete 保存的 pending
allocation。这里不会调用未完成对象的普通 destructor。

一旦 constructor 返回并把 pointer 写入 `self`，pending-new cleanup 就结束。之后的异常
路径只清理栈上 `Variant`/`ttstr`、已构造的 `modulePaths` 元素与 vector backing：

- Android arm64 内联 landing tail 释放 vector 元素/backing 后直接
  `_Unwind_Resume`；没有 Engine/ResourceManager destructor call；
- Android armv7 init body没有 owner destructor/delete landing call；
- iOS arm64 `0x1001B4D20` 最后调用 vector destructor，再 resume；没有读取 self 的
  `+0/+8` owner slots；
- iOS armv7 `0x1B489E` 的 SjLj switch 清理临时 TJS owner、pending allocation 与
  vector，随后 resume；没有 Engine/ResourceManager destructor call。

因此可以列出原版失败状态：

| 抛出阶段 | 释放 | 泄漏 |
| --- | --- | --- |
| RM allocation | 无已分配 RM | 无 |
| RM constructor | pending RM storage | 未完整 RM 不调用普通 dtor |
| adaptor/Engine allocation/Engine constructor | 临时 Variant；pending Engine storage（若已有） | 已发布 RM |
| paths copy/load/property/Player seed/applyMetadata | 临时量与 paths vector | 已发布 Engine 和 RM |

外层 `new EmoteObject(...)` 在 constructor 失败时只 delete EmoteObject 自身 storage；C++
不会调用未完成对象的 normal destructor。因此上述已发布 raw pointer 不会由外层补回收。

## 4. 为什么不能改为 `unique_ptr`

若这两个字段是 `unique_ptr`，语言级 member unwinding 会在 EmoteObject constructor 抛出
时自动逆序销毁已经构造完成的 member：在 Engine 已发布后至少会销毁 Engine，再销毁 RM；
即使只有 RM 已发布，也必须销毁 RM。四端都没有这些调用。

normal destructor 本身也支持 raw-source 形状：它在 destructor body 中先手写 delete
Engine，再手写 delete RM，最后 C++ 自动销毁 paths vector。若两个 pointer 是普通按声明
顺序放置的 `unique_ptr` member，纯自动析构的逆序本应是 `paths -> Engine -> RM`，而不是
参考中的 `Engine -> RM -> paths`；要得到参考顺序仍需在 body 里显式 reset。constructor
unwind 已排除了这种 reset-only-正常路径解释。

因此本地 `_rm` 与 `_engine` 继续使用 raw pointer，并禁止为“异常安全”迁移到
`unique_ptr`。这不是推荐新代码的设计，而是复原参考产品已经存在的泄漏边界。

## 5. 正常所有权与借用关系

正常完成构造后，EmoteObject 是两个对象的唯一 delete owner：

```text
EmoteObject normal destruction
  -> Engine destructor + operator delete
  -> ResourceManager destructor + operator delete
  -> modulePaths vector destructor
```

构造用的 sticky adaptor Variant 只在栈上存在到 Engine member 发布之后；Engine/Player
内部持有各自引用计数 owner，但 adaptor 不接管原生 ResourceManager 的 delete。Engine
必须先于 RM 销毁，保证内部 Variant/child Player 释放时 native RM 仍存活。

## 6. 本地与 IDB 恢复

本地数据成员与手写正常 destructor 已经符合共同结构；本轮纠正的是过时论证和边界注释，
没有以 RAII 修复原版失败状态。四份 recovery IDB 加入对应的 40B/20B EmoteObject 类型，
constructor/destructor 原型，以及“pending allocation 会回收、已发布 raw owner 不回收”的
注释；iOS 两份独立 unwind body 另命名为 `_guess`。

绝对地址只存在于本文的四端映射，compiled source comment 保持地址无关。

## 2026-08-16 V146 fresh addendum：load尾部stack owner树

本页关于published RM/Engine raw owner在后续异常中不回收的结论继续成立。V146进一步从四端
constructor主函数fresh恢复了触发这类后续异常的精确stack source tree：成员paths先copy，
但load loop与project `back()`都读取caller input；所有load result复用一个working Variant，
其最后值在`base` getter写回时被覆盖；metadata独立存活，base working Variant再建立唯一
retained `ncbPropAccessor`，以两个独立hint顺序读取typed `chara/motion`。

applyMetadata之后正常局部释放严格为motion、chara、base accessor、metadata、base working
Variant。任一load/property/Player seed/applyMetadata异常都会展开已经构造的这些stack owner及
paths member，但仍不会读取/销毁已发布RM/Engine slots。portable已同步input/member身份、
working-slot覆盖与base accessor，不改变本页的原生leak矩阵。完整地址、hint与验证见
`analysis/motionplayer_emoteobject_input_path_metadata_base_ncb_owner_four_binary_2026-08-16.md`。
