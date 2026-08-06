// --- VLFS 加载器：注册元数据即可玩，无全量下载/解压/复制 ---
// 远端来源（?xp3= / ?game= / 画廊配置的 URL）。

(function () {
    var L = window.KrKr2Loaders;

    function report(hooks, pct, text) {
        if (typeof hooks.onProgress === 'function') hooks.onProgress(pct, text);
    }

    // 流式下载为 Blob，带进度且 JS 堆峰值有界：每 64MB 块段合并为
    // 段 Blob（Blob 拼接引用而非复制），下载中堆驻留 ≤64MB。
    async function fetchBlobWithProgress(url, onProgress) {
        var resp = await fetch(url);
        if (!resp.ok) throw new Error('HTTP ' + resp.status + ' ' + resp.statusText);
        var total = parseInt(resp.headers.get('Content-Length') || '0', 10);
        if (!resp.body) return await resp.blob();
        var reader = resp.body.getReader();
        var segments = [], chunks = [], chunkBytes = 0, loaded = 0;
        var SEGMENT = 64 * 1024 * 1024;
        while (true) {
            var r = await reader.read();
            if (r.done) break;
            chunks.push(r.value);
            chunkBytes += r.value.length;
            loaded += r.value.length;
            if (onProgress) onProgress(loaded, total);
            if (chunkBytes >= SEGMENT) {
                segments.push(new Blob(chunks));
                chunks = [];
                chunkBytes = 0;
            }
        }
        if (chunks.length) segments.push(new Blob(chunks));
        return new Blob(segments);
    }

    // 探测远程服务器对 Range 请求的支持。优先 HEAD 查标头；若无标头则用
    // 1-byte Range (0-0) 探测，防止因跨域 CORS 未 Exposed 标头而误降级为全量下载。
    async function probeRemoteRange(url) {
        var ranges = false, size = -1, fingerprint = '';
        try {
            var head = await fetch(url, { method: 'HEAD' });
            if (head.ok) {
                ranges = (head.headers.get('Accept-Ranges') || '').toLowerCase() === 'bytes';
                size = parseInt(head.headers.get('Content-Length') || '-1', 10);
                var contentSha256 = (head.headers.get('X-Content-SHA256') || '').trim().toLowerCase();
                if (/^[0-9a-f]{64}$/.test(contentSha256)) fingerprint = 'sha256-' + contentSha256;
            }
        } catch (e) {}

        if (!ranges || size <= 0) {
            try {
                var test = await fetch(url, { headers: { 'Range': 'bytes=0-0' } });
                if (test.status === 206) {
                    ranges = true;
                    var cr = test.headers.get('Content-Range'); // e.g. "bytes 0-0/123456"
                    if (cr) {
                        var match = cr.match(/\/(\d+)$/);
                        if (match) size = parseInt(match[1], 10);
                    }
                }
            } catch (e) {}
        }

        return { ranges: ranges, size: size, fingerprint: fingerprint };
    }

    // ?xp3=：优先 HTTP Range 懒加载；服务器不支持时整包 Blob（仍 off-heap）
    L.handlers['xp3-url'] = async function (src, hooks) {
        report(hooks, 0, 'Probing game data...');
        await window.KrKr2VLFS.ready;

        var probe = await probeRemoteRange(src.url);

        if (probe.ranges && probe.size > 0) {
            VLFS.registerRemote('/data.xp3', src.url, probe.size, true);
            console.log('[vlfs] remote xp3 (Range lazy-load): ' + src.url + ', ' + probe.size + ' bytes');
        } else {
            report(hooks, 0, 'Downloading game data...');
            var blob = await fetchBlobWithProgress(src.url, function (loaded, total) {
                if (total > 0) {
                    var pct = Math.round(loaded / total * 100);
                    report(hooks, pct, 'Downloading... ' + pct + '%');
                } else {
                    report(hooks, null, 'Downloading... ' + (loaded / 1048576).toFixed(1) + ' MB');
                }
            });
            VLFS.registerBlobFile('/data.xp3', blob);
            console.log('[vlfs] remote xp3 (whole blob fallback): ' + blob.size + ' bytes');
        }

        // 单文件挂在固定路径，引擎自己找得到，不需要 startupXp3Path
        return { startupXp3Path: null };
    };

    // ?game=：优先通过 HTTP Range 只读 ZIP 中央目录和实际文件区间；服务器
    // 不支持 Range 时才退回整包 Blob。这样 ASan 的大 Wasm 内存与大游戏 ZIP
    // 不会同时常驻 Chromium renderer，文件树和条目读取语义保持不变。
    L.handlers['zip-url'] = async function (src, hooks) {
        report(hooks, 0, 'Probing game archive...');
        await window.KrKr2VLFS.ready;

        var probe = await probeRemoteRange(src.url);

        var reg;
        if (probe.ranges && probe.size > 0) {
            report(hooks, 0, 'Reading archive index...');
            reg = await VLFS.registerZipRemote(src.url, probe.size, {
                fingerprint: probe.fingerprint || undefined,
                onProgress: function (done, total, path) {
                    report(hooks, Math.round(done / total * 100),
                        'Extracting (' + done + '/' + total + ') ' + path.substring(1));
                }
            });
            console.log('[vlfs] remote zip (Range): ' + src.url + ', ' + probe.size + ' bytes');
        } else {
            report(hooks, 0, 'Downloading game archive...');
            var blob = await fetchBlobWithProgress(src.url, function (loaded, total) {
                if (total > 0) {
                    var pct = Math.round(loaded / total * 100);
                    // 下载占进度条前 60%，解压占后 40%
                    report(hooks, pct * 0.6,
                        'Downloading... ' + pct + '% (' + (loaded / 1048576).toFixed(1) + ' MB)');
                } else {
                    report(hooks, null, 'Downloading... ' + (loaded / 1048576).toFixed(1) + ' MB');
                }
            });
            report(hooks, 60, 'Extracting archive...');
            reg = await VLFS.registerZipBlob(blob, {
                onProgress: function (done, total, path) {
                    report(hooks, 60 + Math.round(done / total * 40),
                        'Extracting (' + done + '/' + total + ') ' + path.substring(1));
                }
            });
            console.log('[vlfs] zip blob fallback: ' + blob.size + ' bytes');
        }
        console.log('[vlfs] zip registered: ' + reg.paths.length + ' entries, ' +
                    reg.xp3Paths.length + ' xp3');

        report(hooks, 100, 'Starting game...');
        return {
            startupXp3Path: await L.resolveStartupXp3(reg.xp3Paths, hooks, src.entry)
        };
    };
    // ?json=：JSON 清单模式加载散装文件
    L.handlers['json-url'] = async function (src, hooks) {
        report(hooks, 0, 'Fetching game manifest...');
        await window.KrKr2VLFS.ready;

        var res = await fetch(src.url);
        if (!res.ok) throw new Error('Failed to fetch manifest: ' + res.status);
        var manifest = await res.json();
        
        var xp3Paths = [];
        var totalFiles = manifest.length;
        
        for (var i = 0; i < totalFiles; i++) {
            var item = manifest[i];
            if (!item.name || !item.url) continue;
            
            report(hooks, Math.round((i / totalFiles) * 100), 'Mounting ' + item.name + '...');
            
            // 规范化路径名，必须以 '/' 开头
            var path = item.name.startsWith('/') ? item.name : ('/' + item.name);
            
            // 如果 JSON 中没提供 size，则探测获取
            var size = item.size;
            if (typeof size !== 'number' || size <= 0) {
                var probe = await probeRemoteRange(item.url);
                size = probe.size;
            }
            
            if (size > 0) {
                VLFS.registerRemote(path, item.url, size, true);
                if (path.toLowerCase().endsWith('.xp3')) {
                    xp3Paths.push(path);
                }
            } else {
                console.warn('[vlfs] Skip mounting ' + item.name + ': could not determine size');
            }
        }
        
        console.log('[vlfs] json manifest registered: ' + totalFiles + ' entries, ' + xp3Paths.length + ' xp3');
        report(hooks, 100, 'Starting game...');
        
        return {
            startupXp3Path: await L.resolveStartupXp3(xp3Paths, hooks, src.entry)
        };
    };

    L.fetchBlobWithProgress = fetchBlobWithProgress;
})();
