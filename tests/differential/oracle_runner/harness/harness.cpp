/* harness.cpp — aarch64 guest for differential oracles.
 *
 * Loaded as `libharness.so` inside the repacked `krkr2-harness.apk`
 * (HarnessActivity extends Cocos2dxActivity). Cocos2dx runs its normal
 * init chain in this process; commands that need TVPMainScene or the
 * full TVPScriptEngine must wait until that GL-thread bootstrap reaches
 * the corresponding state.
 *
 * The host (adb_engine.py) speaks line-oriented ASCII over a TCP
 * socket forwarded by `adb forward tcp:5039 tcp:5039`. The single
 * entry point is `harness_rpc_main_fd(so_path, fd)`, invoked from
 * Java_org_github_krkr2_HarnessActivity_runRpcServeFd via JNI.
 *
 * Protocol with the host:
 *
 *   Startup:
 *     harness -> READY <libkrkr2_base_hex> <heap_base_hex>\n
 *
 *   Per command (host -> harness):
 *     CALL <fn_hex> <ret> <nints> <int_hex>* <ndbls> <dbl_bits_hex>*
 *         ret ∈ {int,uint,bool,ptr,double,void}; ints in hex u64; doubles
 *         as their IEEE754 bit pattern in hex u64 (avoids parsing issues).
 *     READ <addr_hex> <n_dec>
 *     WRITE <addr_hex> <n_dec> <hex_bytes>
 *     TJS_INIT                                -- build a private tTJS instance
 *     TJS_EXEC <ascii_hex>                    -- tTJS::ExecScript on the private tTJS
 *     TJS_EXEC_STR <ascii_hex>                -- ExecScript that yields a String;
 *                                                returns OK_STR <utf8_hex>
 *     TJS_GLOBAL <utf16le_key_hex>            -- fetch global as heap-resident tTJSVariant,
 *                                                returns its guest-VA hex
 *     STARTUP_FROM <utf8_hex_path>            -- call TVPMainScene::startupFrom
 *                                                with a real gnustl std::string
 *     QUIT
 *
 *   Responses (harness -> host):
 *     OK <retval_hex>          // CALL/TJS_GLOBAL with int/uint/bool/ptr return
 *     OK_DOUBLE <bits_hex>     // CALL with double return
 *     OK_VOID                  // CALL with void return, or QUIT, WRITE, TJS_EXEC
 *     OK_DATA <hex_bytes>      // READ
 *     OK_STR  <utf8_hex>       // TJS_EXEC_STR
 *     ERR <message>            // any failure
 *
 * AAPCS64 dispatch: we use a "universal signature" trick — declare a function
 * pointer taking all 8 x-args and all 8 d-args, cast the target to it, and
 * let the compiler marshal via AAPCS64. The callee reads only the regs its
 * real signature names; unused slots carry garbage that's ignored.
 */

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HEAP_VA   0x50000000UL
#define HEAP_SIZE (16 * 1024 * 1024)

/* libkrkr2 internal offsets (relative to the .so load base). Sourced from
 * IDA: sub_97EA40 is the tTJS C++ ctor (allocates native class registry
 * for Array/Dict/Math/etc); sub_97FD58 is tTJS::ExecScript; sub_97F310
 * is tTJS::GetGlobalNoAddRef which returns the GlobalContext dispatch;
 * sub_A13878 is the ttstr ASCII constructor (returns a refcounted
 * tTJSVariantString*); sub_A0F778 is tTJSVariant::Clear. */
#define OFF_TTJS_CTOR          0x97EA40
#define OFF_TTJS_EXECSCRIPT    0x97FD58
#define OFF_TTJS_GETGLOBAL     0x97F310
#define OFF_TTSTR_FROM_ASCII   0xA13878
#define OFF_TTJSVARIANT_CLEAR  0xA0F778

/* TVPMainScene bootstrap helpers. Symbol names observed in libkrkr2.so:
 *   _ZN12TVPMainScene11GetInstanceEv    -> 0xA9D4D4
 *   _ZN12TVPMainScene11startupFromERKSs -> 0xA9F954
 * The `Ss` parameter is GNU libstdc++/gnustl std::string, so this harness
 * must be built with the matching legacy ABI. The string is a temporary
 * const& consumed by startupFrom; ownership never crosses the .so boundary. */
