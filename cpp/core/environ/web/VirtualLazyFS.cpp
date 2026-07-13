//---------------------------------------------------------------------------
// VirtualLazyFS C ABI 实现 — 见 VirtualLazyFS.h 头注释。
//
// 所有 EM_JS / EM_ASYNC_JS 体只在浏览器主线程执行（window.VLFS 是主线程
// 单例）；非主线程调用由本层经 emscripten proxying 系统队列转发。
// 主线程 JSPI 挂起期间事件循环空闲，恰好可服务 worker 的代理请求，
// 两机制互不阻塞（已由 /tmp/vlfs-spike S3 实证）。
//---------------------------------------------------------------------------
#include "VirtualLazyFS.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <vector>

// ===========================================================================
// JS 桥（仅主线程调用）
// ===========================================================================

EM_JS(int, vlfs_js_enabled, (), {
    return (typeof VLFS !== 'undefined' && VLFS._entries) ? 1 : 0;
});

EM_JS(int, vlfs_js_has, (const char *path), {
    var p = UTF8ToString(path);
    var r = VLFS.has(p);
    if (r) return r;
    var resolved = VLFS.resolveCase(p);
    return resolved ? VLFS.has(resolved) : 0;
});

// 返回: 0 不存在; 1 文件; 2 目录。size 写入 *(double*)sizePtr
EM_JS(int, vlfs_js_stat, (const char *path, double *sizePtr), {
    var p = UTF8ToString(path);
    var s = VLFS.stat(p);
    if (!s) {
        var resolved = VLFS.resolveCase(p);
        if (resolved) s = VLFS.stat(resolved);
    }
    if (!s) return 0;
    HEAPF64[sizePtr >> 3] = s.size;
    return s.isDir ? 2 : 1;
});

EM_JS(int, vlfs_js_resolve_case, (const char *path, char *out, int outCap), {
    var r = VLFS.resolveCase(UTF8ToString(path));
    if (r === null) return 0;
    stringToUTF8(r, out, outCap);
    return 1;
});

// 打包目录列表到 _malloc 缓冲（C 解析后 free）：
// [u32 count] 每项 [u8 isDir][f64 size(LE,逐字节)][u16 nameLen][name utf8]
EM_JS(char *, vlfs_js_listdir, (const char *path), {
    var list = VLFS.listdir(UTF8ToString(path));
    if (list === null) return 0;
    var enc = new TextEncoder();
    var names = [];
    var total = 4;
    for (var i = 0; i < list.length; i++) {
        var nb = enc.encode(list[i].name);
        names.push(nb);
        total += 1 + 8 + 2 + nb.length;
    }
    var buf = new Uint8Array(total);
    var dv = new DataView(buf.buffer);
    dv.setUint32(0, list.length, true);
    var p = 4;
    for (var j = 0; j < list.length; j++) {
        dv.setUint8(p, list[j].isDir ? 1 : 0); p += 1;
        dv.setFloat64(p, list[j].size, true); p += 8;
        dv.setUint16(p, names[j].length, true); p += 2;
        buf.set(names[j], p); p += names[j].length;
    }
    var ptr = _malloc(total);
    HEAPU8.set(buf, ptr);
    return ptr;
});

EM_JS(int, vlfs_js_open, (const char *path, int writeMode), {
    return VLFS.open(UTF8ToString(path), writeMode ? 1 : 0);
});

EM_JS(int, vlfs_js_close, (int fd), { return VLFS.close(fd); });

EM_JS(double, vlfs_js_seek, (int fd, double offset, int whence), {
    return VLFS.seek(fd, offset, whence);
});

EM_JS(double, vlfs_js_size, (int fd), { return VLFS.sizeOf(fd); });

// FSA 懒元数据补全（罕见路径）
EM_ASYNC_JS(double, vlfs_js_ensure_size_await, (int fd), {
    var f = VLFS._fds.get(fd);
    if (!f) return -1;
    if (f.entry.kind === 'fsa' && f.entry.size < 0) {
        f.entry.file = await f.entry.handle.getFile();
        f.entry.size = f.entry.file.size;
    }
    return f.entry.size;
});

EM_JS(int, vlfs_js_write, (int fd, const void *buf, int len), {
    return VLFS.write(fd, HEAPU8.subarray(buf, buf + len));
});

// 同步快路径：overlay / 块缓存命中。未命中返回 -1
EM_JS(int, vlfs_js_read_cached, (int fd, void *buf, int len), {
    var data = VLFS.readCached(fd, len);
    if (data === null) return -1;
    HEAPU8.set(data, buf);
    return data.length;
});

