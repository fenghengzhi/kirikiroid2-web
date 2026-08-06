// 本地来源加载器：单 .xp3 文件、整包 .zip、上传的文件夹、
// File System Access 目录句柄。

(function () {
    var L = window.KrKr2Loaders;

    function report(hooks, pct, text) {
        if (typeof hooks.onProgress === 'function') hooks.onProgress(pct, text);
    }

    // --- Fallback: .xp3 file picker ---
    L.handlers['xp3-file'] = async function (src, hooks) {
        report(hooks, 50, 'Indexing ' + src.file.name + '...');
        await window.KrKr2VLFS.ready;
        // File 即 Blob：零拷贝注册，按需切片读取
        VLFS.registerBlobFile('/data.xp3', src.file);
        report(hooks, 100, 'Starting...');
        return { startupXp3Path: null };
    };

    L.handlers['zip-file'] = async function (src, hooks) {
        report(hooks, 10, 'Indexing ' + src.file.name + '...');
        await window.KrKr2VLFS.ready;
        // deflate 条目在注册阶段立即全部解压落 OPFS（stored 条目零成本）
        var reg = await VLFS.registerZipBlob(src.file, {
            onProgress: function (done, total, path) {
                report(hooks, 10 + Math.round(done / total * 90),
                    'Extracting (' + done + '/' + total + ') ' + path.substring(1));
            }
        });
        console.log('[vlfs] local zip registered: ' + reg.paths.length + ' entries');
        report(hooks, 100, 'Starting...');
        return {
            startupXp3Path: await L.resolveStartupXp3(reg.xp3Paths, hooks, src.entry)
        };
    };

    L.handlers['folder'] = async function (src, hooks) {
        var files = src.files;
        var total = files.length;
        await window.KrKr2VLFS.ready;

        var xp3Paths = [];
        var folderPrefix = '/' + files[0].webkitRelativePath.split('/')[0];

        for (var i = 0; i < total; i++) {
            var file = files[i];
            var fullPath = '/' + file.webkitRelativePath;
            var innerPath = fullPath.substring(folderPrefix.length);
            var lowerName = file.name.toLowerCase();

            report(hooks, Math.round(i / total * 100),
                'Indexing (' + (i + 1) + '/' + total + ') ' + file.name);

            // File 即 Blob：零拷贝注册
            VLFS.registerBlobFile(innerPath, file);
            if (lowerName.endsWith('.xp3')) xp3Paths.push(innerPath);
        }

        return {
            startupXp3Path: await L.resolveStartupXp3(xp3Paths, hooks, src.entry)
        };
    };

    // --- File System Access API: directory picker + save sync ---
    async function enumerateDir(dirHandle, prefix) {
        var files = [];
        for await (var entry of dirHandle.values()) {
            var path = prefix + '/' + entry.name;
            if (entry.kind === 'directory') {
                files = files.concat(await enumerateDir(entry, path));
            } else {
                files.push({ path: path, handle: entry });
            }
        }
        return files;
    }

    L.handlers['fsa-dir'] = async function (src, hooks) {
        var hostDirHandle = src.handle;

        report(hooks, 0, 'Scanning directory...');
        await window.KrKr2VLFS.ready;

        var dirPrefix = '/' + hostDirHandle.name;
        var entries = await enumerateDir(hostDirHandle, dirPrefix);
        var total = entries.length;
        if (total === 0) {
            var err = new Error('Directory is empty.');
            err.code = 'empty-dir';
            throw err;
        }

        // 目录非空才挂上写回句柄，与原实现的顺序一致（空目录不污染状态）
        window.KrKr2Engine.setHostDir(hostDirHandle, dirPrefix);

        var xp3Paths = [];
        for (var i = 0; i < total; i++) {
            var entry = entries[i];
            var innerPath = entry.path.substring(dirPrefix.length);
            var lowerName = innerPath.substring(innerPath.lastIndexOf('/') + 1).toLowerCase();

            report(hooks, Math.round(i / total * 100),
                'Indexing (' + (i + 1) + '/' + total + ') ' + innerPath.substring(1));

            // 仅注册句柄+尺寸元数据，内容按需经 FSA 切片读取
            await VLFS.registerFSAFileEager(innerPath, entry.handle);
            if (lowerName.endsWith('.xp3')) xp3Paths.push(innerPath);
        }

        report(hooks, 100, 'Starting...');
        return {
            startupXp3Path: await L.resolveStartupXp3(xp3Paths, hooks, src.entry)
        };
    };

    L.enumerateDir = enumerateDir;
})();
