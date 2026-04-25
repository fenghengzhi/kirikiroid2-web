// Native-only Motion playback differential target.
//
// This runner boots the real macOS/Cocos engine path as far as possible:
// GLView, TVPMainScene, Application::StartApplication, XP3 startup.tjs,
// Window/Layer backend, RenderManager, SystemControl, and platform services
// come from krkr2core. Motion state tracing is performed externally by
// tests/differential/python/native_lldb_motion_trace.py.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <cocos2d.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Application.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "Platform.h"
#include "environ/cocos2d/MainScene.h"
#include "ui/extension/UIExtension.h"

#ifndef KRKR2_REPO_ROOT
#define KRKR2_REPO_ROOT "."
#endif

extern std::thread::id TVPMainThreadID;
extern "C" void SDL_SetMainReady();
std::string TVPGetCurrentLanguage();

namespace {

std::string g_startup_xp3;
bool g_startup_started = false;

void ensureLogger(const char *name) {
    if(spdlog::get(name))
        return;
    auto logger = spdlog::stdout_color_mt(name);
    logger->set_level(spdlog::level::info);
}

void initializeLogging() {
    spdlog::set_level(spdlog::level::info);
    ensureLogger("core");
    ensureLogger("tjs2");
    ensureLogger("plugin");
    spdlog::set_default_logger(spdlog::get("core"));
}

[[noreturn]] void finishAndExit(int code) {
    std::cout.flush();
    std::cerr.flush();
    spdlog::shutdown();
    std::_Exit(code);
}

class MotionPlaybackNativeApp final : public cocos2d::Application {
public:
    MotionPlaybackNativeApp() = default;

private:
    void initGLContextAttrs() override {
        GLContextAttrs glContextAttrs = {8, 8, 8, 8, 24, 8};
        cocos2d::GLView::setGLContextAttrs(glContextAttrs);
    }

    bool applicationDidFinishLaunching() override {
        SDL_SetMainReady();
        TVPMainThreadID = std::this_thread::get_id();

        auto *director = cocos2d::Director::getInstance();
        auto *glview = director->getOpenGLView();
        if(!glview) {
            glview = cocos2d::GLViewImpl::create("krkr2 native differential");
            director->setOpenGLView(glview);
        }

        const cocos2d::Size designSize(960, 640);
        glview->setDesignResolutionSize(designSize.width, designSize.height,
                                        ResolutionPolicy::FIXED_WIDTH);
        {
            auto frameSize = glview->getFrameSize();
            auto vp = cocos2d::experimental::Viewport(
                0, 0, static_cast<int>(frameSize.width),
                static_cast<int>(frameSize.height));
            cocos2d::Camera::setDefaultViewport(vp);
        }

        const auto repoRoot = std::filesystem::path(KRKR2_REPO_ROOT);
        std::vector<std::string> searchPath;
        searchPath.emplace_back("res");
        searchPath.emplace_back((repoRoot / "ui" / "cocos-studio").string());
        cocos2d::FileUtils::getInstance()->setSearchPaths(searchPath);
        director->setDisplayStats(false);
        director->setAnimationInterval(1.0f / 60);

        TVPInitUIExtension();
        LocaleConfigManager::GetInstance()->Initialize(TVPGetCurrentLanguage());

        TVPMainScene *scene = TVPMainScene::CreateInstance();
        director->runWithScene(scene);

        if(!scene->startupFrom(g_startup_xp3)) {
            std::cerr << "TVPMainScene::startupFrom rejected path: "
                      << g_startup_xp3 << "\n";
            finishAndExit(1);
        }
        g_startup_started = true;

        scene->scheduleOnce([](float) { finishAndExit(0); }, 0.25f,
                            "motion_playback_native_finish");
        return true;
    }

    void applicationDidEnterBackground() override {
        ::Application->OnDeactivate();
        cocos2d::Director::getInstance()->stopAnimation();
    }

    void applicationWillEnterForeground() override {
        ::Application->OnActivate();
        cocos2d::Director::getInstance()->startAnimation();
    }
};

void printUsage(const char *argv0) {
    std::cerr
        << "Usage: " << (argv0 ? argv0 : "motion_playback_native")
        << " --startup-xp3 PATH\n";
}

} // namespace

int main(int argc, char **argv) {
    initializeLogging();

    for(int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        const auto requireValue = [&](const char *name) -> const char * {
            if(i + 1 >= argc || !argv[i + 1]) {
                std::cerr << name << " requires a value\n";
                printUsage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };

        if(arg == "--startup-xp3" || arg == "--xp3") {
            g_startup_xp3 = requireValue(arg.c_str());
        } else if(arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if(g_startup_xp3.empty()) {
        printUsage(argv[0]);
        return 2;
    }

    std::error_code ec;
    const auto absoluteXp3 =
        std::filesystem::absolute(std::filesystem::path(g_startup_xp3), ec);
    g_startup_xp3 = ec ? g_startup_xp3 : absoluteXp3.string();
    if(!std::filesystem::exists(g_startup_xp3)) {
        std::cerr << "startup xp3 not found: " << g_startup_xp3 << "\n";
        return 2;
    }

    MotionPlaybackNativeApp app;
    const int result = app.run();
    return g_startup_started ? 0 : result;
}
