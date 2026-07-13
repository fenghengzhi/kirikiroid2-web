/*
 * VirtualLazyFS (VLFS) — 游戏文件懒加载虚拟文件系统（主线程单例）。
 *
 * 取代「下载 ZIP → 全量解压 → FS.writeFile 进 MEMFS」的旧链路：
 * C++ 引擎的文件 CRUD 经 cpp/core/environ/web/VirtualLazyFS.cpp 的 EM_JS 桥
 * 落到这里。读路径按需取数据，写路径走内存 overlay。
 *
 * 内存驻留硬约束：不在内存持有任何全量文件数据。
 *  - 能 Blob 则 Blob（下载包/拖拽 File/ZIP stored 条目切片，off-heap）；
 *  - 不能 Blob 则 OPFS（ZIP deflate 条目在注册阶段立即全部流式解压落盘，
 *    不懒解压；OPFS 初始化时清空）；
 *  - 内存仅限有界块级 LRU 读缓存 + 写 overlay（存档级小文件）。
 *
 * 线程模型：所有方法只在浏览器主线程调用。wasm 主线程经 JSPI（EM_ASYNC_JS）
 * 挂起等待异步读；pthread 经 emscripten proxying 投递到主线程后回调完成。
 */
(function () {
    'use strict';

    var BLOCK_SIZE = 256 * 1024;          // 块级缓存粒度
    var BLOCK_CACHE_BUDGET = 16 * 1024 * 1024; // 块缓存内存预算
    var DIRECT_READ_THRESHOLD = 512 * 1024;    // ≥ 此长度的读绕过块缓存直读源
    // 每个页面实例在这个根目录下使用独立的会话目录。Document 销毁与
    // FileSystemWritableFileStream 的底层关闭不是原子操作；若固定复用
    // vlfs-tmp/eN，新页面可能在旧写流收尾期间撞上
    // NoModificationAllowedError。会话隔离保证旧句柄只能锁住旧路径。
    var OPFS_ROOT_DIR = 'vlfs-tmp';

    function makeOpfsSessionName() {
        if (typeof crypto !== 'undefined' &&
            typeof crypto.randomUUID === 'function') {
            return 'session-' + crypto.randomUUID();
        }
        return 'session-' + Date.now().toString(36) + '-' +
            Math.random().toString(36).slice(2);
    }

    function normPath(p) {
        if (!p) return '/';
        p = p.replace(/\\/g, '/');
        if (p[0] !== '/') p = '/' + p;
        var parts = p.split('/');
        var out = [];
        for (var i = 0; i < parts.length; i++) {
            var s = parts[i];
            if (!s || s === '.') continue;
            if (s === '..') { out.pop(); continue; }
            out.push(s);
        }
        return '/' + out.join('/');
    }

    function dirname(p) {
        var i = p.lastIndexOf('/');
        return i <= 0 ? '/' : p.substring(0, i);
    }

    function basename(p) {
        return p.substring(p.lastIndexOf('/') + 1);
    }

    var VLFS = {
        _entries: new Map(),     // path → entry
        _lowerIndex: new Map(),  // lowercased path → canonical path（文件与目录都收录）
        _dirs: new Map(),        // dirPath → Map<lowerName, name>（孩子名，含子目录）
        _fds: new Map(),
        _nextFd: 1,
        _nextEntryId: 1,
        _blockCache: new Map(),  // `${entry.id}@${blockIdx}` → Uint8Array；Map 迭代序当 LRU 用
        _blockCacheBytes: 0,
        _opfsDir: null,
        _opfsSessionName: null,
        _statsHit: 0,
        _statsMiss: 0,
        // 写关闭钩子：shell.html 赋值，做 IDB write-through + MEMFS 小文件镜像
        onWriteClose: null,

        // ---------- 生命周期 ----------

        async init() {
            this._entries.clear();
            this._lowerIndex.clear();
            this._dirs.clear();
            this._fds.clear();
            this._blockCache.clear();
            this._blockCacheBytes = 0;
            this._ensureDirNode('/');
            // OPFS 是临时缓存，但不能复用上一 Document 的文件路径：浏览器
            // 可能已经释放 Web Lock，却仍在异步关闭旧 writable stream。
            // 先创建当前会话的唯一目录，使当前启动不依赖旧目录能否删除；
            // 再尽力回收其它会话和旧版直接写在 vlfs-tmp 下的 eN 文件。
            try {
                var root = await navigator.storage.getDirectory();
                var opfsRoot = await root.getDirectoryHandle(
                    OPFS_ROOT_DIR, { create: true });
                var sessionName = makeOpfsSessionName();
                this._opfsDir = await opfsRoot.getDirectoryHandle(
                    sessionName, { create: true });
                this._opfsSessionName = sessionName;

                for await (var pair of opfsRoot.entries()) {
                    var name = pair[0];
                    if (name === sessionName) continue;
                    try {
                        await opfsRoot.removeEntry(name, { recursive: true });
                    } catch (e) {
                        // 旧 Document 的写流可能仍在关闭；保留本次删不掉的
                        // 目录，后续启动继续回收。当前会话使用不同路径，
                        // 因而不会受这个残留句柄影响。
                    }
                }
            } catch (e) {
                console.warn('[vlfs] OPFS unavailable:', e);
                this._opfsDir = null;
                this._opfsSessionName = null;
            }
        },

        // ---------- 注册（shell.html 调用） ----------

        _ensureDirNode(path) {
            path = normPath(path);
            var chain = [];
            while (!this._dirs.has(path)) {
                chain.push(path);
                if (path === '/') break;
                path = dirname(path);
            }
            // 自根向下创建，保证设置孩子链接时父目录已存在
            for (var i = chain.length - 1; i >= 0; i--) {
                var p = chain[i];
                this._dirs.set(p, new Map());
                this._lowerIndex.set(p.toLowerCase(), p);
                if (p !== '/') {
                    this._dirs.get(dirname(p)).set(basename(p).toLowerCase(), basename(p));
                }
            }
        },

        _register(path, entry) {
            path = normPath(path);
            var old = this._entries.get(path);
            if (old) this._dropEntryCache(old);
            entry.id = this._nextEntryId++;
            this._entries.set(path, entry);
            this._lowerIndex.set(path.toLowerCase(), path);
            var dir = dirname(path);
            this._ensureDirNode(dir);
            this._dirs.get(dir).set(basename(path).toLowerCase(), basename(path));
            return entry;
        },

        registerBlobFile(path, blob) {
            return this._register(path, { kind: 'blob', size: blob.size, blob: blob });
        },

        registerFSAFile(path, fileHandle) {
            // size 注册时未知（避免逐文件 getFile 的启动开销时可传 -1，首次访问补全）
            return this._register(path, { kind: 'fsa', size: -1, handle: fileHandle, file: null });
        },

        async registerFSAFileEager(path, fileHandle) {
            var f = await fileHandle.getFile();
            var e = this._register(path, { kind: 'fsa', size: f.size, handle: fileHandle, file: f });
            return e;
        },

        registerRemote(path, url, size, supportsRanges) {
            return this._register(path, {
                kind: supportsRanges ? 'remote' : 'blob',
                size: size, url: url, blob: null
            });
        },

        registerOverlayFile(path, data) {
            return this._register(path, { kind: 'overlay', size: data.length, data: data, cap: data.length });
        },

        /*
         * 解析 ZIP 中央目录（EOCD/ZIP64），把每个条目注册为 VLFS 文件。
         * stored 条目 = Blob 切片，永不解压；deflate 条目在注册阶段**立即
         * 全部**流式解压落 OPFS（不懒解压，避免游戏中途首读卡顿），解压
         * 走 DecompressionStream→OPFS 管道，内存恒定不驻留全量数据。
         * opts.onProgress(done, total, path) 报告解压进度。
         * 返回 { paths, xp3Paths }。
         */
        async registerZipBlob(blob, opts) {
            opts = opts || {};
            var mountPrefix = opts.mountPrefix || '/';
            var records = await this._parseZipCentralDirectory(blob);
            // 与旧 findCommonZipPrefix 语义一致：剥离唯一公共顶层目录
            var stripPrefix = opts.stripPrefix;
            if (stripPrefix === undefined) stripPrefix = findCommonZipPrefix(records);
            var paths = [], xp3Paths = [], deflated = [];
            for (var i = 0; i < records.length; i++) {
                var r = records[i];
                var inner = stripPrefix ? r.name.substring(stripPrefix.length) : r.name;
                if (!inner) continue;
                var fsPath = normPath(mountPrefix + '/' + inner);
                if (r.method !== 0 && r.method !== 8) {
                    console.warn('[vlfs] unsupported zip method', r.method, 'for', r.name);
                }
                var entry = this._register(fsPath, {
                    kind: 'zip', size: r.uncompSize, srcBlob: blob,
                    method: r.method, compSize: r.compSize,
                    localHeaderOffset: r.localHeaderOffset,
                    dataOffset: -1,      // 懒解析 local header
                    opfsFile: null,      // deflate 落盘后的 File
                    _spill: null         // 进行中的落盘 Promise（并发去重）
                });
                if (r.method === 8) deflated.push({ path: fsPath, entry: entry });
                paths.push(fsPath);
                if (fsPath.toLowerCase().endsWith('.xp3')) xp3Paths.push(fsPath);
            }
            // 立即全量解压全部 deflate 条目（顺序执行，IO 受限；
            // _ensureOpfsSpill 幂等，残留的读时兜底路径不会重复解压）
            for (var j = 0; j < deflated.length; j++) {
                if (opts.onProgress)
                    opts.onProgress(j, deflated.length, deflated[j].path);
                await this._ensureOpfsSpill(deflated[j].entry);
            }
            if (opts.onProgress && deflated.length)
                opts.onProgress(deflated.length, deflated.length, '');
            return { paths: paths, xp3Paths: xp3Paths };
        },

        // ---------- 元数据（同步，EM_JS 直调） ----------

        // 0=不存在 1=文件 2=目录
        has(path) {
            path = normPath(path);
            if (this._entries.has(path)) return 1;
            if (this._dirs.has(path)) return 2;
            return 0;
        },

        stat(path) {
            path = normPath(path);
            var e = this._entries.get(path);
            if (e) return { size: e.size, isDir: false };
            if (this._dirs.has(path)) return { size: 0, isDir: true };
            return null;
        },

        // 大小写不敏感解析：返回规范路径或 null
        resolveCase(path) {
            path = normPath(path);
            if (this._entries.has(path) || this._dirs.has(path)) return path;
            return this._lowerIndex.get(path.toLowerCase()) || null;
        },

        listdir(path) {
            path = normPath(path);
            var children = this._dirs.get(path);
            if (!children) {
                var resolved = this.resolveCase(path);
                if (resolved === null) return null;
                children = this._dirs.get(resolved);
                if (!children) return null;
                path = resolved;
            }
            var out = [];
            for (var name of children.values()) {
                var child = path === '/' ? '/' + name : path + '/' + name;
                var e = this._entries.get(child);
                if (e) out.push({ name: name, isDir: false, size: e.size < 0 ? 0 : e.size });
                else if (this._dirs.has(child)) out.push({ name: name, isDir: true, size: 0 });
            }
            return out;
        },

        mkdir(path) {
            this._ensureDirNode(normPath(path));
            return 0;
        },

        unlink(path) {
            path = normPath(path);
            var e = this._entries.get(path);
            if (!e) return -1;
            this._dropEntryCache(e);
            this._entries.delete(path);
            this._lowerIndex.delete(path.toLowerCase());
            var d = this._dirs.get(dirname(path));
            if (d) d.delete(basename(path).toLowerCase());
            return 0;
        },

        _dropEntryCache(entry) {
            for (var key of Array.from(this._blockCache.keys())) {
                if (key.startsWith(entry.id + '@')) {
                    this._blockCacheBytes -= this._blockCache.get(key).length;
                    this._blockCache.delete(key);
                }
            }
        },

        // ---------- fd 操作 ----------

        // mode: 0=read, 1=write(truncate)
        open(path, mode) {
            path = normPath(path);
            var entry;
            if (mode === 1) {
                entry = this._register(path, {
                    kind: 'overlay', size: 0,
                    data: new Uint8Array(4096), cap: 0
                });
            } else {
                entry = this._entries.get(path);
                if (!entry) {
                    var resolved = this.resolveCase(path);
                    if (resolved) entry = this._entries.get(resolved);
                }
                if (!entry) return -1;
            }
            var fd = this._nextFd++;
            this._fds.set(fd, { entry: entry, path: path, pos: 0, mode: mode });
            return fd;
        },

        close(fd) {
            var f = this._fds.get(fd);
            if (!f) return -1;
            this._fds.delete(fd);
            if (f.mode === 1) {
                f.entry.data = f.entry.data.subarray(0, f.entry.size);
                f.entry.cap = f.entry.size;
                if (this.onWriteClose) {
                    try { this.onWriteClose(f.path, f.entry.data); }
                    catch (e) { console.warn('[vlfs] onWriteClose failed:', f.path, e); }
                }
            }
            return 0;
        },

        // whence: 0=SET 1=CUR 2=END；返回新位置（BigInt 不需要，游戏文件 < 2^53）
        seek(fd, offset, whence) {
            var f = this._fds.get(fd);
            if (!f) return -1;
            var size = f.entry.size;
            var base = whence === 1 ? f.pos : whence === 2 ? size : 0;
            var np = base + offset;
            if (np < 0) return -1;
            f.pos = np;
            return np;
        },

        sizeOf(fd) {
            var f = this._fds.get(fd);
            if (!f) return -1;
            if (f.entry.size < 0) return -1; // fsa 懒 size，需先走一次 read/await 路径
            return f.entry.size;
        },

        write(fd, src) {
            var f = this._fds.get(fd);
            if (!f || f.mode !== 1) return -1;
            var e = f.entry;
            var end = f.pos + src.length;
            if (end > e.data.length) {
                var ncap = Math.max(e.data.length * 2, end, 4096);
                var nd = new Uint8Array(ncap);
                nd.set(e.data.subarray(0, e.size));
                e.data = nd;
            }
            e.data.set(src, f.pos);
            f.pos = end;
            if (end > e.size) e.size = end;
            return src.length;
        },

        /*
         * 同步快路径：overlay 直读；其余仅当覆盖区间的块全部在缓存中时命中。
         * 未命中返回 null，调用方转 read()（JSPI/代理）。
         */
        readCached(fd, len) {
            var f = this._fds.get(fd);
            if (!f) return null;
            var e = f.entry;
            if (e.size >= 0 && f.pos >= e.size) return new Uint8Array(0); // EOF 同步返回
            if (e.kind === 'overlay') {
                var n = Math.min(len, e.size - f.pos);
                var out = e.data.subarray(f.pos, f.pos + n);
                f.pos += n;
                this._statsHit++;
                return out;
            }
            if (e.size < 0 || len >= DIRECT_READ_THRESHOLD) return null;
            var n2 = Math.min(len, e.size - f.pos);
            var first = Math.floor(f.pos / BLOCK_SIZE);
            var last = Math.floor((f.pos + n2 - 1) / BLOCK_SIZE);
            for (var b = first; b <= last; b++) {
                if (!this._blockCache.has(e.id + '@' + b)) { this._statsMiss++; return null; }
            }
            var out2 = new Uint8Array(n2);
            this._assembleFromBlocks(e, f.pos, out2);
            f.pos += n2;
            this._statsHit++;
            return out2;
        },

        async read(fd, len) {
            var f = this._fds.get(fd);
            if (!f) throw new Error('vlfs: bad fd ' + fd);
            var e = f.entry;
            if (e.kind === 'fsa' && e.size < 0) {
                e.file = await e.handle.getFile();
                e.size = e.file.size;
            }
            if (f.pos >= e.size) return new Uint8Array(0);
            var n = Math.min(len, e.size - f.pos);
            var out;
            if (e.kind === 'overlay') {
                out = e.data.subarray(f.pos, f.pos + n);
            } else if (n >= DIRECT_READ_THRESHOLD) {
                // 大读直读源，不污染块缓存
                out = await this._readSource(e, f.pos, n);
            } else {
                var first = Math.floor(f.pos / BLOCK_SIZE);
                var last = Math.floor((f.pos + n - 1) / BLOCK_SIZE);
                for (var b = first; b <= last; b++) await this._ensureBlock(e, b);
                out = new Uint8Array(n);
                this._assembleFromBlocks(e, f.pos, out);
            }
            f.pos += n;
            return out;
        },

        // ---------- 内部：块缓存与数据源 ----------

        _assembleFromBlocks(e, pos, out) {
            var done = 0;
            while (done < out.length) {
                var b = Math.floor((pos + done) / BLOCK_SIZE);
                var block = this._blockCache.get(e.id + '@' + b);
                // LRU touch
                this._blockCache.delete(e.id + '@' + b);
                this._blockCache.set(e.id + '@' + b, block);
                var off = (pos + done) - b * BLOCK_SIZE;
                var n = Math.min(out.length - done, block.length - off);
                out.set(block.subarray(off, off + n), done);
                done += n;
            }
        },

        async _ensureBlock(e, blockIdx) {
            var key = e.id + '@' + blockIdx;
            if (this._blockCache.has(key)) return;
            var off = blockIdx * BLOCK_SIZE;
            var n = Math.min(BLOCK_SIZE, e.size - off);
            var data = await this._readSource(e, off, n);
            if (this._blockCache.has(key)) return; // 并发取块去重（后到丢弃）
            this._blockCache.set(key, data);
            this._blockCacheBytes += data.length;
            while (this._blockCacheBytes > BLOCK_CACHE_BUDGET) {
                var oldest = this._blockCache.keys().next().value;
                this._blockCacheBytes -= this._blockCache.get(oldest).length;
                this._blockCache.delete(oldest);
            }
        },

        async _readSource(e, pos, len) {
            switch (e.kind) {
                case 'blob': {
                    if (!e.blob) { // registerRemote 的非 Range 降级：懒整包拉取为 Blob
                        if (!e._fetch) {
                            e._fetch = fetch(e.url).then(function (r) {
                                if (!r.ok) throw new Error('fetch ' + e.url + ': ' + r.status);
                                return r.blob();
                            });
                        }
                        e.blob = await e._fetch;
                        e.size = e.blob.size;
                    }
                    var buf = await e.blob.slice(pos, pos + len).arrayBuffer();
                    return new Uint8Array(buf);
                }
                case 'fsa': {
                    if (!e.file) { e.file = await e.handle.getFile(); e.size = e.file.size; }
                    var fbuf = await e.file.slice(pos, pos + len).arrayBuffer();
                    return new Uint8Array(fbuf);
                }
                case 'remote': {
                    var resp = await fetch(e.url, {
                        headers: { 'Range': 'bytes=' + pos + '-' + (pos + len - 1) }
                    });
                    if (resp.status !== 206 && resp.status !== 200)
                        throw new Error('range fetch ' + e.url + ': ' + resp.status);
                    var rbuf = await resp.arrayBuffer();
                    // 服务器忽略 Range 返回 200 全量时裁剪
                    if (resp.status === 200 && rbuf.byteLength > len)
                        return new Uint8Array(rbuf, pos, len).slice();
                    return new Uint8Array(rbuf, 0, Math.min(len, rbuf.byteLength)).slice();
                }
                case 'zip': {
                    if (e.method === 0) {
                        if (e.dataOffset < 0) await this._resolveZipDataOffset(e);
                        var zbuf = await e.srcBlob
                            .slice(e.dataOffset + pos, e.dataOffset + pos + len).arrayBuffer();
                        return new Uint8Array(zbuf);
                    }
                    await this._ensureOpfsSpill(e);
                    var obuf = await e.opfsFile.slice(pos, pos + len).arrayBuffer();
                    return new Uint8Array(obuf);
                }
                default:
                    throw new Error('vlfs: unreadable entry kind ' + e.kind);
            }
        },

        // local file header 的 name/extra 长度可能与中央目录不同，须读 local header 定位数据区
        async _resolveZipDataOffset(e) {
            var hdr = new DataView(await e.srcBlob
                .slice(e.localHeaderOffset, e.localHeaderOffset + 30).arrayBuffer());
            if (hdr.getUint32(0, true) !== 0x04034b50)
                throw new Error('vlfs: bad zip local header @' + e.localHeaderOffset);
            var nameLen = hdr.getUint16(26, true);
            var extraLen = hdr.getUint16(28, true);
            e.dataOffset = e.localHeaderOffset + 30 + nameLen + extraLen;
        },

        /*
         * deflate 条目流式解压落 OPFS（恒定内存），之后随机读 OPFS。
         * registerZipBlob 在注册阶段对全部 deflate 条目立即调用本函数
         * （读路径保留调用仅作幂等兜底）。DEFLATE 不可随机访问是格式
         * 约束；OPFS 由 init() 按页面会话隔离并回收旧会话。
         */
        async _ensureOpfsSpill(e) {
            if (e.opfsFile) return;
            if (e._spill) return e._spill;
            if (!this._opfsDir) throw new Error('vlfs: OPFS unavailable for deflate entry');
            var self = this;
            e._spill = (async function () {
                if (e.dataOffset < 0) await self._resolveZipDataOffset(e);
                var name = 'e' + e.id;
                var fh = await self._opfsDir.getFileHandle(name, { create: true });
                var w = await fh.createWritable();
                var src = e.srcBlob.slice(e.dataOffset, e.dataOffset + e.compSize).stream();
                await src.pipeThrough(new DecompressionStream('deflate-raw')).pipeTo(w);
                var f = await fh.getFile();
                if (f.size !== e.size) {
                    console.warn('[vlfs] spill size mismatch', name, f.size, '!=', e.size);
                    e.size = f.size;
                }
                e.opfsFile = f;
            })();
            try { await e._spill; } finally { e._spill = null; }
        },

        // ---------- ZIP 中央目录解析 ----------

        async _parseZipCentralDirectory(blob) {
            // EOCD: 22 字节定长 + ≤65535 注释，从尾部扫描签名
            var tailLen = Math.min(blob.size, 65557 + 20);
            var tailOff = blob.size - tailLen;
            var tail = new DataView(await blob.slice(tailOff).arrayBuffer());
            var eocd = -1;
            for (var i = tail.byteLength - 22; i >= 0; i--) {
                if (tail.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
            }
            if (eocd < 0) throw new Error('vlfs: not a zip (EOCD not found)');
            var count = tail.getUint16(eocd + 10, true);
            var cdSize = tail.getUint32(eocd + 12, true);
            var cdOffset = tail.getUint32(eocd + 16, true);
            if (count === 0xFFFF || cdSize === 0xFFFFFFFF || cdOffset === 0xFFFFFFFF) {
                // ZIP64: EOCD locator 紧邻 EOCD 之前
                var locOff = eocd - 20;
                if (locOff < 0 || tail.getUint32(locOff, true) !== 0x07064b50)
                    throw new Error('vlfs: zip64 locator not found');
                var z64Off = Number(tail.getBigUint64(locOff + 8, true));
                var z64 = new DataView(await blob.slice(z64Off, z64Off + 56).arrayBuffer());
                if (z64.getUint32(0, true) !== 0x06064b50)
                    throw new Error('vlfs: bad zip64 EOCD');
                count = Number(z64.getBigUint64(32, true));
                cdSize = Number(z64.getBigUint64(40, true));
                cdOffset = Number(z64.getBigUint64(48, true));
            }
            var cd = new DataView(await blob.slice(cdOffset, cdOffset + cdSize).arrayBuffer());
            var cdBytes = new Uint8Array(cd.buffer);
            var records = [];
            var p = 0;
            for (var n = 0; n < count && p + 46 <= cd.byteLength; n++) {
                if (cd.getUint32(p, true) !== 0x02014b50) break;
                var flags = cd.getUint16(p + 8, true);
                var method = cd.getUint16(p + 10, true);
                var compSize = cd.getUint32(p + 20, true);
                var uncompSize = cd.getUint32(p + 24, true);
                var nameLen = cd.getUint16(p + 28, true);
                var extraLen = cd.getUint16(p + 30, true);
                var commentLen = cd.getUint16(p + 32, true);
                var lho = cd.getUint32(p + 42, true);
                var nameBytes = cdBytes.subarray(p + 46, p + 46 + nameLen);
                // ZIP64 extra (id 0x0001)：按 0xFFFFFFFF 占位顺序补全
                if (compSize === 0xFFFFFFFF || uncompSize === 0xFFFFFFFF || lho === 0xFFFFFFFF) {
                    var ep = p + 46 + nameLen, eEnd = ep + extraLen;
                    while (ep + 4 <= eEnd) {
                        var eid = cd.getUint16(ep, true);
                        var esz = cd.getUint16(ep + 2, true);
                        if (eid === 0x0001) {
                            var q = ep + 4;
                            if (uncompSize === 0xFFFFFFFF) { uncompSize = Number(cd.getBigUint64(q, true)); q += 8; }
                            if (compSize === 0xFFFFFFFF) { compSize = Number(cd.getBigUint64(q, true)); q += 8; }
                            if (lho === 0xFFFFFFFF) { lho = Number(cd.getBigUint64(q, true)); q += 8; }
                            break;
                        }
                        ep += 4 + esz;
                    }
                }
                var name = decodeZipName(nameBytes, (flags & 0x0800) !== 0);
                p += 46 + nameLen + extraLen + commentLen;
                if (name.endsWith('/')) continue;       // 目录项
                if (flags & 0x0001) {
                    console.warn('[vlfs] encrypted zip entry skipped:', name);
                    continue;
                }
                records.push({
                    name: name, method: method, compSize: compSize,
                    uncompSize: uncompSize, localHeaderOffset: lho
                });
            }
            return records;
        },

        stats() {
            return {
                entries: this._entries.size,
                blockCacheBytes: this._blockCacheBytes,
                hit: this._statsHit, miss: this._statsMiss
            };
        }
    };

    function decodeZipName(bytes, utf8Flag) {
        if (utf8Flag) return new TextDecoder('utf-8').decode(bytes);
        var ascii = true;
        for (var i = 0; i < bytes.length; i++) if (bytes[i] >= 0x80) { ascii = false; break; }
        if (ascii) return String.fromCharCode.apply(null, bytes);
        // 无 UTF-8 标志但字节合法 UTF-8 → 按 UTF-8（与旧 JSZip 解压行为
        // 一致，常见于 macOS/Linux 打包器）；否则试 Shift-JIS（日系打包
        // 工具）；都失败回退非严格 UTF-8。fatal:true 才会真正抛错，
        // TextDecoder 默认模式只产生替换字符不报错，不能用于探测。
        try { return new TextDecoder('utf-8', { fatal: true }).decode(bytes); }
        catch (e) {}
        try { return new TextDecoder('shift-jis', { fatal: true }).decode(bytes); }
        catch (e) {}
        return new TextDecoder('utf-8').decode(bytes);
    }

    // 与旧 shell.html findCommonZipPrefix 等价：所有条目共享的唯一顶层目录
    function findCommonZipPrefix(records) {
        var prefix = null;
        for (var i = 0; i < records.length; i++) {
            var name = records[i].name;
            var slash = name.indexOf('/');
            if (slash < 0) return '';
            var top = name.substring(0, slash + 1);
            if (prefix === null) prefix = top;
            else if (prefix !== top) return '';
        }
        return prefix || '';
    }

    window.VLFS = VLFS;
})();
