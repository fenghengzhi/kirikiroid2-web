# [ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans] 完整执行链分析

> 分析对象: libkrkr2.so (kirikiroid2 Android, arm64-v8a) + 千恋万花游戏脚本
> 分析方法: IDA Pro MCP反编译 + TJS2字节码反汇编(tjsdump) + KAG脚本解密(ksdec)
> **注意: 不分析当前项目代码，仅分析libkrkr2.so和原始游戏脚本**

---

## 1. KAG脚本上下文

### 1.1 custom.ks 中的Logo播放流程 (line 84-93)

```
*logo
    @call target=*caution        ; 显示注意事项
*logo_show
    [sysvoice name=brand delayrun=1000]
    [delaystart nowarn]
    [ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]   ; ← 分析目标
    [ev waitmovie]               ; 等待动画播放完毕
    [delaycancel]
    [ev storage=m2logo.mtn chara=m2cheeseware_logo motion=back_white notrans]
    [ev waitmovie]
    ...
```

**关键参数**:
- `storage=yuzulogo.mtn` — PSB/MTN动画文件路径
- `chara=LOGO` — 角色标识(Player.chara属性)
- `motion=yuzulogo` — Timeline标签名
- `notrans` — 不使用过渡效果(直接显示)

### 1.2 patch.tjs 中的关键设置

```javascript
useD3D = 0;  // 所有平台(包括Android)都不使用D3D模式
```

这意味着AffineSourceMotion走的是**非D3D路径** — 渲染到motionWorkLayer而非D3DAdaptor。

### 1.3 AffineSourceMotion.tjs 中的 extSourceMap 注册

```javascript
// 编译后TJS字节码确认的顶层注册:
AffineSourceMotion.extSourceMap[".PSB"] = AffineSourceMotion;
AffineSourceMotion.extSourceMap[".MTN"] = AffineSourceMotion;
```

当`storage`的扩展名为`.MTN`时，KAG系统通过`extSourceMap`查找并使用`AffineSourceMotion`类。

---

## 2. TJS层执行链路 (从字节码反汇编确认)

### 2.1 [ev] 标签处理 — KAGEnvImage.tjs

`[ev]`是KAG环境图像管理标签。KAGEnvImage在构造函数中注册了tag handlers:

```javascript
// KAGEnvImage constructor (bytecode line ~400):
commands["ev"]      = tag handler for ev
commands["notrans"] = function() { ... }
commands["waitmovie"] = function() { syncMode=1; updateFlag=1; waitMode=2; }
```

处理流程:

1. **KAGEnvironment.onTag("ev", params)** — KAG conductor分发标签到KAGEnvImage
2. **KAGEnvImage._setOptions(params)** — 解析参数(storage, chara, motion等)
3. **KAGEnvImage.updateImageSource()** — 更新图像源:
   - 读取`imageFile`(由storage参数设置)
   - 通过`env.isExistentImageFile()`查找文件
   - 创建或复用`imageSource`(AffineSourceMotion实例)
   - 设置`enableFade`标志

### 2.2 AffineSourceMotion 初始化和加载

从tjsdump反汇编确认的流程:

```
AffineSourceMotion (TJS class, extends AffineSource)
    │
    ├── _player = new Motion.Player()        // C++原生NCB对象
    │
    ├── _player.chara = "LOGO"               // 设置角色标识
    │
    ├── _player.motion = "yuzulogo.mtn"      // 触发setMotion
    │       └── Player_setMotion_NCBWrapper (0x681CAC)
    │           └── Player_playImpl (0x6B21E8, flags=0)
    │               └── Player_setMotionImpl (0x6B2284)
    │                   └── Player_loadMotion_guess (0x6B0F10)
    │                       ├── 调用objthis.onFindMotion({chara:"LOGO", motion:"yuzulogo.mtn"})
    │                       ├── 从返回结果获取chara和motion属性
    │                       ├── 构建路径: "motion/" + chara + "/" + motion
    │                       ├── 调用ResourceManager.findMotion(projectRef, path)
    │                       └── 解析PSB/MTN → 建立timeline/layer tree
    │
    ├── _player.play("yuzulogo")             // 播放指定timeline
    │       └── Player_play_NCBWrapper (0x67F40C)
    │           └── Player_playImpl (0x6B21E8, flags with label)
    │               └── timeline["yuzulogo"].playing = true
    │                   timeline["yuzulogo"].currentTime = 0
    │
    └── onMotionStart()                      // TJS callback
            ├── _playing = 1
            ├── _lastPlaying = 1
            └── 遍历tags → entryDelay注册定时action
```

### 2.3 notrans 参数处理

```javascript
// KAGEnvImage tag handler for "notrans" (bytecode确认):
function() {
    var d = new Dictionary();
    d["method"] = "";      // 空过渡方法
    d["time"] = 0;         // 零时间
    self.trans = d;        // 直接显示，无视觉过渡
}
```

### 2.4 AffineLayer.entryFlip — 注册帧更新

```javascript
// AffineLayer.entryFlip() (bytecode line 486):
function entryFlip() {
    if (window.entryFlipLayer !== void) {
        if (_rasterTime !== void || _image.isFlip) {
            window.entryFlipLayer(this);  // 注册到Window的flip列表
        }
    }
}
```

这是动画驱动的**入口注册点** — AffineLayer通过`window.entryFlipLayer(this)`将自己注册到Window的帧更新列表中。Window在每帧flip时遍历列表调用`updateFlip(delta)`。

---

## 3. 每帧驱动链路 (帧循环)

### 3.1 帧循环触发

