# MotionPlayer & EmotePlayer 逆向分析报告

**目标文件**: `libkrkr2.so` (ARM64, Android)
**分析工具**: IDA Pro + MCP
**SHA256**: `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

---

## 1. 概述

在 `libkrkr2.so` 中，**MotionPlayer** 和 **EmotePlayer** 是两个负责角色动画播放的核心模块。EmotePlayer 是对 E-mote SDK 的封装，以 TJS2 脚本层暴露接口；MotionPlayer 则是更底层的动画纹理/帧管理器，负责从 PSB (E-mote 数据格式) 中查找和加载动画源图。

### 关键发现

| 项目 | EmotePlayer | MotionPlayer |
|------|------------|--------------|
| **类型** | TJS2 脚本层插件类 | 底层纹理/动画帧管理器 |
| **注册方式** | 作为 `EmotePlayer` 类注册到 TJS2 引擎 | 内部 C++ 模块，不直接暴露给脚本 |
| **DLL名** | `emoteplayer.dll` (概念上) | 无独立 DLL |
| **主要函数** | `sub_52E504` (类注册, 5460 bytes) | `sub_6948E8` (findSource, 4856 bytes) |
| **方法数** | ~40+ 个 TJS2 方法/属性 | 通过回调和数据结构被间接调用 |

---

## 2. EmotePlayer 分析

### 2.1 初始化流程

#### 静态初始化 (`sub_42EB00`, 0x42EB00)

这是一个静态构造函数，在库加载时执行：

```
sub_42EB00():
  - 初始化全局状态 (qword_1AB7E68 等)
  - 设置浮点初始值 (1.0f)
  - 将 sub_682528 注册为 "emoteplayer.dll" 的入口回调
  - 存储到全局注册链表 (xmmword_1AB8920)
```

#### 插件入口 (`sub_682528`, 0x682528, 640 bytes)

这是 EmotePlayer 插件的 TJS2 注册入口，在引擎加载 "emoteplayer.dll" 时调用：

```
sub_682528():
  1. 调用 sub_A136C0("m") 创建模块字符串
  2. 通过 sub_8E3C20() 获取 TJS2 引擎实例
  3. 在引擎中查找 "Motion" 命名空间 (vtable+32 = getMember)
  4. 调用 sub_685BC0(L"EmotePlayer", 1) 加载/初始化 EmotePlayer 类
  5. 调用 sub_9F5AF4() 将 "EmotePlayer" 类注册到 "Motion" 命名空间
     - 关联全局 qword_1AB8078
     - 标志位: 0x10000 (TJS2 类注册标志)
  6. 查找 "ResourceManager" 子对象
  7. 注册 sub_685D30 作为回调处理器 (PSB 解密种子设置)
  8. 注册 "setEmotePSBDecryptSeed" 方法到 ResourceManager
  9. 注册 "setEmotePSBDecryptFunc" 方法 (loc_685E60)
```

**调用图**:
```
静态初始化 sub_42EB00
  └─> 注册回调 sub_682528 (emoteplayer.dll 入口)
       ├─> sub_685BC0  (EmotePlayer 类加载)
       │    └─> sub_67FAC8 (内部初始化)
       │         └─> sub_6863C4 (延迟初始化回调)
       ├─> sub_685D30  (setEmotePSBDecryptSeed 实现)
       └─> loc_685E60  (setEmotePSBDecryptFunc 实现)
```

### 2.2 D3DEmotePlayer 类 (`sub_530C3C` + `sub_52E504`)

#### 类初始化 (`sub_530C3C`, 0x530C3C, 212 bytes)

```
sub_530C3C():
  1. 注册类型 ID "D" (dword_1AB26F8)
  2. 加载 "emoteplayer.dll" 模块引用 (sub_A136C0)
  3. 注册 "D3DLayerObjectNativeInstance" 类型 (dword_1AB2484)
     - 这是连接 TJS2 Layer 对象和 E-mote 原生实例的桥梁
