#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/emscripten/CCFileUtils-emscripten.h"
#include "platform/CCCommon.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

using namespace std;

NS_CC_BEGIN

FileUtils* FileUtils::getInstance()
{
    if (s_sharedFileUtils == nullptr)
    {
        s_sharedFileUtils = new FileUtilsEmscripten();
        if (!s_sharedFileUtils->init())
        {
            delete s_sharedFileUtils;
            s_sharedFileUtils = nullptr;
        }
    }
    return s_sharedFileUtils;
}

FileUtilsEmscripten::FileUtilsEmscripten()
{
}

bool FileUtilsEmscripten::init()
{
    _defaultResRootPath = "/";
    return FileUtils::init();
}

string FileUtilsEmscripten::getWritablePath() const
{
    return "/save/";
}

bool FileUtilsEmscripten::isFileExistInternal(const std::string& filePath) const
{
    if (filePath.empty())
        return false;

    struct stat st;
    if (stat(filePath.c_str(), &st) == 0)
    {
        return S_ISREG(st.st_mode);
    }
    return false;
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
