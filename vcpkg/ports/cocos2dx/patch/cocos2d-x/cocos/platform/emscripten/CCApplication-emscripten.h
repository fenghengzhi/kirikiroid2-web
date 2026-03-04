#ifndef CC_APPLICATION_EMSCRIPTEN_H
#define CC_APPLICATION_EMSCRIPTEN_H

#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/CCCommon.h"
#include "platform/CCApplicationProtocol.h"
#include <string>

NS_CC_BEGIN

class Rect;

class Application : public ApplicationProtocol
{
public:
    Application();
    virtual ~Application();

    virtual void setAnimationInterval(float interval) override;

    int run();

    static Application* getInstance();

    CC_DEPRECATED_ATTRIBUTE static Application* sharedApplication();

    virtual LanguageType getCurrentLanguage() override;
    virtual const char * getCurrentLanguageCode() override;
    virtual std::string getVersion() override;
    virtual bool openURL(const std::string &url) override;
    virtual Platform getTargetPlatform() override;

    void setResourceRootPath(const std::string& rootResDir);
    const std::string& getResourceRootPath(void);

    void mainLoop();

protected:
    long _animationInterval;
    std::string _resourceRootPath;
    static Application * sm_pSharedApplication;
};

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#endif // CC_APPLICATION_EMSCRIPTEN_H
