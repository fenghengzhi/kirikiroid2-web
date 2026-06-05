# MotionPlayer P3-B 证据 dossier（2026-06-05）

> 只读证据核实。为 P3-B 架构重构（RM ownership: native value → dispatch-in + parentPlayer 链）
> 备齐反编译证据，供下一 session 直接动手。权威：libkrkr2.so 反编译，本 session 亲自取得。
> **本 dossier 不改任何 cpp/ 代码，不改 IDB。**

---

## 块 1：binary Player ctor 签名 + RM dispatch 槽

### 1.1 反编译地址表

| 项 | 地址 | 事实 |
|---|------|------|
| Player ctor | `Player_ctor@0x6CED30` | **单参** `(this, iTJSDispatch2* resourceManager_dispatch)`。无 parentPlayer 参 |
| Player 工厂 | `Player_factory@0x6f6dc0` | NCB createInstance 调用入口（唯一 xref = `Player_ncb_createInstance@0x6f6ce8`）|
| tTJSVariant copy-ctor | `sub_A0F5E0@0xA0F5E0` | 拷 16B body + type word(+16)，按 type AddRef（case3 obj=++refcount@+4，case2 str=atomic inc，case1 dispatch=IUnknown AddRef）|
| variant→object 强转 | `sub_A0E48C` | 把 variant 槽转成 object type（dispatch）|
| 工厂参数解包 | `sub_6F6E6C@0x6F6E6C` | 从 createInstance 入参数组取**第一个 TJS 参数**（RM dispatch obj），包成 variant 传给 ctor |

### 1.2 ctor 里的 RM 槽（byte-verified）

ctor 0x6CED30 内对 RM dispatch 的三次拷贝（同一个 `resourceManager_dispatch` 指针拷三份，各 AddRef 一次）：

```
0x6cee9c: sub_A0F5E0((char*)this + 636, resourceManager_dispatch);   // +636 槽
0x6ceeb0: sub_A0F5E0((char*)this + 656, resourceManager_dispatch);   // +656 槽
0x6cef28: sub_A0F5E0((char*)this + 992, resourceManager_dispatch);   // +992 槽
```

**结论(a)**：ctor 确为单参，无 parentPlayer。
**结论(b)**：+636 / +656 / +992 是**三个独立 tTJSVariant 槽**，全部由**同一个 RM dispatch 指针**拷贝填充（不是三个不同对象）。每槽是完整 16B tTJSVariant + type word，各持一份 AddRef 引用。三份的用途不同（见 1.3），但底层指向同一 RM dispatch 对象。

> 注：IDA 旧注释把 +636/+656 标为 "ttstr (color/transformOrder)"，**已被本次反编译证伪**——它们是 RM dispatch variant 槽，由 sub_A0F5E0(slot, dispatch) 填。+676/+716/+992 等才是 ttstr/variant 混合区，其中 +992 这里也被 RM dispatch 占用（见下）。
> （只读任务，不在 IDB 改注释；此处书面纠正。）

### 1.3 三个 RM 槽的消费者（byte-verified xref + 反编译）

扫描 Player 地址域 [0x670000,0x6E0000) 内引用偏移 636/656/992 的 ADD 指令，逐个反编译确认：

#### +636 槽 —— findSource 快路径 + self-object dispatch

`Motion_Player_findSource@0x6948e8`（读点 0x694928）：
```
v8 = a2 + 636;                              // RM self-object dispatch（+652 = flag, !=1 时 sub_A0E48C 强转）
AddRef(v8); v9 = *v8;                        // RM dispatch 指针
v9->vtable[200](v9, 2, dword_1AB8098, v137); // PropGet  —— 取属性
v10 = *(v137[0] + 8);                        // ★ dispatch 解包出 RM NATIVE 实例指针 ★
... 然后直接读 native:
  *(v10+224)  = spec int (1=krkr / 2=win)    // 0x6949d4
  v10+88      = HashMap A bucket array        // 0x694a78 (sub_6EB8F4 lookup) —— 即 P3-A 已对齐的 map
  *(v10+96)   = HashMap A bucket count
... 或 fallback FuncCall:
  v9->vtable[16](v9, 0, L"findSource", ..., a1+4, 2, v144, v9); // 0x69547c FuncCall "findSource"
```
**用途**：+636 = RM self-object dispatch。findSource 先 PropGet 解包出 native（dword_1AB8098 属性 → native ptr），走 native 快路径直读 +224/+88/+96（HashMap A）；krkr spec 下 fallback 用同一 dispatch FuncCall "findSource"。**混合模型：dispatch facade over native**。