```
Cocos2D mainLoop → Director::drawScene → TVPDeliverContinuousEvent
    │
    ├── Window.flip() / Window.onFlip()
    │       │
    │       ├── 遍历entryFlipLayer列表:
    │       │       └── AffineLayer.updateFlip(delta)        ← 步骤3.2
    │       │
    │       └── 触发Layer.onPaint:
    │               └── AffineLayer.onPaint()                ← 步骤3.3
    │
    └── [重复每帧]
```

### 3.2 AffineLayer.updateFlip(delta) → 停止检测

```javascript
// AffineLayer.updateFlip() (bytecode line 457):
function updateFlip(delta) {
    delta *= speed;         // 应用速度倍率
    var changed = 0;
    
    // 更新raster动画
    if (_rasterTime !== void) {
        _rasterTime.update(delta);
        _doAffine = 2;
        changed = 1;
    }
    
    // ★ 关键: 调用AffineSourceMotion.updateFlip()
    if (_image.isFlip) {
        _image.updateFlip(delta);    // → AffineSourceMotion.updateFlip()
        calcAffine();
        changed = 1;
    }
    
    return changed;
}
```

### 3.2.1 AffineSourceMotion.updateFlip(delta) — 停止检测逻辑

**这是动画停止检测的核心函数**。从字节码反汇编确认:

```javascript
// AffineSourceMotion.updateFlip() (bytecode line 1014):
function updateFlip(delta) {
    _interval += delta;     // 累积帧间隔(毫秒)
    
    if (_storageType == "emote") {
        // Emote路径: 检查animating
        if (!_player.animating && _lastPlaying) {
            onAnimationStop();              // emote动画停止
        }
        // onSync处理...
        _lastPlaying = _player.animating;
    } else {
        // ★ Motion路径 (yuzulogo.mtn走这里):
        if (!_player.playing && _lastPlaying) {
            // playing从true变false → 动画结束!
            onMotionStop();                 // 触发停止回调
        }
        _lastPlaying = _player.playing;     // 记录本帧状态
    }
}
```

**关键机制**: 每帧通过比较`_player.playing`(C++ getter, 读player+1099)与`_lastPlaying`(TJS变量)来检测**边沿触发** — 只在`playing`从true→false的那一帧触发`onMotionStop()`。

### 3.2.2 onMotionStop() — 停止通知链

```javascript
// AffineSourceMotion.onMotionStop() (bytecode line 1108):
function onMotionStop() {
    if (_playing) {
        _playing = 0;                       // 清除TJS层播放标志
        notifyOwner("onMotionStop");        // 通知AffineLayer
    }
    notifyOwner("onMovieStop");             // ★ 通知KAGEnvImage → 解除waitmovie等待
}
```

### 3.3 AffineLayer.onPaint() → 渲染

```javascript
// AffineLayer.onPaint() (bytecode line 775):
function onPaint() {
    super.onPaint();   // KAGLayer.onPaint
    
    if (_doAffine) {
        if (_image !== void) {
            var mtx = new AffineMatrix();
            _image.calcMatrix(mtx);          // 计算仿射矩阵
            _image.updateImage();            // 更新图像(仅在需要时)
            
            if (_rasterTime !== void) {
                // raster路径: 通过临时Layer做raster效果
                if (_rasterLayer === void) {
                    _rasterLayer = new Layer(window, this);
                    _rasterLayer.setSize(KAGLayer.width, KAGLayer.height);
                }
                _rasterLayer.completionType = ...;
                _rasterLayer.drawAffine(...);     // 渲染到rasterLayer
                // 应用raster变形...
                KAGLayer.copyRaster(_rasterLayer, ...);
            } else {
                // ★ 标准路径: 直接调用drawAffine
                _image.drawAffine(this, mtx);     // → AffineSourceMotion.drawAffine()
            }
            
            // clipImage处理(如果设置了clip)
            if (_clipImage !== void) {
                KAGLayer.clipAlphaRect(...);
            }
        }
        _doAffine = 0;
        callOnPaint = 0;
    }
}
```

### 3.4 AffineSourceMotion.drawAffine(target, mtx) — 外层渲染逻辑

从字节码反汇编确认的**完整drawAffine逻辑**:

```javascript
// AffineSourceMotion.drawAffine() (bytecode line 2648):
function drawAffine(owner, mtx) {
    if (_player === void || _player.motion == "") return;
    
    var name = this.name;
    var neutralColor = this.neutralColor;
    var originalTarget = owner;             // 保存原始owner
    var type = this.type;
    
    // ★ 选择渲染目标:
    if (_useD3D) {
        // D3D路径: 使用motionD3DAdaptor
        owner = _window.motionD3DAdaptor;
        owner.clearEnabled = 1;
    } else if (owner === originalTarget && _motionSeparateAdaptor !== void
               && _redrawList.count == 0) {
        // SLA路径: 使用SeparateLayerAdaptor
        owner = _motionSeparateAdaptor;
    } else {
        // ★ 标准路径 (yuzulogo.mtn走这里, useD3D=0, 无SLA):
        owner = _window.motionWorkLayer;    // 获取工作Layer
        var w = owner;
        w.setClip(0, 0, w.width, w.height);
        w.fillRect(0, 0, w.width, w.height, neutralColor);  // 用neutralColor填充
    }
    
    // 设置Layer属性
    owner.ltAlpha = type;                   // Layer type
    
    // 清除Player缓冲
    if (_player.clear !== void) {
        _player.clear();
    }
    
    // ★ 核心: 调用_drawAffine
    _drawAffine(mtx, null, owner);          // → 步骤3.5
    
    // 处理子图像层
    var subLayers = getSubImageLayers();
    if (subLayers !== void) {
        for (var i = 0; i < subLayers.count; i++) {
            var sub = subLayers[i];
            var subImage = sub._image;
            if (subImage instanceof AffineSourceMotion) {
                var subMtx = new AffineMatrix();
                sub.calcMatrix(subMtx);
                subImage.updateImage();
                subImage._drawAffine(subMtx, null, owner);
            }
        }
    }
    
    // ★ 渲染结果拷贝到实际显示Layer:
    if (owner instanceof D3DAdaptor) {
        // D3D路径: captureCanvas → motionWorkLayer → assignImages
        var workLayer = _window.motionWorkLayer;
        owner.captureCanvas(workLayer);
        owner.unloadUnusedTextures();
        _redrawImage();
        originalTarget.assignImages(workLayer);   // Layer.assignImages
    } else if (owner instanceof Layer) {
        // ★ 标准路径 (yuzulogo.mtn):
        _redrawImage();
        originalTarget.assignImages(owner);       // 将workLayer内容拷贝到AffineLayer
    }
}
```

