#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <emscripten.h>

#include <SDL2/SDL_hints.h>

#include "environ/cocos2d/AppDelegate.h"
#include "environ/ui/MainFileSelectorForm.h"

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::debug);

    // 禁用 SDL 触摸→鼠标合成。合成的 MOUSEBUTTONDOWN 在 FINGERUP 之后才入队，
    // 绕过 GLViewImpl 的 _touchActive 防线触发 EventMouse 路径，置位
    // MouseEventOwnsCurrentPress 并卡死（合成 MOUSEBUTTONUP 被时序吞掉），
    // 压制 TVPWindowLayer::onTouchEnded 的触摸合成点击（Move+Down+Click+Up），
    // 表现为触摸能推进对话（只看 MouseDown）但所有按钮不可点。
    // 禁用后触摸只走 FINGER 事件 → cocos touch → TVPWindowLayer 合成，
    // 与 Android kirikiroid2 的纯触摸架构一致。
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    static auto core_logger = spdlog::stdout_color_mt("core");
    static auto tjs2_logger = spdlog::stdout_color_mt("tjs2");
    static auto plugin_logger = spdlog::stdout_color_mt("plugin");

    spdlog::set_default_logger(core_logger);

    static auto pAppDelegate = std::make_unique<TVPAppDelegate>();
    return pAppDelegate->run();
}