#define OFF_TVPMAINSCENE_GETINSTANCE 0xA9D4D4
#define OFF_TVPMAINSCENE_STARTUPFROM 0xA9F954

/* TVPScriptEngine global slot (0x1AE2FD0 relative to libkrkr2 base).
 * cocos2d's applicationDidFinishLaunching path (TVPMainScene::doStartup
 * → TVPInitScriptEngine) parks a live tTJS* here. Under the APK launch
 * path this slot is populated only after the native startup chain reaches
 * script-engine init; early RPC commands may still see it as NULL.
 *
 * (Historical note: the ELF / app_process launch paths used to fall back
 * to a minimal tTJS ctor on a static buffer when the slot was NULL; those
 * modes have been removed and the APK path is the only supported one.) */
#define OFF_TVP_SCRIPT_ENGINE_GLOBAL 0x1AE2FD0

/* iTJSDispatch2 vtable slot for PropGet — standard Kirikiri interface
 * layout: slot 0=Release, slot 1=AddRef, slot 2=QueryInterface, ..., slot 4 (0x20 offset) = PropGet.
 * Verified against sub_69A754 decompile: `(*(vtable+32))(this, flags, key, hint, &result, objthis)`. */
#define VTBL_PROPGET_OFFSET    0x20

/* tTJSVariant is a fat 24-byte struct: 16 bytes payload + 4-byte type + 4-byte pad. */
#define SIZEOF_TTJSVARIANT 24

typedef uint64_t u64_fn(uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t,
                        double, double, double, double,
                        double, double, double, double);
typedef double   dbl_fn(uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t,
                        double, double, double, double,
                        double, double, double, double);
typedef void     void_fn(uint64_t, uint64_t, uint64_t, uint64_t,
                         uint64_t, uint64_t, uint64_t, uint64_t,
                         double, double, double, double,
                         double, double, double, double);

/* ---------- RPC I/O (fd-based; stdio just passes 0/1) ---------- */

/* Global I/O fd context. `harness_rpc_main_fd()` sets them to the same
 * socket fd for both (TCP sockets are bidirectional). Every command
 * handler uses only read_line/println/write_all, never stdio directly. */
static int g_in_fd  = -1;
static int g_out_fd = -1;

static ssize_t write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, p + done, n - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)w;
    }
    return (ssize_t)done;
}

static int read_line(char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r;
        do { r = read(g_in_fd, &c, 1); } while (r < 0 && errno == EINTR);
        if (r <= 0) return n == 0 ? -1 : (int)n;
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

static void write_bytes(const void *buf, size_t n) {
    (void)write_all(g_out_fd, buf, n);
}

static void println(const char *s) {
    size_t len = strlen(s);
    write_bytes(s, len);
    write_bytes("\n", 1);
}

/* parse hex u64; returns 0 on ok, -1 on error. Stops at whitespace/end. */
static int parse_u64_hex(const char **cur, uint64_t *out) {
    while (**cur == ' ') (*cur)++;
    if (!isxdigit((unsigned char)**cur)) return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(*cur, &end, 16);
    if (errno || end == *cur) return -1;
    *out = (uint64_t)v;
    *cur = end;
    return 0;
}

static int parse_u64_dec(const char **cur, uint64_t *out) {
    while (**cur == ' ') (*cur)++;
    if (!isdigit((unsigned char)**cur)) return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(*cur, &end, 10);
    if (errno || end == *cur) return -1;
    *out = (uint64_t)v;
    *cur = end;
    return 0;
}

static int parse_token(const char **cur, char *out, size_t cap) {
    while (**cur == ' ') (*cur)++;
    size_t n = 0;
    while (**cur && **cur != ' ' && n + 1 < cap) {
        out[n++] = *(*cur)++;
    }
    out[n] = '\0';
    return (int)n;
}

/* hex decode into dst. Returns bytes written or -1. */
static int decode_hex(const char *s, size_t slen, uint8_t *dst, size_t cap) {
    if (slen % 2 != 0 || slen / 2 > cap) return -1;
    for (size_t i = 0; i < slen / 2; i++) {
        unsigned hi = s[2 * i], lo = s[2 * i + 1];
        hi = isdigit(hi) ? hi - '0' : (tolower(hi) - 'a' + 10);
        lo = isdigit(lo) ? lo - '0' : (tolower(lo) - 'a' + 10);
        dst[i] = (hi << 4) | lo;
    }
    return (int)(slen / 2);
}

static void hex_encode(const uint8_t *src, size_t n, char *dst) {
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        dst[2 * i]     = h[(src[i] >> 4) & 0xF];
        dst[2 * i + 1] = h[src[i] & 0xF];
    }
    dst[2 * n] = '\0';
}