#### +992 槽 —— 规范 RM，传递给子节点 + findMotion/loadMotion FuncCall

`Player_getResourceManager@0x6d9414`（NCB `L"resourceManager"` getter，绑定于 `Player_ncb_registerMembers @0x6d6bb8` struct v7+48）：
```
return sub_A0F5E0(a2, a1 + 992);             // 直接返回 +992 RM dispatch variant 拷贝
```

`Player_loadMotion@0x6b0f10`（LABEL_34, 0x6b12ac）和 `Player_findMotion@0x6d004c`（LABEL_52, 0x6d04b4）：
```
sub_A0F5E0(v42, a1 + 992);                   // 拷 +992 RM dispatch variant
sub_A0E48C(v42, 1);                          // 强转 object
v25/v36 = v42[0];                            // RM dispatch 指针
v36->vtable[16](v36, 0, L"findMotion", ..., a4, 2, v57, v36); // FuncCall "findMotion"
```

`Player_initNodeFields@0x6b43cc`（stencilType==3 创建 child Player）：
```
Player_ctor(v26, a1 + 124);                  // a1+124 (QWORD idx) = parent+992 → child 的 RM 参数
```
**用途**：+992 = 规范 RM dispatch 槽。(1) NCB "resourceManager" getter 返回它；(2) findMotion/loadMotion 经它 FuncCall；(3) **创建 child Player 时把 parent+992 作为 child ctor 的唯一 RM 参数传入**（继承）。

#### +656 槽 —— 渲染路径

引用点：`Player_renderToCanvas_guess@0x6c75ac`、`sub_6C9CA8`（多处 0x6cb060..0x6cb84c）、ctor、dtor。
（Motion_doAlphaMaskOperation 的 656/992 命中是该函数内别的对象偏移，非 Player 字段，已排除。）
**用途**：+656 = 渲染期 RM dispatch（render-to-canvas / 合成路径取 RM）。**本块未逐行展开渲染消费**（P3-B 迁移面主要在 findSource/findMotion/loadMotion，渲染路径在 P3-B 第一轮可保持现状，见块 3 待决疑点）。

### 块 1 小结
- ctor 单参，RM dispatch 拷 3 份（同一对象）：+636(findSource self-obj，解包 native 快路径)、+656(渲染)、+992(规范 RM，findMotion/loadMotion FuncCall + child 继承源 + NCB getter)。
- binary 是 **dispatch-facade-over-native 混合**：dispatch 进来，findSource 用 PropGet 解包回 native 直读内部 map；findMotion/loadMotion 用纯 dispatch FuncCall。

---

## 块 2：parent 上溯机制 —— `_parentPlayer` 有 binary 对应字段（NOT port 发明）

> 强「本地发明 vs binary 对应」断言，已独立交叉核实（读 read-point + write-point + caller 三方）。

### 2.1 读取点：`sub_6B1ABC@0x6B1ABC`（本地 `initialParameterRawValueLike_0x6B1ABC` 对应物）

caller = `sub_6B1718@0x6b1718`（0x6b196c 调 `sub_6B1ABC(v2, v6-56)`），v2 是 Player（用 v2+392/+400 deque）。确认 **a1 = Player**。

