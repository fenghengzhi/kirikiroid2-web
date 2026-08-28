# reload / clear / unload 与 live node / render source 生命周期四参考二进制联合恢复

日期：2026-08-28  
原始任务：`MP-D12`

## 1. 结论

四端没有一个统一的“render source owner”。同一帧链路中并存五种不同生命周期：

1. `Player` 的 `std::deque<MotionNode>` 独占 node；reload/reset 擦除所有非根 node，Player 析构显式
   clear 最后的 synthetic root。
2. 每个 node 独占一个延迟分配、跨帧复用的 `PreparedRenderItem*`；prepared main/aux/child vectors
   只借用 item 指针，item 的 `sourceState` 又只借用所属 node 内的 `SourceState*`。
3. `LayerGetter` 脚本 facade 只保存一个 `MotionNode*`；它不 retain Player、deque、node 或 generation。
4. `SourceState.object` 是 owning Variant。generic source 命中时它持有 `ObjSource`，而 `ObjSource`
   再 retain raw PSB owner，所以 `ResourceManager::unload` 后 raw metadata 仍可存活。
5. `SourceState.texture` 是非 owning atlas texture pointer。KRKR/Win texture owner 在
   `LoadedResourceRecord` map 中；unload/unloadAll 会 Release map holder，却不会遍历 live Players 清理
   `SourceState.texture`。仍在使用的 node 可以立即留下悬空 texture borrow。

`SourceCache::clearCache` 是另一条独立 clear：它对缓存 Layer 逐个调用 `Invalidate`，随后释放 list
owner并清计数，但不清 ResourceManager module map、不清 node `SourceState.object`、也不清 persistent
`bufLayer`。外部 Variant alias 能继续持有被 invalidated 的同一 Layer dispatch；它不是“清缓存后自动
变成 Void”。D3DAdaptor 的 `removeAllTextures` 又只清 software texture-copy map：key 是 source
texture 的裸 identity，mapped value 才是 owning holder。

正常单线程流程依靠严格时序保证这些借用有效：prepared vectors 在同一个同步 draw/getCommandList
调用内创建、消费并销毁，node 在 consumer 返回后才允许 reload。四端没有锁、generation、hazard
pointer、Player self-retain 或 callback 后二次验证；TJS/Layer/source 回调若重入并执行 reload、
clearCache、unload、unloadAll 或 texture-map clear，当前 render consumer 会继续直接解引用原 borrowed
pointer。这些悬空/invalidated 边界是共同原始行为，本地实现已经匹配，不应增加防御性 snapshot 或
隐式 owner。

## 2. 本轮 fresh 四端证据总量

本轮使用原生 `mcp__idalib__*` 对 80 个独立函数范围重新执行 decompile、完整 disassembly 和
`xrefs_to` 审计。所有 disassembly 均为 `truncated=false`。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 19 | 7,240 | 64 | 19 条任务注释、1 个书签 |
| Android armv7 | 21 | 4,166 | 40 | 21 条任务注释、1 个书签 |
| iOS arm64 | 20 | 3,734 | 50 | 20 条任务注释、1 个书签 |
| iOS armv7 | 20 | 5,284 | 43 | 20 条任务注释、1 个书签 |
| 合计 | 80 | 20,424 | 197 | 80 条注释、4 个书签；四库原位保存 |

本轮根集合包含 node reset/rebuild、Player dtor、deque erase/clear/node/item dtor、两个 LayerGetter
producer、prepared-item builder、render-command consumer、node source resolver、SourceCache clear、
ResourceManager unload/unloadAll及实际 map clear、ObjSource dtor、D3D source getter、software texture
map clear/tree teardown。没有只凭已有报告状态重映射原任务。

## 3. reload、node 与 prepared item 四端映射

