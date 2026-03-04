#include "Platform.h"

#ifdef EMSCRIPTEN

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#undef st_atime
#undef st_mtime
#undef st_ctime
#include <sys/time.h>
#include <unistd.h>
#include <filesystem>

#include <spdlog/spdlog.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "EventIntf.h"
#include "StorageImpl.h"
#include "Defer.h"
#include "ui/MessageBox.h"
#include "cocos2d/MainScene.h"

void TVPGetMemoryInfo(TVPMemoryInfo &m) {
    size_t heapSize = (size_t)sbrk(0);
    m.MemTotal = heapSize / 1024;
    m.MemFree = (heapSize - (size_t)sbrk(0)) / 1024;
    m.SwapTotal = 0;
    m.SwapFree = 0;
    m.VirtualTotal = heapSize / 1024;
    m.VirtualUsed = 0;
}

#include <sched.h>
void TVPRelinquishCPU() { sched_yield(); }

bool TVP_utime(const char *name, time_t modtime) {
    timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}

tjs_int TVPGetSystemFreeMemory() {
    return static_cast<tjs_int>((size_t)sbrk(0) / (1024 * 1024));
}

tjs_int TVPGetSelfUsedMemory() {
    return static_cast<tjs_int>((size_t)sbrk(0) / (1024 * 1024));
}

std::string TVPGetPackageVersionString() { return "web"; }

bool TVPCheckStartupPath(const std::string &path) { return true; }

void TVPControlAdDialog(int adType, int arg1, int arg2) {}
void TVPForceSwapBuffer() {}

std::string TVPGetCurrentLanguage() {
    char buf[16] = {0};
    EM_ASM({
        var lang = navigator.language || navigator.userLanguage || 'en_us';
        lang = lang.replace('-', '_').toLowerCase();
        stringToUTF8(lang, $0, 16);
    }, buf);
    std::string locale(buf);
    if (locale.empty()) return "en_us";
    return locale;
}

EM_JS(int, web_alert, (const char* msg, const char* title), {
    alert(UTF8ToString(title) + '\n' + UTF8ToString(msg));
    return 0;
});

EM_JS(int, web_confirm, (const char* msg, const char* title), {
    return confirm(UTF8ToString(title) + '\n' + UTF8ToString(msg)) ? 0 : 1;
});

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons) {
    auto msg = text.AsStdString();
    auto cap = caption.AsStdString();
    if (vecButtons.size() <= 1) {
        web_alert(msg.c_str(), cap.c_str());
        return 0;
    }
    if (vecButtons.size() == 2) {
        return web_confirm(msg.c_str(), cap.c_str());
    }
    std::vector<std::string> btnStrs;
    btnStrs.reserve(vecButtons.size());
    for (const auto &b : vecButtons) {
        btnStrs.push_back(b.AsStdString());
    }
    TVPMessageBoxForm::show(cap, msg, static_cast<int>(btnStrs.size()),
                            btnStrs.data(), [](int) {});
    return 0;
}

extern "C" int TVPShowSimpleMessageBox(const char *pszText,
                                       const char *pszTitle, unsigned int nButton,
                                       const char **btnText) {
    std::vector<ttstr> vecButtons{};
    for (unsigned int i = 0; i < nButton; ++i) {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

int TVPShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons) {
    spdlog::warn("web platform simple input box not fully implemented");
    return 0;
}

bool TVPCreateFolders(const ttstr &folder);

static bool _TVPCreateFolders(const ttstr &folder) {
    if (folder.IsEmpty())
        return true;

    if (TVPCheckExistentLocalFolder(folder))
        return true;

    const tjs_char *p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if (p[i] == TJS_W(':'))
        return true;

    while (i >= 0 && (p[i] == TJS_W('/') || p[i] == TJS_W('\\')))
        i--;

    if (i >= 0 && p[i] == TJS_W(':'))
        return true;

    for (; i >= 0; i--) {
        if (p[i] == TJS_W(':') || p[i] == TJS_W('/') || p[i] == TJS_W('\\'))
            break;
    }

    ttstr parent(p, i + 1);
    if (!TVPCreateFolders(parent))
        return false;

    return !std::filesystem::create_directory(folder.AsStdString().c_str());
}

bool TVPCreateFolders(const ttstr &folder) {
    if (folder.IsEmpty())
        return true;

    const tjs_char *p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if (p[i] == TJS_W(':'))
        return true;

    if (p[i] == TJS_W('/') || p[i] == TJS_W('\\'))
        i--;

    return _TVPCreateFolders(ttstr(p, i + 1));
}

bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t;
    if (stat(name, &t) != 0) {
        return false;
    }

    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
    s.st_atime = t.st_atim.tv_sec;
    s.st_mtime = t.st_mtim.tv_sec;
    s.st_ctime = t.st_ctim.tv_sec;

    return true;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    return TVP_stat(ttstr{name}.AsStdString().c_str(), s);
}

tjs_uint32 TVPGetRoughTickCount32() {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return static_cast<tjs_uint32>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    return 0;
}

void TVPExitApplication(int code) {
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    emscripten_force_exit(code);
}

bool TVPCheckStartupArg() {
    struct stat st;
    if (stat("/data.xp3", &st) == 0 && S_ISREG(st.st_mode)) {
        spdlog::info("Found /data.xp3, auto-starting game");
        TVPMainScene::GetInstance()->startupFrom("/data.xp3");
        return true;
    }
    return false;
}

void TVPProcessInputEvents() {}

bool TVPDeleteFile(const std::string &filename) {
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    return rename(from.c_str(), to.c_str()) == 0;
}

bool TVPCopyFile(const std::string &from, const std::string &to) {
    FILE *src = fopen(from.c_str(), "rb");
    if (!src) return false;
    FILE *dst = fopen(to.c_str(), "wb");
    if (!dst) { fclose(src); return false; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);
    return true;
}

void TVPSendToOtherApp(const std::string &filename) {}

std::vector<std::string> TVPGetDriverPath() { return {"/"}; }

std::vector<std::string> TVPGetAppStoragePath() {
    return {"/save/"};
}

const std::string &TVPGetInternalPreferencePath() {
    static std::string path = "/save/";
    return path;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int len) {
    FILE *handle = fopen(filepath.AsStdString().c_str(), "wb");
    if (handle) {
        bool ret = fwrite(data, 1, len, handle) == len;
        fclose(handle);
        return ret;
    }
    return false;
}

void TVPShowIME(int x, int y, int w, int h) {}
void TVPHideIME() {}

void TVPPrintLog(const char *str) {
    emscripten_log(EM_LOG_CONSOLE, "%s", str);
}

void TVPCheckMemory() {}

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption) {
    std::vector<ttstr> btns;
    btns.emplace_back(TJS_W("OK"));
    return TVPShowSimpleMessageBox(text, caption, btns);
}

int TVPShowSimpleMessageBoxYesNo(const ttstr &text, const ttstr &caption) {
    std::vector<ttstr> btns;
    btns.emplace_back(TJS_W("Yes"));
    btns.emplace_back(TJS_W("No"));
    return TVPShowSimpleMessageBox(text, caption, btns);
}

#endif // EMSCRIPTEN
