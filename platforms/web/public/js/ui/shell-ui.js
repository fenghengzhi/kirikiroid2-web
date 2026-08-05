// 引擎外壳 UI：加载浮层、错误浮层、canvas 尺寸、本地文件选择器、xp3 多选。
//
// 这一层是 KrKr2Engine 各回调的默认渲染实现。整体删除后引擎仍可启动
// （见 js/engine/engine.js 的 boot 回调全部退化为空实现）。

(function () {
    var statusElement = document.getElementById('status');
    var progressFill = document.getElementById('progress-fill');
    var loadingOverlay = document.getElementById('loading');
    var filePicker = document.getElementById('file-picker');
    var pickerStatus = document.getElementById('picker-status');
    var pickerProgress = document.getElementById('picker-progress');
    var pickerProgressFill = document.getElementById('picker-progress-fill');
    var engineStatusElement = document.getElementById('engine-status');

    function setEngineStatus(text) {
        engineStatusElement.textContent = text;
    }

    // --- 致命错误浮层 ---------------------------------------------------
    // 引擎层（boot-guards / memory / engine.onAbort）只上报结构化信息，
    // 具体 DOM 由这里渲染。
    function showFatal(info) {
        document.querySelector('#error-box h2').textContent = info.title || 'Error';
        document.getElementById('error-message').textContent = info.message || '';
        document.getElementById('error-update-btn').style.display =
            info.allowUpdate === false ? 'none' : '';
        document.getElementById('error-overlay').classList.add('visible');
        if (info.engineStatus) setEngineStatus(info.engineStatus);
    }

    // --- 加载浮层 -------------------------------------------------------
    function showLoading() { loadingOverlay.classList.remove('hidden'); }
    function hideLoading() { loadingOverlay.classList.add('hidden'); }

    function setLoadingStatus(text, pct) {
        if (text !== null && text !== undefined) statusElement.textContent = text;
        if (typeof pct === 'number') progressFill.style.width = pct + '%';
    }

    // --- 本地文件选择器 -------------------------------------------------
    // 两个入口的语义不同，沿用原实现的区别：
    //   showFilePicker —— 自动加载失败/结束后回落，需要把 early-boot 隐藏掉的
    //                     静态落地页重新放出来；
    //   openFilePicker —— 用户主动打开（选完存档空间、点标题栏按钮），
    //                     不动 krkr2-autoload。
    function showFilePicker() {
        // Auto-load failed or finished without a game: re-enable the
        // statically rendered landing page hidden by the early head script.
        document.documentElement.classList.remove('krkr2-autoload');
        filePicker.classList.add('visible');
    }

    function openFilePicker() {
        filePicker.classList.add('visible');
    }

    function hideFilePicker() {
        filePicker.classList.remove('visible');
    }

    function setPickerProgress(pct, text) {
        pickerProgress.classList.add('visible');
        pickerProgressFill.style.width = pct + '%';
        if (text) pickerStatus.textContent = text;
    }

    function hidePickerProgress() {
        pickerProgress.classList.remove('visible');
    }

    function setPickerStatus(text) {
        pickerStatus.textContent = text;
    }

    // --- 多 xp3 选择 ----------------------------------------------------
    // 作为 KrKr2Engine.loadSource 的 chooseEntry 钩子；原实现里这段是加载器
    // 直接调用的，现在由 UI 提供并返回 Promise。
    function chooseXp3(xp3Paths, dirPrefix) {
        return new Promise(function (resolve) {
            var selectorEl = document.getElementById('xp3-selector');
            var listEl = document.getElementById('xp3-list');
            listEl.innerHTML = '';

            xp3Paths.sort(function (a, b) {
                return a.toLowerCase().localeCompare(b.toLowerCase());
            });

            for (var i = 0; i < xp3Paths.length; i++) {
                (function (path) {
                    var name = path.substring(path.lastIndexOf('/') + 1);
                    var dir = path.substring(0, path.lastIndexOf('/'));
                    var relDir = dirPrefix ? dir.substring(dirPrefix.length) : dir;

                    var btn = document.createElement('button');
                    btn.className = 'xp3-item';
                    btn.innerHTML = '<div class="xp3-name">' + name + '</div>' +
                        (relDir ? '<div class="xp3-dir">' + relDir + '</div>' : '');
                    btn.addEventListener('click', function () {
                        selectorEl.classList.remove('visible');
                        resolve(path);
                    });
                    listEl.appendChild(btn);
                })(xp3Paths[i]);
            }

            filePicker.classList.remove('visible');
            pickerProgress.classList.remove('visible');
            selectorEl.classList.add('visible');
        });
    }

    // --- canvas 尺寸跟随容器 + devicePixelRatio ------------------------
    var canvas = document.getElementById('canvas');
    (function () {
        var container = document.getElementById('container');
        function updateCanvasSize() {
            var dpr = window.devicePixelRatio || 1;
            var cssW = Math.round(container.clientWidth) || 1280;
            var cssH = Math.round(container.clientHeight) || 720;
            var w = Math.round(cssW * dpr);
            var h = Math.round(cssH * dpr);
            if (canvas.width !== w || canvas.height !== h) {
                canvas.width = w;
                canvas.height = h;
            }
        }
        updateCanvasSize();
        new ResizeObserver(updateCanvasSize).observe(container);
        window.addEventListener('resize', updateCanvasSize);
        if (window.matchMedia) {
            var mqDpr = window.matchMedia('(resolution: ' + window.devicePixelRatio + 'dppx)');
            if (mqDpr.addEventListener) {
                mqDpr.addEventListener('change', updateCanvasSize);
            }
        }
    })();

    canvas.addEventListener('webglcontextlost', function (e) {
        alert('WebGL context lost. Please reload the page.');
        e.preventDefault();
    }, false);

    window.KrKr2UI = {
        canvas: canvas,
        setEngineStatus: setEngineStatus,
        showFatal: showFatal,
        showLoading: showLoading,
        hideLoading: hideLoading,
        setLoadingStatus: setLoadingStatus,
        showFilePicker: showFilePicker,
        openFilePicker: openFilePicker,
        hideFilePicker: hideFilePicker,
        setPickerProgress: setPickerProgress,
        hidePickerProgress: hidePickerProgress,
        setPickerStatus: setPickerStatus,
        chooseXp3: chooseXp3
    };

    // 画廊标题栏的「打开本地文件」与选择器自身的关闭按钮（内联 onclick）
    window.closeLocalPicker = hideFilePicker;
    window.openLocalPicker = openFilePicker;
})();
