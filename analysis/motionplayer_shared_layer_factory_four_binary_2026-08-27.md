# motionplayer shared Layer Variant factory（四参考二进制，2026-08-27）

## 1. 入口与取证形态

| 端 | factory evidence | body instructions | cleanup |
|---|---:|---:|---:|
| Android arm64 | materializer内联两次：`0x6CB660..0x6CB6F8`、`0x6CB8A4..0x6CB93C` | 包含于已完整读取的 398 条 materializer body | DWARF landing pads同一函数内 |
| Android armv7 | `0x57AC1C` | 61 | — |
| iOS arm64 | `0x1001008A8` | 58 | — |
| iOS armv7 | `0xFDA14` | 104 | `0xFDB22`, 12 instructions |

三个独立 factory共 223 条主函数指令已经完整读取；Android arm64两处内联 site在上一项
完整 398 条 materializer反汇编中逐指令覆盖。iOS armv7额外 12 条 SjLj cleanup也已完整
读取。独立函数、inline site与 cleanup已命名或注释/bookmark，四个 IDB均已保存。

这是 SourceCache、SeparateLayerAdaptor、render-command compositor和 Player internal
workspace共用的创建原语。A64是否内联是编译器决定，不改变共同 source结构。

## 2. 输入不是新 owner

factory接收两个 `const tTJSVariant &`：owner和parent。它直接构造 `argv[2]`指向调用者
已有的完整 Variant，不先 copy、不转换 Object，也不规范化 objthis。因此 owner/parent
可以是任意 Variant type；Object closure的 object/objthis差异原样穿过 TJS调用边界。

参数顺序严格为 `[owner, parent]`。Player materializer传 target.window作为 owner、原始
target作为 parent；其他调用者可以传 Window.mainWindow/primaryLayer或 SLA持久 owner/
target，但不改变 factory本身。

## 3. raw global与 `CreateNew`

共同控制流：

```cpp
iTJSDispatch2 *global = TVPGetScriptDispatch();
iTJSDispatch2 *created = nullptr;
tTJSVariant *args[] = { &owner, &parent };
(void)global->CreateNew(
    0, "Layer", &layerClassMemberHint,
    &created, 2, args, global);
```

global是 raw owning pointer；factory没有把它放进 Variant或 scope guard。调用 flags为0，
member使用一个 process-global `Layer` hint，result out-param初始为 null，argc固定2，
receiver与objthis都是 global。`CreateNew` HRESULT完全不读取；普通失败、成功或非标准
成功码都走同一后续路径。

## 4. 结果 closure与引用计数

调用后以 `created`同时作为 Object和ObjThis构造结果 Variant。created非空时四端都对同一
dispatch执行两次 AddRef，然后把两个指针和 Object type tag写入结果。随后固定：

1. raw `created->Release()`；
2. raw `global->Release()`；
3. 返回结果 Variant。

因此正常成功时，结果保存完整 self-closure；factory本身不额外保留 raw created/global。
返回使用普通 sret/ABI result槽，调用者再按自己的 persistent发布顺序 copy/move。

## 5. 尖锐失败与异常边界

- `created == nullptr`时仍形成 Object-tagged null closure，随后无条件执行 raw
  `created->Release()`；没有 null恢复或 fabricated Void结果；
- ignored HRESULT不会阻止上一步；
- raw global/created没有 RAII scope guard；`CreateNew`抛出时不会由 factory补偿 Release；
- iOS armv7在 `CreateNew`返回后才注册此局部阶段的 SjLj context；其 12 条 cleanup只负责
  已live结果 Variant的析构与继续 unwind，不把 raw global/created提升为受管 owner；
- Release或Variant构造期间的可抛/terminate细节由 ABI landing state决定，factory不做
  catch、status转换或重试。

这些边界看似危险，但属于四端共同原始行为；调用者不应为它增加 null guard、HRESULT
分支或事务式清理。

## 6. 本地实现与验证状态

本地 `detail::createLayerVariant_guess`逐项匹配：raw `TVPGetScriptDispatch`、null raw
created、borrowed owner/parent argv、ignored-status `CreateNew`、self-closure Variant、
unconditional created Release、global Release、按值返回。无需语义修改。

本项标记 `IMPLEMENTED`。已完成三端 fresh full decompile/disassembly、Android arm64两个
inline site复核、223 条独立 body、12 条 armv7 cleanup、本地逐语句对照、IDB改进、
coverage 12列校验和 `git diff --check`。正式 CMake/unit/Web build仍因本机缺少相关工具链
且无既有 build/out目录而未运行。
