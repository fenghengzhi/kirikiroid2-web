# `Player` 非多态 payload 与 NCB adaptor vtable 四端审计

## 结论

`MP-L04` 的四端结论不是“恢复出一个 `Player` vtable”，而是更关键的否定性
disposition：原始 `motion::Player` payload 在四个参考二进制中都是非多态对象，没有
vptr、虚函数表面、virtual/complete/deleting destructor 对，也没有多继承 this-adjusting
thunk。唯一与脚本 `Motion.Player` 同时出现的 vtable 属于外层
`ncbInstanceAdaptor<Player>` TJS native-instance shell，不能移植到 `Player` 本体。

`Player` 只有一个 ordinary non-virtual destructor。所有 owning site 都执行“直接调用
ordinary destructor，随后 scalar `operator delete`”；iOS 的 4-byte tail branch 只是
链接/调用距离 thunk，不调整 `this`，不是多继承 thunk。

本地 `Player` 保持无基类、无 virtual 成员和普通析构；`ncbind.hpp` 的
`ncbInstanceAdaptor<T>` 保持独立的虚析构/`Invalidate` shell。没有 C++ 语义修改。四个
IDB 已补充确定性名字、注释、vtable 书签并保存。

## 1. `Player` 构造首字段证明

| 平台 | `Player` ctor | allocation size | 完整指令 | 首两个 pointer-width store |
|---|---:|---:|---:|---|
| Android arm64 | `0x6CC110` | `0x568` | 593 | `{this, null}` 写入 `+0/+8` |
| Android armv7 | `0x5935C4` | `0x3B0` | 281 | `{this, null}` 写入 `+0/+4` |
| iOS arm64 | `0x10011EC04` | `0x4B8` | 226 | `{this, null}` 写入 `+0/+8` |
| iOS armv7 | `0x11D488` | `0x348` | 499 | `this` 写 `+0`，null 写 `+4` |

四端 fresh decompile 和 constructor prefix disassembly 一致：函数入口保存寄存器后，
对象上的第一个业务写是 `rootPlayer = this`，第二个是 `parentPlayer = nullptr`。没有在
`+0` 写 rodata/data vtable address，也没有先写 vptr 后被业务字段覆盖的 base-constructor
call。

这两个字段随后被 child/nested owner 链按构造成功后的 publication frontier 更新；它们
是普通 raw relationship fields。把 `+0` 解释成 vptr 会同时破坏 root/self 语义、四端
字段布局和所有后续直接 load。

constructor 的完整 xref 分母在每端都是四个 code caller、零 data xref：

1. `EmoteEngine` 构造主 `Player`；
2. type-3 child-motion node builder；
3. particle child builder；
4. NCB `Motion.Player` native materializer。

iOS 的 Engine caller 经过一条单指令 tail branch；它不调整参数寄存器。

## 2. ordinary destructor 与完整 xref 分母

| 平台 | ordinary `Player::~Player` | 完整指令 | xref 数 |
|---|---:|---:|---:|
| Android arm64 | `0x6CCEBC` | 311 | 7 code / 0 data |
| Android armv7 | `0x593C24` | 99 | 4 code / 0 data |
| iOS arm64 | `0x10011F2A0` | 101 | 4 code / 0 data |
| iOS armv7 | `0x11DCC4` | 175 | 4 code / 0 data |

xref 数不同来自 ICF、combined internal entries、异常 cleanup 和 iOS tail thunk 的恢复
形状，不是不同的 source virtual surface。联合 caller 后只有三类释放边界：

- `EmoteEngine` 的 single-pointer owner 正常析构/constructor unwind；
- `ncbInstanceAdaptor<Player>` 对脚本直接对象、type-3 child 和 particle child 的释放；
- NCB native 构造已经分配 `Player`、但 receiver attach/后续 publication 失败时的局部
  cleanup。

所有 owner site 都显示同一序列：

```text
if (player != null):
    Player_dtor(player)          // direct call or no-adjust tail thunk
    scalar_operator_delete(player)
```

没有 caller 通过 `[*player + slot]` 间接调用，没有 destructor address 的 vtable/data
xref，也没有一个函数把 `Player_dtor` 与 scalar delete 封装成 `Player` 自身的 Itanium
deleting-destructor entry。

## 3. iOS tail thunk 不是多继承 thunk

iOS arm64 `0x10011F548` 和 iOS armv7 `0x11E034` 都是单条无条件 branch 到 ordinary
destructor。constructor 也各有同形状 tail thunk。两者在 branch 前没有：

- `ADD/SUB this, constant`；
- 从 secondary-base slot 取 enclosing object；
- vcall/vbase offset load；
- 参数寄存器交换。

所以它们只解决链接/调用距离或 ICF placement，不能标成 MI adjustment thunk。Android
两端直接调用 ordinary entry，更进一步确认共同源码没有 secondary base。

## 4. 真正的 vtable：`ncbInstanceAdaptor<Player>`

外层 adaptor 的公共逻辑布局为：

```text
ncbInstanceAdaptor<Player> shell
    vptr                        // TJS native-instance polymorphic shell
    Player* native             // raw pointer; owner iff sticky == false
    bool sticky
```

vtable address point 与五个前缀 virtual entries 为：

