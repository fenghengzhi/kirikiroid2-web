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
#include <dirent.h>
#include <filesystem>
#include <algorithm>

#include <spdlog/spdlog.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "EventIntf.h"
#include "StorageIntf.h"
#include "StorageImpl.h"
#include "Defer.h"
#include "ui/MessageBox.h"
#include "cocos2d/MainScene.h"
#include "LayerFrameDumper.h"

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

// ---------------------------------------------------------------------------
// FSAFS: save-persistence helpers (write-back to host via File System Access API)
// ---------------------------------------------------------------------------

EM_JS(void, fsafs_ensure_loaded, (const char *path_ptr), {});

EM_JS(int, fsafs_is_host_stream, (const char *path_ptr), { return 0; });

EM_JS(int, fsafs_open_stream, (const char *path_ptr), { return -1; });

EM_JS(double, fsafs_get_stream_size, (int stream_id), { return 0; });

EM_JS(int, fsafs_read_stream, (int stream_id, void *buf, double offset, int length), { return -1; });

EM_JS(void, fsafs_close_stream, (int stream_id), {});

EM_JS(void, fsafs_mark_written, (const char *path_ptr), {
    var p = UTF8ToString(path_ptr);
    if (!Module._loadedFiles) Module._loadedFiles = {};
    Module._loadedFiles[p] = true;
});

EM_JS(void, fsafs_flush_file, (const char *path_ptr), {
    var p = UTF8ToString(path_ptr);
    if (!Module._hostDirHandle) {
        if (Module._saveSpaceId && (p.startsWith('/savedata/') || p.startsWith('/save/'))) {
            try { idbSaveFile(p, FS.readFile(p)); } catch (e) {
                console.warn('[IDB] flush fallback failed:', p, e);
            }
        }
        return;
    }
    var dirHandle = Module._hostDirHandle;
    var prefix = Module._hostDirPrefix;
    setTimeout(async function() {
        try {
            var data = FS.readFile(p);
            var relPath = p;
            if (prefix && relPath.startsWith(prefix + '/')) {
                relPath = relPath.substring(prefix.length);
            }
            var parts = relPath.split('/').filter(Boolean);
            var fileName = parts.pop();
            var cur = dirHandle;
            for (var i = 0; i < parts.length; i++) {
                cur = await cur.getDirectoryHandle(parts[i], { create: true });
            }
            var fh = await cur.getFileHandle(fileName, { create: true });
            var writable = await fh.createWritable();
            await writable.write(data);
            await writable.close();
        } catch (err) {
            console.warn('[FSAFS] flush_file FAILED: ' + p + ' - ' + err.message);
        }
    }, 0);
});

// ---------------------------------------------------------------------------

EM_JS(int, web_alert, (const char* msg, const char* title), {
    console.warn('[alert] ' + UTF8ToString(title) + '\n' + UTF8ToString(msg));
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
    // On web, don't actually exit — emscripten_force_exit breaks the runtime
    // without EXIT_RUNTIME set. Game scripts (e.g. keybinder.tjs exception
    // handlers) may call System.exit() for non-fatal errors that the game
    // can survive. Log the exit request and continue.
    spdlog::warn("TVPExitApplication({}) called — ignored on web build", code);
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
}

static bool tryStartFromDir(const std::string &dir) {
    struct stat st;
    std::string xp3File;
    bool hasStartupTjs = false;

    DIR *dirp = opendir(dir.c_str());
    if (!dirp) return false;

    dirent *dp;
    while ((dp = readdir(dirp))) {
        std::string name = dp->d_name;
        if (name.empty() || name[0] == '.') continue;

        std::string full = dir;
        if (full.back() != '/') full += '/';
        full += name;

        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "data.xp3") {
            closedir(dirp);
            spdlog::info("Found {}, auto-starting game", full);
            TVPMainScene::GetInstance()->startupFrom(full);
            return true;
        }
        if (lower.size() > 4 &&
            lower.compare(lower.size() - 4, 4, ".xp3") == 0 &&
            xp3File.empty()) {
            xp3File = full;
        }
        if (lower == "startup.tjs") {
            hasStartupTjs = true;
        }
    }
    closedir(dirp);

    if (!xp3File.empty()) {
        spdlog::info("Found {}, auto-starting game", xp3File);
        TVPMainScene::GetInstance()->startupFrom(xp3File);
        return true;
    }
    if (hasStartupTjs) {
        spdlog::info("Found startup.tjs in {}, auto-starting game", dir);
        TVPMainScene::GetInstance()->startupFrom(dir);
        return true;
    }
    return false;
}

