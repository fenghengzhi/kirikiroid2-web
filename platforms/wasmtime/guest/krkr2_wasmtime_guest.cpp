// Generic Wasmtime headless guest ABI.
//
// The current implementation reuses the existing full-engine motion playback
// Wasmtime substrate for startup/TJS/platform stubs, but this target opts into
// the real software RenderManager instead of the motion-only no-op renderer.

#include <cstdlib>
#include <cstring>

#include <emscripten/emscripten.h>

#define KRKR2_WASMTIME_USE_REAL_RENDER_MANAGER 1
#include "../../../tests/differential/wasm/motion_playback_wasmtime.cpp"

namespace {

int g_trace_mask = 0;

int traceMaskFromConfig(const char *config, int len) {
    if(!config || len <= 0)
        return 0;
    const std::string text(config, static_cast<size_t>(len));
    int mask = 0;
    if(text.find("motion") != std::string::npos)
        mask |= 1;
    if(text.find("log") != std::string::npos)
        mask |= 2;
    if(text.find("framebuffer") != std::string::npos)
        mask |= 4;
    return mask;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_init(const char *configJson, int len) {
    resetState();
    g_trace_mask = traceMaskFromConfig(configJson, len);
    (void)g_trace_mask;
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_startup_from(const char *path, int len) {
    return mp_startup_from(path, len);
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_tick(double) {
    rebuildTraceJson();
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_send_input(const char *, int) {
    setError("krkr2_wasm_send_input: unsupported in headless v1");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void krkr2_wasm_set_trace(int mask) {
    g_trace_mask = mask;
    (void)g_trace_mask;
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_trace_ptr() {
    return mp_get_trace_ptr();
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_trace_len() {
    return mp_get_trace_len();
}

EMSCRIPTEN_KEEPALIVE
void krkr2_wasm_clear_trace() {
    g_frame_json.clear();
    rebuildTraceJson();
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_error_ptr() {
    return mp_get_error_ptr();
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_error_len() {
    return mp_get_error_len();
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