/* ---------- command handlers ---------- */

static void handle_call(const char *args) {
    uint64_t fn_addr;
    char ret_kind[16];
    uint64_t nints, ndbls;
    uint64_t ints[8] = {0};
    double   dbls[8] = {0};

    const char *cur = args;
    if (parse_u64_hex(&cur, &fn_addr) < 0) { println("ERR parse fn"); return; }
    if (parse_token(&cur, ret_kind, sizeof(ret_kind)) <= 0) {
        println("ERR parse ret_kind"); return;
    }
    if (parse_u64_dec(&cur, &nints) < 0 || nints > 8) {
        println("ERR parse nints"); return;
    }
    for (uint64_t i = 0; i < nints; i++) {
        if (parse_u64_hex(&cur, &ints[i]) < 0) {
            println("ERR parse int"); return;
        }
    }
    if (parse_u64_dec(&cur, &ndbls) < 0 || ndbls > 8) {
        println("ERR parse ndbls"); return;
    }
    for (uint64_t i = 0; i < ndbls; i++) {
        uint64_t bits;
        if (parse_u64_hex(&cur, &bits) < 0) {
            println("ERR parse dbl"); return;
        }
        memcpy(&dbls[i], &bits, sizeof(double));
    }

    char buf[64];
    if (strcmp(ret_kind, "double") == 0) {
        dbl_fn *fn = (dbl_fn *)(uintptr_t)fn_addr;
        double r = fn(ints[0], ints[1], ints[2], ints[3],
                      ints[4], ints[5], ints[6], ints[7],
                      dbls[0], dbls[1], dbls[2], dbls[3],
                      dbls[4], dbls[5], dbls[6], dbls[7]);
        uint64_t bits;
        memcpy(&bits, &r, sizeof(bits));
        snprintf(buf, sizeof(buf), "OK_DOUBLE %llx", (unsigned long long)bits);
        println(buf);
    } else if (strcmp(ret_kind, "void") == 0) {
        void_fn *fn = (void_fn *)(uintptr_t)fn_addr;
        fn(ints[0], ints[1], ints[2], ints[3],
           ints[4], ints[5], ints[6], ints[7],
           dbls[0], dbls[1], dbls[2], dbls[3],
           dbls[4], dbls[5], dbls[6], dbls[7]);
        println("OK_VOID");
    } else {
        /* int / uint / bool / ptr — all fetched from x0 */
        u64_fn *fn = (u64_fn *)(uintptr_t)fn_addr;
        uint64_t r = fn(ints[0], ints[1], ints[2], ints[3],
                        ints[4], ints[5], ints[6], ints[7],
                        dbls[0], dbls[1], dbls[2], dbls[3],
                        dbls[4], dbls[5], dbls[6], dbls[7]);
        snprintf(buf, sizeof(buf), "OK %llx", (unsigned long long)r);
        println(buf);
    }
}

static void handle_read(const char *args) {
    uint64_t addr, n;
    const char *cur = args;
    if (parse_u64_hex(&cur, &addr) < 0 || parse_u64_dec(&cur, &n) < 0) {
        println("ERR parse"); return;
    }
    if (n > 65536) { println("ERR read too large"); return; }
    static char out[4 + 2 * 65536 + 1];
    memcpy(out, "OK_DATA ", 8);
    hex_encode((const uint8_t *)(uintptr_t)addr, (size_t)n, out + 8);
    size_t total = 8 + 2 * (size_t)n;
    write_bytes(out, total);
    write_bytes("\n", 1);
}

static void handle_write(const char *args) {
    uint64_t addr, n;
    const char *cur = args;
    if (parse_u64_hex(&cur, &addr) < 0 || parse_u64_dec(&cur, &n) < 0) {
        println("ERR parse"); return;
    }
    while (*cur == ' ') cur++;
    size_t slen = strlen(cur);
    if (slen != 2 * n) { println("ERR hex size mismatch"); return; }
    if (decode_hex(cur, slen, (uint8_t *)(uintptr_t)addr, (size_t)n) < 0) {
        println("ERR hex decode"); return;
    }
    println("OK_VOID");
}