// JSPI 挂起读（仅主线程、且调用栈源自 promising 导出时合法）
EM_ASYNC_JS(int, vlfs_js_read_await, (int fd, void *buf, int len), {
    try {
        var data = await VLFS.read(fd, len);
        // 注: await 期间 wasm 内存可能 grow，HEAPU8 引用的是 glue 模块级
        // 变量，updateMemoryViews 后此处取到的已是新视图
        HEAPU8.set(data, buf);
        return data.length;
    } catch (e) {
        console.error('[vlfs] read failed:', e);
        return -1;
    }
});

// 真正的 callback 异步读：即使块缓存命中也经 Promise microtask 完成，
// 从而保证 C++ completion 永不内联。ctx 非零时由 C++ finish 函数结束
// proxy_callback_with_ctx 任务，再把 completion 代理回提交线程。
EM_JS(void, vlfs_js_read_async_start,
      (void *req, void *ctx, int fd, void *buf, int len), {
          Promise.resolve()
              .then(function() {
                  if(len <= 0)
                      return new Uint8Array(0);
                  var cached = VLFS.readCached(fd, len);
                  return cached != null ? cached : VLFS.read(fd, len);
              })
              .then(
                  function(data) {
                      HEAPU8.set(data, buf);
                      _vlfs_async_read_main_finish(req, ctx, data.length);
                  },
                  function(err) {
                      console.error('[vlfs] async read failed:', err);
                      _vlfs_async_read_main_finish(req, ctx, -1);
                  });
      });

EM_JS(int, vlfs_js_unlink, (const char *path), {
    return VLFS.unlink(UTF8ToString(path));
});

EM_JS(int, vlfs_js_mkdir, (const char *path), {
    return VLFS.mkdir(UTF8ToString(path));
});

// 代理读完成回调（在主线程 JS 中调用，写回结果并唤醒阻塞的 worker）
extern "C" EMSCRIPTEN_KEEPALIVE void vlfs_proxy_finish(void *ctx) {
    emscripten_proxy_finish((em_proxying_ctx *)ctx);
}

// 在主线程发起读：先试同步缓存命中，未命中 .then() 异步完成
EM_JS(void, vlfs_js_read_proxied, (void *ctx, int fd, void *buf, int len, int *resPtr), {
    var cached = VLFS.readCached(fd, len);
    if (cached !== null) {
        HEAPU8.set(cached, buf);
        HEAP32[resPtr >> 2] = cached.length;
        _vlfs_proxy_finish(ctx);
        return;
    }
    VLFS.read(fd, len).then(function (data) {
        HEAPU8.set(data, buf);
        HEAP32[resPtr >> 2] = data.length;
        _vlfs_proxy_finish(ctx);
    }, function (err) {
        console.error('[vlfs] proxied read failed:', err);
        HEAP32[resPtr >> 2] = -1;
        _vlfs_proxy_finish(ctx);
    });
});

EM_JS(void, vlfs_js_ensure_size_proxied, (void *ctx, int fd, double *resPtr), {
    var f = VLFS._fds.get(fd);
    var done = function (v) { HEAPF64[resPtr >> 3] = v; _vlfs_proxy_finish(ctx); };
    if (!f) { done(-1); return; }
    if (f.entry.kind === 'fsa' && f.entry.size < 0) {
        f.entry.handle.getFile().then(function (file) {
            f.entry.file = file;
            f.entry.size = file.size;
            done(file.size);
        }, function () { done(-1); });
        return;
    }
    done(f.entry.size);
});

// ===========================================================================
// 线程分发
// ===========================================================================

