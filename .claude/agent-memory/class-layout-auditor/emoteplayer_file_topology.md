---
name: emoteplayer-file-topology
description: motionplayer 模块 EmotePlayer 类族的本地文件拓扑与真实类清单 — 没有 EmotePlayerImpl/PrimaryEmotePlayer/MotionLayerMgr/独立 D3DEmotePlayer.cpp; 真实类 EmotePlayer(壳)/EmoteObject/D3DEmotePlayer 全在 EmotePlayer.h+EmotePlayer.cpp, EmoteEngine 在 EmoteEngine.h
metadata:
  type: project
---

# motionplayer EmotePlayer 类族 — 文件与类拓扑（审计基准）

## 不存在的文件/类（不要再去找）
仓库中**没有** `EmotePlayerImpl.h/.cpp`、`PrimaryEmotePlayer.h/.cpp`、
`MotionLayerMgr.h/.cpp`、独立的 `D3DEmotePlayer.h/.cpp`。
二进制中**也没有** `PrimaryEmotePlayer` NCB 类（repo grep 无命中）。
"EmotePlayerImpl" 是过时分析命名，物理上就是 EmoteEngine(1496B)，已在
EmoteEngine.h 实现。

## 真实类清单（4 个类，3 个文件）
- `cpp/plugins/motionplayer/EmotePlayer.h` 声明 4 个类:
  - `EmotePlayer`（L43-53）：退化 24B NCB 壳 — `EmoteEngine* _payload`(+8) + `bool _owned`(+16)，从不使用
  - `EmoteObject`（L73-88）：40B，`tTJSVariant _module` + `EmoteEngine* _engine`(+8 raw)
  - `D3DEmotePlayer`（L96-326）：完整 API NCB 类，持有 `EmoteObject* _emoteObj`(末尾) + 壳层标量
  - `EmoteEngine`（EmoteEngine.h，1496B，见 [[emoteengine-1496b-layout]]）
- `cpp/plugins/motionplayer/EmotePlayer.cpp`：**实现的是 D3DEmotePlayer + EmoteObject 的方法**，不是 EmotePlayer 壳（壳无方法体，default ctor/dtor）

## NCB 注册（main.cpp）
- L345 `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer){ NCB_CONSTRUCTOR(()); }` — 二进制只注册空 finalize（sub_6862C8 = `EmotePlayer_finalize_noop` return 0），本地多出一个可调 ctor（NCB 框架副作用，轻微偏差）
- L546 `NCB_REGISTER_CLASS(D3DEmotePlayer){ NCB_CONSTRUCTOR((ResourceManager)); ... }` — 完整方法表
- 两类无继承（与二进制一致）；见 [[emoteplayer-24b-shell]]

## 关键校验（2026-05-31 反编译复核）
- EmotePlayerNativeInstance_create @0x68629C: new(0x18), vtable off_1A18BB0, +8=0, +16=0 ✅
- EmotePlayerNativeInstance_destroy @0x6862D0: gate `if(+8 && !+16){ sub_67F4B8(+8); delete }` ✅
- sub_6862C8 = `EmotePlayer_finalize_noop` 确认 return 0 ✅
