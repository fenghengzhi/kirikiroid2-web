// IndexedDB 存档空间持久化。
//
// 每个「存档空间」是一个独立的 IndexedDB 数据库（krkr2-space-<name>），
// 内含单一 object store 'files'，key 为引擎侧的绝对路径。
// 被引擎层（write-through / 启动时恢复）和 UI 层（列表 / 导入导出）共用。

(function () {
    // --- IndexedDB save space persistence ---
    var IDB_PREFIX = 'krkr2-space-';
    var currentIdb = null;

    function idbOpen(spaceId) {
        return new Promise(function (resolve, reject) {
            var req = indexedDB.open(IDB_PREFIX + spaceId, 1);
            req.onupgradeneeded = function (e) { e.target.result.createObjectStore('files'); };
            req.onsuccess = function (e) { currentIdb = e.target.result; resolve(currentIdb); };
            req.onerror = function () { reject(req.error); };
        });
    }

    function idbSaveFile(path, data) {
        if (!currentIdb) return;
        try {
            var tx = currentIdb.transaction('files', 'readwrite');
            tx.objectStore('files').put(new Uint8Array(data), path);
        } catch (e) { console.warn('[IDB] save failed:', path, e); }
    }

    function idbLoadAll() {
        if (!currentIdb) return Promise.resolve([]);
        return new Promise(function (resolve) {
            var tx = currentIdb.transaction('files', 'readonly');
            var store = tx.objectStore('files');
            var results = [];
            var cursorReq = store.openCursor();
            cursorReq.onsuccess = function (e) {
                var cursor = e.target.result;
                if (cursor) {
                    results.push({ path: cursor.key, data: cursor.value });
                    cursor.continue();
                } else { resolve(results); }
            };
            cursorReq.onerror = function () { resolve(results); };
        });
    }

    function idbListSpaces() {
        if (indexedDB.databases) {
            return indexedDB.databases().then(function (dbs) {
                return dbs
                    .filter(function (d) { return d.name && d.name.startsWith(IDB_PREFIX); })
                    .map(function (d) { return d.name.substring(IDB_PREFIX.length); });
            });
        }
        var saved = localStorage.getItem('krkr2-spaces');
        return Promise.resolve(saved ? JSON.parse(saved) : []);
    }

    function idbRegisterSpace(name) {
        try {
            var saved = JSON.parse(localStorage.getItem('krkr2-spaces') || '[]');
            if (saved.indexOf(name) < 0) { saved.push(name); localStorage.setItem('krkr2-spaces', JSON.stringify(saved)); }
        } catch (e) {}
    }

    function idbUnregisterSpace(name) {
        try {
            var saved = JSON.parse(localStorage.getItem('krkr2-spaces') || '[]');
            saved = saved.filter(function (s) { return s !== name; });
            localStorage.setItem('krkr2-spaces', JSON.stringify(saved));
        } catch (e) {}
    }

    function idbDeleteSpace(spaceId) {
        return new Promise(function (resolve) {
            var req = indexedDB.deleteDatabase(IDB_PREFIX + spaceId);
            req.onsuccess = function () { idbUnregisterSpace(spaceId); resolve(); };
            req.onerror = function () { resolve(); };
        });
    }

    function idbGetSpaceInfo(spaceId) {
        return new Promise(function (resolve) {
            var req = indexedDB.open(IDB_PREFIX + spaceId, 1);
            req.onupgradeneeded = function (e) { e.target.result.createObjectStore('files'); };
            req.onsuccess = function (e) {
                var db = e.target.result;
                try {
                    var tx = db.transaction('files', 'readonly');
                    var store = tx.objectStore('files');
                    var count = 0, totalSize = 0;
                    var cur = store.openCursor();
                    cur.onsuccess = function (ev) {
                        var c = ev.target.result;
                        if (c) { count++; totalSize += c.value.byteLength || 0; c.continue(); }
                        else { db.close(); resolve({ count: count, size: totalSize }); }
                    };
                    cur.onerror = function () { db.close(); resolve({ count: 0, size: 0 }); };
                } catch (err) { db.close(); resolve({ count: 0, size: 0 }); }
            };
            req.onerror = function () { resolve({ count: 0, size: 0 }); };
        });
    }

    async function idbRestoreSaves() {
        var files = await idbLoadAll();
        if (files.length === 0) return;
        await window.KrKr2FS.waitForFS();
        for (var i = 0; i < files.length; i++) {
            // MEMFS 镜像（遗留 fopen 读）+ VLFS overlay（引擎读流优先走 VLFS）
            window.KrKr2FS.writeFileToFS(files[i].path, files[i].data);
            VLFS.registerOverlayFile(files[i].path, new Uint8Array(files[i].data));
        }
        console.log('[IDB] Restored ' + files.length + ' save file(s)');
    }

    async function idbExportZip(spaceId) {
        var db = await new Promise(function (resolve, reject) {
            var req = indexedDB.open(IDB_PREFIX + spaceId, 1);
            req.onupgradeneeded = function (e) { e.target.result.createObjectStore('files'); };
            req.onsuccess = function (e) { resolve(e.target.result); };
            req.onerror = function () { reject(req.error); };
        });
        var files = await new Promise(function (resolve) {
            var tx = db.transaction('files', 'readonly');
            var results = [];
            var cur = tx.objectStore('files').openCursor();
            cur.onsuccess = function (e) {
                var c = e.target.result;
                if (c) { results.push({ path: c.key, data: c.value }); c.continue(); }
                else { resolve(results); }
            };
            cur.onerror = function () { resolve(results); };
        });
        db.close();
        var zip = new JSZip();
        for (var i = 0; i < files.length; i++) {
            zip.file(files[i].path.replace(/^\//, ''), files[i].data);
        }
        var blob = await zip.generateAsync({ type: 'blob' });
        var a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = spaceId + '-saves.zip';
        a.click();
        URL.revokeObjectURL(a.href);
    }

    async function idbImportZip(file) {
        var zip = await JSZip.loadAsync(file);
        var spaceName = file.name.replace(/\.zip$/i, '').replace(/-saves$/, '');
        await idbOpen(spaceName);
        idbRegisterSpace(spaceName);
        var entries = [];
        zip.forEach(function (p, e) { if (!e.dir) entries.push({ path: p, entry: e }); });
        for (var i = 0; i < entries.length; i++) {
            var data = await entries[i].entry.async('uint8array');
            var path = '/' + entries[i].path;
            idbSaveFile(path, data);
        }
        if (currentIdb) { currentIdb.close(); currentIdb = null; }
        return spaceName;
    }

    window.KrKr2IDB = {
        open: idbOpen,
        saveFile: idbSaveFile,
        loadAll: idbLoadAll,
        listSpaces: idbListSpaces,
        registerSpace: idbRegisterSpace,
        unregisterSpace: idbUnregisterSpace,
        deleteSpace: idbDeleteSpace,
        getSpaceInfo: idbGetSpaceInfo,
        restoreSaves: idbRestoreSaves,
        exportZip: idbExportZip,
        importZip: idbImportZip
    };
})();