/* ---------- TJS helpers ---------- */

/* handle_tjs_init picks a strategy at runtime:
 *
 *   1. If the TVPScriptEngine global at libkrkr2+0x1AE2FD0 is already
 *      populated (i.e. cocos2d's native startup chain already initialized
 *      TVPScriptEngine), read it.
 *      This is "Full TJS" — every NCB class registered, Motion.* etc.
 *   2. Fallback: call the tTJS C++ ctor (sub_97EA40) on a static 0x68-byte
 *      buffer. Registers Array/Dict/Math only. Kept for defensiveness —
 *      the APK scalar runners still use this mode before startupFrom. */
static uint64_t g_so_base = 0;
static uint8_t  g_tjs_buf[0x68];    /* fallback static tTJS storage */
static void    *g_tjs_ptr = NULL;   /* filled by handle_tjs_init */
static int      g_tjs_inited = 0;

static inline void *libkrkr2_fn(uint64_t off) {
    return (void *)(uintptr_t)(g_so_base + off);
}

/* Crash diagnostics for the Full-TJS init path — `TVPInitScriptEngine`
 * recurses through a config/argv init chain that expects globals to
 * already be populated by normal Android app launch. If any of those
 * reads fall over, emit something useful on stderr before the process
 * dies so we can differentiate "SIGBUS at wild PC" (uninit'd fn ptr)
 * from "SIGSEGV deref NULL" (missing Application context).
 *
 * Raw sigcontext offsets on bionic arm64 (see bionic/libc/kernel/uapi/
 * asm-arm64/asm/sigcontext.h): fault_address @ +0x00, regs[31] @ +0x08,
 * sp @ +0x100, pc @ +0x108. ucontext_t.uc_mcontext starts at +0xB0
 * inside ucontext_t on bionic arm64 LP64. */
#define MCTX_OFF_IN_UCTX    0xB0
#define MCTX_FAULT_ADDR     0x00
#define MCTX_SP_OFF         0x100
#define MCTX_PC_OFF         0x108
#define MCTX_LR_OFF         0xF8   /* regs[30] */

/* Custom std::terminate handler.
 *
 * The harness must be built with NDK r17c + gnustl_static to match
 * libkrkr2.so's old GNU C++ ABI. Keep a demangle-free terminate handler
 * anyway: if a future build accidentally reintroduces libc++abi, or a
 * foreign exception escapes past our catches, we want a clear process
 * death instead of a crash inside a mismatched demangler. */
static void harness_terminate_handler() {
    (void)write(2, "ERR std::terminate called (cross-runtime exception?)\n", 53);
    (void)write(1, "ERR terminate\n", 14);
    _exit(3);
}

static void install_crash_handlers(void) {
    std::set_terminate(harness_terminate_handler);
}

static void handle_tjs_init(void) {
    if (g_tjs_inited) {
        char buf[48];
        snprintf(buf, sizeof(buf), "OK %llx", (unsigned long long)(uintptr_t)g_tjs_ptr);
        println(buf);
        return;
    }

    /* Step 1: if cocos2d already initialized the engine (APK mode), the
     * TVPScriptEngine global at libkrkr2+0x1AE2FD0 holds a live tTJS*.
     * This is the preferred path because every NCB class (Motion.*,
     * Window, Layer, Plugins, ...) is registered. */
    void **slot = (void **)libkrkr2_fn(OFF_TVP_SCRIPT_ENGINE_GLOBAL);
    if (*slot != NULL) {
        g_tjs_ptr = *slot;
        g_tjs_inited = 1;
        char buf[64];
        snprintf(buf, sizeof(buf), "OK %llx",
                 (unsigned long long)(uintptr_t)g_tjs_ptr);
        println(buf);
        return;
    }

    /* Step 2: standalone path — no cocos2d, no JVM. Just construct a
     * bare tTJS into our static buffer. Registers Array/Dict/Math — good
     * enough for bezier_curve / position_interp. */
    typedef void (*ctor_fn)(void *this_ptr);
    ((ctor_fn)libkrkr2_fn(OFF_TTJS_CTOR))(g_tjs_buf);
    g_tjs_ptr = g_tjs_buf;
    g_tjs_inited = 1;
    char buf[64];
    snprintf(buf, sizeof(buf), "OK %llx",
             (unsigned long long)(uintptr_t)g_tjs_ptr);
    println(buf);
}