### 3.5 AffineSourceMotion._drawAffine(mtx, revmtx, target) — 内层渲染

**这是progress、setDrawAffineTranslateMatrix、draw三个C++调用的集中点:**

```javascript
// AffineSourceMotion._drawAffine() (bytecode line 2648):
function _drawAffine(mtx, revmtx, target) {
    if (_player === void || _player.motion == "") return;
    
    if (_storageType == "emote") {
        // Emote路径: setCoord, setRotate, setScale等
        // ... (省略emote细节)
        _player.progress(0);
        _player.setDrawAffineTranslateMatrix(m11, m21, m12, m22, m14, m24);
    } else {
        // ★ Motion路径 (yuzulogo.mtn走这里):
        
        // 1. 推进时间线
        if (_interval > 0) {
            _player.progress(_interval);    // C++: Player_progressCompat (0x6D2A98)
            _interval = 0;
        } else {
            _player.progress(0);            // 零进度调用(仍触发updateLayers等)
        }
        
        // 2. 计算仿射变换矩阵
        //    从mtx提取transformAreaX/Y数组:
        var ax = mtx.transformAreaX;        // [x0, x1, x2] 三个变换点
        var ay = mtx.transformAreaY;        // [y0, y1, y2]
        var ox = ax[0], oy = ay[0];         // 原点
        var dx = ax[1] - ox;                // X轴方向
        var ex = ax[2] - ox;                // 额外X分量
        var dy = ay[1] - oy;                // Y轴方向
        var ey = ay[2] - oy;               // 额外Y分量
        
        // 3. 设置Player仿射矩阵
        _player.setDrawAffineTranslateMatrix(dx, ey, ex, dy, ox, oy);
        //  → C++: Player_setDrawAffineTranslateMatrix (0x6D4F14)
        //  → 存储6个double到player+808~844
        
        // 4. 计算逆矩阵(用于碰撞检测等)
        var det = dx * dy - ey * ex;
        revmtx = new Dictionary();
        revmtx.a = dy / det;
        revmtx.b = -ex / det;
        revmtx.c = -ey / det;
        revmtx.d = dx / det;
        revmtx.tx = (ex * oy - ox * dy) / det;
        revmtx.ty = (ox * ey - dx * oy) / det;
    }
    
    // 5. 设置completionType
    _player.completionType = _completionType;
    
    // 6. ★ 核心渲染调用
    _player.draw(target);               // C++: Player_drawImpl (0x6D5FB8)
}
```

**关键发现: progress、setDrawAffineTranslateMatrix、draw是在同一个TJS函数中顺序调用的，不是分开的回调。**

---

## 4. C++层执行链路 (libkrkr2.so反编译)

### 4.1 Player NCB注册

模块注册入口: `motionplayer_ncb_register` (0x6D9B08)
Player成员注册: `D3DPlayer_registerNCB_guess` (0x6D69C8, 10800字节, 92个属性/方法)

| TJS方法名 | C++函数地址 | 说明 |
|-----------|------------|------|
| `play` | `0x6D2C08` (raw callback) → `0x6B21E8` | 播放timeline |
| `progress` | `0x6D2A98` (raw callback) → `0x6C106C` | 推进时间线 |
| `draw` | `0x6D5FB8` (raw callback) | 渲染到Layer/D3DAdaptor/SLA |
| `stop` | `0x6D9A30` | 停止播放(仅设置playing=false) |
| `motion` (setter) | `0x6C1B20` → `0x6B21E8`(flags=0) | 设置motion文件 |
| `motion` (getter) | `0x6D9544` | 读取player+976处的ttstr |
| `playing` (getter) | `0x6D9794` | 读取player+1099处的byte |
| `allplaying` (getter) | `0x6CCE34` | 递归检查所有子Player |
| `chara` | via NCB | 存储在player+968 |
| `setDrawAffineTranslateMatrix` | `0x6D4F14` | 存6个double到player+808~844 |
| `clear` | via NCB | 清除渲染缓冲 |
| `completionType` | via NCB | 设置混合类型 |
| `captureCanvas` | via `0x6AD92C` (D3DAdaptor) | 捕获画布 |

### 4.2 Player_loadMotion_guess (0x6B0F10) — 加载MTN文件

从IDA反编译确认的完整加载流程:

