#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/emscripten/CCApplication-emscripten.h"
#include <cstring>
#include <sys/time.h>
#include "base/CCDirector.h"
#include "base/ccUtils.h"
#include "platform/CCFileUtils.h"
#include <emscripten.h>
#include <emscripten/html5.h>

NS_CC_BEGIN

Application * Application::sm_pSharedApplication = nullptr;
static bool s_mainLoopRunning = false;

// VirtualLazyFS(JSPI): tick 必须是具名导出且列入 -sJSPI_EXPORTS，emscripten
// glue 的 getWasmTableEntry 才会给该函数指针包 WebAssembly.promising，使
// tick 内的文件读（EM_ASYNC_JS）可以挂起。挂起期间 RAF 仍会触发下一帧
// （实测 100ms 挂起期间 12 次），引擎不可重入 —— 用守卫跳过这些帧。
static bool s_inTick = false;

extern "C" void TVPWebPerfTickBegin();
extern "C" void TVPWebPerfTickEnd();
extern "C" void TVPWebPerfTickRejected();

extern "C" EMSCRIPTEN_KEEPALIVE void krkr2_main_loop_tick(void *arg) {
    if (s_inTick) {
        TVPWebPerfTickRejected();
        return;
    }
    struct TickGuard {
        TickGuard() {
            s_inTick = true;
            TVPWebPerfTickBegin();
        }
        ~TickGuard() {
            TVPWebPerfTickEnd();
            s_inTick = false;
        }
    } guard;
    auto app = static_cast<Application*>(arg);
    app->mainLoop();
}

Application::Application()
: _animationInterval(1.0f/60.0f*1000.0f)
{
    CC_ASSERT(! sm_pSharedApplication);
    sm_pSharedApplication = this;
}

Application::~Application()
{
    CC_ASSERT(this == sm_pSharedApplication);
    sm_pSharedApplication = nullptr;
}

void Application::mainLoop() {
    auto director = Director::getInstance();
    director->mainLoop();
}

int Application::run()
{
    initGLContextAttrs();

    if (!applicationDidFinishLaunching())
    {
        return 1;
    }

    s_mainLoopRunning = true;
    // simulate_infinite_loop=0：JSPI 下 main 是 promising 导出，
    // simulate_infinite_loop=1 的 'unwind' 异常会变成 main promise 的
    // rejection。改为正常返回，运行时因 noExitRuntime（EXIT_RUNTIME=0
    // 默认）保活，主循环继续由 RAF 驱动。
    emscripten_set_main_loop_arg(krkr2_main_loop_tick, this, 0, 0);

    return 0;
}

void Application::setAnimationInterval(float interval)
{
    _animationInterval = interval * 1000.0f;
    if (s_mainLoopRunning) {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
    }
}

void Application::setResourceRootPath(const std::string& rootResDir)
{
    _resourceRootPath = rootResDir;
    if (_resourceRootPath[_resourceRootPath.length() - 1] != '/')
    {
        _resourceRootPath += '/';
    }
    FileUtils* pFileUtils = FileUtils::getInstance();
    std::vector<std::string> searchPaths = pFileUtils->getSearchPaths();
    searchPaths.insert(searchPaths.begin(), _resourceRootPath);
    pFileUtils->setSearchPaths(searchPaths);
}

const std::string& Application::getResourceRootPath(void)
{
    return _resourceRootPath;
}

Application::Platform Application::getTargetPlatform()
{
    return Platform::OS_LINUX;
}

std::string Application::getVersion()
{
    return "1.0";
}

bool Application::openURL(const std::string &url)
{
    EM_ASM({
        window.open(UTF8ToString($0), '_blank');
    }, url.c_str());
    return true;
}

Application* Application::getInstance()
{
    CC_ASSERT(sm_pSharedApplication);
    return sm_pSharedApplication;
}

Application* Application::sharedApplication()
{
    return Application::getInstance();
}

const char * Application::getCurrentLanguageCode()
{
    static char code[3] = {0};
    EM_ASM({
        var lang = navigator.language || navigator.userLanguage || 'en';
        var c = lang.substring(0, 2);
        stringToUTF8(c, $0, 3);
    }, code);
    return code;
}

LanguageType Application::getCurrentLanguage()
{
    const char* code = getCurrentLanguageCode();
    return utils::getLanguageTypeByISO2(code);
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
