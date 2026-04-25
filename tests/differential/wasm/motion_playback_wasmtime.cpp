// Wasmtime-only Motion playback differential target.
//
// This module intentionally executes the real logo_test_oracle.xp3
// startup.tjs path through TJS. It supplies a headless Window layer and a
// test-local implementation of the MotionTraceWeb symbols so the production
// Browser trace hook remains untouched.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <emscripten/emscripten.h>

#include "Application.h"
#include "FilePathUtil.h"
#include "GraphicsLoaderIntf.h"
#include "PluginImpl.h"
#include "Platform.h"
#include "RenderManager.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"
#include "SysInitIntf.h"
#include "SystemControl.h"
#include "TVPScreen.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "combase.h"
#include "motionplayer/MotionTraceWeb.h"
#include "motionplayer/Player.h"
#include "motionplayer/RuntimeSupport.h"
#include "tjsNative.h"
#include "tjsError.h"

extern void TVPLoadInternalPlugins();

class tTJSNC_PhaseVocoder : public tTJSNativeClass {
public:
    tTJSNC_PhaseVocoder();
    static tjs_uint32 ClassID;

private:
    iTJSNativeInstance *CreateNativeInstance() override;
};

namespace {

std::string g_trace_json = "[]";
std::string g_error;
std::string g_stage;
std::vector<std::string> g_frame_json;
std::vector<unsigned char> g_framebuffer;
int g_framebuffer_width = 0;
int g_framebuffer_height = 0;
int g_framebuffer_pitch = 0;
int g_framebuffer_format = 0;
int g_framebuffer_frame_no = 0;
std::unordered_map<std::string, std::vector<unsigned char>> g_staged_files;
std::unordered_map<int, const std::vector<unsigned char> *> g_open_streams;
int g_next_stream_id = 1;

std::string jsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 2);
    for(char c : in) {
        switch(c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void writeJsonString(std::ostringstream &out, const std::string &value) {
    out << '"' << jsonEscape(value) << '"';
}

std::string localPathFromStorageUri(const std::string &path) {
    constexpr char kFileDot[] = "file://.";
    constexpr char kFile[] = "file://";
    if(path.rfind(kFileDot, 0) == 0) {
        return path.substr(sizeof(kFileDot) - 1);
    }
    if(path.rfind(kFile, 0) == 0) {
        return path.substr(sizeof(kFile) - 1);
    }
    return path;
}

std::string stripLeadingDotSlash(std::string path) {
    while(path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    return path;
}

void addStagedAlias(const std::string &alias,
                    const std::vector<unsigned char> &bytes) {
    if(alias.empty()) return;
    g_staged_files[alias] = bytes;
}

void stageFileBytes(const std::string &path,
                    const unsigned char *data,
                    size_t len) {
    std::vector<unsigned char> bytes(data, data + len);
    const std::string local = localPathFromStorageUri(path);
    const std::string stripped = stripLeadingDotSlash(local);

    addStagedAlias(path, bytes);
    addStagedAlias(local, bytes);
    addStagedAlias(stripped, bytes);
    if(!stripped.empty() && stripped[0] != '/') {
        addStagedAlias("/" + stripped, bytes);
        addStagedAlias("./" + stripped, bytes);
    }
}

const std::vector<unsigned char> *findStagedFile(const std::string &path) {
    auto it = g_staged_files.find(path);
    if(it != g_staged_files.end()) return &it->second;

    const std::string local = localPathFromStorageUri(path);
    it = g_staged_files.find(local);
    if(it != g_staged_files.end()) return &it->second;

    const std::string stripped = stripLeadingDotSlash(local);
    it = g_staged_files.find(stripped);
    if(it != g_staged_files.end()) return &it->second;

    if(!stripped.empty() && stripped[0] != '/') {
        it = g_staged_files.find("/" + stripped);
        if(it != g_staged_files.end()) return &it->second;
    }
    if(!local.empty() && local[0] == '/') {
        it = g_staged_files.find(local.substr(1));
        if(it != g_staged_files.end()) return &it->second;
    }
    return nullptr;
}

void writeJsonDouble(std::ostringstream &out, double value) {
    if(std::isnan(value) || std::isinf(value)) {
        out << "null";
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.12g", value);
    out << buf;
}

std::string ptrString(const void *ptr) {
    if(!ptr) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%p", ptr);
    return buf;
}

void appendNodeJson(std::ostringstream &out,
                    const motion::detail::MotionNode &node,
                    int flatIndex) {
    const auto &accum = node.accumulated;
    out << "{";
    out << "\"index\":" << flatIndex;
    out << ",\"label\":";
    writeJsonString(out, node.layerName);
    out << ",\"nodeType\":" << node.nodeType;
    out << ",\"visible\":" << (accum.visible ? "true" : "false");
    out << ",\"active\":" << (accum.active ? "true" : "false");
    out << ",\"flipX\":" << (accum.flipX ? "true" : "false");
    out << ",\"flipY\":" << (accum.flipY ? "true" : "false");
    out << ",\"posX\":";
    writeJsonDouble(out, accum.posX);
    out << ",\"posY\":";
    writeJsonDouble(out, accum.posY);
    out << ",\"posZ\":";
    writeJsonDouble(out, accum.posZ);
    out << ",\"angleDeg\":";
    writeJsonDouble(out, accum.angle);
    out << ",\"scaleX\":";
    writeJsonDouble(out, accum.scaleX);
    out << ",\"scaleY\":";
    writeJsonDouble(out, accum.scaleY);
    out << ",\"slantX\":";
    writeJsonDouble(out, accum.slantX);
    out << ",\"slantY\":";
    writeJsonDouble(out, accum.slantY);
    out << ",\"opacity\":" << accum.opacity;
    out << ",\"blendMode\":" << node.stencilType;
    out << ",\"drawFlag\":" << (node.drawFlag ? "true" : "false");
    out << ",\"drawnThisFrame\":"
        << (node.drawnThisFrame ? "true" : "false");
    out << ",\"currentImage\":";
    writeJsonString(out, node.interpolatedCache.src);
    out << "}";
}

void rebuildTraceJson() {
    std::ostringstream out;
    out << "[";
    for(size_t i = 0; i < g_frame_json.size(); ++i) {
        if(i) out << ",";
        out << g_frame_json[i];
    }
    out << "]";
    g_trace_json = out.str();
}

struct TraceState {
    bool inProgress = false;
    void *objthis = nullptr;
    std::vector<motion::Player *> players;
    int frameCounter = 0;
};

TraceState &traceState() {
    static TraceState state;
    return state;
}

void emitProgressFrame(motion::Player *fallbackPlayer) {
    auto &state = traceState();
    std::ostringstream out;
    out << "{";
    out << "\"frameId\":" << state.frameCounter++;
    out << ",\"objthis\":";
    if(state.objthis) {
        writeJsonString(out, ptrString(state.objthis));
    } else {
        out << "null";
    }
    out << ",\"topPlayer\":";
    motion::Player *topPlayer =
        !state.players.empty() ? state.players.front() : fallbackPlayer;
    if(topPlayer) {
        writeJsonString(out, ptrString(topPlayer));
    } else {
        out << "null";
    }
    out << ",\"playerCount\":" << state.players.size();
    out << ",\"layout\":\"wasmtime-runtime\"";
    out << ",\"layers\":[";

    int flatIndex = 0;
    bool first = true;
    for(motion::Player *player : state.players) {
        if(!player) continue;
        const auto *runtime = player->runtime();
        if(!runtime) continue;
        for(const auto &node : runtime->nodes) {
            if(!first) out << ",";
            first = false;
            appendNodeJson(out, node, flatIndex++);
        }
    }

    out << "]}";
    g_frame_json.push_back(out.str());
}

size_t framebufferBytesPerPixel(TVPTextureFormat::e format) {
    return format == TVPTextureFormat::Gray ? 1u : 4u;
}

void captureFramebuffer(iTVPTexture2D *tex) {
    if(!tex)
        return;

    const int width = static_cast<int>(tex->GetWidth());
    const int height = static_cast<int>(tex->GetHeight());
    if(width <= 0 || height <= 0)
        return;

    const auto format = tex->GetFormat();
    const size_t bytesPerPixel = framebufferBytesPerPixel(format);
    const int sourcePitch = tex->GetPitch();
    const int sourcePitchAbs = std::abs(sourcePitch);
    const int destPitch = width * static_cast<int>(bytesPerPixel);

    g_framebuffer.assign(static_cast<size_t>(destPitch) *
                             static_cast<size_t>(height),
                         0);
    for(int y = 0; y < height; ++y) {
        const void *line = tex->GetScanLineForRead(static_cast<tjs_uint>(y));
        if(!line)
            continue;
        std::memcpy(g_framebuffer.data() + static_cast<size_t>(y) * destPitch,
                    line,
                    static_cast<size_t>(std::min(sourcePitchAbs, destPitch)));
    }

    g_framebuffer_width = width;
    g_framebuffer_height = height;
    g_framebuffer_pitch = destPitch;
    g_framebuffer_format = static_cast<int>(format);
    ++g_framebuffer_frame_no;
}

class HeadlessWindowLayer final : public iWindowLayer {
public:
    void SetPaintBoxSize(tjs_int w, tjs_int h) override { setSize(w, h); }
    bool GetFormEnabled() override { return true; }
    void SetDefaultMouseCursor() override {}
    void GetCursorPos(tjs_int &x, tjs_int &y) override { x = 0; y = 0; }
    void SetCursorPos(tjs_int, tjs_int) override {}
    void SetHintText(const ttstr &) override {}
    void SetAttentionPoint(tjs_int, tjs_int, const tTVPFont *) override {}
    void ZoomRectangle(tjs_int &, tjs_int &, tjs_int &, tjs_int &) override {}
    void BringToFront() override {}
    void ShowWindowAsModal() override {}
    bool GetVisible() override { return visible_; }
    void SetVisible(bool visible) override { visible_ = visible; }
    const char *GetCaption() override { return caption_.c_str(); }
    void SetCaption(const std::string &caption) override { caption_ = caption; }
    void SetWidth(tjs_int w) override { width_ = w; }
    void SetHeight(tjs_int h) override { height_ = h; }
    void SetSize(tjs_int w, tjs_int h) override { setSize(w, h); }
    void GetSize(tjs_int &w, tjs_int &h) override { w = width_; h = height_; }
    [[nodiscard]] tjs_int GetWidth() const override { return width_; }
    [[nodiscard]] tjs_int GetHeight() const override { return height_; }
    void GetWinSize(tjs_int &w, tjs_int &h) override { w = width_; h = height_; }
    void SetZoom(tjs_int numer, tjs_int denom) override {
        ZoomNumer = numer;
        ZoomDenom = denom;
    }
    void UpdateDrawBuffer(iTVPTexture2D *tex) override {
        captureFramebuffer(tex);
    }
    void InvalidateClose() override { delete this; }
    bool GetWindowActive() override { return true; }
    void Close() override { visible_ = false; }
    void OnCloseQueryCalled(bool) override {}
    void InternalKeyDown(tjs_uint16, tjs_uint32) override {}
    void OnKeyUp(tjs_uint16, int) override {}
    void OnKeyPress(tjs_uint16, int, bool, bool) override {}
    [[nodiscard]] tTVPImeMode GetDefaultImeMode() const override {
        return imDisable;
    }
    void SetImeMode(tTVPImeMode) override {}
    void ResetImeMode() override {}
    void UpdateWindow(tTVPUpdateType) override {}
    void SetVisibleFromScript(bool visible) override { visible_ = visible; }
    void SetUseMouseKey(bool use) override { useMouseKey_ = use; }
    [[nodiscard]] bool GetUseMouseKey() const override { return useMouseKey_; }
    void ResetMouseVelocity() override {}
    void ResetTouchVelocity(tjs_int) override {}
    bool GetMouseVelocity(float &x, float &y, float &speed) const override {
        x = 0.0f;
        y = 0.0f;
        speed = 0.0f;
        return false;
    }
    void TickBeat() override {}
    cocos2d::Node *GetPrimaryArea() override { return nullptr; }

private:
    void setSize(tjs_int w, tjs_int h) {
        width_ = w;
        height_ = h;
    }

    tjs_int width_ = 640;
    tjs_int height_ = 480;
    bool visible_ = false;
    bool useMouseKey_ = false;
    std::string caption_;
};

#ifndef KRKR2_WASMTIME_USE_REAL_RENDER_MANAGER
class HeadlessTexture final : public iTVPTexture2D {
public:
    HeadlessTexture(unsigned int w, unsigned int h, TVPTextureFormat::e fmt) :
        iTVPTexture2D(static_cast<tjs_int>(w), static_cast<tjs_int>(h)),
        format_(fmt) {
        if(format_ == TVPTextureFormat::None) format_ = TVPTextureFormat::RGBA;
        pixel_.resize(static_cast<size_t>(std::max(1u, w)) *
                      static_cast<size_t>(std::max(1u, h)) * bytesPerPixel());
    }

    TVPTextureFormat::e GetFormat() const override { return format_; }
    const void *GetScanLineForRead(tjs_uint l) override {
        const size_t offset = static_cast<size_t>(l) * GetPitch();
        if(offset >= pixel_.size()) return nullptr;
        return pixel_.data() + offset;
    }
    void *GetScanLineForWrite(tjs_uint l) override {
        return const_cast<void *>(GetScanLineForRead(l));
    }
    tjs_int GetPitch() const override {
        return static_cast<tjs_int>(std::max<tjs_int>(1, Width) *
                                    bytesPerPixel());
    }
    void Update(const void *, TVPTextureFormat::e format, int,
                const tTVPRect &) override {
        format_ = format == TVPTextureFormat::None ? format_ : format;
    }
    uint32_t GetPoint(int, int) override { return 0; }
    void SetPoint(int, int, uint32_t) override {}
    bool IsStatic() override { return false; }
    bool IsOpaque() override { return false; }
    cocos2d::Texture2D *GetAdapterTexture(cocos2d::Texture2D *origTex) override {
        return origTex;
    }

private:
    size_t bytesPerPixel() const {
        return format_ == TVPTextureFormat::Gray ? 1u : 4u;
    }

    TVPTextureFormat::e format_;
    std::vector<unsigned char> pixel_;
};

class HeadlessRenderMethod final : public iTVPRenderMethod {};

class HeadlessRenderManager final : public iTVPRenderManager {
public:
    iTVPTexture2D *CreateTexture2D(const void *, int, unsigned int w,
                                   unsigned int h, TVPTextureFormat::e format,
                                   int) override {
        return new HeadlessTexture(w, h, format);
    }
    iTVPTexture2D *CreateTexture2D(tTVPBitmap *) override {
        return new HeadlessTexture(1, 1, TVPTextureFormat::RGBA);
    }
    iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *) override {
        return new HeadlessTexture(1, 1, TVPTextureFormat::RGBA);
    }
    iTVPTexture2D *CreateTexture2D(unsigned int neww, unsigned int newh,
                                   iTVPTexture2D *tex) override {
        auto format = tex ? tex->GetFormat() : TVPTextureFormat::RGBA;
        return new HeadlessTexture(neww, newh, format);
    }
    bool IsSoftware() override { return true; }
    const char *GetName() override { return "wasmtime-headless"; }
    bool GetRenderStat(unsigned int &drawCount, uint64_t &vmemsize) override {
        drawCount = 0;
        vmemsize = 0;
        return true;
    }
    void OperateRect(iTVPRenderMethod *, iTVPTexture2D *, iTVPTexture2D *,
                     const tTVPRect &, const tRenderTexRectArray &) override {}
    void OperateTriangles(iTVPRenderMethod *, int, iTVPTexture2D *,
                          iTVPTexture2D *, const tTVPRect &,
                          const tTVPPointD *,
                          const tRenderTexQuadArray &) override {}
    void OperatePerspective(iTVPRenderMethod *, int, iTVPTexture2D *,
                            iTVPTexture2D *, const tTVPRect &,
                            const tTVPPointD *,
                            const tRenderTexQuadArray &) override {}
};

HeadlessRenderMethod &headlessRenderMethod() {
    static HeadlessRenderMethod method;
    return method;
}

HeadlessRenderManager &headlessRenderManager() {
    static HeadlessRenderManager manager;
    return manager;
}
#endif

void resetState() {
    g_error.clear();
    g_stage.clear();
    g_trace_json = "[]";
    g_frame_json.clear();
    g_framebuffer.clear();
    g_framebuffer_width = 0;
    g_framebuffer_height = 0;
    g_framebuffer_pitch = 0;
    g_framebuffer_format = 0;
    g_framebuffer_frame_no = 0;
    auto &state = traceState();
    state.inProgress = false;
    state.objthis = nullptr;
    state.players.clear();
    state.frameCounter = 0;
}

void setError(const std::string &message) {
    if(g_stage.empty()) {
        g_error = message;
    } else {
        g_error = g_stage + ": " + message;
    }
}

void setStage(const char *stage) {
    g_stage = stage ? stage : "";
}

template <typename Fn>
int runWithErrors(Fn &&fn) {
    try {
        fn();
        rebuildTraceJson();
        return 1;
    } catch(const TJS::eTJSScriptError &e) {
        std::ostringstream msg;
        msg << ttstr(e.GetMessage()).AsStdString();
        if(e.GetBlockName()) {
            msg << " at " << ttstr(e.GetBlockName()).AsStdString()
                << ":" << e.GetSourceLine();
        }
        msg << " pos " << e.GetPosition();
        const auto trace = ttstr(e.GetTrace()).AsStdString();
        if(!trace.empty()) msg << "\n" << trace;
        setError(msg.str());
    } catch(const TJS::eTJS &e) {
        setError(ttstr(e.GetMessage()).AsStdString());
    } catch(const std::exception &e) {
        setError(e.what());
    } catch(const tjs_char *e) {
        setError(ttstr(e).AsStdString());
    } catch(const char *e) {
        setError(e);
    } catch(...) {
        setError("unknown C++ exception");
    }
    rebuildTraceJson();
    return 0;
}

void appendProjectDelimiterIfNeeded() {
    if(TVPProjectDir.IsEmpty()) return;
    struct stat st {};
    std::string nativePath = TVPNativeProjectDir.AsStdString();
    std::string lowerNativePath = nativePath;
    std::transform(lowerNativePath.begin(), lowerNativePath.end(),
                   lowerNativePath.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const bool nativeLooksArchive =
        lowerNativePath.size() >= 4 &&
        lowerNativePath.compare(lowerNativePath.size() - 4, 4, ".xp3") == 0;
    const bool nativeFile =
        !nativePath.empty() && stat(nativePath.c_str(), &st) == 0 &&
        S_ISREG(st.st_mode);
    if(nativeLooksArchive || nativeFile ||
       TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir)) {
        TVPProjectDir += TVPArchiveDelimiter;
    } else {
        TVPProjectDir += TJS_W("/");
    }
}

void initializeBaseSystemsForWasmtime() {
    tTJSVariant v;
    setStage("TVPInitializeBaseSystems: arcdelim");
    if(TVPGetCommandLine(TJS_W("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    setStage("TVPInitializeBaseSystems: current directory");
    TVPSetCurrentDirectory(
        IncludeTrailingBackslash(ExtractFileDir(ExePath())));

    // logo_test_oracle.xp3 has no msgmap.tjs; skipping the optional lookup
    // avoids exercising auto-path archive enumeration before startup.tjs.
}

} // namespace

namespace motion::detail {

MotionTraceProgressScope::MotionTraceProgressScope(Player *player,
                                                   void *objthis) :
    _player(player) {
    auto &state = traceState();
    state.inProgress = true;
    state.objthis = objthis;
    state.players.clear();
}

MotionTraceProgressScope::~MotionTraceProgressScope() {
    auto &state = traceState();
    if(!state.inProgress) return;
    emitProgressFrame(_player);
    state.inProgress = false;
    state.objthis = nullptr;
    state.players.clear();
}

void motionTraceRecordUpdatePlayer(Player *player) {
    auto &state = traceState();
    if(!state.inProgress || !player) return;
    state.players.push_back(player);
}

} // namespace motion::detail

iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *) {
    return new HeadlessWindowLayer();
}

void TVPRemoveWindowLayer(iWindowLayer *layer) {
    delete static_cast<HeadlessWindowLayer *>(layer);
}

#ifndef KRKR2_WASMTIME_USE_REAL_RENDER_MANAGER
void iTVPTexture2D::Release() {
    if(RefCount <= 1) {
        delete this;
        return;
    }
    --RefCount;
}

void iTVPTexture2D::RecycleProcess() {}

void iTVPRenderManager::Initialize() {}

void iTVPRenderManager::RegisterRenderMethod(const char *,
                                             iTVPRenderMethod *) {}

iTVPRenderMethod *iTVPRenderManager::GetRenderMethod(const char *,
                                                     uint32_t *) {
    return &headlessRenderMethod();
}

iTVPRenderMethod *iTVPRenderManager::CompileRenderMethod(const char *,
                                                         const char *, int,
                                                         unsigned int) {
    return &headlessRenderMethod();
}

iTVPRenderMethod *iTVPRenderManager::GetOrCompileRenderMethod(
    const char *, uint32_t *, const char *, int, unsigned int) {
    return &headlessRenderMethod();
}

iTVPRenderMethod *iTVPRenderManager::GetRenderMethod(tjs_int, bool, int) {
    return &headlessRenderMethod();
}

void TVPRegisterRenderManager(const char *, iTVPRenderManager *(*)()) {}

iTVPRenderManager *TVPGetRenderManager() { return &headlessRenderManager(); }

iTVPRenderManager *TVPGetRenderManager(const TJS::tTJSString &) {
    return &headlessRenderManager();
}

iTVPRenderManager *TVPGetSoftwareRenderManager() {
    return &headlessRenderManager();
}

bool TVPIsSoftwareRenderManager() { return true; }
#endif

void TVPConsoleLog(const ttstr &line, bool) {
    std::fprintf(stderr, "%s\n", line.AsStdString().c_str());
}

namespace TJS {
void TVPConsoleLog(const ttstr &line) {
    std::fprintf(stderr, "%s\n", line.AsStdString().c_str());
}
} // namespace TJS

int _argc = 0;
char **_argv = nullptr;

tTVPApplication *Application = new tTVPApplication();

tTVPApplication::tTVPApplication() :
    is_attach_console_(false), tarminate_(false),
    application_activating_(true), has_map_report_process_(false),
    image_load_thread_(nullptr), ArgC(0), ArgV(nullptr) {}

tTVPApplication::~tTVPApplication() = default;

bool tTVPApplication::StartApplication(ttstr) { return true; }

void tTVPApplication::Run() {}

void tTVPApplication::ProcessMessages() {}

void tTVPApplication::PrintConsole(const ttstr &mes, bool important) {
    TVPConsoleLog(mes, important);
}

void tTVPApplication::SetTitle(const ttstr &caption) { title_ = caption; }

void tTVPApplication::Terminate() {
    tarminate_ = true;
    TVPTerminated = true;
}

void tTVPApplication::PostUserMessage(const std::function<void()> &func,
                                      void *, int) {
    if(func) func();
}

void tTVPApplication::FilterUserMessage(
    const std::function<void(std::vector<std::tuple<void *, int, tMsg>> &)>
        &func) {
    if(func) func(m_lstUserMsg);
}

void tTVPApplication::OnActivate() { application_activating_ = true; }

void tTVPApplication::OnDeactivate() { application_activating_ = false; }

void tTVPApplication::OnExit() { Terminate(); }

void tTVPApplication::OnLowMemory() {}

bool tTVPApplication::GetNotMinimizing() const { return true; }

void tTVPApplication::LoadImageRequest(iTJSDispatch2 *, tTJSNI_Bitmap *,
                                       const ttstr &) {}

void tTVPApplication::RegisterActiveEvent(
    void *, const std::function<void(void *, eTVPActiveEvent)> &) {}

ttstr TVPGetErrorDialogTitle() { return ttstr(TJS_W("Wasmtime Error")); }

ttstr ExePath() { return TVPNativeProjectDir; }

ttstr TVPGetMessageByLocale(const std::string &key) {
    return ttstr(key.c_str());
}

void TVPOpenPatchLibUrl() {}

tTVPSystemControl *TVPSystemControl = nullptr;
bool TVPSystemControlAlive = false;

void tTVPSystemControl::InvokeEvents() {}

void tTVPSystemControl::CallDeliverAllEventsOnIdle() {}

void tTVPSystemControl::BeginContinuousEvent() {}

void tTVPSystemControl::EndContinuousEvent() {}

void tTVPSystemControl::NotifyCloseClicked() {}

void tTVPSystemControl::NotifyEventDelivered() {}

bool tTVPSystemControl::ApplicationIdle() { return true; }

void tTVPSystemControl::SystemWatchTimerTimer() {}

void TVPInitFontNames() {}

void TVPInitWindowOptions() {}

std::string TVPShowFileSelector(const std::string &,
                                const std::string &filename,
                                std::string, bool) {
    return filename;
}

extern "C" const IID IID_IUnknown = {0, 0, 0, {0}};
extern "C" const IID IID_IStream = {0, 0, 0, {0}};
extern "C" const IID IID_ISequentialStream = {0, 0, 0, {0}};

tTVPArchive *TVPOpen7ZArchive(const ttstr &, tTJSBinaryStream *, bool) {
    return nullptr;
}

void TVPLoadBPG(void *, void *, tTVPGraphicSizeCallback,
                tTVPGraphicScanLineCallback, tTVPMetaInfoPushCallback,
                tTJSBinaryStream *, tjs_int, tTVPGraphicLoadMode) {}

void TVPLoadJXR(void *, void *, tTVPGraphicSizeCallback,
                tTVPGraphicScanLineCallback, tTVPMetaInfoPushCallback,
                tTJSBinaryStream *, tjs_int, tTVPGraphicLoadMode) {}

void TVPLoadHeaderBPG(void *, tTJSBinaryStream *, iTJSDispatch2 **) {}

void TVPLoadHeaderJXR(void *, tTJSBinaryStream *, iTJSDispatch2 **) {}

void TVPSaveAsJXR(void *, tTJSBinaryStream *, const iTVPBaseBitmap *,
                  const ttstr &, iTJSDispatch2 *) {}

bool TVPAcceptSaveAsJXR(void *, const ttstr &, iTJSDispatch2 **) {
    return false;
}

int PVRTDecompressPVRTC(const void *, int, int, void *, bool) { return 0; }

std::string ExtractFileDir(const std::string &filename) {
    const auto pos = filename.find_last_of("/\\");
    if(pos == std::string::npos) return {};
    return filename.substr(0, pos);
}

void TVPDetectCPU() {}

ttstr TVPGetPlatformName() { return ttstr(TJS_W("wasmtime")); }

ttstr TVPGetOSName() { return ttstr(TJS_W("wasi")); }

bool TVPGetJoyPadAsyncState(tjs_uint, bool) { return false; }

bool TVPGetKeyMouseAsyncState(tjs_uint, bool) { return false; }

unsigned long ColorToRGB(unsigned int col) { return col & 0x00ffffffu; }

class tTJSNI_MenuItem;
void TVPShowPopMenu(tTJSNI_MenuItem *) {}

int tTVPScreen::GetWidth() { return 2048; }

int tTVPScreen::GetHeight() { return 1536; }

int tTVPScreen::GetDesktopLeft() { return 0; }

int tTVPScreen::GetDesktopTop() { return 0; }

int tTVPScreen::GetDesktopWidth() { return GetWidth(); }

int tTVPScreen::GetDesktopHeight() { return GetHeight(); }

class tTJSNI_VideoOverlay;
class iTVPVideoOverlay;
void GetVideoOverlayObject(tTJSNI_VideoOverlay *, IStream *, const tjs_char *,
                           const tjs_char *, tjs_uint64,
                           iTVPVideoOverlay **out) {
    if(out) *out = nullptr;
}

void GetVideoLayerObject(tTJSNI_VideoOverlay *, IStream *, const tjs_char *,
                         const tjs_char *, tjs_uint64,
                         iTVPVideoOverlay **out) {
    if(out) *out = nullptr;
}

void GetMixingVideoOverlayObject(tTJSNI_VideoOverlay *, IStream *,
                                 const tjs_char *, const tjs_char *,
                                 tjs_uint64, iTVPVideoOverlay **out) {
    if(out) *out = nullptr;
}

void GetMFVideoOverlayObject(tTJSNI_VideoOverlay *, IStream *,
                             const tjs_char *, const tjs_char *, tjs_uint64,
                             iTVPVideoOverlay **out) {
    if(out) *out = nullptr;
}

LocaleConfigManager *LocaleConfigManager::GetInstance() {
    static std::aligned_storage_t<sizeof(LocaleConfigManager),
                                  alignof(LocaleConfigManager)> storage;
    return reinterpret_cast<LocaleConfigManager *>(&storage);
}

const std::string &LocaleConfigManager::GetText(const std::string &tid) {
    static std::string text;
    text = tid;
    return text;
}

void LocaleConfigManager::Initialize(const std::string &) {}

bool LocaleConfigManager::initText(cocos2d::ui::Text *) { return true; }

bool LocaleConfigManager::initText(cocos2d::ui::Button *) { return true; }

bool LocaleConfigManager::initText(cocos2d::ui::Text *,
                                   const std::string &) {
    return true;
}

bool LocaleConfigManager::initText(cocos2d::ui::Button *,
                                   const std::string &) {
    return true;
}

IndividualConfigManager *IndividualConfigManager::GetInstance() {
    static std::aligned_storage_t<sizeof(IndividualConfigManager),
                                  alignof(IndividualConfigManager)> storage;
    return reinterpret_cast<IndividualConfigManager *>(&storage);
}

bool IndividualConfigManager::CheckExistAt(const std::string &) { return false; }

bool IndividualConfigManager::CreatePreferenceAt(const std::string &) {
    return true;
}

bool IndividualConfigManager::UsePreferenceAt(const std::string &) {
    return true;
}

template <>
bool IndividualConfigManager::GetValue<bool>(const std::string &,
                                             const bool &defVal) {
    return defVal;
}

template <>
int IndividualConfigManager::GetValue<int>(const std::string &,
                                           const int &defVal) {
    return defVal;
}

template <>
float IndividualConfigManager::GetValue<float>(const std::string &,
                                               const float &defVal) {
    return defVal;
}

template <>
std::string
IndividualConfigManager::GetValue<std::string>(const std::string &,
                                               const std::string &defVal) {
    return defVal;
}

std::vector<std::string> IndividualConfigManager::GetCustomArgumentsForPush() {
    return {};
}

tTJSNativeClass *TVPCreateNativeClass_CDDASoundBuffer() {
    return new tTJSNativeClass(TJS_W("CDDASoundBuffer"));
}

tTJSNativeClass *TVPCreateNativeClass_MIDISoundBuffer() {
    return new tTJSNativeClass(TJS_W("MIDISoundBuffer"));
}

tTJSNativeClass *TVPCreateNativeClass_WaveSoundBuffer() {
    return new tTJSNativeClass(TJS_W("WaveSoundBuffer"));
}

tjs_uint32 tTJSNC_PhaseVocoder::ClassID = static_cast<tjs_uint32>(-1);

tTJSNC_PhaseVocoder::tTJSNC_PhaseVocoder() :
    tTJSNativeClass(TJS_W("PhaseVocoder")) {}

iTJSNativeInstance *tTJSNC_PhaseVocoder::CreateNativeInstance() {
    return nullptr;
}

void TVPGetMemoryInfo(TVPMemoryInfo &m) {
    m.MemTotal = 512 * 1024;
    m.MemFree = 256 * 1024;
    m.SwapTotal = 0;
    m.SwapFree = 0;
    m.VirtualTotal = m.MemTotal;
    m.VirtualUsed = m.MemTotal - m.MemFree;
}

void TVPRelinquishCPU() {}

bool TVP_utime(const char *, time_t) { return true; }

tjs_int TVPGetSystemFreeMemory() { return 256; }

tjs_int TVPGetSelfUsedMemory() { return 64; }

std::string TVPGetPackageVersionString() { return "wasmtime"; }

bool TVPCheckStartupPath(const std::string &) { return true; }

void TVPControlAdDialog(int, int, int) {}

void TVPForceSwapBuffer() {}

std::string TVPGetCurrentLanguage() { return "en_us"; }

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &) {
    std::fprintf(stderr, "[message] %s: %s\n",
                 caption.AsStdString().c_str(), text.AsStdString().c_str());
    return 0;
}

extern "C" int TVPShowSimpleMessageBox(const char *text, const char *title,
                                       unsigned int, const char **) {
    std::fprintf(stderr, "[message] %s: %s\n", title ? title : "",
                 text ? text : "");
    return 0;
}

extern "C" void fsafs_ensure_loaded(const char *) {}

extern "C" int fsafs_is_host_stream(const char *path) {
    return path && findStagedFile(path) ? 1 : 0;
}

extern "C" int fsafs_open_stream(const char *path) {
    const auto *bytes = path ? findStagedFile(path) : nullptr;
    if(!bytes) return -1;
    const int id = g_next_stream_id++;
    g_open_streams[id] = bytes;
    return id;
}

extern "C" double fsafs_get_stream_size(int id) {
    auto it = g_open_streams.find(id);
    if(it == g_open_streams.end() || !it->second) return 0.0;
    return static_cast<double>(it->second->size());
}

extern "C" int fsafs_read_stream(int id, void *buffer, double offset,
                                 int size) {
    auto it = g_open_streams.find(id);
    if(it == g_open_streams.end() || !it->second || !buffer || size <= 0)
        return -1;
    const auto &bytes = *it->second;
    if(offset < 0.0) return -1;
    const auto pos = static_cast<size_t>(offset);
    if(pos >= bytes.size()) return 0;
    const auto available = bytes.size() - pos;
    const auto toCopy = std::min<size_t>(available, static_cast<size_t>(size));
    std::memcpy(buffer, bytes.data() + pos, toCopy);
    return static_cast<int>(toCopy);
}

extern "C" void fsafs_close_stream(int id) {
    g_open_streams.erase(id);
}

extern "C" void fsafs_mark_written(const char *) {}

extern "C" void fsafs_flush_file(const char *) {}

int TVPShowSimpleInputBox(ttstr &, const ttstr &, const ttstr &,
                          const std::vector<ttstr> &) {
    return 0;
}

bool TVPCreateFolders(const ttstr &folder) {
    if(folder.IsEmpty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(folder.AsStdString(), ec);
    return !ec;
}

bool TVP_stat(const char *name, tTVP_stat &s) {
    const std::string local = localPathFromStorageUri(name ? name : "");
    if(const auto *bytes = findStagedFile(local)) {
        s.st_mode = S_IFREG | 0444;
        s.st_size = static_cast<tjs_int64>(bytes->size());
        s.st_atime = 0;
        s.st_mtime = 0;
        s.st_ctime = 0;
        return true;
    }
    struct stat st {};
    if(stat(local.c_str(), &st) != 0) return false;
    s.st_mode = st.st_mode;
    s.st_size = st.st_size;
    s.st_atime = st.st_atim.tv_sec;
    s.st_mtime = st.st_mtim.tv_sec;
    s.st_ctime = st.st_ctim.tv_sec;
    return true;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    return TVP_stat(ttstr{name}.AsStdString().c_str(), s);
}

tjs_uint32 TVPGetRoughTickCount32() {
    using namespace std::chrono;
    return static_cast<tjs_uint32>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

void TVPExitApplication(int code) {
    TVPTerminateCode = code;
    TVPTerminated = true;
}

bool TVPCheckStartupArg() { return false; }

void TVPProcessInputEvents() {}

bool TVPDeleteFile(const std::string &filename) {
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    return rename(from.c_str(), to.c_str()) == 0;
}

bool TVPCopyFile(const std::string &from, const std::string &to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    return !ec;
}

void TVPSendToOtherApp(const std::string &) {}

std::vector<std::string> TVPGetDriverPath() { return {"/"}; }

std::vector<std::string> TVPGetAppStoragePath() { return {"/tmp/"}; }

const std::string &TVPGetInternalPreferencePath() {
    static std::string path = "/tmp/";
    return path;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int len) {
    FILE *f = std::fopen(filepath.AsStdString().c_str(), "wb");
    if(!f) return false;
    const bool ok = std::fwrite(data, 1, len, f) == len;
    std::fclose(f);
    return ok;
}

void TVPShowIME(int, int, int, int) {}

void TVPHideIME() {}

void TVPPrintLog(const char *str) {
    if(str) std::fprintf(stderr, "%s\n", str);
}

void TVPCheckMemory() {}

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption) {
    std::vector<ttstr> buttons{ttstr(TJS_W("OK"))};
    return TVPShowSimpleMessageBox(text, caption, buttons);
}

int TVPShowSimpleMessageBoxYesNo(const ttstr &text, const ttstr &caption) {
    std::vector<ttstr> buttons{ttstr(TJS_W("Yes")), ttstr(TJS_W("No"))};
    return TVPShowSimpleMessageBox(text, caption, buttons);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int mp_write_file(const char *path, int pathLen, const void *data, int dataLen) {
    if(!path || pathLen <= 0 || !data || dataLen < 0) {
        setError("mp_write_file: invalid argument");
        return 0;
    }

    const std::string filePath(path, static_cast<size_t>(pathLen));
    stageFileBytes(filePath, static_cast<const unsigned char *>(data),
                   static_cast<size_t>(dataLen));
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int mp_startup_from(const char *path, int len) {
    resetState();
    if(!path || len <= 0) {
        setError("empty xp3 path");
        return 0;
    }

    const std::string xp3Path(path, static_cast<size_t>(len));
    return runWithErrors([&]() {
        setStage("configure project path");
        TVPNativeProjectDir = ttstr(xp3Path.c_str());
        const ttstr normalizedProject = TVPNormalizeStorageName(TVPNativeProjectDir);
        TVPProjectDir = normalizedProject;
        TVPSetCurrentDirectory(TVPNormalizeStorageName(
            IncludeTrailingBackslash(ExtractFileDir(TVPNativeProjectDir))));

        setStage("TVPInitScriptEngine");
        TVPInitScriptEngine();
        setStage("TVPInitFontNames");
        TVPInitFontNames();
        setStage("configure archive project path");
        TVPProjectDir = normalizedProject;
        appendProjectDelimiterIfNeeded();
        TVPSetCurrentDirectory(TVPProjectDir);
        TVPAddAutoPath(TVPProjectDir);
        initializeBaseSystemsForWasmtime();
        setStage("TVPLoadInternalPlugins");
        TVPLoadInternalPlugins();
        setStage("TVPSystemInit");
        TVPSystemInit();
        setStage("TVPInitializeStartupScript");
        TVPInitializeStartupScript();
        setStage("");
    });
}

EMSCRIPTEN_KEEPALIVE
int mp_get_trace_ptr() {
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_trace_json.c_str()));
}

EMSCRIPTEN_KEEPALIVE
int mp_get_trace_len() {
    return static_cast<int>(g_trace_json.size());
}

EMSCRIPTEN_KEEPALIVE
int mp_get_error_ptr() {
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_error.c_str()));
}

EMSCRIPTEN_KEEPALIVE
int mp_get_error_len() {
    return static_cast<int>(g_error.size());
}

} // extern "C"