```c
void Player_loadMotion(NativePlayer *player, ttstr *chara, ttstr **motion, result *out) {
    // 1. 创建参数Dictionary, 设置chara和motion属性
    iTJSDispatch2 *params = TJSCreateDictionaryObject();
    params->PropSet("chara", chara);
    params->PropSet("motion", motion);
    
    // 2. 调用TJS对象的onFindMotion回调
    //    → TJS层的AffineSourceMotion.onFindMotion()
    //    → 返回修改后的Dictionary(可能改变chara/motion路径)
    iTJSDispatch2 *objthis = player->objthis;  // player+16
    objthis->FuncCall("onFindMotion", out, 1, &params);
    
    // 3. 从回调结果中读取实际的chara和motion值
    ttstr actualChara = out->PropGet("chara");   // 可能被onFindMotion修改
    ttstr actualMotion = out->PropGet("motion");
    
    // 4. 构建资源路径: "motion/" + chara + "/" + motion
    ttstr path = "motion/" + actualChara + "/" + actualMotion;
    
    // 5. 通过ResourceManager.findMotion加载PSB
    //    player+992 = resourceManager引用
    //    player+1012 = projectRef引用
    iTJSDispatch2 *resourceMgr = player->resourceManager;  // player+992
    resourceMgr->FuncCall("findMotion", out, 2, [projectRef, path]);
    //    → ResourceManager_loadResource (0x6A8D8C)
    //    → 读取PSB文件, 检查id="motion", spec="krkr"/"win"
    //    → 解析timeline/layer树
}
```

### 4.3 Player_playImpl (0x6B21E8) — play/setMotion分发器

```c
void Player_playImpl(NativePlayer *player, uint32_t flags, tTJSVariant **params) {
    if ((flags & 0x10) && !player->project) {
        // flags=0x10: 设置stealth motion key (player+768)
        player->motionKey = *params;
    } else {
        // 实际加载: 调用Player_setMotionImpl
        Player_setMotionImpl(player, params, flags);  // 0x6B2284
        
        // 如果有待处理的stealth motion:
        if (player->motionKey) {
            Player_setMotionImpl(player, &player->motionKey, 0x10);
            player->motionKey = NULL;
        }
    }
}
```

### 4.4 Player_progressCompat (0x6D2A98) — 时间推进

```c
// TJS raw callback
int Player_progressCompat(tTJSVariant *result, int numParams, tTJSVariant **params,
                          iTJSDispatch2 *objthis) {
    NativePlayer *player = getNativeInstance(objthis);
    double deltaMs = params[0]->AsReal();
    
    // 毫秒 → 60fps帧数: delta * 60.0 / 1000.0
    double deltaFrames = deltaMs * 60.0 / 1000.0;
    
    // 四步处理:
    Player_progress_inner(player, deltaFrames);   // 推进currentTime
    Player_updateLayers(player);                   // 重算Layer变换矩阵
    Player_calcBounds(player);                     // 更新包围盒
    Player_dispatchEvents(player, objthis);        // 触发onAction/onSync回调
}
```

### 4.4.1 Player_progress_inner (0x6C106C) — 核心时间推进

```c
void Player_progress_inner(NativePlayer *player, double deltaFrames) {
    double speed    = *(double*)(player + 1168);  // 播放速度
    double actualDelta = speed * deltaFrames;      // speed在这里乘入
    *(double*)(player + 592) = actualDelta;        // frameDelta
    
    double currentTime = *(double*)(player + 1120);
    double lastTime    = *(double*)(player + 1128); // motion结束时间
    double loopTime    = *(double*)(player + 1136); // 循环点(负数=不循环)
    
    currentTime += actualDelta;
    
    if (currentTime > lastTime) {
        if (loopTime >= 0) {
            // 循环模式: 从loopTime重新开始
            while (currentTime >= lastTime)
                currentTime = currentTime + loopTime - lastTime;
        } else {
            // 非循环模式: ★ 设置playing=false
            *(byte*)(player + 1099) = 0;    // playing = false
        }
    }
    *(double*)(player + 1120) = currentTime;
}
```

**关键**: 当`currentTime > lastTime`且`loopTime < 0`时，C++层设置`playing = false`。TJS层在下一帧的`updateFlip()`中通过polling检测到这个变化。

### 4.5 Player_dispatchEvents (0x6C4490) — 事件分发

```c
void Player_dispatchEvents(NativePlayer *player, iTJSDispatch2 *objthis) {
    // 遍历player+936~944处的事件队列:
    for (event in eventQueue(player+936, player+944)) {
        if (event->type == 0) {
            // ★ onAction事件: 携带两个参数
            objthis->FuncCall("onAction", 2, [event->param1, event->param2]);
        } else if (event->type == 1) {
            // onSync事件: 无参数
            objthis->FuncCall("onSync");
        }
    }
}
```

**注意**: C++层只分发`onAction`和`onSync`，不直接分发`onMotionStop`。停止检测完全在TJS层的`updateFlip()`中通过polling `_player.playing`属性实现。

### 4.6 Player_setDrawAffineTranslateMatrix (0x6D4F14)

```c
void Player_setDrawAffineTranslateMatrix(NativePlayer *player,
    double m11, double m21, double m12, double m22, double m14, double m24) {
    // 存储6个仿射矩阵值到player结构体:
    *(double*)(player + 808) = m11;   // X轴缩放/旋转
    *(double*)(player + 816) = m21;   // Y轴剪切
    *(double*)(player + 824) = m12;   // X轴剪切
    *(double*)(player + 832) = m22;   // Y轴缩放/旋转
    *(double*)(player + 840) = m14;   // X平移
    *(double*)(player + 848) = m24;   // Y平移(注: 实际偏移可能有变化)
}
```

### 4.7 Player_drawImpl (0x6D5FB8) — 核心渲染