/* TJS_EXEC runs a UTF-8/ASCII TJS source string through ExecScript.
 * Protocol: argument is the source text, hex-encoded (so we can carry
 * newlines / special chars over the ASCII line protocol). */
static void handle_tjs_exec(const char *args) {
    if (!g_tjs_inited) {
        println("ERR tjs not init");
        return;
    }
    while (*args == ' ') args++;
    size_t hex_len = strlen(args);
    if (hex_len % 2 != 0) {
        println("ERR bad hex");
        return;
    }
    size_t src_len = hex_len / 2;
    static uint8_t src_buf[65536];
    if (src_len + 1 > sizeof(src_buf)) {
        println("ERR source too large");
        return;
    }
    if (decode_hex(args, hex_len, src_buf, src_len) < 0) {
        println("ERR hex decode");
        return;
    }
    src_buf[src_len] = 0;

    /* Build a ttstr from ASCII. sub_A13878 returns a tjs_char* (really a
     * tTJSVariantString* pointing just past the refcount header). The
     * ExecScript arg is a `const ttstr &`, which in Kirikiri ABI is
     * `tTJSVariantString**`.
     *
     * Wrap in try/catch — TJS raises C++ exceptions (eTJSScriptError /
     * eTJSVariantError) on malformed input. Without the catch a terminate()
     * aborts the whole harness, killing the batch run. */
    try {
        typedef void *(*mkstr_fn)(const char *ascii);
        void *ttstr_payload = ((mkstr_fn)libkrkr2_fn(OFF_TTSTR_FROM_ASCII))(
            (const char *)src_buf);
        if (!ttstr_payload) {
            println("ERR ttstr alloc failed");
            return;
        }
        void *slot = ttstr_payload;
        typedef int (*exec_fn)(void *this_ptr, void **script_ref,
                               void *result, void *context,
                               void *name, int lineofs);
        (void)((exec_fn)libkrkr2_fn(OFF_TTJS_EXECSCRIPT))(
            g_tjs_ptr, &slot, NULL, NULL, NULL, 0);
    } catch (std::exception &e) {
        char err[256];
        snprintf(err, sizeof(err),
                 "ERR exec threw std::exception: %.200s", e.what());
        println(err);
        return;
    } catch (...) {
        println("ERR exec threw unknown exception");
        return;
    }

    println("OK_VOID");
}

/* Forward decl — definition lives below the TJS_GLOBAL section. */
static void *tjs_alloc_variant(void);

/* TJS_EXEC_STR runs a script and reads back the last-expression result as
 * a UTF-8 string. Used by the motion_playback oracle adapter to ferry a
 * JSON snapshot from inside libkrkr2 to the host without adding any new
 * libkrkr2 dependencies — the script is expected to evaluate to a String.
 *
 * Layout assumptions (verified against libkrkr2 sub_A12E4C / sub_A138BC /
 * sub_A0F790 decompiles):
 *   tTJSVariant (24B):
 *     +0   payload (8B): tTJSVariantString * for tvtString
 *     +16  type tag (4B): 2 = tvtString
 *   tTJSVariantString:
 *     +60  Length (uint32, in tjs_char chars)
 *     +16  inline UTF-16LE chars when Length < 22
 *     +8   pointer to UTF-16LE chars when Length >= 22 (already past the
 *          8-byte capacity header set by sub_A138BC).
 */
