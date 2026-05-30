---
name: emoteplayer-24b-shell
description: motion::EmotePlayer (非 D3D) 是退化 NCB 类 — 24B native instance(vtable/+8 ptr/+16 ownership byte),只注册空 finalize,无 constructor;+8 payload 是 EmoteEngine(1496B, sub_67F4B8 析构)。与 D3DEmotePlayer 完全独立
metadata:
  type: project
---

# motion::EmotePlayer (24B 退化 NCB 类) 权威记忆

注意区分两个类:**EmotePlayer**(本记忆,退化壳)vs **D3DEmotePlayer**(完整 API 类,见 [[emoteengine-1496b-layout]] / [[player-1384b-flat-spec]])。二进制中完全独立,无继承。

## native instance 布局 (24B, operator new(0x18))

工厂 `EmotePlayerNativeInstance_create @ 0x68629C`:
| 偏移 | 类型 | ctor 初值 | 语义 |
|---|---|---|---|
| +0 | vtable ptr | off_1A18BB0 | NCB iTJSNativeInstance 8槽接口表 |
| +8 | void* (payload) | 0 (懒创建) | EmoteEngine(1496B) — 析构器 sub_67F4B8 |
| +16 | byte | 0 | ownership 标志(=1 时 destroy 不 delete +8) |

## vtable @ 0x1A18BB0 (NCB iTJSNativeInstance 接口, 非 C++ 编译器生成)
| 槽 | 地址 | 函数 |
|---|---|---|
| 0 (+0) | 0x5242A8 | return 0 (Construct stub) |
| 1 (+8) | 0x6862D0 | EmotePlayerNativeInstance_destroy — gate `+8 && !+16` → sub_67F4B8 + delete |
| 2 (+16) | 0x5242B4 | tail-call vtable+32 (Invalidate?) |
| 3 (+24) | 0x686314 | in-place dtor(重置 vtable→off_19FD828)|
| 4 (+32) | 0x686374 | deleting dtor(delete this)|
| 5 (+40) | 0 (null) | |
| 6 (+48) | 0 (null) | |
| 7 (+56) | 0x9F6D60 | AddRef (++[a1+12]) |

## NCB 注册 (EmotePlayer_NCB_classInit @ 0x686148)
- class object = operator new(0xB0=176B), vtable off_19FD6C8, +152 classID, +168 工厂指针
- **只注册一个成员 `finalize` → sub_6862C8 (return 0, 空操作)**
- **二进制不注册 constructor**;本地 main.cpp:299-301 用 `NCB_CONSTRUCTOR(())` 多出一个可调构造(轻微偏差,NCB 框架副作用,非 fidelity 关键)

## 本地映射 (cpp/plugins/motionplayer/EmotePlayer.h:32-42)
本地 `class EmotePlayer { vtable; void* _slot1=+8; bool _slot2=+16; }` 对应的是
**24B native instance 本身**,但实际运行时 ncbind 用 `ncbInstanceAdaptor<EmotePlayer>` 当 native instance:
- adaptor._instance(ptr) ≈ 二进制 +8 / adaptor._sticky(bool) ≈ 二进制 +16 / adaptor C++ vtable ≈ +0
- 本地 EmotePlayer 是空壳,_slot1/_slot2 从不被使用 → 退化类,运行时无 payload(+8 永为 null,destroy 是 no-op)

## 关键纠正:对象链拓扑
- **D3DEmotePlayer** 链: native(≥56B) → EmoteObject(40B: +0 loader 0xE8, +8 EmoteEngine, +16/+24 PSB vector)
- **EmotePlayer**(24B) 链: native → +8 **直接 EmoteEngine(1496B)**(用同一 sub_67F4B8 析构),无 EmoteObject 中间层
- EmotePlayer.h:44-53 注释把对象链画成单一拓扑,实际 EmotePlayer 与 D3DEmotePlayer 是两条不同链

## 引用计数模式(全二进制一致)
payload 内部用 `__ldaxr/__stlxr` 原子 inc + `tTJSVariant_Release` 减引用;native instance 自身用 vtable+56 (++[+12]) AddRef。**非 shared_ptr**。