```c
void Player_drawImpl(NativePlayer *player, tTJSVariant *param) {
    
    // Step 1: 检查param是否为D3DAdaptor (NIS classID检查)
    if (param is D3DAdaptor) {              // dword_1AB8820
        player->d3dDrawMode = true;          // player+909 = 1
        Player_drawToD3DAdaptor_guess(player);  // 0x6D5B90
        return;
    }
    
    // Step 2: 检查param是否为SeparateLayerAdaptor
    if (param is SeparateLayerAdaptor) {    // dword_1AB87F8
        Player_DrawSLA(player, param);       // 0x6D5658
        return;
    }
    
    // Step 3: ★ 标准Layer路径 (yuzulogo.mtn走这里)
    //         解析param为Layer对象
    if (Player_resolveLayerAndBitmap(player, &layerInfo, &bitmapInfo)) {
        
        if (player->d3dDrawMode) {
            // D3D→Layer fallback路径
            D3DAdaptor *adaptor = getOrCreateD3DAdaptor();
            layer.setSize(adaptor->width, adaptor->height);
            layer.visible = 1;
            D3DAdaptor_renderFromPlayer_guess(adaptor, player, &layerInfo);
            D3DAdaptor_captureCanvas(adaptor, param);
            layer.Release();
        } else {
            // ★ 纯Layer路径:
            
            // 3a. 应用平移偏移
            Player_applyTranslateOffset_guess(player, &layerInfo);  // 0x6D5264
            
            // 3b. ★★ 核心渲染 — 遍历所有可见层，对每个源图应用仿射变换
            Player_renderToCanvas_guess(player, param, &layerInfo, &bitmapInfo);
            //     → 0x6C7440: 大型渲染函数(~62KB)
            //     → 遍历flattenedLayerNodes(在loadMotion时构建)
            //     → 对每个node: 
            //        1. 从PSB获取源图(textureSrc)
            //        2. 计算该node的仿射变换(位置+缩放+旋转+父变换)
            //        3. 结合setDrawAffineTranslateMatrix的全局矩阵
            //        4. 调用Layer.OperateAffine()合成到目标Layer
            
            // 3c. 更新Layer
            Player_updateLayerAfterDraw_guess(player, param);  // 0x6CE7D8
        }
    }
}
```

### 4.7.1 Player_renderToCanvas_guess (0x6C7440) — 渲染核心

这是一个约62KB的大函数，是最核心的渲染逻辑:

1. **获取渲染参数** — 从param中提取Layer对象引用
2. **遍历flattenedLayerNodes** — 在loadMotion时已扁平化好的渲染节点列表
3. **对每个可见节点**:
   - 从PSB内嵌资源或外部文件获取源图像
   - 计算节点局部仿射变换矩阵(位置、缩放、旋转)
   - 与父节点的变换矩阵级联
   - 与`setDrawAffineTranslateMatrix`设置的全局矩阵组合
   - 调用`Layer.OperateAffine()`将源图合成到目标Layer
4. **应用alpha/blend模式** — 根据节点属性设置混合方式

### 4.7.2 Player_updateLayerAfterDraw_guess (0x6CE7D8) — 后处理

```c
void Player_updateLayerAfterDraw_guess(NativePlayer *player, tTJSVariant *param) {
    if (*(player + 613)) {                       // 某个标志位
        sub_6CE19C(player, param);               // 创建/获取内部渲染Layer
        
        iTJSDispatch2 *internalLayer = *(player + 696);  // 内部渲染Layer
        
        // ★ assignImages: 将渲染结果从内部缓冲区传到display tree
        internalLayer->FuncCall("assignImages", 1, [param]);
    }
}
```

### 4.8 Player_DrawSLA (0x6D5658) — SLA路径渲染

当`_motionSeparateAdaptor`存在时进入此路径:

```c
void Player_DrawSLA(NativePlayer *player, tTJSVariant *slaParam) {
    if (!sub_6D5164(player, &layerInfo, &bitmapInfo)) return;
    
    Player_applyTranslateOffset_guess(player, &layerInfo);
    
    if (!byte_1AB84F4) {  // ogl_accurate_render配置
        // 标准路径: 直接渲染到SLA的目标Layer
        Layer *target = Player_ResolveSLATarget_guess(slaParam);
        int w = target->clipRect.width;
        int h = target->clipRect.height;
        Player_RenderMotionFrame_guess(target, w, h, player, &layerInfo, &bitmapInfo);
        Layer_UpdateRect_guess(target, 0);       // 触发显示更新
    } else {
        // 精确渲染路径(OpenGL)
        sub_6C9CA8(player, slaParam, &layerInfo, &bitmapInfo);
        sub_6CE938(player, slaParam + 20);
    }
}
```

---

## 5. 动画结束和waitmovie等待机制

### 5.1 动画结束检测 — 完整链路

```
C++层 (每帧):
    Player_progress_inner (0x6C106C)
        → currentTime += speed * deltaFrames
        → if (currentTime > lastTime && loopTime < 0):
            ★ playing = false (player+1099 = 0)
            
TJS层 (每帧, updateFlip中):
    AffineSourceMotion.updateFlip()
        → 检测 !_player.playing && _lastPlaying
        → 边沿触发: onMotionStop()
            → _playing = 0
            → notifyOwner("onMotionStop")
            → notifyOwner("onMovieStop")       ← 通知KAGEnvImage
```

### 5.2 [ev waitmovie] 等待机制

```javascript
// waitmovie tag handler (bytecode line 3993):
function() {
    syncMode = 1;       // 启用同步模式
    updateFlag = 1;     // 标记需要更新
    waitMode = 2;       // 设置等待模式=2 (movie等待)
}
```

KAG conductor在每帧检查KAGEnvImage的等待状态:
- `waitMode == 2` → conductor进入等待循环
- 每帧检查`canWaitMovie()`的返回值
- 当`canWaitMovie()`返回0(falsy)时，conductor恢复执行

### 5.3 canWaitMovie — 关键发现

```javascript
// AffineSourceMotion.canWaitMovie() (bytecode line 1142):
function canWaitMovie() {
    if (_player !== void) {
        if (_storageType == "emote") {
            return _playing;    // emote类型: 返回_playing状态
        }
        return 0;              // ★ motion类型(.mtn): 永远返回0!
    }
}
```