### 3.1 reset / rebuild / destruction

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| reset old tree | `0x6B2AD8`，244 | `0x581F3C`，212 | `0x100109ACC`，221 | `0x107358`，312 |
| build new tree | `0x6B25D0`，320 | `0x581CC8`，176 | `0x1001097C8`，142 | `0x107060`，207 |
| Player dtor | `0x6CCEBC`，311 | `0x593C24`，99 | `0x10011F2A0`，101 | `0x11DCC4`，175 |
| non-root suffix erase | `0x6F11EC`，274 | `0x5AE7A8`，221 | `0x10011DDB8`，303 | `0x11C6B4`，337 |
| explicit root clear | `0x6F174C`，65 | `0x593EFC`，24 | `0x10012A38C`，64 | `0x129004`，65 |
| MotionNode dtor | `0x6F206C`，92 | `0x5AF220`，50 | `0x10012A48C`，69 | `0x1290A6`，68 |
| PreparedRenderItem dtor / final deque dtor | item `0x6F21DC`，37 | item `0x5AF2D0`，28 | deque `0x10012A344`，18；item内联于node dtor | deque `0x128FDC`，15；item内联于node dtor |

reload builder 先构造 owning motion-content accessor，再 reset 旧树。accessor 构造前抛出时旧树完整；
reset 一旦开始，就先走 child invalidation、layer-id release 和 persistent item layer-id release，最后
才执行 `[begin+1,end)` suffix erase。之后的新 layer getter、recursive node append 或字段读取若抛出，
旧树不恢复，只保留已经发布的新树前缀。

`MotionNode::~MotionNode` 的显式 body 先 delete `preparedRenderItem`，随后普通 C++ reverse member
destruction 才释放 node 的 Variant/string/vector，包括 `SourceState.object`。因此 item 内借用的
`sourceState` 在 item 析构期间仍指向 live node storage；item 销毁后 source object owner 才释放。
Player destructor 对非根后缀执行同一 reset，删除 render adaptor 后再显式 `nodes.clear()` 销毁 root。

### 3.2 borrowed facade 与 render consumer

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| single LayerGetter | `0x6D0CD4`，41 | `0x595EF4`，19 | `0x100121D64`，19 | `0x120B2C`，19 |
| LayerGetter list | `0x6D2368`，117 | `0x596CD4`，73 | `0x100122DC0`，51 | `0x121E18`，90 |
| append prepared items | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| build render commands | `0x6C2208`，1,766 | `0x58C7C4`，1,348 | `0x1001167BC`，1,083 | `0x114118`，1,582 |

LayerGetter producer 只分配 8/4-byte facade并写 node address；没有 Player AddRef、node index、label copy
或 generation。facade adaptor可以活过 Player/node，所有 getter仍直接解引用 raw pointer；树替换后
访问不是 Void/default，而是未定义的 dangling dereference。

prepared builder 的普通 item 路径将 `entry.sourceState = &node.source`，同时写入 borrowed
`parentItem`，而 child/group vectors也只保存其他 node-owned item pointer。main/aux vectors是 caller
stack上的临时容器，不 delete item。render-command consumer按可信指针序列遍历，跨 Layer creation、
descriptor setter、source load、size/property reads、copy/mask等回调仍然读取 live item/sourceState。

由四端共同控制流可推导的 reentrant 边界是：上述任一回调若同步重入同一个 Player 的 reset/rebuild/
destruction，当前 item、sourceState、parentItem、childItems和 main/aux 后续元素都可能失效。consumer
没有 reload counter或再次定位 node；异常 cleanup只释放已经构造的 TJS/ttstr/vector临时 owner，
不会为 borrowed pointer 延长生命周期。

## 4. source resolver、unload 与 raw/texture 分裂

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| findSourceForNode | `0x691CC8`，1,191 | `0x570500`，676 | `0x1000F316C`，586 | `0xEF97C`，952 |
| unload one | `0x6A697C`，87 | `0x57B6F8`，47 | `0x100101A28`，35 | `0xFEC04`，69 |
| unloadAll callback/combined root | `0x6A5F74`（入口`0x6A60D8`），126 | `0x57B32C`，2 | `0x1001012CC`，2 | `0xFE3FE`，2 |
| actual loaded-module map clear | 同一126条combined root内联 | `0x59A62C`，17 | `0x10013A138`，22 | `0x13A246`，20 |
| ObjSource dtor | `0x6E145C`，101 | `0x5A1EE8`，13 | `0x100132A60`，16 | `0x131AF8`，50 |