parent 链上溯（byte-verified）：
```
v8 = a1;                                     // 起点 = self Player
LABEL_3:
  v10 = v8;
  v20 = Player_HM2_find_node(v10+320, hash%*(v10+328), a2);  // 0x6b1ba8: 在 self HM2(+320) 查 label
  if (v20 && *v20) return *(*(v20)+16);       // 命中 → HM2 value (double)
  v8 = *(v10 + 8);                            // ★ 取 parent = *(Player+8) ★  (0x6b1bb8)
  if (!v8) return 0.0;                         // 链尽头 miss → 0.0
  v21 = *(v8 + 280);                           // parent+280 = parent 的 node-list 区间起点
  ... 扫 parent 的 type3/type4 node-list（v21[8]..v21[9]），命中返回 v21[6] ...
  // 内层走完后 v8 = *(v10+8) 继续上溯（LABEL_3）
```

**Player+8 = parent Player 指针**。读取点 = 0x6b1bb8（`v8 = *(v10+8)`）。
本地 PlayerVariable.cpp:267 的 `player = player->_parentPlayer` 上溯**对应这条链**，只是本地每层只查 HM2(`_evalResultValues`)，**省略了** binary 在每层 parent 额外扫描的 parent+280 type3/type4 node-list（v21 链）。这部分本地缺失（见待决疑点）。

### 2.2 写入点：`Player_initNodeFields@0x6b3c78` case `stencilType==3`

byte-verified（0x6b43c0..0x6b43dc）：
```
v26 = operator new(0x568u);                  // 新 child Player (1384B)
Player_ctor(v26, a1 + 124);                  // child ctor，RM 参数 = a1+124(QWORD) = parent+992
v27 = *a1;                                    // parent+0 (self-ptr)
*(_QWORD *)v26       = v27;                   // child+0 = parent+0
*(_QWORD *)(v26 + 8) = a1;                    // ★ child+8 = a1 (parent Player) ★  (0x6b43dc)
```

**写入点 = 0x6b43dc**。每个 stencilType==3（嵌套 Player）子节点构造时，child+8 被设为 parent Player。child 还从 parent+992 继承 RM dispatch（作为 child ctor 唯一参数）。

### 2.3 结论

**`_parentPlayer` 100% 不是 port 发明。**
- binary 字段：**Player+8**
- 设置点：`Player_initNodeFields@0x6b43dc`（stencilType==3 child 构造）
- 读取点：`sub_6B1ABC@0x6b1bb8`（变量值上溯查找）
- 默认值：ctor 0x6ced70 `*((_QWORD*)this+1)=0`（+8=0，根 Player 无 parent）

本地 PlayerCore.cpp:90 `Player::Player(rm, parentPlayer)` 把 parent 作为**第二 ctor 参数**——这是 port 对 binary「ctor 单参 + 构造后立即 `child+8=parent`」两步的合并简化。**语义正确，但 ctor 签名与 binary 不一致**（binary 单参）。P3-B 收敛 ctor 到单参后，parent 设置须移到构造后的独立赋值（对齐 0x6b43dc）。

---

## 块 3：RM ownership 迁移面

### 3.1 本地 `_resourceManagerNative` 消费者清单（grep cpp/plugins/motionplayer/）

