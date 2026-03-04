#ifndef CC_GLVIEWIMPL_EMSCRIPTEN_H
#define CC_GLVIEWIMPL_EMSCRIPTEN_H

#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/CCGLView.h"
#include "platform/CCStdC.h"

#include <SDL2/SDL.h>
#include <set>

NS_CC_BEGIN

class CC_DLL GLViewImpl : public GLView
{
public:
    static GLViewImpl* create(const std::string& viewName);
    static GLViewImpl* createWithRect(const std::string& viewName, Rect rect, float frameZoomFactor = 1.0f);
    static GLViewImpl* createWithFullScreen(const std::string& viewName);

    virtual ~GLViewImpl();

    virtual bool isOpenGLReady() override;
    virtual void end() override;
    virtual void swapBuffers() override;
    virtual void setIMEKeyboardState(bool open) override;
    virtual void setFrameSize(float width, float height) override;

    bool windowShouldClose();
    void pollEvents();

    virtual void setViewPortInPoints(float x, float y, float w, float h) override;
    virtual void setScissorInPoints(float x, float y, float w, float h) override;

protected:
    GLViewImpl();
    bool initWithRect(const std::string& viewName, Rect rect, float frameZoomFactor);
    bool initWithFullScreen(const std::string& viewName);

    bool _captured;
    bool _isInRetinaMonitor;
    float _frameZoomFactor;

    float _mouseX;
    float _mouseY;

    SDL_Window* _sdlWindow;
    SDL_GLContext _sdlGLContext;

    bool _touchActive = false;
    std::set<intptr_t> _activeTouchIds;

    void handleSDLEvents();
    void onMouseEvent(SDL_Event& event);
    void onKeyEvent(SDL_Event& event);

    friend class GLFWEventHandler;
};

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#endif // CC_GLVIEWIMPL_EMSCRIPTEN_H