generic fallback 把 `ResourceManager.findSource` 返回的 owning Variant写到 node `SourceState.object`。
其中 `ObjSource` 内部 raw node保存一个 retained PSB holder；因此 module map erase 只减少缓存引用，
外部 source object仍支撑 raw PSB allocation。

KRKR/Win atlas route不同：node只把 `LoadedResourceRecord` 中 texture holder的 raw pointer写入
`SourceState.texture`，不 AddRef。unload hit 销毁 module record时按两个 texture map和 PSB holder的
owner顺序 Release；它不扫描 Player，也不把 source.valid/texture清零。下次 D3D source getter最先
检查这个 raw pointer并直接返回，甚至不会重新查 ResourceManager。故 unload与live atlas node之间
没有安全 handoff；调用者必须避免这种时序，移植不能悄悄为SourceState增加texture holder。

## 5. SourceCache clear 与仍在使用的 Layer

| 平台 | clearCache root | 完整指令 |
|---|---|---:|
| Android arm64 | combined `0x6A4CD4`（clear entry `0x6A5818`） | 763，含load/clear相邻范围 |
| Android armv7 | `0x57B018` | 30 |
| iOS arm64 | `0x100100F10` | 29 |
| iOS armv7 | `0xFE0D4` | 29 |

共同顺序：遍历 list；只有 entry.layer Variant 类型精确为 Object 才调用
`Object->Invalidate(0,null,null,Object)`，忽略普通 status；全部完成后 `list.clear()`，最后 byte counter
置零。任一 Invalidate 抛出时，先前 Layer可能已 invalidated，list和计数却未清，形成 partial commit。

外部 Variant对 cached Layer的引用不在list owner之下。clear释放缓存引用，但外部 alias继续持有同一
dispatch；由于clear已调用Invalidate，它只是“仍有引用的 invalidated对象”。当前 render source resolve
也可能已经CopyRef该Layer；重入clear不会销毁这份call-local owner，却会同步改变其valid状态，返回后
native路径没有重新验证。

`bufLayer`、ResourceManager module map、node source object和D3DAdaptor texture-copy map均不受
`clearCache`影响。析构SourceCache则反过来只直接释放list owner，不调用public clear，所以不会发送
Invalidate；二者必须保持分离。

## 6. D3D source texture 与 software-copy map

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| source texture getter | `0x6EE440`，160 | `0x5AC518`，157 | `0x10014019C`，118 | `0x1414C0`，196 |
| removeAllTextures callback | `0x6AAC98`，14 | `0x57CF74`，2 | `0x100103D58`，12 | `0x101138`，11 |
| map erase tree | `0x6D8C38`，24 | clear/reset `0x59A8CE`，10；tree `0x59A8EC`，18 | `0x1001285E4`，23 | `0x127928`，51 |

D3D source getter 的返回值均为 raw borrow：atlas fast path直接返回 `SourceState.texture`；fallback
Layer的main-image texture也不AddRef。software renderer桥接 map的 key同样只是原texture pointer
identity，mapped copy holder才AddRef。map clear逐node Release mapped copy，但不Release、不验证、
不清理raw key所代表的source texture。

因此存在两种独立悬空：module/source Layer结束可让map key成为悬空identity；removeAllTextures可让
之前只借用mapped holder的返回值失效。首次software miss还有一份未由caller释放的factory reference，
所以该特定返回值可在map clear后继续存活；后续map hit只借用holder，不能把首miss的非对称行为推广
成所有结果都安全。

## 7. 生命周期图

```text
Player
  owns deque<MotionNode>
      owns SourceState.object Variant ------> ObjSource ------> PSBRawOwner
      borrows SourceState.texture ----------> LoadedResourceRecord texture holder
      owns PreparedRenderItem*               (deleted by MotionNode dtor)
             borrows SourceState*
             borrows parent/child item pointers
  temporary main/aux vectors borrow PreparedRenderItem*

LayerGetter adaptor
  owns one-pointer facade
  facade borrows MotionNode*                 (no Player/node owner)

SourceCache list
  owns cached Layer Variants
  clearCache invalidates Layer, then releases list owners
  external Layer Variants may retain the invalidated dispatch

D3DAdaptor software map
  borrows source-texture pointer as key
  owns copied texture as mapped holder
```

