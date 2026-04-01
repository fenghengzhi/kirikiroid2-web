# KAG AffineSourceMotion drawAffine 渲染链分析

通过解包游戏脚本（tjsdump反汇编TJS字节码）和反编译libkrkr2.so确定KAG如何驱动Logo动画渲染。

## 1. Logo动画的KAG触发流程

### 游戏脚本入口（custom.ks）
```
*logo_show
    [ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]
    [ev waitmovie]
```

### [ev] 标签执行链
`[ev]` 不是直接的tag handler，而是一个 `KAGEnvImage` 环境对象：
```
KAGEnvironment.execCommand("ev")
→ getCommandTarget("ev") → getEnvObject("ev") → KAGEnvImage
→ KAGEnvImage.doCommand(params)
→ setImageFile(params) → _setImageFile() → updateImageSource()
→ world.getImageSource(storage) → createAffineSource()
→ findAffineSource("yuzulogo.mtn") → 提取扩展名".MTN"
→ extSourceMap[".MTN"] = AffineSourceMotion （在AffineSourceMotion.tjs顶层注册）
→ new AffineSourceMotion(window, colorkey)
→ loadImages("yuzulogo.mtn") → _loadImages()
    → new MotionResourceManager() → loadResource()
    → _storageType = "motion"（因为扩展名是.mtn）
    → createPlayer() → new Motion.Player(resourceManager)
    → _setOptions({chara: "LOGO", motion: "yuzulogo"})
→ AffineSource 绑定到 AffineLayer
→ entryOwner(AffineLayer) → _owners.add(layer)
```

### [ev waitmovie] 等待流程
```
KAGEnvImage.doCommand("waitmovie")
→ EnvLayerObject.createWait(2)
→ AffineSourceMotion.canWaitMovie() → 返回 _playing
→ entryWait({ name: "movie_world_ev", stopfunc: "stopMovie" })
→ conductor 等待 trigger "movie_world_ev"
→ 动画结束时: onMotionStop() → notifyOwner("onMovieStop")
→ EnvLayerObject.onMovieStop() → world.trigger("movie_world_ev")
→ conductor 恢复执行
```

## 2. 逐帧渲染驱动机制

### 注册连续处理器
```
AffineLayer.entryFlip() → window.entryFlipLayer(this)
→ MainWindow.entryFlipLayer(layer)
→ flipLayers.add(layer)
→ flipStart()
→ System.addContinuousHandler(onFlipTimerInterval)
```

### 逐帧回调链
```
Cocos2D scene.update(delta)
→ Application->Run()
→ SystemWatchTimerTimer()
→ DeliverEvents() → TVPDeliverAllEvents()
→ if (TVPProcessContinuousHandlerEventFlag):
    TVPDeliverContinuousEvent()
    → 遍历 TVPContinuousHandlerVector
    → 调用 onFlipTimerInterval(tick)

onFlipTimerInterval(tick):
→ delta = tick - lastTick
→ updateFlipLayers(delta)
    → 遍历所有 flipLayers
    → AffineLayer.updateFlip(delta)
        → AffineSourceMotion.updateFlip(delta)
            → _interval = delta  // 存储本帧时间增量
            → 检查 playing/animating 状态变化
    → AffineLayer.calcAffine()
        → _doAffine = true
        → _updateRegion.update() // 标记脏区域

→ KiriKiri2 渲染系统检测到脏区域
→ AffineLayer.onPaint()
    → KAGLayer.onPaint()  // 基类
    → if (_doAffine && _image != void):
        mtx = new AffineMatrix()
        _image.calcMatrix(mtx)
        _image.updateImage()
        // 处理 raster 特效（如果有）
        drawAffine()  // 调用自身的drawAffine方法
    → _doAffine = 0
    → callOnPaint()
```

## 3. AffineLayer.drawAffine() 字节码分析