**关键发现**: 原始代码中，对于motion类型(非emote)，`canWaitMovie()`**永远返回0**。这意味着:

1. `[ev waitmovie]`对于.mtn文件**默认不会等待** — conductor会立即通过
2. 但实际游戏中logo确实播放了 — 说明等待机制可能在更上层(KAGEnvImage.waitMode处理)或通过`onMovieStop`回调来控制
3. 本地web移植中在ScriptMgnIntf.cpp中override canWaitMovie是**有意义的设计决策**

---

## 6. 完整调用链总结

```
═══════════════════════════════════════════════════════════════════
[ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]
═══════════════════════════════════════════════════════════════════

Phase 1: 初始化 (一次性)
─────────────────────────────────────────────────────────────────
KAG Conductor
  └→ KAGEnvironment.onTag("ev", {storage:"yuzulogo.mtn", chara:"LOGO", motion:"yuzulogo", notrans:true})
      └→ KAGEnvImage._setOptions(params)
          ├→ KAGEnvImage.updateImageSource()
          │   ├ Storages.extractStorageExt("yuzulogo.mtn") → ".MTN"
          │   ├ extSourceMap[".MTN"] → AffineSourceMotion class
          │   └ imageSource = new AffineSourceMotion() / 复用已有
          │
          ├→ _player = new Motion.Player()           [C++ NCB对象创建]
          │
          ├→ _player.chara = "LOGO"                  [设置角色标识]
          │
          ├→ _player.motion = "yuzulogo.mtn"         [设置motion → 触发加载]
          │   └→ Player_setMotion_NCBWrapper (0x681CAC)
          │       └→ Player_playImpl (0x6B21E8, flags=0)
          │           └→ Player_setMotionImpl (0x6B2284)
          │               └→ Player_loadMotion_guess (0x6B0F10)
          │                   ├ objthis.onFindMotion({chara:"LOGO", motion:"yuzulogo.mtn"})
          │                   ├ 构建路径: "motion/LOGO/yuzulogo.mtn"
          │                   ├ ResourceManager.findMotion(project, path)
          │                   │   └→ ResourceManager_loadResource (0x6A8D8C)
          │                   │       └ 读取PSB文件, 校验id="motion", spec="krkr"/"win"
          │                   ├ 解析PSB → 建立timeline/layer树
          │                   └ Player_initNonEmoteMotion_guess (0x6B365C)
          │                       └ 初始化非emote类型(type=0)
          │
          ├→ _player.play("yuzulogo")                [播放指定timeline]
          │   └→ Player_play_NCBWrapper (0x67F40C)
          │       └→ Player_playImpl (0x6B21E8, flags with label)
          │           └ timeline["yuzulogo"].playing = true
          │             timeline["yuzulogo"].currentTime = 0
          │
          ├→ onMotionStart()                         [TJS回调]
          │   ├ _playing = 1
          │   ├ _lastPlaying = 1
          │   └ 遍历tags → entryDelay注册定时action
          │
          └→ notrans处理: trans = {method:"", time:0}  [直接显示,无过渡]

AffineLayer.entryFlip()                              [注册帧更新]
  └→ window.entryFlipLayer(this)                     [加入Window的flip列表]

─────────────────────────────────────────────────────────────────
Phase 2: 每帧渲染循环 (持续)
─────────────────────────────────────────────────────────────────

Cocos2D mainLoop → Director::drawScene → TVPDeliverContinuousEvent
  │
  ├→ Window.flip()
  │   └→ AffineLayer.updateFlip(deltaMs)
  │       ├ delta *= speed                           [应用AffineLayer速度]
  │       └→ AffineSourceMotion.updateFlip(delta)
  │           ├ _interval += delta                   [累积时间间隔]
  │           └ 停止检测:
  │             if (!_player.playing && _lastPlaying):
  │               → onMotionStop()                   [见Phase 3]
  │             _lastPlaying = _player.playing
  │
  └→ AffineLayer.onPaint()
      ├ mtx = new AffineMatrix()
      ├ _image.calcMatrix(mtx)                       [计算仿射矩阵]
      └→ AffineSourceMotion.drawAffine(this, mtx)    [外层渲染]
          │
          ├ 选择渲染目标: target = _window.motionWorkLayer  [非D3D标准路径]
          ├ target.fillRect(neutralColor)                    [清空工作Layer]
          ├ _player.clear()                                  [清除Player缓冲]
          │
          ├→ _drawAffine(mtx, null, target)                  [内层渲染]
          │   │
          │   ├→ _player.progress(_interval)                 [C++: 推进时间]
          │   │   └→ Player_progressCompat (0x6D2A98)
          │   │       ├ deltaFrames = deltaMs * 60.0 / 1000.0
          │   │       ├→ Player_progress_inner (0x6C106C)
          │   │       │   ├ actualDelta = speed * deltaFrames
          │   │       │   ├ currentTime += actualDelta
          │   │       │   └ if (currentTime > lastTime && loopTime < 0):
          │   │       │       ★ playing = false (player+1099)
          │   │       ├→ Player_updateLayers (0x6BB33C)       [重算变换]
          │   │       ├→ Player_calcBounds (0x6C3D04)         [更新包围盒]
          │   │       └→ Player_dispatchEvents (0x6C4490)     [分发onAction/onSync]
          │   │
          │   ├→ _player.setDrawAffineTranslateMatrix(dx,ey,ex,dy,ox,oy)
          │   │   └→ (0x6D4F14) 存储6个double到player+808~848
          │   │
          │   └→ _player.draw(target)                        [C++: 渲染]
          │       └→ Player_drawImpl (0x6D5FB8)
          │           ├ (非D3D, 非SLA → 纯Layer路径)
          │           ├→ Player_applyTranslateOffset_guess (0x6D5264)
          │           ├→ Player_renderToCanvas_guess (0x6C7440)  [62KB大函数]
          │           │   ├ 遍历flattenedLayerNodes
          │           │   ├ 对每个可见node:
          │           │   │   ├ 获取源图(PSB内嵌资源/外部文件)
          │           │   │   ├ 计算节点仿射变换 × 全局矩阵
          │           │   │   └ Layer.OperateAffine() 合成到target
          │           │   └ 应用alpha/blend模式
          │           └→ Player_updateLayerAfterDraw_guess (0x6CE7D8)
          │               └ assignImages(param)              [内部Buffer→target]
          │
          └ originalTarget.assignImages(target)              [workLayer→AffineLayer]

[重复以上每帧循环]

─────────────────────────────────────────────────────────────────
Phase 3: 动画结束
─────────────────────────────────────────────────────────────────

C++: Player_progress_inner
  └ currentTime >= lastTime && loopTime < 0
    → playing = false (player+1099 = 0)

下一帧 → AffineSourceMotion.updateFlip():
  └ !_player.playing && _lastPlaying == true
    → onMotionStop()
        ├ _playing = 0
        ├ notifyOwner("onMotionStop")
        └ notifyOwner("onMovieStop")         → KAGEnvImage

═══════════════════════════════════════════════════════════════════
[ev waitmovie]
═══════════════════════════════════════════════════════════════════

KAG Conductor → KAGEnvImage.onTag("waitmovie")
  → syncMode = 1, updateFlag = 1, waitMode = 2
  → conductor进入等待循环
    → 每帧检查 canWaitMovie()
      → motion类型: 永远返回0 (★ 原始设计: 不等待motion)
    → conductor立即恢复执行
```