## 8. reset / clear / unload 的精确结果矩阵

| 操作 | 会结束 | 不会结束/不会修复 |
|---|---|---|
| Player reload/reset | 非根node、其PreparedRenderItem、SourceState owning members、label map | surviving LayerGetter、已复制到外部的borrowed item/source pointers不会失效通知 |
| Player destructor | 上述全部，加root item/node、render adaptor和Player Variants | surviving LayerGetter仍只剩dangling raw pointer |
| SourceCache clearCache | cached Layer先Invalidate，再释放list owner；counter归零 | bufLayer、module map、ObjSource、node source、D3D map均保留 |
| SourceCache destructor | 直接释放cache list/buf/owner members | 不发送public clear的Invalidate |
| ResourceManager unload | 一个module的KRKR/Win texture holders和cache PSB holder | ObjSource/raw dispatch自己的PSB owner保留；live atlas texture borrow不清零 |
| ResourceManager unloadAll | 所有module record owner | SourceCache list、live Player/LayerGetter不遍历 |
| D3DAdaptor removeAllTextures | mapped copied-texture holders和tree nodes | raw key/source textures、Player SourceState不触碰 |

所有操作均无锁。并发调用本身就是容器/refcount data race；即使同线程重入避免了并发数据竞争，也仍会
按上述已提交顺序产生 invalidated或dangling pointer，没有事务回滚。

## 9. 本地逐行对照

本地结构和调用顺序已经对应四端：

- `cpp/plugins/motionplayer/PlayerMotionLoad.cpp:84`、`:165`：旧树 reset 与 accessor-before-reset rebuild；
- `cpp/plugins/motionplayer/PlayerCore.cpp:173`：析构 reset、adaptor delete、root clear；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:30`、`:424`：item-first node dtor与suffix erase；
- `cpp/plugins/motionplayer/MotionNode.h:263`、`:293`：node-owned SourceState和item raw owner；
- `cpp/plugins/motionplayer/RuntimeSupport.h:119`、`:159`：borrowed sourceState和prepared pointer vectors；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:716`、`:724`，`SourceCache.h:241`：LayerGetter raw facade；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:442`、`:779`：persistent item和late sourceState borrow；
- `cpp/plugins/motionplayer/PlayerRenderExecute.cpp:396`：跨回调的trusted borrowed consumer；
- `cpp/plugins/motionplayer/PlayerResource.cpp:665`：owning source object与borrowed texture publication；
- `cpp/plugins/motionplayer/SourceCache.cpp:581`、`:716`：render-source resolve与cache clear；
- `cpp/plugins/motionplayer/ResourceManager.cpp:393`、`:539`：单module/全module map erase；
- `cpp/plugins/motionplayer/D3DAdaptor.cpp:70`：software texture holder map clear。

没有发现需要修改production语义的差异。本轮新增一个unit source回归：构造live LayerGetter，同时在
node的SourceState和PreparedRenderItem各安装一个可观察owner；执行与reload相同的non-root suffix
erase后，item owner先释放、source owner随后释放，而LayerGetter脚本对象仍存活但不得再解引用。
这锁定“facade活着不等于node活着”的四端边界。

## 10. 验证状态

现有与新增测试资产覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:14108`：LayerGetter读取live node而非snapshot；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:14232`：新增suffix erase释放source/item owner、getter facade
  继续存活的边界；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:14412`：prepared builder回调重入和persistent item alias；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:16071`、`:16153`：bufLayer alias、clearCache Invalidate与析构差异；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:27307`：module unload后外部raw dispatch继续读metadata；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:11290`：D3D software copy holder与factory ref非对称。

本轮完成20,424条完整指令、197个`xrefs_to`、80条任务注释、4个书签和四库保存；coverage与
163-ticket映射随后重生成并执行严格列数/重复ID/`git diff --check`检查。正式native unit、Web Debug
和真实重入/differential runtime执行仍归`MP-V`；静态闭合不伪称这些命令已经通过。

`MP-D12` 没有剩余task-local静态差异。