static void handle_tjs_exec_str(const char *args) {
    if (!g_tjs_inited) {
        println("ERR tjs not init");
        return;
    }
    while (*args == ' ') args++;
    size_t hex_len = strlen(args);
    if (hex_len % 2 != 0) {
        println("ERR bad hex");
        return;
    }
    size_t src_len = hex_len / 2;
    static uint8_t src_buf[262144];
    if (src_len + 1 > sizeof(src_buf)) {
        println("ERR source too large");
        return;
    }
    if (decode_hex(args, hex_len, src_buf, src_len) < 0) {
        println("ERR hex decode");
        return;
    }
    src_buf[src_len] = 0;

    /* Allocate a fresh result variant on the per-session variant heap so
     * its lifetime is bounded by tjs_reset_variant_heap(). */
    void *result_var = tjs_alloc_variant();
    if (!result_var) { println("ERR variant heap full"); return; }

    try {
        typedef void *(*mkstr_fn)(const char *ascii);
        void *ttstr_payload = ((mkstr_fn)libkrkr2_fn(OFF_TTSTR_FROM_ASCII))(
            (const char *)src_buf);
        if (!ttstr_payload) {
            println("ERR ttstr alloc failed");
            return;
        }
        void *slot = ttstr_payload;
        typedef int (*exec_fn)(void *this_ptr, void **script_ref,
                               void *result, void *context,
                               void *name, int lineofs);
        (void)((exec_fn)libkrkr2_fn(OFF_TTJS_EXECSCRIPT))(
            g_tjs_ptr, &slot, result_var, NULL, NULL, 0);
    } catch (std::exception &e) {
        char err[256];
        snprintf(err, sizeof(err),
                 "ERR exec threw std::exception: %.200s", e.what());
        println(err);
        return;
    } catch (...) {
        println("ERR exec threw unknown exception");
        return;
    }

    /* Inspect the variant: type tag at +16, payload at +0. */
    uint32_t tag = *(const uint32_t *)((const uint8_t *)result_var + 16);
    if (tag != 2 /* tvtString */) {
        char err[64];
        snprintf(err, sizeof(err), "ERR result not string (type=%u)", tag);
        println(err);
        return;
    }
    void *str_ptr = *(void **)result_var;
    if (!str_ptr) {
        println("ERR null string payload");
        return;
    }
    uint32_t length = *(const uint32_t *)((const uint8_t *)str_ptr + 60);
    const uint16_t *u16;
    if (length < 22) {
        u16 = (const uint16_t *)((const uint8_t *)str_ptr + 16);
    } else {
        u16 = *(const uint16_t **)((const uint8_t *)str_ptr + 8);
        if (!u16) {
            println("ERR null long-string ptr");
            return;
        }
    }

    /* UTF-16LE → UTF-8, then hex-encode. Worst case bytes-per-char = 4
     * (BMP outside ASCII can take 3, surrogate pairs handled below). Hex
     * doubles that. Cap at 1 MiB to mirror the harness line buffer. */
    static uint8_t utf8_buf[1u << 20];
    size_t utf8_len = 0;
    for (uint32_t i = 0; i < length;) {
        if (utf8_len + 4 > sizeof(utf8_buf)) {
            println("ERR string too large");
            return;
        }
        uint32_t cp = u16[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < length) {
            uint32_t low = u16[i];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            utf8_buf[utf8_len++] = (uint8_t)cp;
        } else if (cp < 0x800) {
            utf8_buf[utf8_len++] = (uint8_t)(0xC0 | (cp >> 6));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8_buf[utf8_len++] = (uint8_t)(0xE0 | (cp >> 12));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else {
            utf8_buf[utf8_len++] = (uint8_t)(0xF0 | (cp >> 18));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            utf8_buf[utf8_len++] = (uint8_t)(0x80 | (cp & 0x3F));
        }
    }

    static char hex_buf[2 * sizeof(utf8_buf) + 16];
    static const char hex_digits[] = "0123456789abcdef";
    size_t hex_pos = 0;
    static const char prefix[] = "OK_STR ";
    memcpy(hex_buf + hex_pos, prefix, sizeof(prefix) - 1);
    hex_pos += sizeof(prefix) - 1;
    for (size_t i = 0; i < utf8_len; ++i) {
        if (hex_pos + 2 >= sizeof(hex_buf)) {
            println("ERR hex buffer overflow");
            return;
        }
        hex_buf[hex_pos++] = hex_digits[(utf8_buf[i] >> 4) & 0xF];
        hex_buf[hex_pos++] = hex_digits[utf8_buf[i] & 0xF];
    }
    hex_buf[hex_pos] = 0;
    println(hex_buf);
}

/* TJS_GLOBAL looks up a global variable on the tTJS's GlobalContext by
 * UTF-16LE name (hex-encoded, null-terminated in the wire format). It
 * allocates a fresh 24-byte tTJSVariant from the guest heap, writes the
 * PropGet result into it, and returns the guest VA of that variant. */
static void *g_heap_cursor = NULL;   /* separate from the main oracle heap */

static void *tjs_alloc_variant(void) {
    static uint8_t tjs_variant_area[SIZEOF_TTJSVARIANT * 4096];
    if (!g_heap_cursor) g_heap_cursor = tjs_variant_area;
    uint8_t *cur = (uint8_t *)g_heap_cursor;
    if (cur + SIZEOF_TTJSVARIANT > tjs_variant_area + sizeof(tjs_variant_area)) {
        return NULL;
    }
    g_heap_cursor = cur + SIZEOF_TTJSVARIANT;
    memset(cur, 0, SIZEOF_TTJSVARIANT);
    return cur;
}

static void tjs_reset_variant_heap(void) { g_heap_cursor = NULL; }

static void handle_tjs_global(const char *args) {
    if (!g_tjs_inited) {
        println("ERR tjs not init");
        return;
    }
    while (*args == ' ') args++;
    size_t hex_len = strlen(args);
    if (hex_len == 0 || hex_len % 2 != 0) {
        println("ERR bad key hex");
        return;
    }
    size_t key_bytes = hex_len / 2;
    /* Key must be null-terminated UTF-16LE → key_bytes must be even and
     * end in \0\0. Caller is responsible for appending L'\0'. */
    if (key_bytes % 2 != 0 || key_bytes < 2) {
        println("ERR key not utf-16");
        return;
    }
    static uint8_t key_buf[4096];
    if (key_bytes > sizeof(key_buf)) {
        println("ERR key too long");
        return;
    }
    if (decode_hex(args, hex_len, key_buf, key_bytes) < 0) {
        println("ERR hex decode");
        return;
    }

    /* GlobalContext = tTJS::GetGlobalNoAddRef(tTJS*). Returns iTJSDispatch2*. */
    typedef void *(*getg_fn)(void *this_ptr);
    void *ctx = ((getg_fn)libkrkr2_fn(OFF_TTJS_GETGLOBAL))(g_tjs_ptr);
    if (!ctx) {
        println("ERR null global context");
        return;
    }

    /* Allocate result tTJSVariant. */
    void *result = tjs_alloc_variant();
    if (!result) { println("ERR variant heap full"); return; }

    /* iTJSDispatch2::PropGet(flags=0, key_u16, hint=NULL, result, objthis=ctx).
     * vtable slot at offset +0x20 -> PropGet. */
    void **vtable = *(void ***)ctx;
    typedef int (*propget_fn)(void *this_ptr, uint32_t flags,
                              const uint16_t *key, void *hint,
                              void *result, void *objthis);
    propget_fn propget = (propget_fn)vtable[VTBL_PROPGET_OFFSET / 8];
    int rc = propget(ctx, 0, (const uint16_t *)key_buf, NULL, result, ctx);
    if (rc < 0) {
        char err[48];
        snprintf(err, sizeof(err), "ERR PropGet rc=%d", rc);
        println(err);
        return;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "OK %llx", (unsigned long long)(uintptr_t)result);
    println(buf);
}

/* STARTUP_FROM constructs a real GNU-libstdc++ std::string inside the
 * ABI-matched harness and passes it by const& to TVPMainScene::startupFrom.
 * This replaces the old host-side hand-built gnustl string layout. */
static void handle_startup_from(const char *args) {
    while (*args == ' ') args++;
    size_t hex_len = strlen(args);
    if (hex_len == 0 || hex_len % 2 != 0) {
        println("ERR bad path hex");
        return;
    }
    size_t path_len = hex_len / 2;
    static uint8_t path_buf[8192];
    if (path_len + 1 > sizeof(path_buf)) {
        println("ERR path too long");
        return;
    }
    if (decode_hex(args, hex_len, path_buf, path_len) < 0) {
        println("ERR hex decode");
        return;
    }
    path_buf[path_len] = 0;

    try {
        typedef void *(*get_instance_fn)(void);
        void *scene = ((get_instance_fn)libkrkr2_fn(
            OFF_TVPMAINSCENE_GETINSTANCE))();
        if (!scene) {
            println("ERR TVPMainScene::GetInstance returned null");
            return;
        }

        std::string path((const char *)path_buf, path_len);
        typedef bool (*startup_from_fn)(void *this_ptr,
                                        const std::string &path);
        bool ok = ((startup_from_fn)libkrkr2_fn(
            OFF_TVPMAINSCENE_STARTUPFROM))(scene, path);

        char buf[16];
        snprintf(buf, sizeof(buf), "OK %x", ok ? 1 : 0);
        println(buf);
    } catch (std::exception &e) {
        char err[256];
        snprintf(err, sizeof(err),
                 "ERR startupFrom threw std::exception: %.200s", e.what());
        println(err);
    } catch (...) {
        println("ERR startupFrom threw unknown exception");
    }
}

/* ---------- RPC main (shared between standalone ELF and JNI entry) ---------- */

static int harness_bootstrap(const char *so_path) {
    /* Install before dlopen so faults in libkrkr2 loader code get reported. */
    install_crash_handlers();

    /* Reserve the oracle heap region if not already mapped by a previous
     * call (idempotent: MAP_FIXED would silently overwrite, so check
     * first). When running inside an APK, libkrkr2 is already loaded
     * by cocos2d, so dlopen below just bumps refcount. */
    static int heap_mapped = 0;
    if (!heap_mapped) {
        void *heap = mmap((void *)HEAP_VA, HEAP_SIZE, PROT_READ | PROT_WRITE,
                          MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (heap == MAP_FAILED || heap != (void *)HEAP_VA) {
            fprintf(stderr, "harness: mmap heap at 0x%lx failed\n",
                    (unsigned long)HEAP_VA);
            return 1;
        }
        heap_mapped = 1;
    }

    void *h = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "harness: dlopen(%s) failed: %s\n", so_path, dlerror());
        return 1;
    }

    /* Resolve .so base via dlsym + dladdr on any exported symbol. */
    /* Try several well-known exports (C++ names are mangled; use C ones). */
    void *probe = dlsym(h, "Java_org_tvp_kirikiri2_KR2Activity_initDump");
    if (!probe) probe = dlsym(h, "LzmaDec_Init");
    if (!probe) probe = dlsym(h, "FT_Init_FreeType");
    if (!probe) probe = dlsym(h, "_init");
    uint64_t so_base = 0;
    if (probe) {
        Dl_info info;
        if (dladdr(probe, &info)) so_base = (uint64_t)info.dli_fbase;
    }
    if (!so_base) {
        fprintf(stderr, "harness: could not determine libkrkr2 base\n");
        return 1;
    }

    g_so_base = so_base;   /* cached for TJS helpers */

    /* Announce readiness on the caller-selected output fd. */
    char ready[64];
    snprintf(ready, sizeof(ready), "READY %llx %lx",
             (unsigned long long)so_base, (unsigned long)HEAP_VA);
    println(ready);
    return 0;
}

