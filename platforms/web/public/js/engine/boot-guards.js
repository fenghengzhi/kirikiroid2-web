// 引擎启动前的平台守卫与全局补丁。
//
// 本文件在 index.html 里排在所有 UI 脚本之前，等价于原 shell.html 单脚本块的
// 顶部位置 —— 单例锁是 ifAvailable 抢占，必须尽早发起。
//
// 与原实现的唯一结构差异：致命错误不再由本层直接写 DOM，而是记进
// KrKr2Guards.fatal，交给 UI 层（js/ui/shell-ui.js）渲染。这样引擎层不依赖
// 任何具体 DOM 结构，产品前端可以整体替换。

(function () {
    var guards = {
        // Promise<boolean>：是否取得跨标签页单例锁
        singletonReady: null,
        // 浏览器是否缺少 WebAssembly JSPI
        jspiUnsupported: false,
        // {code, title, message, allowUpdate} —— 由 UI 层渲染的致命错误
        fatal: null,
        reportFatal: function (info) {
            // 先到先得：JSPI 缺失的指引不应被随后的 OOM/abort 覆盖
            if (!guards.fatal) guards.fatal = info;
        }
    };
    window.KrKr2Guards = guards;

    // --- Web 平台边界：同一 origin 只允许一个活跃引擎实例 ---
    // VLFS 的 OPFS 临时缓存按页面会话隔离，但两个引擎实例仍会争用全局
    // 运行资源和持久化存档空间。Web Locks 的持有期与 Document 生命周期
    // 绑定，标签页关闭/崩溃后浏览器会自动释放，不需要心跳或超时猜测。
    var resolveEngineSingleton;
    guards.singletonReady = new Promise(function (resolve) {
        resolveEngineSingleton = resolve;
    });

    function reportSingletonConflict() {
        guards.reportFatal({
            code: 'singleton',
            title: 'Engine Already Running',
            message:
                '同一站点已有另一个 KrKr2 引擎窗口正在运行。请关闭另一个窗口后刷新本页。\n' +
                'Another KrKr2 engine tab is already running for this site. Close it, then reload this page.',
            allowUpdate: false,
            engineStatus: 'Another engine tab is already running'
        });
    }

    if (navigator.locks && typeof navigator.locks.request === 'function') {
        navigator.locks.request(
            'krkr2-web-engine-instance',
            { mode: 'exclusive', ifAvailable: true },
            function (lock) {
                if (!lock) {
                    reportSingletonConflict();
                    resolveEngineSingleton(false);
                    return;
                }
                resolveEngineSingleton(true);
                // 保持 callback pending 即持有锁；Document 销毁时自动释放。
                return new Promise(function () {});
            }
        ).catch(function (error) {
            console.error('[singleton] Web Lock request failed:', error);
            reportSingletonConflict();
            resolveEngineSingleton(false);
        });
    } else {
        // 目标 Chromium 137+ 均支持 Web Locks；仅在其他实现缺少该 API 时
        // 保持原有单窗口行为，避免把平台能力缺失误报成已有实例。
        console.warn('[singleton] Web Locks unavailable; cross-tab guard disabled');
        resolveEngineSingleton(true);
    }

    // --- JSPI 能力检测：主线程文件读依赖 WebAssembly JSPI（VirtualLazyFS），
    // 缺失时引擎会死在 glue 的 "JSPI not supported" 断言上，用户只能看到
    // 天书。这里提前检测并按浏览器给出明确的升级/换浏览器指引。---
    guards.jspiUnsupported = !(typeof WebAssembly !== 'undefined' &&
        typeof WebAssembly.Suspending === 'function' &&
        typeof WebAssembly.promising === 'function');
    if (guards.jspiUnsupported) {
        (function () {
            var ua = navigator.userAgent;
            // iOS 上所有浏览器（含 Chrome 的 CriOS / Firefox 的 FxiOS）都被
            // 强制使用 WebKit 内核，一律按 Safari 处理；iPadOS 13+ 默认伪装
            // 桌面 Mac UA，用触点数辨别
            var isIOS = /iPhone|iPad|iPod/.test(ua) ||
                (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1);
            var isFirefox = /Firefox\//.test(ua);
            var isChromium = /Chrome\/|Chromium\/|Edg\/|OPR\//.test(ua);
            var isSafari = /Safari\//.test(ua) && !isChromium && !isFirefox;
            var advice;
            if (isIOS || isSafari) {
                advice = '请升级到 Safari 27（iOS 27）或以上版本。\nPlease upgrade to Safari 27 (iOS 27) or later.';
            } else if (isFirefox) {
                advice = '请改用最新版 Chrome 浏览器。\nPlease use the latest version of Google Chrome instead.';
            } else if (isChromium) {
                advice = '请将浏览器升级到最新版本（需要 Chrome 137 或以上）。\nPlease update your browser to the latest version (Chrome 137 or later).';
            } else {
                advice = '请使用最新版 Chrome 浏览器。\nPlease use the latest version of Google Chrome.';
            }
            guards.reportFatal({
                code: 'jspi',
                title: 'Browser Not Supported',
                message:
                    '当前浏览器缺少引擎所需的 WebAssembly JSPI 能力。\n' +
                    'This browser lacks WebAssembly JSPI, which the engine requires.\n\n' + advice,
                // 强制更新解决不了能力缺失，隐藏避免误导
                allowUpdate: false
            });
        })();
    }

    // Fix WebGL shader precision mismatch on mobile:
    // cocos2d-x injects uniforms (mat4/mat3) without explicit precision into both
    // vertex (highp default) and fragment (mediump default) shaders, causing link failure.
    (function () {
        var typePattern = /uniform\s+(?!highp\s|mediump\s|lowp\s)(mat[234]|vec[234]|float|int)\s/g;
        function fixPrecision(shader, source) {
            return source.replace(typePattern, 'uniform highp $1 ');
        }
        var proto = WebGLRenderingContext.prototype;
        var origSS = proto.shaderSource;
        proto.shaderSource = function (shader, source) { return origSS.call(this, shader, fixPrecision(shader, source)); };
        if (typeof WebGL2RenderingContext !== 'undefined') {
            var proto2 = WebGL2RenderingContext.prototype;
            var origSS2 = proto2.shaderSource;
            proto2.shaderSource = function (shader, source) { return origSS2.call(this, shader, fixPrecision(shader, source)); };
        }
    })();

    // Keep native dialog behavior, but mirror alert/confirm content
    // to the console for easier debugging.
    (function () {
        var origAlert = window.alert;
        var origConfirm = window.confirm;
        window.alert = function (msg) {
            console.warn('[alert]', msg);
            return origAlert.call(window, msg);
        };
        window.confirm = function (msg) {
            console.warn('[confirm]', msg);
            var result = origConfirm.call(window, msg);
            console.warn('[confirm result]', result);
            return result;
        };
    })();
})();
