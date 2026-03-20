#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/emscripten/CCGLViewImpl-emscripten.h"
#include "base/CCDirector.h"
#include "base/CCTouch.h"
#include "base/CCEventDispatcher.h"
#include "base/CCEventKeyboard.h"
#include "base/CCEventMouse.h"
#include "base/CCIMEDispatcher.h"
#include "2d/CCCamera.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <SDL2/SDL.h>

#include <unordered_map>

NS_CC_BEGIN

static void getCSSToCanvasScale(float &scaleX, float &scaleY) {
    int canvasW, canvasH;
    double cssW, cssH;
    emscripten_get_canvas_element_size("#canvas", &canvasW, &canvasH);
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    scaleX = (cssW > 0) ? (float)canvasW / (float)cssW : 1.0f;
    scaleY = (cssH > 0) ? (float)canvasH / (float)cssH : 1.0f;
}

GLViewImpl::GLViewImpl()
    : _captured(false)
    , _isInRetinaMonitor(false)
    , _frameZoomFactor(1.0f)
    , _mouseX(0.0f)
    , _mouseY(0.0f)
    , _sdlWindow(nullptr)
    , _sdlGLContext(nullptr)
{
}

GLViewImpl::~GLViewImpl()
{
    if (_sdlGLContext) {
        SDL_GL_DeleteContext(_sdlGLContext);
    }
    if (_sdlWindow) {
        SDL_DestroyWindow(_sdlWindow);
    }
    SDL_Quit();
}

GLViewImpl* GLViewImpl::create(const std::string& viewName)
{
    double cssW, cssH;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    if (cssW <= 0) cssW = 960;
    if (cssH <= 0) cssH = 640;
    return createWithRect(viewName, Rect(0, 0, (float)cssW, (float)cssH), 1.0f);
}