```

#### 方法注册 (`sub_52E504`, 0x52E504, 5460 bytes)

这是 D3DEmotePlayer 最核心的函数，注册了所有暴露给 TJS2 脚本的方法和属性。通过 `sub_54242C()` 注册方法，通过 `sub_52FA58()` 注册常量。

##### 常量定义

| 常量名 | 值 | 说明 |
|--------|-----|------|
| `MaskModeStencil` | 0 | 模板遮罩模式 |
| `MaskModeAlpha` | 1 | Alpha 遮罩模式 |
| `TimelinePlayFlagParallel` | 1 | 时间线并行播放 |
| `TimelinePlayFlagSequential` | 2 | 时间线顺序播放 |

##### 属性 (Property)

| 属性名 | Getter | Setter | 说明 |
|--------|--------|--------|------|
| `module` | `sub_52FB98` | (只读) | E-mote 模块引用 |
| `visible` | `sub_53007C` | `sub_530084` | 可见性 |
| `smoothing` | `sub_530090` | `sub_530098` | 平滑处理 |
| `meshDivisionRatio` | `sub_5300A4` | `sub_5300B8` | 网格细分比率 |
| `bustScale` | `sub_5300CC` | `sub_5300DC` | 胸部缩放 |
| `hairScale` | `sub_5300F0` | `sub_530100` | 头发缩放 |
| `partsScale` | `sub_530110` | `sub_530120` | 部件缩放 |
| `bodyScale` | `sub_530130` | `sub_530140` | 身体缩放 |
| `animating` | `sub_530A38` | (只读) | 是否正在动画中 |
| `playCallback` | `sub_530A74` | (只读) | 播放回调 |

##### 方法 (Function)

| 方法名 | 实现函数 | 说明 |
|--------|---------|------|
| `create` | `sub_52FD84` | 创建 EmotePlayer 实例 |
| `load` | `sub_52FDD4` | 加载 E-mote 数据 |
| `show` | `sub_530068` | 显示角色 |
| `hide` | `sub_530074` | 隐藏角色 |
| `assignState` | `sub_530150` | 分配状态 (**TODO: 未实现**) |
| `setCoord` | `sub_5301EC` | 设置坐标 |
| `setScale` | `sub_530260` | 设置缩放 |
| `getScale` | `sub_5302DC` | 获取缩放 |
| `setRot` | `sub_5302E4` | 设置旋转 |
| `getRot` | `sub_53030C` | 获取旋转 |
| `setColor` | `sub_530314` | 设置颜色 |
| `getColor` | `sub_530320` | 获取颜色 |
| `countVariables` | `sub_53041C` | 变量数量 (**TODO: 未实现**) |
| `getVariableLabelAt` | `sub_530530` | 获取变量标签 (**TODO: 未实现**) |
| `countVariableFrameAt` | `sub_530568` | 变量帧数 (**TODO: 未实现**) |
| `getVariableFrameLabelAt` | `sub_530588` | 获取变量帧标签 (**TODO: 未实现**) |
| `getVariableFrameValueAt` | `sub_5305A8` | 获取变量帧值 (**TODO: 未实现**) |
| `setVariable` | `sub_5305C8` | 设置变量 |
| `getVariable` | `sub_5305D4` | 获取变量 |
| `startWind` | `sub_530680` | 启动风力效果 |
| `stopWind` | `sub_53068C` | 停止风力效果 |
| `countMainTimelines` | `sub_5306AC` | 主时间线数量 |
| `getMainTimelineLabelAt` | `sub_5306C8` | 获取主时间线标签 |
| `countDiffTimelines` | `sub_5306D4` | 差分时间线数量 |
| `getDiffTimelineLabelAt` | `sub_5306F0` | 获取差分时间线标签 |
| `countPlayingTimelines` | `sub_5306FC` | 播放中时间线数量 |
| `getPlayingTimelineLabelAt` | `sub_530718` | 获取播放中时间线标签 |
| `getPlayingTimelineFlagsAt` | `sub_530724` | 获取播放中时间线标志 |
| `isLoopTimeline` | `sub_530730` | 是否循环时间线 |
| `getTimelineTotalFrameCount` | `sub_5307D4` | 获取时间线总帧数 |
| `playTimeline` | `sub_530880` | 播放时间线 |
| `isTimelinePlaying` | `sub_53088C` | 时间线是否播放中 |
| `stopTimeline` | `sub_530898` | 停止时间线 |
| `setTimeline` | `sub_5308A4` | 设置时间线 |
| `getTimelineBlendRatio` | `sub_5308B4` | 获取时间线混合比率 |
| `fadeInTimeline` | `sub_530A10` | 时间线淡入 |
| `fadeOutTimeline` | `sub_530A1C` | 时间线淡出 |
| `skip` | `sub_530A44` | 跳过动画 |
| `addPlayCallback` | `sub_530A50` | 添加播放回调 |
| `progress` | `sub_530A5C` | 推进动画 |
| `setOuterForce` | `sub_530A8C` | 设置外力 |
| `getOuterForce` | `sub_530B28` | 获取外力 (**TODO: 未实现**) |
| `contains` | `sub_530B5C` | 包含判定 (碰撞检测) |

### 2.3 未实现功能

以下方法包含 `TODO: implement D3DEmotePlayer::xxx()` 字符串，说明是占位实现：

- `assignState` (0x530150) - 调用 `sub_95440C` 输出 TODO 日志
- `countVariables` (0x53041C) - 类似
- `getVariableLabelAt` (0x530530) - 类似
- `countVariableFrameAt` (0x530568) - 类似
- `getVariableFrameLabelAt` (0x530588) - 类似
- `getVariableFrameValueAt` (0x5305A8) - 类似
- `getOuterForce` (0x530B28) - 类似

### 2.4 PSB 解密机制

EmotePlayer 注册了两个 PSB (E-mote 数据格式) 解密相关方法到 ResourceManager：

1. **setEmotePSBDecryptSeed** (`sub_685D30`, 0x685D30, 1048 bytes)
   - 接收一个参数 (种子值)
   - 根据参数类型 (string/int/float) 进行转换
   - 将种子传递给 `sub_6A87D0` 进行解密配置
   - 支持的参数类型: 1=string→int, 2=string, 3=octet→int, 4=int, 5=float→int

2. **setEmotePSBDecryptFunc** (`loc_685E60`)
   - 设置自定义解密函数回调

---

## 3. MotionPlayer 分析

### 3.1 MotionPlayer 类注册 (`sub_6AB8BC`, 0x6AB8BC, 1596 bytes)

MotionPlayer 作为 TJS2 类注册，提供动画管理接口：

##### 方法列表

| 方法名 | 实现函数 | 说明 |
|--------|---------|------|
| (构造函数) | `sub_6AB8BC` 内 | 类注册和初始化 |
| `loadSource` | `loc_6A7BA8` | 加载动画源数据 |
| `clearCache` | `loc_6A8438` | 清除缓存 |
| `busy` (属性) | `sub_6A84FC` | 是否繁忙 (只读) |
| `isExistMotion` | `sub_6A8D8C` | 检查动画是否存在 |
| `findMotion` | (在 sub_6B0F10 中) | 查找动画 |
| (更多方法) | ... | 纹理/帧/运动控制 |

**字符串引用**:
- `isExistMotion` (0x14D8FE2) → xref 到 `sub_6AB8BC` (类注册)
- `findMotion` (0x14D8FFE) → xref 到 `sub_6AB8BC` 和 `sub_6B0F10`
- `particleMotionList` (0x14D9632) → xref 到 `sub_6B3C78`
- `stealthMotion` (0x14D9960) → xref 到 `sub_6D69C8`
- `getLayerMotion` (0x14D9C0E) → xref 到 `sub_6D69C8`
- `LayerTypeMotion` (0x14D9D1A) → xref 到 `sub_6D9B08`

### 3.2 findSource 核心函数 (`sub_6948E8`, 0x6948E8, 4856 bytes)

这是 MotionPlayer 最复杂的函数，负责从 PSB 数据中查找和加载动画纹理源。

#### 函数签名

```c
__int64 sub_6948E8(
    __int64 a1,     // 输出: source 结构体 (width/height/origin/clip/texture等)
    __int64 a2,     // 输入: 动画上下文 (包含资源管理器引用、配置等)
    unsigned __int64 **a3,  // 输入: 纹理名称/路径
    unsigned __int64 **a4   // 输入: 图标名称/路径
);
```

#### 处理流程

```
sub_6948E8():
  1. 获取资源管理器引用 (a2 + 636)
  2. 获取动画上下文 (a2 + 1012)

  3. 检查资源管理器中是否有缓存的源 (vtable+200, getMember)
     - 读取 dword_1AB8098 相关属性

  4. 检查是否为 "blank" 纹理 (sub_9B1ED0)
     - 如果是 blank，跳到 LABEL_142

  5. 根据数据类型 (offset+224) 分支:
     ├─ type==2: PSB 内嵌纹理处理
     │   a. 通过哈希查找纹理 (sub_6EB8F4)
     │   b. 解析 "source" 节点 (sub_598C58)
     │   c. 如果纹理缓存未命中:
     │      - 读取 "texture" 子节点
     │      - 提取 truncated_width, truncated_height, width, height
     │      - 读取 "type" (纹理格式): "RGBA8" 或 "A8L8"
     │      - 读取 "pixel" (原始像素数据)
     │      - 分配纹理内存 (4 * width * height)
     │      - 格式转换:
     │        * RGBA8: 调用 TVPReverseRGB() 反转 RGB 通道
     │        * A8L8: 手动转换为 RGBA (亮度→RGB, Alpha→A)
     │        * 其他: 输出 "MotionPlayer.findSource: Unsupported texture format '%1'"
     │      - 创建纹理对象 (sub_695D04, vtable+24)
     │      - 缓存纹理到哈希表 (sub_6E2150)
     │   d. 解析 "icon" 子节点获取额外纹理信息
     │   e. 设置源属性: originX, originY, width, height
     │   f. 设置裁剪区域: left, top, right(=left+width), bottom(=top+height)
     │
     ├─ type==1: 外部文件引用
     │   a. 保存纹理路径到 a1+112
     │   b. 如果 byte(a2+909) 标志位设置:
     │      - 调用 sub_695DE8() 加载外部纹理
     │
     └─ 其他: 跳到 LABEL_142

  6. LABEL_142: 脚本层回调模式
     a. 构建纹理路径 (可能拼接 "/" 和文件夹路径)
     b. 调用脚本回调函数 "f" (vtable+16) 在 TJS2 层查找
     c. 如果成功:
        - 解析返回的字典: width, height, originX, originY, blank
        - 解析 "clip" 子对象: left, top, right, bottom
        - 计算纹理尺寸: (int)width, (int)height
