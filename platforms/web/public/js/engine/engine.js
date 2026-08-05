// KrKr2 引擎 facade —— 前端与 wasm 之间的唯一接口。
//
// 设计意图：产品前端（js/ui/*）整体删除后，本文件加上 engine/ 其余几支仍能独立
// 启动引擎。因此这里不出现任何 document.getElementById；所有对外可见的状态都经
// 回调上报，所有 DOM 决策留给调用方。
//
// ── 与 C++ 侧的契约（字段名不可更改）────────────────────────────────────
//   Module._startupXp3Path   多 xp3 时的启动目标。由 cpp/core/environ/web/
//                            Platform.cpp 的 krkr2_get_startup_xp3_path()
//                            经 EM_JS 读取。
//   Module._saveSpaceId      当前存档空间 id（纯 JS 侧状态，供写回链路判断）
//   Module._hostDirHandle    File System Access 目录句柄（纯 JS 侧状态）
//   Module._hostDirPrefix    上述句柄对应的引擎路径前缀（纯 JS 侧状态）
// ────────────────────────────────────────────────────────────────────
//
// 用法：
//   KrKr2Engine.boot({ canvas, renderer, saveSpace, onStatus, onIdle,
//                      onReady, onRunning, onError });
//   await KrKr2Engine.loadSource({type:'zip-url', url, entry},
//                                { onProgress, chooseEntry });

