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

static void emscripten_main_loop_callback(void *arg) {
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
    emscripten_set_main_loop_arg(emscripten_main_loop_callback, this, 0, 1);

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