```javascript
// AffineLayer.drawAffine() 伪代码
function drawAffine() {
    if (_doAffine > 1) doUpdate = true;

    if (type == "ltBinder") {
        region = doUpdate ? void : _updateRegion;
        _image.drawAffine(region);  // → AffineSourceMotion.drawAffine(region)
    } else {
        setClip(...);
        if (doUpdate) { _clearRegion.clearTarget(); _clearRegion.clear(); }
        // 设置clip区域（如果有_clip）
        region = doUpdate ? void : _updateRegion;
        _image.drawAffine(region);  // → AffineSourceMotion.drawAffine(region)
        if (revmtx) { _clearRegion.update(); _updateRegion.clear(); }
    }
}
```

## 4. AffineSourceMotion.drawAffine() 字节码分析

```javascript
// AffineSourceMotion.drawAffine(target) 伪代码
function drawAffine(target) {
    if (_player == void) return;        // Guard 1
    if (_player.motion == "") return;   // Guard 2: motion属性必须非空！

    var name = this.name;
    var neutralColor = target.neutralColor;
    var drawTarget = target;  // 保存原始target
    var type = target.type;

    // 确定渲染目标
    if (_useD3D) {
        target = _window.motionD3DAdaptor;
        target.clearEnabled = 1;
    } else if (target == void && _motionSeparateAdaptor !== undefined
               && _redrawList.count == 0) {
        target = _motionSeparateAdaptor;
    } else {
        target = _window.motionWorkLayer;
        target.setClip(0, 0, target.width, target.height);
        target.fillRect(0, 0, target.width, target.height, neutralColor);
        drawTarget.type = "ltAlpha";
    }

    // 清除和绘制
    if (_player.clear !== undefined) _player.clear();
    _drawAffine(mtx, revmtx, target);  // ← 核心绘制

    // 后处理：将渲染结果传输到显示层
    if (target instanceof D3DAdaptor) {
        captureCanvas(...);
        unloadUnusedTextures();
        _redrawImage(...);
        Layer.assignImages.call(drawTarget, motionWorkLayer);
    } else if (target instanceof Layer) {
        _redrawImage(...);
        Layer.assignImages.call(drawTarget, target);
    }

    drawTarget.type = type;  // 恢复type
}
```

## 5. AffineSourceMotion._drawAffine() 字节码分析

```javascript
// AffineSourceMotion._drawAffine(mtx, revmtxDst, src) 伪代码
function _drawAffine(mtx, revmtxDst, src) {
    if (_player == void) return;        // Guard 1
    if (_player.motion == "") return;   // Guard 2

    if (_storageType == "emote") {
        // Emote特有：设置坐标、旋转、缩放
        _player.setCoord(src._imagex, src._imagey);
        if (_imagerotate) _player.setRotate(-_imagerotate * 2 * PI / 360);
        scale = _emotescale * (_imagezoom || 1) * (100 / resolutionx);
        _player.setScale(scale);
        _player.meshDivisionRatio = calcParam(_meshdivisionratio);
        _player.bustScale = calcParam(_bustscale);
        _player.hairScale = calcParam(_hairscale);
        _player.partsScale = calcParam(_partsscale);
    }

    // ===== 以下代码 emote 和 motion 共享 =====
    moveProgress(_interval);

    if (_interval > 0) {
        result = _player.progress(_interval);
        _interval = 0;
    } else {
        result = _player.progress(0);
    }

    if (result) {
        // progress返回truthy → 使用mtx中的矩阵
        _player.setDrawAffineTranslateMatrix(mtx.m11, mtx.m21, mtx.m12,
                                              mtx.m22, mtx.m14, mtx.m24);
    } else {
        // progress返回falsy → 计算transformArea
        var ax = mtx.transformAreaX([1, 1]);
        var ay = mtx.transformAreaY([1, 1]);
        // ... 计算逆矩阵 revmtx ...
        _player.setDrawAffineTranslateMatrix(...);
        revmtxDst.revmtx = { a, b, c, d, tx, ty };  // 存储逆矩阵
    }

    _player.completionType = _completionType;
    _player.draw();  // ← 最终调用C++ Player::draw()，无参数！
}
```

