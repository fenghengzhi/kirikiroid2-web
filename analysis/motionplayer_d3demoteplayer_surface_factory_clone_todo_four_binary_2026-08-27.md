# D3DEmotePlayer 四端脚本表面、工厂、克隆与 TODO 边界审计

## 结论

`D3DEmotePlayer` 是 `DrawDeviceD3D.dll` 注册链中的独立 NCB 类，而不是
`Motion.EmotePlayer` 的别名或继承表面。四个参考二进制共同给出同一套
源级语义：

- 原生对象公开继承 `D3DLayerListener`，持有一个不计数的 `D3DLayer *`、
  两个原始 `EmoteObject *` owner 槽、两个 scale 浮点数以及 visible / smoothing
  两个字节；64 位对象大小为 `0x38`，32 位对象大小为 `0x24`。
- NCB 注册器发布 4 个整数常量和严格按原顺序交错的 54 个成员；`load`
  是唯一 raw callback，历史拼写 `queing` 必须保留。
- typed factory 只消费 arg0 并忽略多余参数；正常路径把它严格解包为
  `D3DLayer`，构造并注册新的 listener shell，再把非 sticky native 写入
  receiver adaptor。精确的单 Void empty-adaptor sentinel 在外层生成包装器中
  截获，不进入普通构造语义。
- `clone` 新建并注册一个 shell，只克隆 primary `EmoteObject`；secondary
  保持 null。构造完成后 inner clone 的异常不再由 pending-new cleanup
  持有，因此会泄漏完整 shell 及其 listener registration；这是四端一致的
  原版边界，不能用 RAII 修复。
- `assignState` 会在参数是 Object 时尝试一次 `D3DEmotePlayer` native probe，
  但无论 probe 是否成功都会抛出精确 TODO。另 6 个查询接口完全忽略参数并
  立即抛出各自的精确 TODO。
- complete destructor 先删除 secondary、再删除 primary，在两个 delete 都
  完成后才把两槽一起清零，然后通过 listener 基类从借用的 `D3DLayer`
  注销；deleting destructor 在此之后才调用 scalar `operator delete`。

本地实现已匹配上述行为，本切面没有发现需要修改的 C++ 语义差异。

## 四端定位与完整读取

| 语义根 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| DrawDeviceD3D 静态注册记录 | `0x42CBD8` | `0x2FF094` | `0x10024CB00` | `0x24E6D8` |
| delayed subclass wrapper | `0x542178` | `0x4A3AD0` | `0x100245634` | `0x245D28` |
| ClassInfo / class setup | `0x542434` | `0x4A3BD0` | `0x1002456F8` | `0x245E8C` |
| 4 常量 + 54 成员注册器 | `0x52E8E4`（1309） | `0x494078`（478） | `0x100232278`（432） | `0x230F46`（539） |
| typed factory / adaptor attach | `0x542B44`（89） | `0x4A4080`（78） | `0x100245DC0`（67） | `0x2465B8`（115） |
| clone | `0x53039C`（41） | `0x4949D4`（16） | `0x100232DC8`（19） | `0x2319DC`（54） |
| complete destructor | `0x533FE0`（36） | `0x497870`（12） | `0x100236374`（12） | `0x235076`（12） |
| deleting destructor | `0x534078`（9） | `0x497894`（13） | `0x1002363A8`（13） | `0x23509A`（13） |
| `IsVisible` vtable slot | `0x53409C`（36） | `0x4978BC`（40） | `0x1002363E0`（26） | `0x2350C2`（29） |
| `Draw` vtable slot | `0x53412C`（33） | `0x497930`（34） | `0x100236448`（23） | `0x23511A`（21） |

括号内是完整反汇编的指令数。注册器均使用分页读取到 `done=true`：
Android arm64 的 1309 条指令覆盖 `0..1308`，其余三端分别一次完整读取
478、432 和 539 条。factory、clone、所有 TODO 叶子、析构与两个派生 vtable
回调也都完整读取到函数末尾，并与新鲜反编译和字符串 xref 交叉核对。

## 独立类层级与对象布局

派生 vptr 的前四个槽在四端均为：

1. complete destructor；
2. deleting destructor；
3. `IsVisible()`；
4. `Draw(iTVPTexture2D *)`。

构造时先建立 `D3DLayerListener` 基类状态并调用 owner 的虚拟
`AddListener(this)`，再切换到派生 vptr 并初始化派生字段。共同布局如下：

| 字段 | 64 位偏移 | 32 位偏移 | 所有权 / 默认值 |
|---|---:|---:|---|
| vptr | `+0x00` | `+0x00` | 派生虚表 |
| borrowed `D3DLayer *` | `+0x08` | `+0x04` | 不 AddRef，不拥有 |
| stretch type | `+0x10` | `+0x08` | `8` |
| bicubic parameter | `+0x14` | `+0x0C` | `-0.5f` |
| primary `EmoteObject *` | `+0x18` | `+0x10` | raw owner，初始 null |
| secondary `EmoteObject *` | `+0x20` | `+0x14` | raw owner，初始 null |
| base scale | `+0x28` | `+0x18` | `1.0f` |
| user scale | `+0x2C` | `+0x1C` | `1.0f` |
| visible | `+0x30` | `+0x20` | false |
| smoothing | `+0x31` | `+0x21` | false |

