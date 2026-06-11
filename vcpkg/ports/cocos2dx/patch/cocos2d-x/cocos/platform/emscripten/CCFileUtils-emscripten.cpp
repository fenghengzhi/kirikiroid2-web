#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/emscripten/CCFileUtils-emscripten.h"
#include "platform/CCCommon.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

using namespace std;

// VirtualLazyFS 桥（由 krkr2 应用在最终链接时提供，见
// cpp/core/environ/web/VirtualLazyFS.cpp）。UI 资源/字体不再经
// --preload-file 驻留 MEMFS，而是注册进 VLFS 由此按需读取。
// 弱符号：不含 VLFS 的链接产物（如 wasmtime 差分工具链）下地址为 0，
// 走原 MEMFS 路径。
extern "C" {
__attribute__((weak)) int krkr2_vlfs_exists(const char* path);
__attribute__((weak)) unsigned char* krkr2_vlfs_read_all(const char* path,
                                                         unsigned int* outLen);
}

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

FileUtils::Status FileUtilsEmscripten::getContents(const std::string& filename,
                                                   ResizableBuffer* buffer) const
{
    if (krkr2_vlfs_read_all && !filename.empty())
    {
        const std::string fullPath = fullPathForFilename(filename);
        if (!fullPath.empty() && (!krkr2_vlfs_exists || krkr2_vlfs_exists(fullPath.c_str())))
        {
            unsigned int len = 0;
            unsigned char* data = krkr2_vlfs_read_all(fullPath.c_str(), &len);
            if (data)
            {
                buffer->resize(len);
                memcpy(buffer->buffer(), data, len);
                free(data);
                return Status::OK;
            }
        }
    }
    return FileUtils::getContents(filename, buffer);
}

bool FileUtilsEmscripten::isFileExistInternal(const std::string& filePath) const
{
    if (filePath.empty())
        return false;

    if (krkr2_vlfs_exists && krkr2_vlfs_exists(filePath.c_str()) == 1)
        return true;

    struct stat st;
    if (stat(filePath.c_str(), &st) == 0)
    {
        return S_ISREG(st.st_mode);
    }
    return false;
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
