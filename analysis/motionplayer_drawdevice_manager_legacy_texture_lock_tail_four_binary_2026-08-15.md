# motionplayer DrawDevice manager legacy texture-lock 尾部：四参考二进制对照

日期：2026-08-15

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS
armv7 四份参考二进制。本文专门闭合 `DrawDeviceManagerItem` 在
`PrimaryOwner` 之后的 pointer/int32/bool 三槽；旧 `libkrkr2.so` 注释不作为结论来源。

## 结论

这三个槽不是 padding，也不是仍在工作的 manager 状态机。四份当前参考都显式把它们
构造成 `null / 0 / true`，但完整 manager-item 虚函数面、非虚 `UpdateSettings`、
software override、Add/Remove 和析构链均不再读取、复写或清理它们。

它们与 KrKr 公开的历史 `DrawDeviceD3D::LayerManagerInfo` 尾部具有唯一而连续的结构签名：

```cpp
void *textureBuffer;
long texturePitch;
bool lastOK;
```

历史构造器也按 `textureBuffer(NULL), texturePitch(0), lastOK(true)` 初始化。固定提交证据：

- [`LayerManagerInfo.h`](https://github.com/krkrz/krkrz/blob/49c4d53506edecb824cd7b2cff8d32959b1f1b70/src/plugins/win32/drawdeviceD3D/LayerManagerInfo.h)
- [`LayerManagerInfo.cpp`](https://github.com/krkrz/krkrz/blob/49c4d53506edecb824cd7b2cff8d32959b1f1b70/src/plugins/win32/drawdeviceD3D/LayerManagerInfo.cpp)

因此当前恢复将字段名收敛为 `textureBuffer`、`texturePitch`、`lastOK`，并把它们标为
dormant legacy texture-lock tail。这里没有把旧版的 `lock/copy/unlock` 行为移植回来：
四份当前二进制没有那条数据流，bitmap completion 已经走 render texture 路径。

`texturePitch` 的当前 ABI 类型必须是 32 位整数。历史 Windows 源码中的 `long` 也是
32 位；但 Android/iOS 64 位 ABI 的 `long` 为 64 位，而两份 64 位参考都只分配四字节
槽并用 `STR WZR` 写入，所以移植源码使用 `tjs_int`，不能机械照抄成平台 `long`。

## 构造入口与完整对象大小

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `DrawDeviceManagerItem` ctor | `0x53287C` | `0x496480` | `0x100234FA8` | `0x233C14` |
| base item allocation size | `0x60` | `0x38` | `0x68` | `0x3C` |
| software item allocation size | `0x68` | `0x3C` | `0x70` | `0x40` |
| manager-item vtable address point | `0x19FAC18` | `0x10AB028` | `0x101AEE880` | `0x183907C` |

software item 只在 base item 尾后追加一个独立的 `iTVPTexture2D *SoftwareTexture`。这个
指针由 software override 读取、替换、Release，也由 software destructor Release；它与
base 中 dormant `textureBuffer` 不是同一个槽，不能再合并为旧恢复里的单一软件纹理字段。

## ABI 布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `D3DLayerObject` base size | `0x38` | `0x20` | `0x40` | `0x24` |
| `Manager` | `+0x38` | `+0x20` | `+0x40` | `+0x24` |
| `PrimaryLayer` | `+0x40` | `+0x24` | `+0x48` | `+0x28` |
| `PrimaryOwner` | `+0x48` | `+0x28` | `+0x50` | `+0x2C` |
| `textureBuffer` | `+0x50` | `+0x2C` | `+0x58` | `+0x30` |
| `texturePitch` | `+0x58` | `+0x30` | `+0x60` | `+0x34` |
| `lastOK` | `+0x5C` | `+0x34` | `+0x64` | `+0x38` |
| 未初始化 tail padding | `+0x5D..0x5F` | `+0x35..0x37` | `+0x65..0x67` | `+0x39..0x3B` |
| software `SoftwareTexture` | `+0x60` | `+0x38` | `+0x68` | `+0x3C` |

`lastOK` 只由单字节 store 初始化。`operator new` 返回的是原始存储，四个构造器都没有
whole-object memset，因此其后的三个对齐字节保持未指定旧内存内容；software 派生指针
从下一个自然对齐位置开始并单独清零。序列化或整对象 `memcmp` 若把这三个 padding 字节
当确定值，将不符合参考行为。

## 构造写入

| 写入 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Manager = manager` | `0x5328CC` | `0x4964A8` | `0x100234FEC` | `0x233C74` |
| `textureBuffer = null` | `0x5328D0` | `0x4964AA` | `0x100234FF0` | `0x233C7A` |
| `texturePitch = 0` | `0x5328D4` | `0x4964AA` | `0x100234FF4` | `0x233C7E` |
| `lastOK = true` | `0x5328D8` | `0x4964AE` | `0x100234FF8` | `0x233C8C` |
| `PrimaryLayer = manager->GetPrimaryLayer()` | `0x5328F0` | `0x4964CA` | `0x10023500C` | `0x233CBA` |
| `PrimaryOwner = owner helper()` | `0x5328F8` | `0x4964D0` | `0x100235014` | `0x233CC6` |

Android armv7 用一条 `STRD` 同时写 `textureBuffer` 与 `texturePitch`；这仍是两个独立的
四字节成员，不是一个八字节字段。iOS arm64 在更早的一条 pair store 中同时写
`D3DLayerObject` 尾部和 `Manager`，也不改变上述成员边界。

四份构造次序在优化器重排后均体现同一源级约束：legacy 三槽在调用 manager 的虚函数、
取得 primary layer/owner、执行 `UpdateSettings` 和 Fill 主图之前已经初始化。后续严格
解引用失败时，对象不会完成构造；由相应 factory/new-expression 清理已分配存储，但三槽
本身没有资源清理动作。

## 消费者审计

manager-item vtable 均有 11 个槽：

1. complete/destructor body；
2. deleting destructor；
3. `IsVisible`；
4. `Draw`；
5. `OnParentHasParent` no-op；
6. `OnDetached` no-op；
7. `AddListener`；
8. `RemoveListener`；
9. `OnUpdate`；
10. `TransformPoint`（固定 false）；
11. `GetSourceTexture`。

逐槽复核结果：

- dtor/deleting dtor 直接复用 `D3DLayerObject` 清理，只处理 parent/list 容器；不读取三槽，
  不把 `textureBuffer` 置空，也不根据 `lastOK` 分支；
- `IsVisible` 只用 `PrimaryOwner`；
- `Draw` 使用 parent、`Manager`、`PrimaryOwner` 和经虚槽取得的 draw texture；
- slots 5–9 只操作 `D3DLayerObject` base；
- `TransformPoint` 不读取对象；
- base `GetSourceTexture` 读取传入 bitmap 的纹理，不读取 item 尾部；
- software override 只读写派生的 `SoftwareTexture`，不是 `textureBuffer`；
- 非虚 `UpdateSettings` 只用 `PrimaryOwner` 并更新 base 的 plane/front/back；
- `AddLayerManager` 只负责分配、构造和发布 item；`RemoveLayerManager` 只清 manager data、
  delete item 后调用 base remove。

此外对四份 DrawDevice 插件代码区的实际成员偏移进行了 rendered-listing 扫描，并把每个
命中反查到函数边界。`lastOK` 的各自偏移只有构造器单字节 store；`textureBuffer` /
`texturePitch` 的其他同偏移命中均属于 bitmap、root、D3DLayer 或栈对象，不以 manager
item 为基址。结合完整 vtable 和所有已识别非虚入口，当前四份构建没有遗漏的消费者。

## 与历史实现的差异

历史 `LayerManagerInfo` 的这三项是有效状态：

- `lock()` 把 locked texture 的像素指针/stride 写入 `textureBuffer/texturePitch`；
- `copy()` 用二者计算目标行地址；
- `unlock()` 和 `free()` 解锁后清空 `textureBuffer`；
- `lastOK` 记录 lock 结果并抑制连续失败日志。

四份当前参考没有相关日志字符串，也没有上述字段消费者。当前 manager 绘制从
`iTVPLayerManager::GetDrawBuffer()` 取得 bitmap，再由 `GetSourceTexture` 直接取得或上传
`iTVPTexture2D`；`StartBitmapCompletion` 也只在该 draw-buffer bitmap 自身取得 render
target/reference 并执行 render-manager 操作，完全不读取根对象 `CurrentTarget`。因此只能
保留三项构造/布局行为，不能恢复已经不存在的 legacy lock 状态机。

## 源码、测试和恢复库落点

- `cpp/plugins/DrawDeviceD3D.cpp` 将泛化的 `ManagerStatePointer/Value/Flag_guess` 改成
  `textureBuffer/texturePitch/lastOK`，保持 `null/0/true` 构造值，并标明 dormant lineage；
- 既有 Add/Remove 与 manager settings 单测已经实际构造和删除 base/software item；三槽
  没有当前公开可观察面，未添加会伪造消费者的测试接口；
- 四份 recovery IDB 已增加对应 manager-item ABI 结构、ctor/`UpdateSettings` typed
  prototype、构造注释和 legacy-tail bookmark，并已保存；重新反编译四个构造器均直接显示
  `textureBuffer = nullptr`、`texturePitch = 0`、`lastOK = true`；
- `git diff --check` 通过，仅报告仓库现有 CRLF 转换提示；
- 完整 motionplayer unit-test TU Emscripten syntax check 通过，仅有仓库既有 `_tss`
  deprecated-literal warning；
- `Web Debug Build` 成功重编 `DrawDeviceD3D.cpp`、链接插件静态库和 `index.html`，仅有
  仓库既有 Emscripten pthread/memory-growth、JSPI 与 JS library warning。

## 保守边界

- 四份 stripped binary 单独不能恢复私有成员拼写；字段名来自同一插件公开历史源码与
  当前四端类型/顺序/初值的联合证据。若未来取得更接近参考版本的未剥离源码，应以其为准；
- `texturePitch` 的名字可复用，平台 C++ 类型不可照抄历史 `long`；
- 当前“无消费者”只描述这四份链接产物，不能证明更早或未链接分支从未使用这些字段；
- 不得因为字段 dormant 就删除它们、value-initialize 整对象或清零 tail padding，否则会
  改变对象大小、software 派生字段偏移和构造边界。