`IsVisible` 不读取 script-visible `visible` 字节。它只在 owner scale 与缓存的
base scale 发生精确 float 不等时，更新 primary Engine 的 scale controller，
最后恒返回 true。`Draw` 把 `(0, 0)` 通过 borrowed `D3DLayer` 做 transform，
再调用 primary Player 的 draw-to-texture 路径。这进一步证明它是 D3D listener
shell，而不是 `Motion.EmotePlayer` 的 payload facade。

## NCB 常量与 54 成员顺序

四端注册器先发布：

| 常量 | 值 |
|---|---:|
| `MaskModeStencil` | 0 |
| `MaskModeAlpha` | 1 |
| `TimelinePlayFlagParallel` | 1 |
| `TimelinePlayFlagDifference` | 2 |

随后发布以下 54 项，顺序和 method/property 类型均一致：

| # | 成员 | 类型 / 关键边界 |
|---:|---|---|
| 1 | `module` | read-only property |
| 2 | `clear` | typed method |
| 3 | `load` | 唯一 raw callback |
| 4 | `clone` | typed method |
| 5 | `show` | typed method |
| 6 | `hide` | typed method |
| 7 | `visible` | read/write property |
| 8 | `smoothing` | read/write property |
| 9 | `meshDivisionRatio` | read/write property |
| 10 | `queing` | read/write property；原版错拼 |
| 11 | `hairScale` | read/write property |
| 12 | `partsScale` | read/write property |
| 13 | `bustScale` | read/write property |
| 14 | `assignState` | typed method；故意 TODO |
| 15 | `setCoord` | typed method |
| 16 | `setScale` | typed method |
| 17 | `getScale` | typed method |
| 18 | `setRot` | typed method |
| 19 | `getRot` | typed method |
| 20 | `setColor` | typed method |
| 21 | `getColor` | typed method |
| 22 | `countVariables` | typed method；故意 TODO |
| 23 | `getVariableLabelAt` | typed method；故意 TODO |
| 24 | `countVariableFrameAt` | typed method；故意 TODO |
| 25 | `getVariableFrameLabelAt` | typed method；故意 TODO |
| 26 | `getVariableFrameValueAt` | typed method；故意 TODO |
| 27 | `setVariable` | typed method |
| 28 | `getVariable` | typed method |
| 29 | `startWind` | typed method |
| 30 | `stopWind` | typed method |
| 31 | `countMainTimelines` | typed method |
| 32 | `getMainTimelineLabelAt` | typed method |
| 33 | `countDiffTimelines` | typed method |
| 34 | `getDiffTimelineLabelAt` | typed method |
| 35 | `countPlayingTimelines` | typed method |
| 36 | `getPlayingTimelineLabelAt` | typed method |
| 37 | `getPlayingTimelineFlagsAt` | typed method |
| 38 | `isLoopTimeline` | typed method |
| 39 | `getTimelineTotalFrameCount` | typed method |
| 40 | `playTimeline` | typed method |
| 41 | `isTimelinePlaying` | typed method |
| 42 | `stopTimeline` | typed method |
| 43 | `setTimelineBlendRatio` | 绑定五参数 `setTimeline` |
| 44 | `getTimelineBlendRatio` | typed method |
| 45 | `fadeInTimeline` | typed method |
| 46 | `fadeOutTimeline` | typed method |
| 47 | `animating` | read-only property |
| 48 | `skip` | typed method |
| 49 | `pass` | 绑定无参 timeline pass body |
| 50 | `progress` | typed method |
| 51 | `modified` | read-only property |
| 52 | `setOuterForce` | typed method |
| 53 | `getOuterForce` | typed method；故意 TODO |
| 54 | `contains` | typed method |

## typed factory 的共同伪代码与边界

四端的编译器形态不同，但可归一化为：

```text
pending = operator new(sizeof(D3DEmotePlayer))
arg0 = (argc > 0) ? copy(argv[0]) : Variant(Void)
owner = strict_native_unbox<D3DLayer>(arg0)   // 非匹配对象产生 null borrowed target
fresh = construct D3DEmotePlayer(pending, owner)

adaptor = receiver.GetNativeInstanceAdaptor(D3DEmotePlayerClassID)
if adaptor attachment succeeds:
    adaptor.native = fresh
    return success

fresh.deleting_destructor()
return TJS_E_NATIVECLASSCRASH  // -1008
```

需要保留的细节：

- shell storage 在参数转换之前分配；转换或构造抛出时，仅 pending storage
  由 new-expression cleanup 负责。
- 只复制 / 消费 arg0，多余参数不参与语义。
- null owner 仍能构造一个未注册 listener 的 shell；后续依赖 owner 的成员是
  原版尖锐边界。
- receiver attach 失败时调用 deleting destructor，已经注册的 listener 因而会
  正常注销，随后释放 shell。
