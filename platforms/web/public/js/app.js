// 装配层：把 URL 参数、DOM 事件与 KrKr2Engine 接起来。
//
// 这是整个前端唯一「知道所有人」的文件，也是替换前端时唯一需要重写的文件。

(function () {
    var UI = window.KrKr2UI;
    var Engine = window.KrKr2Engine;
    var SpaceUI = window.KrKr2SpaceUI;

    // --- URL 参数 -------------------------------------------------------
    var urlParams = new URLSearchParams(window.location.search);
    var xp3Url = urlParams.get('xp3');
    var gameZipUrl = urlParams.get('game');
    var entryXp3 = urlParams.get('entry');
    var renderer = window.KrKr2FS.normalizeRenderer(urlParams.get('renderer'));
    var localZipPickerEnabled = window.KrKr2Config.localZipPicker || urlParams.has('pickZip');
    var hasFileSystemAPI = typeof window.showDirectoryPicker === 'function';

    // --- 平台守卫上报的致命错误（单例冲突 / JSPI 缺失 / 共享内存 OOM）----
    if (window.KrKr2Guards.fatal) {
        UI.showFatal(window.KrKr2Guards.fatal);
    }

    // --- 选择器按钮可见性 -----------------------------------------------
    if (localZipPickerEnabled) {
        document.getElementById('pick-zip-btn').style.display = '';
    }
    // Show File System API button when supported
    if (hasFileSystemAPI) {
        document.getElementById('pick-dir-api-btn').style.display = '';
        document.getElementById('picker-divider').style.display = '';
    }

    // --- 启动引擎 -------------------------------------------------------
    Engine.boot({
        canvas: UI.canvas,
        renderer: renderer,

        onStatus: function (text, pct) {
            UI.setLoadingStatus(text, pct);
            if (!Engine.isGameReady()) {
                UI.setEngineStatus(
                    typeof pct === 'number' ? text + ' ' + Math.round(pct) + '%' : text);
            }
        },

        onIdle: function () {
            UI.hideLoading();
            // 没有自动加载目标、也没有已就绪的游戏 → 回到本地文件选择器
            if (!Engine.isGameReady() && !xp3Url && !gameZipUrl) {
                UI.showFilePicker();
            }
        },

        // Only the 'user-file' dependency remains: the engine itself
        // is ready and waiting for the user to pick a game.
        onReady: function () { UI.setEngineStatus('Engine ready'); },

        onSourceReady: function () {
            UI.hideFilePicker();
            UI.showLoading();
        },

        onRunning: function () { UI.hideLoading(); },

        onError: function (info) { UI.showFatal(info); }
    });

    window.onerror = function () {
        if (window.Module && window.Module.setStatus) Module.setStatus('Error: see console');
    };

    // --- 远端来源：进度打在加载浮层上 -----------------------------------
    function startRemote(src) {
        UI.showLoading();
        UI.setLoadingStatus('Probing game data...', 0);
        return Engine.loadSource(src, {
            onProgress: function (pct, text) { UI.setLoadingStatus(text, pct); },
            chooseEntry: function (paths) { return UI.chooseXp3(paths, ''); }
        }).catch(function (err) {
            UI.setLoadingStatus('Failed: ' + err.message, 0);
            console.error('[app] remote load failed:', err);
            UI.showFilePicker();
            UI.hideLoading();
        });
    }

    // --- 本地来源：进度打在选择器自带的进度条上 -------------------------
    function loadLocal(src) {
        return Engine.loadSource(src, {
            onProgress: function (pct, text) {
                UI.setPickerProgress(typeof pct === 'number' ? pct : 0, text);
            },
            chooseEntry: function (paths) { return UI.chooseXp3(paths, ''); }
        }).catch(function (err) {
            // 用户主动取消目录选择不是错误
            if (err && err.name === 'AbortError') return;
            UI.setPickerStatus('Error: ' + err.message);
            UI.hidePickerProgress();
            console.error('[app] local load failed:', err);
        });
    }

    window.KrKr2App = { startRemote: startRemote, loadLocal: loadLocal };

    // --- 自动加载（?game= / ?xp3=）--------------------------------------
    if (gameZipUrl) {
        SpaceUI.autoSelectSpace().then(function () {
            startRemote({ type: 'zip-url', url: gameZipUrl, entry: entryXp3 });
        });
    } else if (xp3Url) {
        SpaceUI.autoSelectSpace().then(function () {
            startRemote({ type: 'xp3-url', url: xp3Url });
        });
    }

    // --- Button bindings ------------------------------------------------
    document.getElementById('pick-dir-api-btn').addEventListener('click', async function () {
        var handle;
        try {
            handle = await window.showDirectoryPicker({ mode: 'readwrite' });
        } catch (err) {
            if (err.name !== 'AbortError') UI.setPickerStatus('Error: ' + err.message);
            return;
        }
        loadLocal({ type: 'fsa-dir', handle: handle });
    });

    document.getElementById('pick-file-btn').addEventListener('click', function () {
        SpaceUI.ensureSpaceThen(function () {
            document.getElementById('file-input').click();
        });
    });
    document.getElementById('pick-dir-btn').addEventListener('click', function () {
        SpaceUI.ensureSpaceThen(function () {
            document.getElementById('dir-input').click();
        });
    });
    document.getElementById('pick-zip-btn').addEventListener('click', function () {
        SpaceUI.ensureSpaceThen(function () {
            document.getElementById('zip-input').click();
        });
    });

    document.getElementById('file-input').addEventListener('change', function (e) {
        var file = e.target.files[0];
        if (file) loadLocal({ type: 'xp3-file', file: file });
        e.target.value = '';
    });

    document.getElementById('dir-input').addEventListener('change', function (e) {
        var files = Array.from(e.target.files);
        if (files.length > 0) loadLocal({ type: 'folder', files: files });
        e.target.value = '';
    });

    document.getElementById('zip-input').addEventListener('change', function (e) {
        var file = e.target.files[0];
        if (file) loadLocal({ type: 'zip-file', file: file });
        e.target.value = '';
    });

    // --- Drag & drop ----------------------------------------------------
    var dropZone = document.getElementById('drop-zone');
    dropZone.addEventListener('click', function () {
        SpaceUI.ensureSpaceThen(function () {
            document.getElementById('file-input').click();
        });
    });
    dropZone.addEventListener('dragover', function (e) {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });
    dropZone.addEventListener('dragleave', function () {
        dropZone.classList.remove('dragover');
    });
    dropZone.addEventListener('drop', function (e) {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        var file = e.dataTransfer.files[0];
        if (file) {
            SpaceUI.ensureSpaceThen(function () {
                loadLocal({ type: 'xp3-file', file: file });
            });
        }
    });
})();