namespace {

inline bool OnMain() { return emscripten_is_main_runtime_thread(); }

inline em_proxying_queue *Queue() { return emscripten_proxy_get_system_queue(); }

inline pthread_t MainThread() { return emscripten_main_runtime_thread_id(); }

// 把任意 lambda 同步代理到主线程执行（worker 阻塞等待）
template <typename F>
void RunOnMainSync(F &&fn) {
    emscripten_proxy_sync(
        Queue(), MainThread(),
        [](void *arg) { (*static_cast<F *>(arg))(); }, &fn);
}

struct ProxiedReadReq {
    int fd;
    void *buf;
    int len;
    int result;
};

struct AsyncReadReq {
    int fd;
    void *buf;
    int len;
    int result;
    VLFS::AsyncReadCallback completion;
};

void CompleteAsyncRead(void *arg) {
    auto *req = static_cast<AsyncReadReq *>(arg);
    int result = req->result;
    auto completion = std::move(req->completion);
    delete req;
    try {
        if(completion)
            completion(result);
    } catch(...) {
        // Completion exceptions must not escape through Emscripten's proxy
        // queue and prevent later callbacks from being serviced.
    }
}

void CancelAsyncRead(void *arg) {
    auto *req = static_cast<AsyncReadReq *>(arg);
    req->result = -1;
    CompleteAsyncRead(req);
}

void ProxiedAsyncReadTask(em_proxying_ctx *ctx, void *arg) {
    auto *req = static_cast<AsyncReadReq *>(arg);
    vlfs_js_read_async_start(req, ctx, req->fd, req->buf, req->len);
}

void ProxiedReadTask(em_proxying_ctx *ctx, void *arg) {
    auto *r = static_cast<ProxiedReadReq *>(arg);
    vlfs_js_read_proxied(ctx, r->fd, r->buf, r->len, &r->result);
}

struct ProxiedSizeReq {
    int fd;
    double result;
};

void ProxiedSizeTask(em_proxying_ctx *ctx, void *arg) {
    auto *r = static_cast<ProxiedSizeReq *>(arg);
    vlfs_js_ensure_size_proxied(ctx, r->fd, &r->result);
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void
vlfs_async_read_main_finish(void *arg, em_proxying_ctx *ctx, int result) {
    auto *req = static_cast<AsyncReadReq *>(arg);
    req->result = result;
    if(ctx) {
        emscripten_proxy_finish(ctx);
    } else {
        CompleteAsyncRead(req);
    }
}

namespace VLFS {

bool Enabled() {
#ifdef KRKR2_WASMTIME_HEADLESS
    // Wasmtime 差分 guest 无浏览器宿主，主线程代理队列也无人消费，
    // RunOnMainSync 探测会 futex abort——编译期恒禁用，走 MEMFS 路径。
    return false;
#else
    static int cached = -1;
    if (cached < 0) {
        int v = 0;
        if (OnMain()) {
            v = vlfs_js_enabled();
        } else {
            RunOnMainSync([&] { v = vlfs_js_enabled(); });
        }
        cached = v;
    }
    return cached != 0;
#endif
}

int Has(const char *path) {
    if (OnMain()) return vlfs_js_has(path);
    int r = 0;
    RunOnMainSync([&] { r = vlfs_js_has(path); });
    return r;
}

bool Stat(const char *path, uint64_t *size, int *isDir) {
    double sz = 0;
    int r;
    if (OnMain()) {
        r = vlfs_js_stat(path, &sz);
    } else {
        RunOnMainSync([&] { r = vlfs_js_stat(path, &sz); });
    }
    if (!r) return false;
    if (size) *size = (uint64_t)sz;
    if (isDir) *isDir = (r == 2) ? 1 : 0;
    return true;
}

bool ResolveCase(const char *path, std::string &out) {
    char buf[1024];
    int r;
    if (OnMain()) {
        r = vlfs_js_resolve_case(path, buf, sizeof(buf));
    } else {
        RunOnMainSync([&] { r = vlfs_js_resolve_case(path, buf, sizeof(buf)); });
    }
    if (!r) return false;
    out = buf;
    return true;
}

bool ListDir(const char *path,
             const std::function<void(const char *, bool, uint64_t)> &cb) {
    char *packed;
    if (OnMain()) {
        packed = vlfs_js_listdir(path);
    } else {
        RunOnMainSync([&] { packed = vlfs_js_listdir(path); });
    }
    if (!packed) return false;
    const uint8_t *p = (const uint8_t *)packed;
    uint32_t count;
    memcpy(&count, p, 4);
    p += 4;
    std::vector<char> name;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t isDir = *p++;
        double size;
        memcpy(&size, p, 8);
        p += 8;
        uint16_t nameLen;
        memcpy(&nameLen, p, 2);
        p += 2;
        name.assign((const char *)p, (const char *)p + nameLen);
        name.push_back('\0');
        p += nameLen;
        cb(name.data(), isDir != 0, (uint64_t)size);
    }
    free(packed);
    return true;
}

int Open(const char *path, int writeMode) {
    if (OnMain()) return vlfs_js_open(path, writeMode);
    int r;
    RunOnMainSync([&] { r = vlfs_js_open(path, writeMode); });
    return r;
}

int Close(int fd) {
    if (OnMain()) return vlfs_js_close(fd);
    int r;
    RunOnMainSync([&] { r = vlfs_js_close(fd); });
    return r;
}

int64_t Seek(int fd, int64_t offset, int whence) {
    double r;
    if (OnMain()) {
        r = vlfs_js_seek(fd, (double)offset, whence);
    } else {
        RunOnMainSync([&] { r = vlfs_js_seek(fd, (double)offset, whence); });
    }
    return (int64_t)r;
}

int64_t Size(int fd) {
    double r;
    if (OnMain()) {
        r = vlfs_js_size(fd);
        if (r < 0) r = vlfs_js_ensure_size_await(fd);
        return (int64_t)r;
    }
    RunOnMainSync([&] { r = vlfs_js_size(fd); });
    if (r >= 0) return (int64_t)r;
    ProxiedSizeReq req{ fd, -1 };
    emscripten_proxy_sync_with_ctx(Queue(), MainThread(), ProxiedSizeTask, &req);
    return (int64_t)req.result;
}

int Read(int fd, void *buf, int len) {
    if (len <= 0) return 0;
    if (OnMain()) {
        int n = vlfs_js_read_cached(fd, buf, len);
        if (n >= 0) return n;
        return vlfs_js_read_await(fd, buf, len);
    }
    ProxiedReadReq req{ fd, buf, len, -1 };
    if (!emscripten_proxy_sync_with_ctx(Queue(), MainThread(), ProxiedReadTask,
                                        &req))
        return -1;
    return req.result;
}

void ReadAsync(int fd, void *buf, int len, AsyncReadCallback completion) {
    auto *req = new AsyncReadReq{ fd, buf, len, -1, std::move(completion) };
    if(OnMain()) {
        vlfs_js_read_async_start(req, nullptr, fd, buf, len);
        return;
    }

    if(!emscripten_proxy_callback_with_ctx(
           Queue(), MainThread(), ProxiedAsyncReadTask, CompleteAsyncRead,
           CancelAsyncRead, req)) {
        req->result = -1;
        // Preserve the non-inline contract even if the target thread has
        // already exited and the proxy task cannot be enqueued.
        emscripten_async_call(CompleteAsyncRead, req, 0);
    }
}

int Write(int fd, const void *buf, int len) {
    if (len <= 0) return 0;
    if (OnMain()) return vlfs_js_write(fd, buf, len);
    int r;
    RunOnMainSync([&] { r = vlfs_js_write(fd, buf, len); });
    return r;
}

int Unlink(const char *path) {
    if (OnMain()) return vlfs_js_unlink(path);
    int r;
    RunOnMainSync([&] { r = vlfs_js_unlink(path); });
    return r;
}

int MkDir(const char *path) {
    if (OnMain()) return vlfs_js_mkdir(path);
    int r;
    RunOnMainSync([&] { r = vlfs_js_mkdir(path); });
    return r;
}

} // namespace VLFS

// ===========================================================================
// cocos2dx 桥（CCFileUtils-emscripten.cpp 以弱符号引用这两个函数，
// UI 资源/字体经 VLFS 按需读取，不再驻留 MEMFS）
// ===========================================================================

// Enabled() 门控：CCFileUtils 注释中"wasmtime 工具链不链接 VLFS、弱符号
// 为 0"的假设不成立（核心库统一编译，headless guest 同样含本文件），
// 必须在桥入口处按运行时开关短路。
extern "C" int krkr2_vlfs_exists(const char *path) {
    if (!VLFS::Enabled()) return 0;
    return VLFS::Has(path) == 1 ? 1 : 0;
}

extern "C" unsigned char *krkr2_vlfs_read_all(const char *path,
                                              unsigned int *outLen) {
    if (!VLFS::Enabled()) return nullptr;
    int fd = VLFS::Open(path, 0);
    if (fd < 0) return nullptr;
    int64_t size = VLFS::Size(fd);
    if (size < 0) {
        VLFS::Close(fd);
        return nullptr;
    }
    auto *buf = (unsigned char *)malloc(size ? (size_t)size : 1);
    if (!buf) {
        VLFS::Close(fd);
        return nullptr;
    }
    int64_t total = 0;
    while (total < size) {
        int64_t want = size - total;
        if (want > (1 << 20)) want = 1 << 20;
        int n = VLFS::Read(fd, buf + total, (int)want);
        if (n <= 0) break;
        total += n;
    }
    VLFS::Close(fd);
    if (total != size) {
        free(buf);
        return nullptr;
    }
    *outLen = (unsigned int)total;
    return buf;
}

#endif // __EMSCRIPTEN__