- exact-one-Void 是生成包装器的 empty-adaptor sentinel，不能等同普通的
  `factory(nullptr)` 调用。

## clone、异常所有权与析构

共同 clone 伪代码是：

```text
copy = new D3DEmotePlayer(borrowedOwner)  // 立即注册 listener
copy.primary = this.primary.clone()
return copy
```

没有 secondary copy，也没有在 inner clone 之前把 shell 放入持续 owner。
iOS armv7 的 SjLj 状态最清楚地显示：call-site 1 只覆盖 pending shell 的构造；
构造返回后先写 `call_site = -1`，再进入 `EmoteObject::clone`。其他三端的
landing-pad / EH 表给出相同的 new-expression ownership 边界。因此 inner
clone 失败时，完整 shell 与已经挂入 owner list 的 listener 一起泄漏。

正常销毁时：

```text
complete_dtor(self):
    delete self.secondary
    delete self.primary
    self.primary = null
    self.secondary = null
    D3DLayerListener::~D3DLayerListener()  // owner.RemoveListener(self)

deleting_dtor(self):
    complete_dtor(self)
    operator delete(self)
```

删除期间两个 owner 槽保留旧地址，直到两个嵌套析构都结束才成对清零。这是
可被重入析构观察的状态，不可改成两个独立的 reset-before-delete。

## 七个故意保留的 TODO

UTF-16LE 原始字节搜索、完整分页和 xref 共同定位了 28 个回调。每个搜索均
读取到 `done=true`；字符串 xref 没有用作唯一证据，回调本体另行反编译和
完整反汇编。

| 回调 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `assignState` | `0x530530` | `0x494AC4` | `0x100232F08` | `0x231B4E` |
| `countVariables` | `0x5307FC` | `0x494CA4` | `0x100233098` | `0x231CEA` |
| `getVariableLabelAt` | `0x530910` | `0x494CB8` | `0x1002330B8` | `0x231D00` |
| `countVariableFrameAt` | `0x530948` | `0x494CDC` | `0x1002330F0` | `0x231D26` |
| `getVariableFrameLabelAt` | `0x530968` | `0x494CF0` | `0x100233110` | `0x231D3C` |
| `getVariableFrameValueAt` | `0x530988` | `0x494D04` | `0x100233130` | `0x231D52` |
| `getOuterForce` | `0x530F08` | `0x4950D0` | `0x100233524` | `0x2322CC` |

精确文本分别为：

```text
TODO: implement D3DEmotePlayer::assignState()
TODO: implement D3DEmotePlayer::countVariables()
TODO: implement D3DEmotePlayer::getVariableLabelAt()
TODO: implement D3DEmotePlayer::countVariableFrameAt()
TODO: implement D3DEmotePlayer::getVariableFrameLabelAt()
TODO: implement D3DEmotePlayer::getVariableFrameValueAt()
TODO: implement D3DEmotePlayer::getOuterForce()
```

除 `assignState` 的 Object/native probe 外，其余六个叶子不读取参数。它们
都没有正常返回路径；反编译中出现的尾部占位返回不属于可达语义。

## 本地逐项对照

| 参考要求 | 本地位置 | 结论 |
|---|---|---|
| 独立 listener shell 与 `0x38/0x24` ABI | `cpp/plugins/motionplayer/EmotePlayer.h:304`、`:472` | 匹配 |
| borrowed owner、listener 注册 / 注销 | `cpp/plugins/DrawDeviceD3D.cpp:786`、`:796` | 匹配 |
| raw primary / secondary 与 scalar 字段顺序 | `cpp/plugins/motionplayer/EmotePlayer.h:442` | 匹配 |
| 4 常量 + 54 成员及顺序 | `cpp/plugins/DrawDeviceD3D.cpp:1753` | 匹配 |
| typed factory | `cpp/plugins/motionplayer/EmotePlayer.cpp:132` | 匹配 |
| primary-only clone 和泄漏窗口 | `cpp/plugins/motionplayer/EmotePlayer.cpp:239` | 匹配 |
| secondary→primary、延后成对清零 | `cpp/plugins/motionplayer/EmotePlayer.cpp:210` | 匹配 |
| `assignState` probe 后必抛 | `cpp/plugins/motionplayer/EmotePlayer.cpp:257` | 匹配 |
| 五个 variable query TODO | `cpp/plugins/motionplayer/EmotePlayer.cpp:343` | 匹配 |
| `getOuterForce` TODO | `cpp/plugins/motionplayer/EmotePlayer.cpp:487` | 匹配 |

因此本次没有进行语义 C++ 修改。四个 IDB 已统一命名关键函数，添加函数注释
和 registrar / factory / clone / complete-destructor 书签，并全部原位保存。

## disposition

- 原始任务：`MP-A30`
- 静态状态：`CLOSED_STATIC`
- 覆盖切面：`MP-A30-D3DEMOTEPLAYER-SURFACE-FACTORY-CLONE-TODO`
- 本任务局部剩余差异：无
- 独立验证工作：仍由 `MP-V01..V16` 总体验证任务跟踪，不能据此宣称
  163 项任务全部完成。