| 平台 | address point | entries |
|---|---:|---|
| Android arm64 | `0x1A1EDF8` | inherited `0x524688`; Invalidate `0x6FB3DC`; inherited `0x524694`; complete dtor `0x6FB420`; deleting dtor `0x6FB480` |
| Android armv7 | `0x10BD108` | inherited `0x48F930`; Invalidate `0x5B6948`; inherited `0x48F936`; complete internal entry `0x5B694C`; deleting internal entry `0x5B6974` |
| iOS arm64 | `0x101AE6EF8` | inherited `0x1000302F4`; Invalidate `0x10014DDE8`; inherited `0x100038D28`; complete dtor `0x10014DDEC`; deleting dtor `0x10014DE30` |
| iOS armv7 | `0x1835408` | inherited `0x2E944`; Invalidate `0x14FAF4`; inherited `0x1296B0`; complete dtor `0x14FAF8`; deleting dtor `0x14FB24` |

Android armv7 把 Invalidate、complete destructor、deleting destructor 和共享
`deleteInstance` helper 合并在一个 IDA function range；原始 vtable pointers 精确指向
`0x5B6948/0x5B694C/0x5B6974` 三个不同 entry。其他三端有独立函数或 tail thunk。

adaptor 的共同释放边界是：

```text
deleteInstance(shell):
    if shell.native != null && !shell.sticky:
        Player_dtor(shell.native)
        scalar_delete(shell.native)
    shell.native = null
    shell.sticky = false

Invalidate(shell):
    deleteInstance(shell)

complete_dtor(shell):
    install derived adaptor vptr
    deleteInstance(shell)
    restore base vptr where retained by ABI/codegen

deleting_dtor(shell):
    install derived adaptor vptr
    deleteInstance(shell)
    scalar_delete(shell)
```

shell 的 deleting destructor 删除的是 shell allocation，不是 `Player` deleting
destructor。sticky shell 只 borrowed `Player`，Invalidate/destructor 清槽但不销毁 native；
当前四参考调用图里的 Player producer 都传 non-sticky，未发现 sticky Player producer。

## 5. owner 图与 publication/reentry

```text
EmoteEngine
    unique/raw single-pointer owner -> Player payload
        publication: Player ctor 正常返回后写 Engine owner slot
        failure before publication: new-expression 释放 raw allocation
        teardown: all seven direct controllers -> Player_dtor -> scalar delete

Motion.Player TJS object / type-3 child TJS object / particle child TJS object
    refcount owner -> tTJSCustomObject
        owns -> ncbInstanceAdaptor<Player> shell
            non-sticky raw owner -> Player payload
            publication: adaptor 注册成功后 shell.native 指向 Player
            attach failure: caller local Player cleanup；未成功注册的 shell 按 TJS 路径释放
            invalidation: ordinary Player dtor + scalar delete，随后清 native/sticky
            shell final release: adaptor deleting dtor 再 scalar-delete shell

Player payload
    borrows -> rootPlayer / parentPlayer relationship pointers
    owns/retains -> 其余对象与容器（由 MP-L01..L03/L09..L12 独立闭合）
```

adaptor 的 native pointer 在 `Player_dtor` 和 scalar delete 完成后才被公共 helper 清零；
因此 native 析构期间普通 lookup 仍可能看到原 pointer。外层 TJS invalidation guard 是否
阻止再次 `Invalidate` 是 shell/object 状态，不把 payload 变成多态对象。Engine 的
`unique_ptr::reset` 在 Android/iOS 的 slot-null store 调度不同，属于 library/compiler
实现边界；共同 owner 语义是 single-owner、direct dtor+delete。

## 6. 本地映射

| 参考实体 | 本地实体 | 结果 |
|---|---|---|
| 非多态 `Player`，首字段 root/self | `cpp/plugins/motionplayer/Player.h:119` | 匹配 |
| ordinary destructor | `cpp/plugins/motionplayer/Player.h:129`; `PlayerCore.cpp:173` | 匹配 |
| 无 base、无 virtual method | `Player.h` class declaration | 匹配 |
| adaptor 独立 vptr/native/sticky | `cpp/core/plugin/ncbind.hpp:121` | 匹配 |
| adaptor virtual destructor + `Invalidate` | `ncbind.hpp:127-136` | 匹配 |
| non-sticky direct native delete 后清槽 | `ncbind.hpp:143-158` | 匹配 |
| shell deleting destructor 与 payload 分离 | C++ virtual shell destruction + ordinary `delete _instance` | 匹配 |

本任务没有 semantic C++ edit。确定性二进制证据还支持把四个 IDB 中原有的
`Player_ctor_guess`、`Player_dtor_guess` 和 iOS无调整 tail thunk 名称改为无 `_guess`
版本。

## 7. Disposition

| 标题中的候选项 | 四端 disposition |
|---|---|
| `Player` vtable | ABSENT：payload 非多态，`+0` 是 rootPlayer self |
| `Player` 虚函数表面 | ABSENT：调用均为直接 entry |
| `Player` complete/deleting destructor pair | ABSENT：只有 ordinary destructor；delete 在 owner site |
| 多继承 thunk | ABSENT：iOS tail branch 无 this adjustment |
| `Motion.Player` 相关 vtable | PRESENT，但属于 `ncbInstanceAdaptor<Player>` shell |
| shell complete/deleting destructor | PRESENT，删除 shell/native 的两个不同层级 |

`MP-L04` 的 task-local 静态缺口为零；全对象/AddRef/Release 总审计仍由 `MP-L16` 与
`MP-V13` 独立跟踪。