---

## 7. Player对象内存布局 (libkrkr2.so)

从反编译确认的关键偏移量:

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| +16 | ptr | objthis | TJS对象引用(回调期间设置) |
| +200 | ptr | rootLayer | 根层数据结构 |
| +456 | double | currentEvalTime | Layer求值时间 |
| +464 | ptr | savedTransform | emote模式保存的变换 |
| +472 | double | emoteAngle | emote角度偏移 |
| +480 | byte | looping | 是否循环 |
| +481 | byte | needsFirstFrame | 首帧未渲染 |
| +482 | byte | isEmoteType | 是emote类型(type==1) |
| +484 | variant | division | emote分割数据 |
| +504 | int | currentDivisionIndex | 当前emote分割索引 |
| +508 | variant | motionList | emote动画列表 |
| +528 | variant | source | 当前加载的motion源对象 |
| +548 | variant | priority | 动画优先级数据 |
| +592 | double | frameDelta | 本帧实际delta(speed*input) |
| +616 | variant | content | motion内容数据 |
| +696 | ptr | internalRenderLayer | 内部渲染Layer(assignImages用) |
| +768 | ttstr* | pendingStealth | 待处理的stealth motion |
| +808 | double | affineM11 | setDrawAffineTranslateMatrix值 |
| +816 | double | affineM21 | |
| +824 | double | affineM12 | |
| +832 | double | affineM22 | |
| +840 | double | affineM14 | X平移 |
| +848 | double | affineM24 | Y平移 |
| +909 | byte | d3dDrawMode | draw(D3DAdaptor)时设置 |
| +936 | ptr | eventQueueBegin | 事件队列起始 |
| +944 | ptr | eventQueueEnd | 事件队列结束 |
| +968 | ttstr* | chara | 角色名称 |
| +976 | ttstr* | motion | 当前motion名称 |
| +984 | ttstr* | stealthMotion | stealth motion名称 |
| +992 | variant | resourceManager | ResourceManager引用 |
| +1012 | variant | projectRef | Project引用 |
| +1072 | variant | tag | motion标签数据 |
| +1099 | byte | playing | 是否正在播放 |
| +1120 | double | currentTime | 当前时间线位置(帧) |
| +1128 | double | lastTime | motion结束时间(帧) |
| +1136 | double | loopTime | 循环重启点(帧，负数=不循环) |
| +1168 | double | speed | 播放速度倍率 |

---

## 8. 关键设计发现

### 8.1 动画驱动是TJS层控制、C++层执行

- **C++ Player类本身没有continuous handler/自驱动机制**
- 动画由外部(AffineLayer)在每帧调用`progress()`和`draw()`来驱动
- C++只负责: 时间推进、Layer变换计算、图像合成
- TJS负责: 调度驱动、停止检测、资源管理、Layer目标选择

### 8.2 停止检测是TJS层的边沿检测(polling)

- C++层在`progress_inner`中设`playing=false`，但**不主动通知TJS**
- TJS层在每帧`updateFlip`中polling `_player.playing`属性
- 通过`_lastPlaying`记录上一帧状态，实现**边沿触发**(只在true→false时触发一次)
- 停止通知链: `updateFlip` → `onMotionStop` → `notifyOwner("onMovieStop")`

### 8.3 progress/setMatrix/draw 的调用模式

三个C++方法在**同一个TJS函数**(`_drawAffine`)中顺序调用:
1. `_player.progress(delta)` — 推进时间(含updateLayers, dispatchEvents)
2. `_player.setDrawAffineTranslateMatrix(...)` — 设置全局仿射矩阵
3. `_player.draw(target)` — 渲染到目标Layer

**这不是三个独立回调，是同一调用栈中的顺序操作。**

### 8.4 canWaitMovie 对motion类型永远返回0

原始TJS代码中，`canWaitMovie()`对非emote类型(包括.mtn)直接返回0。这意味着`[ev waitmovie]`实际上不等待motion动画结束。等待机制可能通过更上层的KAGEnvImage.waitMode/onMovieStop回调来实现。