/* Serve a single RPC session on the given fd (TCP socket, both directions).
 * HarnessActivity inside the repacked APK invokes this through JNI when a
 * client connects. */
extern "C" int harness_rpc_main_fd(const char *so_path, int fd) {
    g_in_fd = fd;
    g_out_fd = fd;
    int rc = harness_bootstrap(so_path);
    if (rc != 0) return rc;

    static char line[131072];
    for (;;) {
        int n = read_line(line, sizeof(line));
        if (n < 0) break;
        if (n == 0) continue;

        if (strncmp(line, "CALL ", 5) == 0) {
            handle_call(line + 5);
        } else if (strncmp(line, "READ ", 5) == 0) {
            handle_read(line + 5);
        } else if (strncmp(line, "WRITE ", 6) == 0) {
            handle_write(line + 6);
        } else if (strncmp(line, "TJS_INIT", 8) == 0) {
            handle_tjs_init();
        } else if (strncmp(line, "TJS_EXEC_STR ", 13) == 0) {
            handle_tjs_exec_str(line + 13);
        } else if (strncmp(line, "TJS_EXEC ", 9) == 0) {
            handle_tjs_exec(line + 9);
        } else if (strncmp(line, "TJS_GLOBAL ", 11) == 0) {
            handle_tjs_global(line + 11);
        } else if (strncmp(line, "STARTUP_FROM ", 13) == 0) {
            handle_startup_from(line + 13);
        } else if (strncmp(line, "TJS_RESET", 9) == 0) {
            tjs_reset_variant_heap();
            println("OK_VOID");
        } else if (strncmp(line, "QUIT", 4) == 0) {
            println("OK_VOID");
            break;
        } else {
            println("ERR unknown command");
        }
    }
    return 0;
}