```

#### 输出结构体布局 (a1)

| 偏移 | 大小 | 含义 |
|------|------|------|
| +0 | 1 | 成功标志 (0/1) |
| +1 | 1 | blank 标志 |
| +4 | var | TJS2 variant (脚本回调结果) |
| +24 | 8 | 纹理对象指针 |
| +32 | 8 (double) | width |
| +40 | 8 (double) | height |
| +48 | 8 (double) | originX |
| +56 | 8 (double) | originY |
| +64 | 8 (double) | clip.left |
| +72 | 8 (double) | clip.top |
| +80 | 8 (double) | clip.right |
| +88 | 8 (double) | clip.bottom |
| +96 | 4 (int) | left (int) |
| +100 | 4 (int) | top (int) |
| +104 | 4 (int) | width (int) |
| +108 | 4 (int) | height (int) |
| +112 | 8 | 纹理路径引用 |

#### 纹理格式处理

1. **RGBA8**: 调用 `TVPReverseRGB()` 反转 RGB 通道顺序 (BGR→RGB 或反之)
2. **A8L8** (Alpha + Luminance): 手动将 2-byte 像素转为 4-byte RGBA
   ```
   for each pixel:
     byte0 = luminance, byte1 = alpha
     output[0] = alpha    // R
     output[1] = alpha    // G
     output[2] = alpha    // B (= luminance 复制)
     output[3] = luminance // A
   ```
3. **其他格式**: 释放内存并输出错误 `"MotionPlayer.findSource: Unsupported texture format '%1'"`

### 3.3 外部纹理加载 (`sub_695DE8`, 0x695DE8, 8012 bytes)

这是一个极其复杂的函数 (cyclomatic complexity = 281)，负责从外部文件加载纹理：

- 解析 "src/" 前缀路径
- 读取 originX, originY, clip, pixel, pal (调色板) 数据
- 支持多种图像数据源和格式
- 内存管理和纹理构建

### 3.4 调用链

MotionPlayer 的 `findSource` 被以下上层函数调用：

| 调用者 | 地址 | 说明 |
|--------|------|------|
| `sub_6B64AC` | 0x6B64AC (972 bytes) | 动画帧切换 - 按时间查找关键帧 |
| `sub_6B6ADC` | 0x6B6ADC (3472 bytes) | 动画序列更新 - 遍历帧序列并更新纹理 |
| `sub_6B7E44` | 0x6B7E44 (672 bytes) | 动画时间推进 - 双缓冲帧管理器 |
| `sub_6B826C` | 0x6B826C (1116 bytes) | 批量纹理预加载 - 遍历所有需要的纹理 |
| `sub_6B9A3C` | 0x6B9A3C (2936 bytes) | 动画播放主循环 - 时间驱动的帧更新 |

#### 关键调用路径

```
动画播放主循环 sub_6B9A3C
  ├─> 动画帧切换 sub_6B64AC
  │    └─> findSource sub_6948E8
  ├─> 动画序列更新 sub_6B6ADC
  │    └─> findSource sub_6948E8
  └─> 动画时间推进 sub_6B7E44
       └─> findSource sub_6948E8

