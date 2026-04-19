/* harness.c — minimal aarch64 bionic guest for differential oracles.
 *
 * Runs on a real Android emulator/device (API 21+) driven via adb. The
 * real linker64 handles relocations, TLS, libc/libm resolution — so
 * the host Python side (adb_engine.py) doesn't have to fake any of it.
 *
 * Protocol with the host (line-oriented ASCII over stdin/stdout):
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
 *     TJS_GLOBAL <utf16le_key_hex>            -- fetch global as heap-resident tTJSVariant,
 *                                                returns its guest-VA hex
 *     QUIT
 *
 *   Responses (harness -> host):
 *     OK <retval_hex>          // CALL/TJS_GLOBAL with int/uint/bool/ptr return
 *     OK_DOUBLE <bits_hex>     // CALL with double return
 *     OK_VOID                  // CALL with void return, or QUIT, WRITE, TJS_EXEC
 *     OK_DATA <hex_bytes>      // READ
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---------- stdio helpers ---------- */

static int read_line(char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        int c = getchar();
        if (c == EOF) return n == 0 ? -1 : (int)n;
        if (c == '\n') break;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return (int)n;
}

static void println(const char *s) {
    fputs(s, stdout);
    fputc('\n', stdout);
    fflush(stdout);
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
    fputs(out, stdout);
    fputc('\n', stdout);
    fflush(stdout);
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

static uint64_t g_so_base = 0;
static uint8_t  g_tjs_buf[0x68];   /* tTJS instance storage (ctor-filled) */
static int      g_tjs_inited = 0;

static inline void *libkrkr2_fn(uint64_t off) {
    return (void *)(uintptr_t)(g_so_base + off);
}

static void handle_tjs_init(void) {
    if (g_tjs_inited) {
        char buf[48];
        snprintf(buf, sizeof(buf), "OK %llx", (unsigned long long)(uintptr_t)g_tjs_buf);
        println(buf);
        return;
    }
    /* tTJS::tTJS(this) — x0 = this */
    typedef void (*ctor_fn)(void *this_ptr);
    ((ctor_fn)libkrkr2_fn(OFF_TTJS_CTOR))(g_tjs_buf);
    g_tjs_inited = 1;
    char buf[48];
    snprintf(buf, sizeof(buf), "OK %llx", (unsigned long long)(uintptr_t)g_tjs_buf);
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
            g_tjs_buf, &slot, NULL, NULL, NULL, 0);
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
    void *ctx = ((getg_fn)libkrkr2_fn(OFF_TTJS_GETGLOBAL))(g_tjs_buf);
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

/* ---------- main ---------- */

int main(int argc, char **argv) {
    const char *so_path = (argc > 1) ? argv[1] : "libkrkr2.so";

    void *heap = mmap((void *)HEAP_VA, HEAP_SIZE, PROT_READ | PROT_WRITE,
                      MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (heap == MAP_FAILED || heap != (void *)HEAP_VA) {
        fprintf(stderr, "harness: mmap heap at 0x%lx failed\n",
                (unsigned long)HEAP_VA);
        return 1;
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

    /* Announce readiness. */
    char ready[64];
    snprintf(ready, sizeof(ready), "READY %llx %lx",
             (unsigned long long)so_base, (unsigned long)HEAP_VA);
    println(ready);

    /* Command loop. */
    static char line[131072];  /* accommodates WRITE hex payload */
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
        } else if (strncmp(line, "TJS_EXEC ", 9) == 0) {
            handle_tjs_exec(line + 9);
        } else if (strncmp(line, "TJS_GLOBAL ", 11) == 0) {
            handle_tjs_global(line + 11);
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

    dlclose(h);
    return 0;
}
