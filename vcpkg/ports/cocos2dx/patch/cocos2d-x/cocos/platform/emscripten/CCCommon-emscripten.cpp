#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/CCCommon.h"
#include <cstdio>
#include <cstdarg>
#include <emscripten.h>

NS_CC_BEGIN

void MessageBox(const char * msg, const char * title)
{
    EM_ASM({
        alert(UTF8ToString($1) + ': ' + UTF8ToString($0));
    }, msg, title);
}

void LuaLog(const char * format)
{
    puts(format);
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