批量预加载 sub_6B826C
  └─> findSource sub_6948E8
```

### 3.5 findMotion (`sub_6B0F10`, 0x6B0F10, 2056 bytes)

查找指定角色的指定动画：

```
sub_6B0F10(a1, a2=chara_name, a3=motion_name, a4=result):
  1. 创建消息对象 (sub_9C8440)
  2. 设置 "chara" 属性为角色名
  3. 设置 "motion" 属性为动画名
  4. 获取脚本回调接口 (a1+16)
  5. 调用 "onFindMotion" 回调到 TJS2 脚本层
  6. 处理返回结果 (可能返回路径或数据)
```

### 3.6 particleMotionList (`sub_6B3C78`, 0x6B3C78, 3212 bytes)

处理粒子动画列表，与 E-mote 编辑数据相关：

```
sub_6B3C78(a1, a2=context, a3=data):
  1. 解析数据节点
  2. 查找 "emoteEdit" 子节点 (emote 编辑配置)
  3. 解析 "label" 属性
  4. 通过 switch-case (13 cases) 处理不同粒子类型
  5. 更新粒子运动列表
```

---

## 4. EmotePlayer 与 MotionPlayer 的关系

```
┌─────────────────────────────────────────────────┐
│                  TJS2 脚本层                      │
│  var ep = new EmotePlayer();                     │
│  ep.load("character.psb");                       │
│  ep.playTimeline("smile");                       │
│  ep.progress(deltaTime);                         │
└─────────────┬───────────────────────────────────┘
              │