| 文件:行 | 用法 | 当前形态 | binary 对应 |
|---|---|---|---|
| PlayerCore.cpp:91 | ctor `_resourceManagerNative(std::move(rm))` | native value 持有 | binary: dispatch 拷进 +636/+656/+992 |
| PlayerCore.cpp:103 | `new ResourceManager(_native)` → CreateAdaptor → `_resourceManager` variant | **方向相反**：native→反包 dispatch | binary: dispatch 进来即存 variant 槽 |
| PlayerCore.cpp:112 | `sourceCache->bindPlayer(this, &_native)` | 把 native 指针给 SourceCache | binary 无独立 SourceCache native，findSource 经 +636 dispatch 解包 native |
| PlayerCore.cpp:351/672/680/690/701 | `activateMotion(.., &_native)` / `resolveMotion(.., &_native)` | native 直调 | binary findMotion 经 +992 dispatch FuncCall "findMotion" |
| PlayerCore.cpp:687 | `_native.getLastLoadedModule()` | native 直调 | binary: RM dispatch PropGet / 内部 lastLoaded |
| PlayerResource.cpp:48 | `_native.clearCache()` | native 直调 | binary: RM 内部，clearCache 经 dispatch |
| PlayerResource.cpp:61/66 | `resolveMotion(.., &_native)` | native 直调 | findMotion via +992 dispatch |
| PlayerResource.cpp:93 | `_native.requireLayerIdForName(name)` | native 直调 | binary: RM dispatch（layer id 分配，需查 binary 对应 method）|
| PlayerResource.cpp:97 | `_native.releaseLayerId(id)` | native 直调 | 同上 |
| PlayerMotionLoad.cpp:65/72/283 | `resolveMotion(.., &_native)` | native 直调 | findMotion via +992 dispatch |
| PlayerMotionLoad.cpp:219/220 | `_native.releaseLayerId(...)` | native 直调 | binary RM dispatch |
| PlayerMotionLoad.cpp:227 | `child->_native = _native` | **child 拷 parent native** | binary 0x6b43cc: child ctor 参数 = parent+992 dispatch（不是拷 native）|
| PlayerUpdateParticles.cpp:447 | `new Player(_native, this)` | child 构造传 native+parent | binary 0x6b3c78: `Player_ctor(child, parent+992)` 然后 `child+8=parent` |
| Player.h:1045 | `ResourceManager _resourceManagerNative;` 字段 | native value 成员 | binary: 无 native 成员，只有 +636/+656/+992 dispatch variant 槽 |

附带（经 SourceCache 间接用 RM native）：
- SourceCache.h:112 `ResourceManager *_resourceManager`（指向 Player 的 _native）
- SourceCache.cpp:419/752/763 `_resourceManager->load(resolved)` —— binary findSource 内 native 解包后直读 HashMap A，无独立 SourceCache 对象 native 调 load

### 3.2 binary 侧对照（迁移目标形态）

| 本地 native 直调 | binary 实际 | 证据地址 |
|---|---|---|
| `resolveMotion(.., &_native)` / activateMotion | `+992` dispatch `FuncCall L"findMotion"` | findMotion 0x6d056c / loadMotion 0x6b1478 |
| `findSource` (SourceCache native) | `+636` dispatch PropGet 解包 native → 读 native +224/+88/+96 HashMap A；或 FuncCall L"findSource" | findSource 0x694928/0x69547c |
| `child->_native = _native` | child ctor 参数 = `parent+992` dispatch；构造后 `child+8 = parent` | initNodeFields 0x6b43cc/0x6b43dc |
| `getResourceManager()` | 返回 `+992` dispatch variant | getResourceManager 0x6d9414 |
| `requireLayerIdForName` / `releaseLayerId` | **未在本 session 定位 binary 对应** —— RM dispatch 上的 layer-id method（待下一 session 反编译）| TODO |

### 3.3 迁移面清单（native 直调 → dispatch 槽 FuncCall）

需改造（按风险升序）：

1. **ctor 签名收敛单参**（PlayerCore.cpp:90 → 单参 rm_dispatch）+ parent 设置移出 ctor。
   - 证据：ctor 0x6CED30 单参；child+8=parent 在构造后（0x6b43dc）。
2. **RM 持有从 native value → dispatch variant 槽**。
   - 本地 `_resourceManagerNative`(native) + `_resourceManager`(反包 dispatch) 两份合一 → 只保留 dispatch（对齐 +636/+656/+992，本地可暂用单个 _resourceManager variant 承担三槽语义）。
   - 证据：ctor 0x6cee9c/0x6ceeb0/0x6cef28。
3. **child 继承 RM**：`child->_native = _native`（MotionLoad:227）/ `new Player(_native, this)`（Particles:447）→ child ctor 收 `parent 的 RM dispatch`，构造后 child._parentPlayer = parent。
   - 证据：0x6b43cc(RM=parent+992) + 0x6b43dc(parent 设置)。