## 6. libkrkr2.so中Player::draw（sub_6D5FB8）的实现

**关键发现：libkrkr2.so中draw()不需要参数，从内部状态解析目标。**

```c
// sub_6D5FB8 — Player::draw(params)
void Player_draw(Player *self, tTJSVariant *params) {
    // Step 1: 检查参数是否是D3DAdaptor
    obj = params[0];  // 即使params为void也不崩溃，obj=null
    if (obj && obj.propGet("interface") succeeds && has D3DAdaptor) {
        self->_d3dDrawMode = true;
        sub_6D5B90(self);  // D3DAdaptor渲染路径
        return;
    }

    // Step 2: 检查参数是否是SeparateLayerAdaptor
    if (obj && obj.propGet("ownerDisp") succeeds && has SLA) {
        sub_6D5658(self);  // SLA渲染路径 → 渲染到owner Layer
        return;
    }

    // Step 3: 从内部状态解析目标
    if (sub_6D5164(self, &layerArr, &rectArr)) {
        if (self->_d3dDrawMode) {
            // D3DAdaptor路径：创建全局D3DAdaptor，渲染到它
            d3d = getOrCreateGlobalD3DAdaptor();
            d3d.setSize(w, h);
            d3d.visible = true;
            sub_6ADE24(d3d, self, layerArr);  // 渲染
            sub_6AD92C(d3d, target);           // 绘制到目标
        } else {
            // 直接Layer渲染路径
            sub_6D5264(self, layerArr);        // 渲染到layer数组
            sub_6C7440(self, params, layerArr, rectArr);  // 合成
            sub_6CE7D8(self, params);          // 后处理
        }
    }
}
```

## 7. 问题分析：为什么drawCompat收到0次调用

### TJS调用链验证
从字节码分析确认：
1. `_drawAffine()` 在最后调用 `_player.draw()` — **无参数**
2. `_player` 是 `Motion.Player` 的NCB实例
3. `draw` 在NCB中注册为 `NCB_METHOD_RAW_CALLBACK(draw, &Player::drawCompat, 0)`

### 当前drawCompat实现问题
```cpp
// Player.cpp:3085
tjs_error Player::drawCompat(...) {
    LOGGER->warn("drawCompat CALLED: ...");  // 从未打印！

    if(numparams < 1 || !param[0] || ...) {
        return TJS_S_OK;  // 无参数时静默返回
    }
    // ...
}
```

`drawCompat CALLED` 从未打印，意味着C++方法从未被调用。

### 可能的原因
1. **Guard 2失败**: `_player.motion == ""` → `_drawAffine`提前返回
   - `getMotion()` 返回 `_motionKey`，如果play时没正确设置，会为空
2. **更上游断裂**: `drawAffine`未被调用（`_doAffine`未被设置，或onPaint未触发）
3. **异常**: `_player.progress()` 或其他TJS方法调用抛出异常，被上层catch静默处理
4. **NCB方法分发失败**: `draw`方法未正确注册或查找失败

## 8. 自动化调试发现

### 通过 playwright-cli 自动化调试确认

1. **`TVPMainScene::update()` 正常运行**: frame=1,2,3,...,300... 持续递增
2. **`TVPDeliverAllEvents()` 每帧调用**: 但 `TVPProcessContinuousHandlerEventFlag` **始终为0**
3. **`doStartup` 正常完成**: `StartApplication` 返回，`scheduleUpdate()` 已调用
4. **事件循环完整运行**: 但因为headless Chrome无法点击年龄验证界面，游戏停在age verification阶段
5. **`TVPAddContinuousHandler` 从未调用**: 因为年龄验证未通过，Logo动画未开始
6. **spdlog/printf 的输出不会出现在浏览器控制台**: 必须使用 `EM_ASM({ console.warn(...) })` 才能被playwright捕获

### 关键发现：输出通道问题
- `spdlog::warn()` → stderr → **不被playwright捕获**
- `printf()` / `fprintf(stderr)` → **不被playwright捕获**（Emscripten pthread环境下stdout/stderr缓冲问题）
- `EM_ASM({ console.warn(...) })` → console.warn → **被playwright捕获** ✅
- 游戏本身的日志使用 `EM_ASM({ console.warn(...) })` 输出