┌─────────────▼───────────────────────────────────┐
│           D3DEmotePlayer (TJS2 类)               │
│  sub_52E504: 注册 40+ 方法/属性                   │
│  - 时间线控制 (play/stop/fade)                    │
│  - 变量系统 (setVariable/getVariable)            │
│  - 物理效果 (wind/outerForce)                    │
│  - 渲染属性 (visible/scale/coord/color)          │
└─────────────┬───────────────────────────────────┘
              │
┌─────────────▼───────────────────────────────────┐
│        EmotePlayer 插件核心 (C++ 层)             │
│  sub_682528: 初始化, 注册到 Motion 命名空间       │
│  sub_685BC0: 类加载和资源管理器配置              │
│  sub_685D30: PSB 解密种子设置                    │
└─────────────┬───────────────────────────────────┘
              │
┌─────────────▼───────────────────────────────────┐
│        MotionPlayer (底层动画引擎)                │
│  sub_6948E8: findSource - 纹理查找/加载          │
│  sub_695DE8: 外部纹理加载                        │
│  sub_6B0F10: findMotion - 动画查找               │
│  sub_6B3C78: particleMotionList - 粒子动画       │
│  sub_6B9A3C: 播放主循环                          │
│  sub_6B7E44: 双缓冲帧管理                        │
│  sub_6B64AC: 关键帧切换                          │
│  sub_6B6ADC: 序列更新                            │
│  sub_6B826C: 批量预加载                          │
└─────────────┬───────────────────────────────────┘
              │
