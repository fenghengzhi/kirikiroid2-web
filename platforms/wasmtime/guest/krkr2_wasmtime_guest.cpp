// Generic Wasmtime headless guest ABI.
//
// This target keeps the guest-side ABI to startup/tick/error/framebuffer glue.
// Browser and Emscripten host services are provided by the Python Wasmtime
// runner; differential data is read by LLDB from guest frames.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <emscripten/emscripten.h>

#include "AppDelegate.h"

#define KRKR2_WASMTIME_USE_REAL_RENDER_MANAGER 1
#include "../../../tests/differential/wasmtime/motion_playback_wasmtime.cpp"

extern "C" {

int krkr2_env_syscall_openat(int dirfd, int path, int flags, int varargs)
    __attribute__((import_module("env"), import_name("__syscall_openat")));
int krkr2_env_syscall_fstat64(int fd, int buf)
    __attribute__((import_module("env"), import_name("__syscall_fstat64")));
int krkr2_env_syscall_stat64(int path, int buf)
    __attribute__((import_module("env"), import_name("__syscall_stat64")));
int krkr2_env_syscall_newfstatat(int dirfd, int path, int buf, int flags)
    __attribute__((import_module("env"), import_name("__syscall_newfstatat")));
int krkr2_env_syscall_lstat64(int path, int buf)
    __attribute__((import_module("env"), import_name("__syscall_lstat64")));

int __syscall_openat(int dirfd, int path, int flags, int varargs) {
    return krkr2_env_syscall_openat(dirfd, path, flags, varargs);
}

int __syscall_fstat64(int fd, int buf) {
    return krkr2_env_syscall_fstat64(fd, buf);
}

int __syscall_stat64(int path, int buf) {
    return krkr2_env_syscall_stat64(path, buf);
}

int __syscall_newfstatat(int dirfd, int path, int buf, int flags) {
    return krkr2_env_syscall_newfstatat(dirfd, path, buf, flags);
}

int __syscall_lstat64(int path, int buf) {
    return krkr2_env_syscall_lstat64(path, buf);
}

} // extern "C"

namespace {

std::unique_ptr<TVPAppDelegate> g_app_delegate;
bool g_app_started = false;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_init(const char *configJson, int len) {
    (void)configJson;
    (void)len;
    resetState();
    return runWithErrors([&]() {
        if(!g_app_delegate)
            g_app_delegate = std::make_unique<TVPAppDelegate>();
        if(!g_app_started) {
            const int rc = g_app_delegate->run();
            if(rc != 0)
                throw std::runtime_error("TVPAppDelegate::run failed");
            g_app_started = true;
        }
    });
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_startup_from(const char *path, int len) {
    return wasmtimeStartupFrom(path, len);
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_tick(double dtMs) {
    return runWithErrors([&]() {
        TVPWasmtimeTickMainScene(static_cast<float>(dtMs / 1000.0));
    });
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_send_input(const char *, int) {
    setError("krkr2_wasm_send_input: unsupported in headless v1");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_error_ptr() {
    return wasmtimeGetErrorPtr();
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_error_len() {
    return wasmtimeGetErrorLen();
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_ptr() {
    if(g_framebuffer.empty())
        return 0;
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_framebuffer.data()));
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_width() {
    return g_framebuffer_width;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_height() {
    return g_framebuffer_height;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_pitch() {
    return g_framebuffer_pitch;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_format() {
    return g_framebuffer_format;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_framebuffer_frame_no() {
    return g_framebuffer_frame_no;
}

} // extern "C"