(function () {
    var booted = false;
    var gameFileReady = false;
    var userFileDepAdded = false;
    var bootOpts = null;

    function noop() {}

    // 引擎自身的加载/运行状态回调，boot() 未提供时全部退化为空实现。
    function cb(name) {
        return (bootOpts && typeof bootOpts[name] === 'function') ? bootOpts[name] : noop;
    }

    // ---------------------------------------------------------------
    // Module —— emscripten glue 在 index.js 顶部读取的全局配置对象
    // （非 MODULARIZE 构建：var Module = globalThis.Module || ...）
    // ---------------------------------------------------------------
    function buildModule() {
        var Module = {
            wasmMemory: window.KrKr2Memory.prealloc || undefined,

            preRun: [function () {
                FS.mkdir('/savedata');
                window.KrKr2FS.applyRendererPreference(bootOpts.renderer);

                Module.addRunDependency('engine-singleton');
                window.KrKr2Guards.singletonReady.then(function (acquired) {
                    if (acquired) Module.removeRunDependency('engine-singleton');
                });

                Module.addRunDependency('user-file');
                userFileDepAdded = true;
                if (gameFileReady) {
                    Module.removeRunDependency('user-file');
                }

                // UI 资源注册完成前引擎不得启动（cocos init 需要字体/UI）
                Module.addRunDependency('vlfs-assets');
                window.KrKr2VLFS.assetsLoaded.then(function () {
                    Module.removeRunDependency('vlfs-assets');
                });
            }],

            postRun: [function () {
                cb('onRunning')();
            }],

            print: function (text) { console.log(text); },
            printErr: function (text) { console.error(text); },

            canvas: null,   // boot() 填入

            setStatus: function (text) {
                if (!text) {
                    cb('onIdle')();
                    return;
                }
                var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
                if (m) {
                    var pct = (parseInt(m[2]) / parseInt(m[4])) * 100;
                    cb('onStatus')(m[1].trim(), pct);
                } else {
                    cb('onStatus')(text, null);
                }
            },

            onAbort: function (what) {
                // JSPI 缺失的指引浮层已在显示，引擎随后的断言 abort 不要覆盖它
                if (window.KrKr2Guards.jspiUnsupported) return;
                cb('onError')({
                    code: 'abort',
                    title: 'Error',
                    message: 'The application has aborted. ' + what,
                    allowUpdate: true
                });
            },

            monitorRunDependencies: function (left) {
                if (left === 0) {
                    Module.setStatus('');
                } else if (!gameFileReady) {
                    if (userFileDepAdded && left === 1) {
                        // Only the 'user-file' dependency remains: the engine itself
                        // is ready and waiting for the user to pick a game.
                        cb('onReady')();
                    } else {
                        Module.setStatus('Preparing engine... (' + left + ' remaining)');
                    }
                } else {
                    Module.setStatus('Starting... (' + left + ' remaining)');
                }
            }
        };
        return Module;
    }

    // ---------------------------------------------------------------
    // 游戏源就绪 → 恢复存档 → 释放 user-file 依赖，引擎随即进入 main()
    // ---------------------------------------------------------------
    function startGame() {
        gameFileReady = true;
        // 数据源已注册，引擎即将进入 main：UI 可以收起选择器、亮出加载浮层。
        cb('onSourceReady')();
        cb('onStatus')('Starting game...', 100);

        function releaseDep() {
            if (userFileDepAdded) Module.removeRunDependency('user-file');
        }

        if (Module._saveSpaceId) {
            cb('onStatus')('Restoring saves...', 100);
            window.KrKr2IDB.restoreSaves().then(function () {
                window.KrKr2FS.applyRendererPreference(bootOpts.renderer);
                releaseDep();
            }).catch(function (e) {
                console.warn('[IDB] Failed to restore saves:', e);
                window.KrKr2FS.applyRendererPreference(bootOpts.renderer);
                releaseDep();
            });
        } else {
            window.KrKr2FS.applyRendererPreference(bootOpts.renderer);
            releaseDep();
        }
    }

    // ---------------------------------------------------------------
    // 对外 API
    // ---------------------------------------------------------------
    var KrKr2Engine = {
        /**
         * 配置 Module 并注入引擎 glue。幂等，重复调用忽略。
         *
         * @param {object}      opts
         * @param {HTMLCanvasElement} opts.canvas       必填
         * @param {string}     [opts.engineScript]      glue 路径，默认 'index.js'
         * @param {string}     [opts.renderer]          '' | 'software' | 'opengl'
         * @param {string}     [opts.saveSpace]         存档空间 id
         * @param {function}   [opts.onStatus]          (text, pct|null)
         * @param {function}   [opts.onIdle]            引擎不再加载任何东西
         * @param {function}   [opts.onReady]           引擎就绪，只差游戏源
         * @param {function}   [opts.onSourceReady]     数据源已注册，即将进入 main
         * @param {function}   [opts.onRunning]         postRun：main() 已跑
         * @param {function}   [opts.onError]           ({code,title,message,allowUpdate})
         */
        boot: function (opts) {
            if (booted) return;
            booted = true;
            bootOpts = opts || {};

            var Module = buildModule();
            Module.canvas = bootOpts.canvas || null;
            if (bootOpts.saveSpace) Module._saveSpaceId = bootOpts.saveSpace;
            window.Module = Module;

            Module.setStatus('Downloading engine...');

            // 引擎 glue 由此处动态注入，而不是 index.html 里的静态 <script>：
            // "配置完成才启动引擎" 成为显式时序，不依赖 <script async> 的隐式
            // 排序。等价于 emscripten 自己在 tools/link.py 里的 inline 分支。
            // glue 顶部的 _scriptName 取自 document.currentScript.src，动态
            // 追加的 script 在执行期同样成立，pthread worker 定位不受影响。
            var script = document.createElement('script');
            script.src = bootOpts.engineScript || 'index.js';
            document.body.appendChild(script);
        },

        /**
         * 注册一个游戏数据源到 VLFS，完成后释放引擎的 user-file 依赖。
         *
         * @param {object}   src  见 js/loaders/*.js
         *   {type:'xp3-url',  url}          {type:'zip-url',  url, entry}
         *   {type:'xp3-file', file}         {type:'zip-file', file}
         *   {type:'folder',   files}        {type:'fsa-dir',  handle}
         * @param {object}  [hooks]
         * @param {function}[hooks.onProgress]   (pct|null, text)
         * @param {function}[hooks.chooseEntry]  (paths[]) => Promise<path>
         *                  多 xp3 时由调用方决策；缺省取排序后的第一个。
         */
        loadSource: function (src, hooks) {
            return window.KrKr2Loaders.load(src, hooks || {}).then(function (result) {
                // 加载器只负责把字节注册进 VLFS 并报告发现的 xp3 列表；
                // 选哪个当启动目标是调用方的决策，故在此统一落到契约字段。
                if (result && result.startupXp3Path) {
                    Module._startupXp3Path = result.startupXp3Path;
                }
                startGame();
            });
        },

        /**
         * 设置存档空间并打开对应 IndexedDB。
         *
         * @param {string|null} id
         * @param {object} [opts]
         * @param {boolean} [opts.remember=true]  写入 localStorage 的 krkr2-last-space
         * @param {boolean} [opts.register=true]  登记进 krkr2-spaces 列表
         *        （indexedDB.databases() 不可用的浏览器靠它枚举空间）
         */
        setSaveSpace: function (id, opts) {
            opts = opts || {};
            Module._saveSpaceId = id;
            if (!id) return Promise.resolve();
            if (opts.remember !== false) localStorage.setItem('krkr2-last-space', id);
            return window.KrKr2IDB.open(id).then(function () {
                if (opts.register !== false) window.KrKr2IDB.registerSpace(id);
            });
        },

        /** 挂载 File System Access 目录句柄，供 VLFS 写回链路使用。 */
        setHostDir: function (handle, prefix) {
            Module._hostDirHandle = handle;
            Module._hostDirPrefix = prefix;
        },

        /** 游戏源是否已就绪（引擎是否已被放行进入 main）。 */
        isGameReady: function () { return gameFileReady; },

        /** 引擎 glue 是否已注入。 */
        isBooted: function () { return booted; }
    };

    window.KrKr2Engine = KrKr2Engine;
})();
