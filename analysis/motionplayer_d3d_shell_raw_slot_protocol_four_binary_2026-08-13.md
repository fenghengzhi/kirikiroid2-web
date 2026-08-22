# D3DEmotePlayer 双 raw-owner slot 协议（四参考，2026-08-13）

本纵切面 fresh 复核 D3DEmotePlayer 的 primary/secondary `EmoteObject` 字段类型、clear、
load、clone 与 normal destruction。结论是两个字段都是 raw owner；最强证据不是“看到
delete”，而是四端共同保留的跨两次 destructor 的 dangling-slot window：先完整 delete
secondary，再完整 delete primary，最后才一次性把两个 member slot 置零。

## 1. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `clear` / slot teardown | `0x530164` | `0x4948C4` | `0x100232C1C` | `0x231840` |
| raw `load` callback | `0x5301B4` | `0x494920` | `0x100232CB0` | `0x231890` |
| typed `clone(D3DLayer*)` | `0x53039C` | `0x4949D4` | `0x100232DC8` | `0x2319DC` |
| normal destructor | `0x533FE0` | `0x497870` | `0x100236374` | `0x235076` |

四端 layout 沿用已确认的 listener base：

| ABI | primary | secondary | scalars/flags | total size |
| --- | ---: | ---: | --- | ---: |
| 64-bit | `+0x18` | `+0x20` | `+0x28..+0x31` | `0x38` |
| 32-bit | `+0x10` | `+0x14` | `+0x18..+0x21` | `0x24` |

owner D3DLayer pointer 位于 listener base 内且是 borrow；本专题的 raw ownership 只指两个
EmoteObject slot。四份 recovery IDB 的 `D3DEmotePlayer_guess` 类型明确区分这三项。

## 2. clear 的决定性 slot 时序

四端机器码都归一为：

```cpp
EmoteObject *secondary = self->secondary;
if (secondary) {
    secondary->~EmoteObject();
    operator delete(secondary);
}

EmoteObject *primary = self->primary;
if (primary) {
    primary->~EmoteObject();
    operator delete(primary);
}

self->primary = nullptr;
self->secondary = nullptr;
```

64-bit 两端用一个 16-byte zero store 清 pair；iOS armv7 用一个 64-bit zero store；
Android armv7 用等价的双 word store。关键不是 store 宽度，而是它在两次完整 delete **之后**。

因此 secondary destructor 返回到 primary destructor/operator delete 完成之间：

- secondary slot 仍非空，却已经指向已释放 storage；
- primary slot 仍指向尚未销毁/正在销毁的对象；
- 只有两次 delete 都完成后 pair 才重新成为可安全复用的 null 状态。

常规 `unique_ptr::reset()` 无法产生这段协议：无论库实现在 delete 前 exchange-null，还是
delete 后才清自己的 slot，一个 member reset 完成后都必须已经为 null，才能执行下一个
member reset。用 `release()` 也会在 delete 前先把 slot 置空。两个普通 raw pointer 配合
尾部 pair-clear 才是自然源码形状。

该窗口通常不会被 EmoteObject destructor 回调观察到，但它决定异常/插桩/allocator hook
边界，属于一比一恢复范围，不能仅以“正常情况下等价”为由改成逐字段 RAII reset。

## 3. load 复用完全相同的 teardown

`load` 的共同顺序是：

```text
clear both old slots using the protocol above
construct empty vector<ttstr>
convert every TJS argument and append
pending = operator new(sizeof(EmoteObject))
EmoteObject_ctor(pending, paths)
primary = pending                         // only after ctor success
destroy paths
return success
```

所以：

- 旧 secondary/primary 在第一个参数转换前已经销毁；
- 参数转换、vector grow、allocation 或 EmoteObject constructor 抛异常时，两个 shell slot
  保持 null；
- pending EmoteObject 完整构造前不会发布 primary；
- EmoteObject 内部 RM/Engine raw owner 的 constructor-failure 泄漏是下一层独立边界，
  不会改变 shell slot 仍为空的事实。

## 4. clone 的 raw 返回临时量是另一条边界

四端 typed clone 都执行：

```cpp
D3DEmotePlayer *copy = new D3DEmotePlayer(targetOwner);
copy->primary = source->primary->clone_guess();
return copy;
```

新 shell constructor 会立即向 target D3DLayer 注册 listener。若 `clone_guess()` 抛异常，
局部 `copy` 没有 RAII owner，shell storage 与已注册 listener 一起泄漏。成功时只写 primary，
secondary 仍为 null，shell scalar/flag 保持 constructor default。

这证明 clone 的局部返回对象是 raw；但它不是判断 primary member 类型的唯一证据，因为
即便 primary 是 `unique_ptr`，尚未到赋值点的新 shell 仍可能因 raw local 泄漏。primary/
secondary 字段类型的决定性证据仍是第 2 节 clear 的 pair protocol。

## 5. normal destructor 与 listener 生命周期

normal derived destructor 设置派生 vptr 后复用或内联同一 secondary→primary→pair-null
teardown；随后 listener base destructor 才从 borrowed owner D3DLayer 注销 shell。也就是：

```text
delete secondary
delete primary
zero both slots
RemoveListener(shell)
```

teardown 不改 base/user scale、visible 或 smoothing；显式 `clear()` 后 shell 仍保持 listener
注册和这些 scalar state，可以再次 `load`。

## 6. 本地与 IDB 恢复

本地字段继续使用两个 raw pointer。`clear()` 已从“delete secondary 后立即清 secondary”
校正为“四端共同的两次 delete 后 pair clear”，析构和 load 自动复用这一协议。clone 继续
用 raw local/return，未用 RAII 修复 listener 泄漏边界。

四份 recovery IDB 已写入自然 0x38/0x24 `D3DEmotePlayer_guess` 类型、clear/load/clone/
normal destructor 的 typed prototype，并补充 pair dangling window、load 提交点与 clone
泄漏的函数注释。

绝对地址只保留在本文；compiled source comment 不编码 ABI 偏移。