4. **findMotion 路径**：`resolveMotion(.., &_native)` → 经 RM dispatch FuncCall "findMotion"。
   - 证据：findMotion 0x6d056c / loadMotion 0x6b1478。
   - ⚠️ 高侵入：resolveMotion/activateMotion 当前是 native C++ 调用链，改为 dispatch FuncCall 需重写整条 motion 解析路径。
5. **findSource 路径**：SourceCache native `load` → RM dispatch PropGet 解包 native 读 HashMap A（P3-A 已把 map 对齐）。
   - 证据：findSource 0x694928/0x694a78。
   - 注：binary findSource 仍解包回 native 直读 map，所以**这一步是「dispatch facade + 内部 native 直读」混合**，不是纯 dispatch。本地 SourceCache native 路径与 binary native 快路径**语义已接近**，迁移面主要是「入口经 +636 dispatch 而非直接持 native 指针」。
6. **layer-id method**（requireLayerIdForName/releaseLayerId）：待定位 binary 对应 RM dispatch method 名后再迁。

---

## 下次 session 动手步骤清单

1. **先反编译 layer-id 路径**：定位 binary RM dispatch 上 layer-id 分配/释放的 method 名（本 session 未覆盖）。grep RM ctor sub_6A88CC 内 RB-tree(+176) / refcount(+216)，及 `requireLayerIdForName` 对应。无证据不动 PlayerResource.cpp:93/97。
2. **ctor 单参收敛**：PlayerCore.cpp:90 改单参 `Player::Player(rm_dispatch)`；parent 设置移到 child 构造点（PlayerUpdateParticles.cpp:447 + PlayerMotionLoad.cpp:227 构造后赋 `child._parentPlayer=this`，对齐 0x6b43dc）。
3. **RM 持有合一**：删 `_resourceManagerNative`(native value)，统一为 dispatch variant（对齐 +636/+656/+992）。findSource 改经 dispatch 解包（保留 native 快路径混合，对齐 0x694928）。
4. **motion 解析路径 dispatch 化**（最大侵入）：resolveMotion/activateMotion → +992 dispatch FuncCall "findMotion"。建议**最后做**，class-layout-auditor 全程守护。
5. 构建 web debug + wasmtime；m2logo(93)/yuzulogo(243) 差分作**非回归守护**（oracle-inert 风险高，主要靠反编译逐行对照）。

## 风险点

- **oracle-inert**：RM ownership 模型差异在 logo 差分中多不可观察（logo 不重度走 findMotion dispatch 路径）。按 CLAUDE.md「oracle-inert 不是 defer 理由」仍做，但验证主要靠反编译对照 + 构建非回归。
- **侵入面最大**：触及对象生命周期（child Player 继承链）+ RM 持有模型 + 所有 motion 解析消费者。须 P3-A 稳定后单独 session。
- **混合模型易误判**：binary findSource 是 dispatch-facade-over-native（解包回 native 直读 map），**不是纯 dispatch**。迁移时勿把 findSource 全改成 dispatch FuncCall（会偏离 binary）。

## 待决疑点

1. **+656 渲染槽消费细节未逐行展开**（renderToCanvas/sub_6C9CA8）。P3-B 第一轮可保持现状，但完整对齐需后续反编译 0x6c75ac/0x6cb060。
2. **parent 链每层的 parent+280 type3/type4 node-list 扫描**（sub_6B1ABC v21 链，0x6b1c1c..0x6b1d88）在本地 initialParameterRawValueLike 缺失（本地只查每层 HM2）。这是 parent 上溯的**完整语义缺口**，与 RM ownership 正交，可独立补。
3. **layer-id RM method**：binary 对应 method 名未定位（块 3 步骤 1）。
4. **findSource 的 dword_1AB8098 PropGet 属性名**：+636 解包 native 用的属性（dword_1AB8098 索引）未解 UTF-16 名，下次确认它是 RM dispatch 上哪个属性返回 native ptr。
