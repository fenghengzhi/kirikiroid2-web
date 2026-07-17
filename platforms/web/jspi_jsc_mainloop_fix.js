// WebKit/JSC JSPI 兼容补丁：覆盖 emscripten 内置 emscripten_set_main_loop_arg。
//
// 背景：glue 的 getWasmTableEntry 依赖「wasmTable.get(ptr) === instance.exports.f」
// 的函数对象身份比较（Asyncify.isAsyncExport）识别 JSPI_EXPORTS 导出，命中才包
// WebAssembly.promising。V8 满足该身份等价（Wasm JS-API 规范要求同一 wasm 函数
// 对应同一 JS 对象），但 JSC（实测 WebKit 26.4 / Safari 27 beta，2026-06）的
// table.get 返回与 export 不同一的函数对象，识别落空 → krkr2_main_loop_tick
// 未包 promising → 主循环 tick 内首次 JSPI 挂起读抛
// "SuspendError: Suspending() wrapper called outside of a promising() context"。
//
// 本项目 emscripten_set_main_loop_arg 的唯一注册者是 krkr2_main_loop_tick
//（JSPI_EXPORTS 唯一成员，见 CMakeLists.txt 与 vcpkg cocos2dx patch
// CCApplication-emscripten.cpp），故此处无条件包 promising——与 Chrome 下
// getWasmTableEntry 命中身份比较后的行为完全一致，对 V8 无行为变化。
addToLibrary({
  emscripten_set_main_loop_arg__deps: ['$setMainLoop'],
  emscripten_set_main_loop_arg: (func, arg, fps, simulateInfiniteLoop) => {
    var wrappedTick = WebAssembly.promising(wasmTable.get(func));
    // Browser-only frame-pump policy. Keep RAF as the sole scheduler, but use
    // its display-synchronised timestamp to limit how often the WASM main loop
    // runs. The target defaults to 15 FPS and can be overridden with
    // `?fps=<positive number>`; invalid values deliberately fall back to 15.
    var targetFps = 15;
    var perfStatsEnabled = false;
    try {
      var params = new URLSearchParams(globalThis.location?.search || '');
      var requestedFps = Number(params.get('fps'));
      if (Number.isFinite(requestedFps) && requestedFps > 0) {
        targetFps = requestedFps;
      }
      var requestedPerfStats = params.get('perfStats');
      perfStatsEnabled = requestedPerfStats === '1' ||
          requestedPerfStats === 'true';
    } catch (e) {}
    var frameInterval = 1000 / targetFps;
    var lastRafTimestamp = -1;
    var accumulatedTime = 0;
    var limiterDiag = globalThis.__krkr2FrameLimiter = {
      targetFps,
      frameInterval,
      rafCount: 0,
      tickCount: 0,
      skipCount: 0,
      lastTimestamp: -1,
      lastElapsed: 0,
      accumulatedTime: 0,
    };

    var findPerfExport = (name) => {
      var direct = wasmExports?.[name];
      if (typeof direct === 'function') return direct;
      var moduleExport = Module?.['_' + name];
      return typeof moduleExport === 'function' ? moduleExport : null;
    };
    var perfGet = findPerfExport('krkr2_web_perf_get');
    var perfSetEnabled = findPerfExport('krkr2_web_perf_set_enabled');
    if (perfSetEnabled) perfSetEnabled(perfStatsEnabled ? 1 : 0);

    var perfFields = [
      'enabled',
      'tickCount',
      'tickDuration',
      'blendCallCount',
      'blendPixelCount',
      'dirtyRectCount',
      'dirtyRectArea',
      'textureUploadCallCount',
      'textureUploadBytes',
      'outsideTickBlendCallCount',
      'outsideTickBlendPixelCount',
      'outsideTickDirtyRectCount',
      'outsideTickDirtyRectArea',
      'outsideTickTextureUploadCallCount',
      'outsideTickTextureUploadBytes',
      'rejectedTickCount',
      'tickActive',
    ];

    var calculatePerfDelta = (before, after) => {
      var elapsedMs = after.timestamp - before.timestamp;
      var delta = {elapsedMs};
      for (var i = 1; i < perfFields.length - 1; ++i) {
        var field = perfFields[i];
        delta[field] = after[field] - before[field];
      }
      delta.tickActive = after.tickActive;
      var seconds = Math.max(elapsedMs / 1000, 0.001);
      var ticks = delta.tickCount;
      delta.perSecond = {
        tickCount: delta.tickCount / seconds,
        tickDuration: delta.tickDuration / seconds,
        blendCallCount: delta.blendCallCount / seconds,
        blendPixelCount: delta.blendPixelCount / seconds,
        dirtyRectArea: delta.dirtyRectArea / seconds,
        textureUploadBytes: delta.textureUploadBytes / seconds,
      };
      delta.perTick = {
        tickDuration: ticks ? delta.tickDuration / ticks : 0,
        blendCallCount: ticks ? delta.blendCallCount / ticks : 0,
        blendPixelCount: ticks ? delta.blendPixelCount / ticks : 0,
        dirtyRectArea: ticks ? delta.dirtyRectArea / ticks : 0,
        textureUploadBytes: ticks ? delta.textureUploadBytes / ticks : 0,
      };
      delta.uncoveredRenderingPath =
          delta.outsideTickBlendCallCount > 0 ||
          delta.outsideTickDirtyRectCount > 0 ||
          delta.outsideTickTextureUploadCallCount > 0;
      return delta;
    };

    var limiterSnapshot = () => ({
      timestamp: performance.now(),
      rafCount: limiterDiag.rafCount,
      tickCount: limiterDiag.tickCount,
      skipCount: limiterDiag.skipCount,
    });

    var perfTraceLastSnapshot = null;
    var perfTraceLastLimiterSnapshot = null;
    var perfTraceErrorLogged = false;
    var writeDevToolsMeasure =
        (name, track, start, end, color, tooltipText, properties) => {
          performance.measure(name, {
            start,
            end,
            detail: {
              devtools: {
                dataType: 'track-entry',
                track,
                trackGroup: 'KrKr2',
                color,
                tooltipText,
                properties,
              },
            },
          });
        };

    var perfStats = globalThis.__krkr2PerfStats = {
      enabled: perfStatsEnabled && !!perfGet,
      traceEnabled: perfStatsEnabled && !!perfGet,
      // Cumulative native counters. tickDuration is wall-clock milliseconds
      // spent inside accepted C++ ticks, including JSPI suspension time.
      snapshot: () => {
        var snapshot = {timestamp: performance.now()};
        for (var i = 0; i < perfFields.length; ++i) {
          snapshot[perfFields[i]] = perfGet ? perfGet(i) : NaN;
        }
        return snapshot;
      },
      // Measure deltas over a stable window and derive per-second/per-tick
      // values. Use the same scene and duration for ?fps=15 and ?fps=60.
      measure: async (durationMs = 10000) => {
        var before = perfStats.snapshot();
        await new Promise((resolve) => setTimeout(resolve, durationMs));
        var after = perfStats.snapshot();
        return calculatePerfDelta(before, after);
      },
      // Chrome DevTools Performance Extensibility API: emit five custom tracks
      // per second. Values are interval deltas so each trace entry is directly
      // comparable between ?fps=15 and ?fps=60 recordings.
      emitTraceSample: () => {
        if (!perfStats.traceEnabled) return null;
        var after = perfStats.snapshot();
        var limiterAfter = limiterSnapshot();
        if (!perfTraceLastSnapshot) {
          perfTraceLastSnapshot = after;
          perfTraceLastLimiterSnapshot = limiterAfter;
          return null;
        }

        var delta = calculatePerfDelta(perfTraceLastSnapshot, after);
        var limiterDelta = {
          rafCount:
              limiterAfter.rafCount - perfTraceLastLimiterSnapshot.rafCount,
          tickCount:
              limiterAfter.tickCount - perfTraceLastLimiterSnapshot.tickCount,
          skipCount:
              limiterAfter.skipCount - perfTraceLastLimiterSnapshot.skipCount,
        };
        perfTraceLastSnapshot = after;
        perfTraceLastLimiterSnapshot = limiterAfter;

        var start = after.timestamp - delta.elapsedMs;
        var end = after.timestamp;
        var outsideColor = delta.uncoveredRenderingPath ? 'error' : 'primary';
        try {
          writeDevToolsMeasure(
              'KrKr2 Frame Limiter', 'Frame Limiter', start, end, 'tertiary',
              `RAF ${limiterDelta.rafCount}, accepted ${delta.tickCount}`,
              [
                ['targetFPS', String(targetFps)],
                ['rafCount', String(limiterDelta.rafCount)],
                ['tickAttempts', String(limiterDelta.tickCount)],
                ['limiterSkips', String(limiterDelta.skipCount)],
                ['acceptedTickCount', String(delta.tickCount)],
                ['rejectedTickCount', String(delta.rejectedTickCount)],
              ]);
          writeDevToolsMeasure(
              'KrKr2 Tick', 'Director::mainLoop', start, end, 'secondary',
              `${delta.perSecond.tickCount.toFixed(2)} ticks/s, ` +
                  `${delta.perTick.tickDuration.toFixed(2)} ms/tick`,
              [
                ['tickCount', String(delta.tickCount)],
                ['tickRate', delta.perSecond.tickCount.toFixed(3)],
                ['tickDurationMs', delta.tickDuration.toFixed(3)],
                ['averageTickDurationMs',
                 delta.perTick.tickDuration.toFixed(3)],
                ['tickDurationMsPerSecond',
                 delta.perSecond.tickDuration.toFixed(3)],
                ['tickActiveAtSample', String(delta.tickActive)],
              ]);
          writeDevToolsMeasure(
              'KrKr2 Blend', 'Software Blend', start, end, outsideColor,
              `${delta.blendPixelCount} px, ` +
                  `${delta.outsideTickBlendPixelCount} outside tick`,
              [
                ['blendCallCount', String(delta.blendCallCount)],
                ['blendPixelCount', String(delta.blendPixelCount)],
                ['blendCallsPerTick',
                 delta.perTick.blendCallCount.toFixed(3)],
                ['blendPixelsPerTick',
                 delta.perTick.blendPixelCount.toFixed(3)],
                ['blendPixelsPerSecond',
                 delta.perSecond.blendPixelCount.toFixed(3)],
                ['outsideTickBlendCallCount',
                 String(delta.outsideTickBlendCallCount)],
                ['outsideTickBlendPixelCount',
                 String(delta.outsideTickBlendPixelCount)],
              ]);
          writeDevToolsMeasure(
              'KrKr2 Dirty Rect', 'Dirty Rect', start, end, outsideColor,
              `${delta.dirtyRectArea} px area, ` +
                  `${delta.outsideTickDirtyRectArea} outside tick`,
              [
                ['dirtyRectCount', String(delta.dirtyRectCount)],
                ['dirtyRectArea', String(delta.dirtyRectArea)],
                ['dirtyRectAreaPerTick',
                 delta.perTick.dirtyRectArea.toFixed(3)],
                ['dirtyRectAreaPerSecond',
                 delta.perSecond.dirtyRectArea.toFixed(3)],
                ['outsideTickDirtyRectCount',
                 String(delta.outsideTickDirtyRectCount)],
                ['outsideTickDirtyRectArea',
                 String(delta.outsideTickDirtyRectArea)],
              ]);
          writeDevToolsMeasure(
              'KrKr2 Texture Upload', 'Texture Upload', start, end,
              outsideColor,
              `${delta.textureUploadBytes} bytes, ` +
                  `${delta.outsideTickTextureUploadBytes} outside tick`,
              [
                ['textureUploadCallCount',
                 String(delta.textureUploadCallCount)],
                ['textureUploadBytes', String(delta.textureUploadBytes)],
                ['textureUploadBytesPerTick',
                 delta.perTick.textureUploadBytes.toFixed(3)],
                ['textureUploadBytesPerSecond',
                 delta.perSecond.textureUploadBytes.toFixed(3)],
                ['outsideTickTextureUploadCallCount',
                 String(delta.outsideTickTextureUploadCallCount)],
                ['outsideTickTextureUploadBytes',
                 String(delta.outsideTickTextureUploadBytes)],
              ]);

          if (delta.uncoveredRenderingPath) {
            performance.mark('KrKr2 Outside-Tick Rendering', {
              startTime: end,
              detail: {
                devtools: {
                  dataType: 'marker',
                  color: 'error',
                  tooltipText: 'Rendering work occurred outside accepted tick',
                  properties: [
                    ['blendPixels',
                     String(delta.outsideTickBlendPixelCount)],
                    ['dirtyRectArea',
                     String(delta.outsideTickDirtyRectArea)],
                    ['textureUploadBytes',
                     String(delta.outsideTickTextureUploadBytes)],
                  ],
                },
              },
            });
          }
        } catch (e) {
          if (!perfTraceErrorLogged) {
            perfTraceErrorLogged = true;
            console.warn('[krkr2] DevTools performance track unavailable', e);
          }
        }
        return delta;
      },
    };
    if (perfStatsEnabled && !perfGet) {
      console.warn('[krkr2] perfStats requested but native exports are missing');
    }
    if (perfStats.traceEnabled) {
      perfStats.emitTraceSample();
      setInterval(perfStats.emitTraceSample, 1000);
    }

    // TVPWebFrameTickUpdate consumes the same timestamp for its main-thread
    // clock phase lock. Install the wrapper before setMainLoop schedules its
    // first RAF so both the limiter and the engine observe the actual RAF
    // timestamp starting with the first callback.
    if (!globalThis.__tvpRafWrapped) {
      globalThis.__tvpRafWrapped = 1;
      globalThis.__tvpRafT = -1;
      var requestRaf = globalThis.requestAnimationFrame.bind(globalThis);
      globalThis.requestAnimationFrame = (callback) => requestRaf((timestamp) => {
        globalThis.__tvpRafT = timestamp;
        callback(timestamp);
      });
    }

    var iterFunc = () => {
      var timestamp = globalThis.__tvpRafT;
      if (!(timestamp >= 0)) timestamp = performance.now();
      limiterDiag.rafCount++;
      limiterDiag.lastTimestamp = timestamp;

      if (lastRafTimestamp < 0) {
        lastRafTimestamp = timestamp;
        limiterDiag.tickCount++;
        return wrappedTick(arg);
      }

      var elapsed = timestamp - lastRafTimestamp;
      lastRafTimestamp = timestamp;
      limiterDiag.lastElapsed = elapsed;
      if (!(elapsed >= 0) || elapsed > 1000) {
        // Do not replay frames accumulated while the tab was suspended.
        accumulatedTime = 0;
        limiterDiag.accumulatedTime = accumulatedTime;
        limiterDiag.tickCount++;
        return wrappedTick(arg);
      }

      accumulatedTime += elapsed;
      limiterDiag.accumulatedTime = accumulatedTime;
      if (accumulatedTime + 0.001 < frameInterval) {
        limiterDiag.skipCount++;
        return;
      }

      // Consume one due frame, preserve its fractional remainder for stable
      // non-divisor rates such as 45 FPS on a 60 Hz display, and discard any
      // additional whole frames accumulated during a stall.
      accumulatedTime = Math.max(0, accumulatedTime - frameInterval);
      accumulatedTime %= frameInterval;
      limiterDiag.accumulatedTime = accumulatedTime;
      limiterDiag.tickCount++;
      return wrappedTick(arg);
    };
    setMainLoop(iterFunc, fps, simulateInfiniteLoop, arg);
  },

  // Browser-only policy boundary.  Android's OpenAL/Oboe device starts
  // synchronously, while a browser is allowed to create its AudioContext only
  // in the suspended state. Emscripten's autoResumeAudioContext uses one-shot
  // listeners tied to the context that existed when it was called. Keep the
  // Android sound object/data flow unchanged, but retain a capture listener so
  // later contexts and a rejected first attempt can use the next real gesture.
  krkr2_install_web_audio_resume__deps: ['$AL'],
  krkr2_install_web_audio_resume: () => {
    if (globalThis.__krkr2WebAudioResumeInstalled) return;
    globalThis.__krkr2WebAudioResumeInstalled = true;

    var resumeCurrentOpenALContext = (event) => {
      if (!event.isTrusted) return;
      var audioContext = AL.currentCtx?.audioCtx;
      if (!audioContext || audioContext.state !== 'suspended') return;
      var promise = audioContext.resume();
      if (promise) promise.catch(() => {});
    };

    for (var event of
         ['pointerdown', 'mousedown', 'touchstart', 'keydown', 'click']) {
      document.addEventListener(event, resumeCurrentOpenALContext, {
        capture: true,
        passive: true,
      });
    }
  },
});