GLViewImpl* GLViewImpl::createWithRect(const std::string& viewName, Rect rect, float frameZoomFactor)
{
    auto ret = new (std::nothrow) GLViewImpl();
    if (ret && ret->initWithRect(viewName, rect, frameZoomFactor)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

GLViewImpl* GLViewImpl::createWithFullScreen(const std::string& viewName)
{
    auto ret = new (std::nothrow) GLViewImpl();
    if (ret && ret->initWithFullScreen(viewName)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GLViewImpl::initWithRect(const std::string& viewName, Rect rect, float frameZoomFactor)
{
    setViewName(viewName);
    _frameZoomFactor = frameZoomFactor;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    int width = static_cast<int>(rect.size.width * _frameZoomFactor);
    int height = static_cast<int>(rect.size.height * _frameZoomFactor);

    _sdlWindow = SDL_CreateWindow(viewName.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!_sdlWindow) {
        return false;
    }

    _sdlGLContext = SDL_GL_CreateContext(_sdlWindow);
    if (!_sdlGLContext) {
        return false;
    }

    SDL_GL_MakeCurrent(_sdlWindow, _sdlGLContext);

    double cssW, cssH;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1; });
    int scaledW = static_cast<int>(cssW * dpr);
    int scaledH = static_cast<int>(cssH * dpr);
    emscripten_set_canvas_element_size("#canvas", scaledW, scaledH);

    setFrameSize(static_cast<float>(scaledW), static_cast<float>(scaledH));

    return true;
}

bool GLViewImpl::initWithFullScreen(const std::string& viewName)
{
    double cssW, cssH;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    if (cssW <= 0) cssW = 960;
    if (cssH <= 0) cssH = 640;
    return initWithRect(viewName, Rect(0, 0, (float)cssW, (float)cssH), 1.0f);
}

bool GLViewImpl::isOpenGLReady()
{
    return _sdlWindow != nullptr && _sdlGLContext != nullptr;
}

void GLViewImpl::end()
{
    if (_sdlGLContext) {
        SDL_GL_DeleteContext(_sdlGLContext);
        _sdlGLContext = nullptr;
    }
    if (_sdlWindow) {
        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
    }
}

void GLViewImpl::swapBuffers()
{
    if (_sdlWindow) {
        SDL_GL_SwapWindow(_sdlWindow);
    }
}

bool GLViewImpl::windowShouldClose()
{
    return false;
}

void GLViewImpl::pollEvents()
{
    handleSDLEvents();
}

void GLViewImpl::setIMEKeyboardState(bool open)
{
    if (open) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

void GLViewImpl::setFrameSize(float width, float height)
{
    GLView::setFrameSize(width, height);
}

void GLViewImpl::setViewPortInPoints(float x, float y, float w, float h)
{
    glViewport(
        (GLint)(x * _scaleX * _frameZoomFactor + _viewPortRect.origin.x * _frameZoomFactor),
        (GLint)(y * _scaleY * _frameZoomFactor + _viewPortRect.origin.y * _frameZoomFactor),
        (GLsizei)(w * _scaleX * _frameZoomFactor),
        (GLsizei)(h * _scaleY * _frameZoomFactor)
    );
}

void GLViewImpl::setScissorInPoints(float x, float y, float w, float h)
{
    glScissor(
        (GLint)(x * _scaleX * _frameZoomFactor + _viewPortRect.origin.x * _frameZoomFactor),
        (GLint)(y * _scaleY * _frameZoomFactor + _viewPortRect.origin.y * _frameZoomFactor),
        (GLsizei)(w * _scaleX * _frameZoomFactor),
        (GLsizei)(h * _scaleY * _frameZoomFactor)
    );
}

static EventKeyboard::KeyCode sdlKeyToCocos(SDL_Keycode key)
{
    static const std::unordered_map<SDL_Keycode, EventKeyboard::KeyCode> keyMap = {
        {SDLK_RETURN, EventKeyboard::KeyCode::KEY_ENTER},
        {SDLK_ESCAPE, EventKeyboard::KeyCode::KEY_ESCAPE},
        {SDLK_BACKSPACE, EventKeyboard::KeyCode::KEY_BACKSPACE},
        {SDLK_TAB, EventKeyboard::KeyCode::KEY_TAB},
        {SDLK_SPACE, EventKeyboard::KeyCode::KEY_SPACE},
        {SDLK_LEFT, EventKeyboard::KeyCode::KEY_LEFT_ARROW},
        {SDLK_RIGHT, EventKeyboard::KeyCode::KEY_RIGHT_ARROW},
        {SDLK_UP, EventKeyboard::KeyCode::KEY_UP_ARROW},
        {SDLK_DOWN, EventKeyboard::KeyCode::KEY_DOWN_ARROW},
        {SDLK_a, EventKeyboard::KeyCode::KEY_A},
        {SDLK_b, EventKeyboard::KeyCode::KEY_B},
        {SDLK_c, EventKeyboard::KeyCode::KEY_C},
        {SDLK_d, EventKeyboard::KeyCode::KEY_D},
        {SDLK_e, EventKeyboard::KeyCode::KEY_E},
        {SDLK_f, EventKeyboard::KeyCode::KEY_F},
        {SDLK_g, EventKeyboard::KeyCode::KEY_G},
        {SDLK_h, EventKeyboard::KeyCode::KEY_H},
        {SDLK_i, EventKeyboard::KeyCode::KEY_I},
        {SDLK_j, EventKeyboard::KeyCode::KEY_J},
        {SDLK_k, EventKeyboard::KeyCode::KEY_K},
        {SDLK_l, EventKeyboard::KeyCode::KEY_L},
        {SDLK_m, EventKeyboard::KeyCode::KEY_M},
        {SDLK_n, EventKeyboard::KeyCode::KEY_N},
        {SDLK_o, EventKeyboard::KeyCode::KEY_O},
        {SDLK_p, EventKeyboard::KeyCode::KEY_P},
        {SDLK_q, EventKeyboard::KeyCode::KEY_Q},
        {SDLK_r, EventKeyboard::KeyCode::KEY_R},
        {SDLK_s, EventKeyboard::KeyCode::KEY_S},
        {SDLK_t, EventKeyboard::KeyCode::KEY_T},
        {SDLK_u, EventKeyboard::KeyCode::KEY_U},
        {SDLK_v, EventKeyboard::KeyCode::KEY_V},
        {SDLK_w, EventKeyboard::KeyCode::KEY_W},
        {SDLK_x, EventKeyboard::KeyCode::KEY_X},
        {SDLK_y, EventKeyboard::KeyCode::KEY_Y},
        {SDLK_z, EventKeyboard::KeyCode::KEY_Z},
        {SDLK_0, EventKeyboard::KeyCode::KEY_0},
        {SDLK_1, EventKeyboard::KeyCode::KEY_1},
        {SDLK_2, EventKeyboard::KeyCode::KEY_2},
        {SDLK_3, EventKeyboard::KeyCode::KEY_3},
        {SDLK_4, EventKeyboard::KeyCode::KEY_4},
        {SDLK_5, EventKeyboard::KeyCode::KEY_5},
        {SDLK_6, EventKeyboard::KeyCode::KEY_6},
        {SDLK_7, EventKeyboard::KeyCode::KEY_7},
        {SDLK_8, EventKeyboard::KeyCode::KEY_8},
        {SDLK_9, EventKeyboard::KeyCode::KEY_9},
        {SDLK_LSHIFT, EventKeyboard::KeyCode::KEY_LEFT_SHIFT},
        {SDLK_RSHIFT, EventKeyboard::KeyCode::KEY_RIGHT_SHIFT},
        {SDLK_LCTRL, EventKeyboard::KeyCode::KEY_LEFT_CTRL},
        {SDLK_RCTRL, EventKeyboard::KeyCode::KEY_RIGHT_CTRL},
        {SDLK_LALT, EventKeyboard::KeyCode::KEY_LEFT_ALT},
        {SDLK_RALT, EventKeyboard::KeyCode::KEY_RIGHT_ALT},
    };
    auto it = keyMap.find(key);
    if (it != keyMap.end()) return it->second;
    return EventKeyboard::KeyCode::KEY_NONE;
}

void GLViewImpl::handleSDLEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION:
            if (!_touchActive)
                onMouseEvent(event);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            onKeyEvent(event);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int w, h;
                emscripten_get_canvas_element_size("#canvas", &w, &h);
                GLView::setFrameSize((float)w, (float)h);
                if (_designResolutionSize.width > 0 && _designResolutionSize.height > 0) {
                    setDesignResolutionSize(_designResolutionSize.width,
                                           _designResolutionSize.height,
                                           _resolutionPolicy);
                }
                auto vp = cocos2d::experimental::Viewport(0, 0, w, h);
                cocos2d::Camera::setDefaultViewport(vp);
                Director::getInstance()->setViewport();
            }
            break;
        case SDL_FINGERDOWN:
        {
            _touchActive = true;
            float x = event.tfinger.x * _screenSize.width;
            float y = event.tfinger.y * _screenSize.height;
            intptr_t id = static_cast<intptr_t>(event.tfinger.fingerId);
            _activeTouchIds.insert(id);
            handleTouchesBegin(1, &id, &x, &y);
            break;
        }
        case SDL_FINGERUP:
        {
            float x = event.tfinger.x * _screenSize.width;
            float y = event.tfinger.y * _screenSize.height;
            intptr_t id = static_cast<intptr_t>(event.tfinger.fingerId);
            _activeTouchIds.erase(id);
            handleTouchesEnd(1, &id, &x, &y);
            if (_activeTouchIds.empty())
                _touchActive = false;
            break;
        }
        case SDL_FINGERMOTION:
        {
            float x = event.tfinger.x * _screenSize.width;
            float y = event.tfinger.y * _screenSize.height;
            intptr_t id = static_cast<intptr_t>(event.tfinger.fingerId);
            handleTouchesMove(1, &id, &x, &y);
            break;
        }
        default:
            break;
        }
    }
}

