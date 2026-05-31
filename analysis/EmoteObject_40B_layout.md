# EmoteObject 内存布局 (40B / 0x28, libkrkr2.so ARM64)

> 权威来源：`EmoteObject_init` (0x67DBE0) / `EmoteObject_destroy` (0x67F434) 反编译。
> 每个偏移有指令级证据。
>
> **归属说明**：这是 libkrkr2.so（NDK clang/ARM64）的字节几何，仅作反编译对照/字段语义确认用，
> **不约束 wasm 本地实现**。wasm（emscripten clang/wasm32，指针 4B）下同一份源码的偏移必然不同，
> 这是 ABI 必然、可接受。复刻目标是源代码结构/数据流/对象生命周期，不是字节偏移。
> 本地 C++ 应写普通类（带字段名/方法语义）让编译器算偏移，不写 #pragma pack POD 硬凑。

## 对象大小

- **40 字节 (0x28)**：`load` 路径 `operator new(0x28)` @0x52FEC0 分配。
- `EmoteObject_init` 清零范围 = +0..+32 共 40B：
  - `*(_OWORD*)a1 = 0` → 清 +0/+8（0x67dbec）
  - `*((_OWORD*)a1+1) = 0` → 清 +16/+24（0x67dbe4）
  - `a1[4] = 0` → 清 +32（0x67dbe8）

## 字段表（init 写入 + destroy 释放，双向确认）

| 偏移 | 类型 | 语义 | 证据 |
|---|---|---|---|
| +0  | ptr | **scriptObject**：`operator new(0xE8)` via sub_6A88CC；destroy 用 sub_6A8B94 + operator delete | init / destroy 0x67f44c |
| +8  | ptr | **EmoteEngine\***：`operator new(0x5D8)`=1496B + EmoteEngine_ctor；destroy 用 sub_67F4B8 + operator delete | init / destroy 0x67f434 |
| +16 | ptr | **std::vector\<tTJSVariant\*\> begin** (a1[2])；init 经 sub_67F0CC 填充 | destroy 0x67f464 |
| +24 | ptr | **std::vector\<tTJSVariant\*\> end** (a1[3]) | destroy 0x67f464 |
| +32 | ptr | **std::vector\<tTJSVariant\*\> cap** (a1[4]) | destroy 0x67f48c |

destroy 的 vector 清理：`for (v4=a1[2]; v4!=a1[3]; ++v4) if(*v4) tTJSVariant_Release(*v4);` 然后 `operator delete(a1[2])`（0x67f464..0x67f4a0）。

## 在对象生命周期脊柱中的位置

```
D3DEmotePlayer 56B  +24/+32 → EmoteObject 40B  +8 → EmoteEngine 1496B(0x5D8) → Player 1384B
```

- D3DEmotePlayer 持有 EmoteObject*（主槽 +24 / 次槽 +32），见 D3DEmotePlayer_56B_layout.md。
- `D3DEmotePlayer_load` (0x52FDD4) 用 `operator new(0x28)` + EmoteObject_init 重建主槽。
- `D3DEmotePlayer_create`（注册名 "clear"，0x52FD84）拆除 +24/+32 两槽，指针置 null。
