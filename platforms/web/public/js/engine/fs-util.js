// Emscripten MEMFS 小工具 + 渲染器偏好写入。
//
// FS 是 glue 在全局作用域声明的（非 MODULARIZE 构建，-s FORCE_FILESYSTEM=1），
// 引擎脚本注入前它不存在，故所有入口都做 typeof 检测。

(function () {
    // --- Common helpers ---
    function waitForFS() {
        return new Promise(function (resolve) {
            if (typeof FS !== 'undefined') { resolve(); return; }
            var check = setInterval(function () {
                if (typeof FS !== 'undefined') { clearInterval(check); resolve(); }
            }, 50);
        });
    }

    function ensureDir(path) {
        var parts = path.split('/').filter(Boolean);
        var cur = '';
        for (var i = 0; i < parts.length; i++) {
            cur += '/' + parts[i];
            try { FS.mkdir(cur); } catch (e) {}
        }
    }

    function writeFileToFS(fullPath, data) {
        var dir = fullPath.substring(0, fullPath.lastIndexOf('/'));
        if (dir) ensureDir(dir);
        FS.writeFile(fullPath, data);
    }

    // --- 渲染器偏好（?renderer=software|opengl）---
    function normalizeRenderer(value) {
        if (value === null || value === undefined) return '';
        value = String(value).trim().toLowerCase();
        if (value === 'software' || value === 'opengl') return value;
        console.warn('[renderer] Ignoring unsupported URL renderer:', value);
        return '';
    }

    function rendererPreferenceXml(existingXml, renderer) {
        var doc = null;
        if (existingXml) {
            try {
                doc = new DOMParser().parseFromString(existingXml, 'application/xml');
                if (doc.getElementsByTagName('parsererror').length ||
                    !doc.documentElement ||
                    doc.documentElement.tagName !== 'GlobalPreference') {
                    doc = null;
                }
            } catch (e) {
                doc = null;
            }
        }
        if (!doc) {
            doc = document.implementation.createDocument('', 'GlobalPreference', null);
        }

        var root = doc.documentElement;
        var items = root.getElementsByTagName('Item');
        var rendererItem = null;
        for (var i = 0; i < items.length; i++) {
            if (items[i].getAttribute('key') === 'renderer') {
                rendererItem = items[i];
                break;
            }
        }
        if (!rendererItem) {
            rendererItem = doc.createElement('Item');
            rendererItem.setAttribute('key', 'renderer');
            root.appendChild(rendererItem);
        }
        rendererItem.setAttribute('value', renderer);
        return new XMLSerializer().serializeToString(doc);
    }

    // 原实现从模块级的 rendererUrlParam 读取；facade 化后由调用方（engine.boot
    // 的 renderer 选项）显式传入，本层不再感知 URL。
    function applyRendererPreference(renderer) {
        if (!renderer || typeof FS === 'undefined') return;
        try {
            ensureDir('/save');
            var path = '/save/GlobalPreference.xml';
            var existing = '';
            try {
                existing = FS.readFile(path, { encoding: 'utf8' });
            } catch (e) {}
            var xml = rendererPreferenceXml(existing, renderer);
            FS.writeFile(path, xml, { encoding: 'utf8' });
            // VLFS overlay 同步（引擎 TVP_stat/读流以 VLFS 优先）
            VLFS.registerOverlayFile(path, new TextEncoder().encode(xml));
            console.log('[renderer] URL override applied:', renderer);
        } catch (e) {
            console.warn('[renderer] Failed to apply URL renderer override:', e);
        }
    }

    window.KrKr2FS = {
        waitForFS: waitForFS,
        ensureDir: ensureDir,
        writeFileToFS: writeFileToFS,
        normalizeRenderer: normalizeRenderer,
        rendererPreferenceXml: rendererPreferenceXml,
        applyRendererPreference: applyRendererPreference
    };
})();