void GLViewImpl::onMouseEvent(SDL_Event& event)
{
    float dprScaleX, dprScaleY;
    getCSSToCanvasScale(dprScaleX, dprScaleY);

    float cursorX = (float)event.button.x * dprScaleX;
    float cursorY = (float)event.button.y * dprScaleY;

    _mouseX = cursorX;
    _mouseY = cursorY;

    float glX = (cursorX - _viewPortRect.origin.x) / _scaleX;
    float glY = _designResolutionSize.height - (cursorY - _viewPortRect.origin.y) / _scaleY;

    switch (event.type) {
    case SDL_MOUSEBUTTONDOWN:
    {
        if (event.button.button == SDL_BUTTON_LEFT) {
            _captured = true;
            intptr_t id = 0;
            handleTouchesBegin(1, &id, &cursorX, &cursorY);
        }

        EventMouse mouseEvent(EventMouse::MouseEventType::MOUSE_DOWN);
        mouseEvent.setCursorPosition(glX, glY);
        mouseEvent.setMouseButton(static_cast<EventMouse::MouseButton>(event.button.button - 1));
        Director::getInstance()->getEventDispatcher()->dispatchEvent(&mouseEvent);
        break;
    }
    case SDL_MOUSEBUTTONUP:
    {
        if (event.button.button == SDL_BUTTON_LEFT && _captured) {
            _captured = false;
            intptr_t id = 0;
            handleTouchesEnd(1, &id, &cursorX, &cursorY);
        }

        EventMouse mouseEvent(EventMouse::MouseEventType::MOUSE_UP);
        mouseEvent.setCursorPosition(glX, glY);
        mouseEvent.setMouseButton(static_cast<EventMouse::MouseButton>(event.button.button - 1));
        Director::getInstance()->getEventDispatcher()->dispatchEvent(&mouseEvent);
        break;
    }
    case SDL_MOUSEMOTION:
    {
        if (_captured) {
            intptr_t id = 0;
            handleTouchesMove(1, &id, &cursorX, &cursorY);
        }

        EventMouse mouseEvent(EventMouse::MouseEventType::MOUSE_MOVE);
        mouseEvent.setCursorPosition(glX, glY);
        Director::getInstance()->getEventDispatcher()->dispatchEvent(&mouseEvent);
        break;
    }
    }
}

void GLViewImpl::onKeyEvent(SDL_Event& event)
{
    EventKeyboard::KeyCode cocosKey = sdlKeyToCocos(event.key.keysym.sym);
    if (cocosKey == EventKeyboard::KeyCode::KEY_NONE) return;

    EventKeyboard keyEvent(cocosKey, event.type == SDL_KEYDOWN);
    Director::getInstance()->getEventDispatcher()->dispatchEvent(&keyEvent);

    if (event.type == SDL_KEYDOWN) {
        IMEDispatcher::sharedDispatcher()->dispatchInsertText(nullptr, 0);
    }
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