EM_JS(char *, krkr2_get_startup_xp3_path, (), {
    if (Module._startupXp3Path) {
        var s = Module._startupXp3Path;
        var len = lengthBytesUTF8(s) + 1;
        var buf = _malloc(len);
        stringToUTF8(s, buf, len);
        return buf;
    }
    return 0;
});

// Auto-mount all sibling .xp3 files in the same directory as the startup xp3.
// This ensures resolution-specific archives (data1080.xp3, bgimage1080.xp3, etc.)
// are available even if the game's resolution detection doesn't add them.
static void autoMountSiblingXp3(const std::string &startupPath) {
    // Extract directory from startup path
    auto lastSlash = startupPath.rfind('/');
    std::string dir = (lastSlash != std::string::npos)
        ? startupPath.substr(0, lastSlash + 1) : "./";
    std::string startupName = (lastSlash != std::string::npos)
        ? startupPath.substr(lastSlash + 1) : startupPath;

    // Lowercase the startup name for comparison
    std::string startupLower = startupName;
    std::transform(startupLower.begin(), startupLower.end(),
                   startupLower.begin(), ::tolower);

    DIR *dirp = opendir(dir.c_str());
    if (!dirp) return;

    dirent *dp;
    while ((dp = readdir(dirp))) {
        std::string name = dp->d_name;
        if (name.empty() || name[0] == '.') continue;

        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Skip non-xp3 files and the startup xp3 itself
        if (lower.size() <= 4 ||
            lower.compare(lower.size() - 4, 4, ".xp3") != 0) continue;
        if (lower == startupLower) continue;

        std::string full = dir + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

        // Add as auto path: "file://./name.xp3>"
        ttstr autoPath = TJS_W("file://.");
        autoPath += ttstr(full.c_str());
        autoPath += TJS_W(">");
        try {
            TVPAddAutoPath(autoPath);
            spdlog::info("Auto-mounted sibling xp3: {}", full);
        } catch (...) {
            spdlog::warn("Failed to auto-mount: {}", full);
        }
    }
    closedir(dirp);
}

bool TVPCheckStartupArg() {
    TVPInstallLayerFrameDumperIfRequested();

    char *selectedXp3 = krkr2_get_startup_xp3_path();
    if (selectedXp3) {
        std::string path(selectedXp3);
        free(selectedXp3);
        autoMountSiblingXp3(path);
        TVPMainScene::GetInstance()->startupFrom(path);
        return true;
    }

    if (tryStartFromDir("/")) return true;

    struct stat st;
    DIR *rootDir = opendir("/");
    if (rootDir) {
        dirent *dp;
        while ((dp = readdir(rootDir))) {
            std::string name = dp->d_name;
            if (name.empty() || name[0] == '.') continue;
            if (name == "dev" || name == "proc" || name == "tmp" ||
                name == "home" || name == "save") continue;
            std::string full = "/" + name;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                if (tryStartFromDir(full)) {
                    closedir(rootDir);
                    return true;
                }
            }
        }
        closedir(rootDir);
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
    if (!src) {
        spdlog::error("[FSAFS] TVPCopyFile fopen src FAILED: {}", from);
        return false;
    }
    FILE *dst = fopen(to.c_str(), "wb");
    if (!dst) {
        spdlog::error("[FSAFS] TVPCopyFile fopen dst FAILED: {}", to);
        fclose(src);
        return false;
    }

    char buf[4096];
    size_t n;
    size_t total = 0;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
        total += n;
    }
    fclose(src);
    fclose(dst);
    fsafs_mark_written(to.c_str());
    fsafs_flush_file(to.c_str());
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
    std::string path = filepath.AsStdString();
    FILE *handle = fopen(path.c_str(), "wb");
    if (handle) {
        bool ret = fwrite(data, 1, len, handle) == len;
        fclose(handle);
        if (ret) {
            fsafs_mark_written(path.c_str());
            fsafs_flush_file(path.c_str());
        }
        return ret;
    }
    spdlog::error("[FSAFS] TVPWriteDataToFile fopen FAILED: {}", path);
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