### 8.5 渲染目标选择优先级

drawAffine中的渲染目标选择逻辑(非D3D路径):
1. 如果`_motionSeparateAdaptor`存在且无redrawList → 使用SLA
2. 否则 → 使用`_window.motionWorkLayer`(临时工作Layer)
3. 渲染完成后通过`assignImages()`拷贝到实际的AffineLayer

---

## 附录A: IDA中已命名的函数

### Player核心方法
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6D9B08 | motionplayer_ncb_register | 模块NCB注册入口 |
| 0x6FDE74 | Player_ncb_classInit | Player类初始化 |
| 0x6D69C8 | D3DPlayer_registerNCB_guess | Player成员注册(92个方法/属性, 10.8KB) |
| 0x6D2C08 | Player_playCompat | play() TJS raw callback |
| 0x6D2A98 | Player_progressCompat | progress() TJS raw callback |
| 0x6D5FB8 | Player_drawImpl | draw() 核心实现 |
| 0x6D9A30 | Player_stop | stop() — 仅设置playing=false |
| 0x6C1B20 | Player_setMotion | motion property setter |
| 0x6D9544 | Player_getMotion_ncb | motion property getter |
| 0x6D9794 | Player_getPlaying | playing property getter |
| 0x6CCE34 | Player_getAllplaying | allplaying getter (递归) |

### Player内部逻辑
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6B21E8 | Player_playImpl | play/setMotion分发器 |
| 0x6B2284 | Player_setMotionImpl | motion加载实现 |
| 0x6C106C | Player_progress_inner | 时间线推进(speed*delta) |
| 0x6BB33C | Player_updateLayers | 更新所有Layer变换 |
| 0x6C3D04 | Player_calcBounds | 计算包围盒 |
| 0x6C4490 | Player_dispatchEvents | 分发onAction/onSync回调 |
| 0x6B0F10 | Player_loadMotion_guess | 加载motion(onFindMotion+findMotion) |
| 0x6B2E90 | Player_initEmoteMotion_guess | 初始化emote类型motion |
| 0x6B365C | Player_initNonEmoteMotion_guess | 初始化非emote类型motion |

### Player渲染链
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6C7440 | Player_renderToCanvas_guess | 渲染到画布(62KB大函数) |
| 0x6D5264 | Player_applyTranslateOffset_guess | 应用平移偏移 |
| 0x6CE7D8 | Player_updateLayerAfterDraw_guess | draw后更新Layer(assignImages) |
| 0x6D2D80 | Player_drawToLayerCompat | 非D3D路径Layer渲染 |
| 0x6D5B90 | Player_drawToD3DAdaptor_guess | D3D路径渲染 |
| 0x6D5658 | Player_DrawSLA | SLA路径渲染 |
| 0x6CBCE4 | Player_buildRenderTree_guess | 构建渲染树 |
| 0x6C72E4 | Player_evaluateTimelines_guess | 评估timeline状态 |
| 0x6AC27C | Player_resetRenderState_guess | 重置渲染状态 |
| 0x6D4F14 | Player_setDrawAffineTranslateMatrix | 存储6个仿射矩阵值 |
| 0x6D5164 | Player_sortRenderNodes_guess | 排序渲染节点 |

### D3DAdaptor
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6ACE94 | D3DAdaptor_registerNCB | NCB注册 |
| 0x6AD518 | D3DAdaptor_constructor | 构造函数(Window,x,y,w,h) |
| 0x6ADB10 | D3DAdaptor_init | 初始化(分配像素缓冲+创建纹理) |
| 0x6AD92C | D3DAdaptor_captureCanvas | 捕获画布(pixel→Layer/纹理) |
| 0x6ADE24 | D3DAdaptor_renderFromPlayer_guess | 从Player渲染到缓冲区 |

### ResourceManager
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6A8D8C | ResourceManager_loadResource | 加载PSB文件(检查id="motion", spec="krkr"/"win") |

### 其他
| 地址 | 名称 | 说明 |
|------|------|------|
| 0x6D004C | Player_findMotion | Player::findMotion(从Player调用) |
| 0x5E423C | MoviePlayer_registerNCB | 视频播放器NCB注册 |
| 0x67FAC8 | Player_registerNCB | Player类NCB注册(旧标记) |
| 0x67F40C | Player_play_NCBWrapper | play() NCB包装 |
| 0x6818D0 | Player_draw_NCBWrapper | draw() NCB包装 |
| 0x681CAC | Player_setMotion_NCBWrapper | motion setter NCB包装 |
| 0x681C88 | Player_getMotion | motion getter(旧标记) |

---

## 附录B: 分析方法和证据来源

| 分析内容 | 方法 | 证据 |
|----------|------|------|
| custom.ks脚本内容 | ksdec解密 + 文本分析 | FEFE编码UTF-16LE, line 89 |
| AffineSourceMotion TJS逻辑 | tjsdump字节码反汇编 | 编译后TJS2字节码 |
| AffineLayer TJS逻辑 | tjsdump字节码反汇编 | 编译后TJS2字节码 |
| KAGEnvImage TJS逻辑 | tjsdump字节码反汇编 | 编译后TJS2字节码 |
| Player C++实现 | IDA Pro MCP反编译 | libkrkr2.so arm64 |
| Player NCB注册 | IDA Pro MCP反编译 | 0x6D69C8, 10.8KB |
| Player内存布局 | IDA Pro MCP反编译 | 偏移量从多个函数交叉确认 |
| extSourceMap注册 | tjsdump字节码反汇编 | AffineSourceMotion.tjs顶层代码 |
| useD3D=0 | ksdec解密patch.tjs | 游戏根目录patch.tjs |