## 9. 连续处理器机制确认

### TVPContinousHandlerLimitFrequency
默认值为0（无频率限制），走 `BeginContinuousEvent` 路径：
- `TVPBeginContinuousEvent()` → `TVPSystemControl->BeginContinuousEvent()`
- 设置 `ContinuousEventCalling = true`
- 此后每帧 `DeliverEvents()` 都会设置 `TVPProcessContinuousHandlerEventFlag = true`
- `TVPDeliverAllEvents()` 检测到flag → 调用 `TVPDeliverContinuousEvent()` → 所有注册的handler被调用

### 事件循环确认
```
Cocos2D scene.update(delta)
→ Application->Run()
→ ProcessMessages() + SystemWatchTimerTimer()
→ DeliverEvents()
    → if(ContinuousEventCalling) flag = true   ← 仅当有handler注册后
    → if(EventEnable) TVPDeliverAllEvents()     ← EventEnable默认true
→ TVPDeliverAllEvents()
    → 处理事件队列
    → if(flag) TVPDeliverContinuousEvent()
        → 调用所有连续处理器handler
    → TVPDeliverWindowUpdateEvents()
```

## 10. Playwright自动化调试确认（使用完整游戏ZIP包）

使用 `/Users/bytedance/Downloads/KRkr高压_千恋万花.zip` 完整游戏包调试确认：

### 关键事实
1. **`progressCompat` 被调用** — delta=1, delta=0, delta=4294967381(溢出!)
2. **`drawCompat` 被调用** — numparams=1（参数正确）
3. **Logo阶段画面为纯黑** — 截图确认（logo-0.png ~ logo-2.png 全黑）
4. **`SetInnerSize` 正常工作** — 960x540 → 1440x810
5. **`flag=1`** — 连续处理器正常运行
6. **之前的测试因为使用不完整XP3文件集导致初始化失败** — 必须使用完整ZIP包

### delta溢出Bug
`progressCompat` 收到的delta值：
- delta=1 (正常)
- delta=0 (正常)
- delta=4294967381 (**溢出!** ≈ 0xFFFFFF85 = unsigned(-123))

TJS `_drawAffine` 调用 `_player.progress(_interval)`，`_interval`由 `onFlipTimerInterval` 设置。
溢出可能原因：TJS的tick差值计算溢出，或者`_interval`类型问题。

### 结论
**drawCompat确实被调用且参数正确**，问题在于：
1. 渲染管线bug — drawCompat渲染的像素未正确显示到WebGL canvas
2. delta溢出 — 可能导致动画帧计算异常

## 11. 下一步（需要在真实浏览器中验证）

### 优先级1：修复delta溢出
### 优先级2：调试drawCompat渲染管线 — 为什么像素不显示
### 优先级3：检查 _player.motion 属性
- `_player.motion` 的 getter 返回 `_motionKey`
- `_motionKey` 在 `setMotion()` 中设置
- 需要确认 `_setOptions({motion: "yuzulogo"})` 是否成功调用了 `setMotion`

### 优先级2：检查连续处理器是否注册
- 添加日志到 `TVPAddContinuousHandler` 确认回调注册

### 优先级3：检查onPaint是否触发
- 在 `Layer::NotifyLayerImageChange` 添加日志
- 或在 C++ 的 `TVPDeliverContinuousEvent` 添加日志确认连续事件处理

## 9. libkrkr2.so中Player NCB "draw" 的注册

**地址**: 字符串 "draw" 在 0x14D73AA (UTF-16LE)
**注册函数**: sub_6D69C8 (Player NCB注册)
**C++实现函数**: sub_6D5FB8
**相邻方法**: chara, motion, frameProgress, **draw**, initPhysics, unserialize
**注册类型**: 与flag=1注册（表示NCB_METHOD_RAW_CALLBACK等价）