┌─────────────▼───────────────────────────────────┐
│           PSB 数据格式 / 纹理系统                 │
│  - RGBA8 纹理 (TVPReverseRGB)                    │
│  - A8L8 纹理 (手动转换)                          │
│  - 哈希表纹理缓存                                │
│  - TJS2 回调纹理加载                             │
└─────────────────────────────────────────────────┘
```

### 数据流

1. **加载阶段**: TJS2 脚本调用 `load()` → D3DEmotePlayer 加载 PSB 数据 → 解密 (如配置了种子) → 解析为内部数据结构
2. **播放阶段**: `progress(dt)` → 播放主循环 → 时间推进 → 关键帧切换 → `findSource()` 加载纹理 → 渲染
3. **纹理管理**: findSource 使用哈希表缓存已加载纹理，避免重复解码

---

## 5. 关键全局变量

| 地址 | 用途 |
|------|------|
| `qword_1AB8078` | EmotePlayer 类对象引用 |
| `qword_1AB8068` | EmotePlayer 内部状态 |
| `dword_1AB8070` | EmotePlayer 标志位 |
| `byte_1AB8060` | EmotePlayer 启用标志 |
| `dword_1AB2720` | D3DEmotePlayer 类型 ID |
| `dword_1AB2484` | D3DLayerObjectNativeInstance 类型 ID |
| `byte_1AB26E8` | D3DEmotePlayer 初始化标志 |
| `dword_1AB8098` | MotionPlayer 资源管理器 member ID |

---

## 6. 总结

### EmotePlayer
- 完整的 E-mote SDK 封装，提供了 40+ 个 TJS2 脚本接口
- 支持时间线动画、物理模拟 (风力/外力)、变量系统
- 部分高级功能标记为 TODO 未实现 (assignState, 变量帧查询, getOuterForce 等)
- 包含 PSB 数据解密支持 (种子+自定义函数)

### MotionPlayer
- 底层纹理和动画帧管理器
- 核心 findSource 函数支持 3 种纹理获取方式: PSB 内嵌、外部文件、TJS2 回调
- 支持 RGBA8 和 A8L8 两种纹理格式
- 使用哈希表缓存纹理，双缓冲帧管理优化性能
- 通过 onFindMotion 回调与 TJS2 脚本层交互实现灵活的资源查找
