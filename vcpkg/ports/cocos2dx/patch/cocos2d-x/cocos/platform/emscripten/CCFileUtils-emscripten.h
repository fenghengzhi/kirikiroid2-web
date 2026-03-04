#ifndef __CC_FILEUTILS_EMSCRIPTEN_H__
#define __CC_FILEUTILS_EMSCRIPTEN_H__

#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/CCFileUtils.h"
#include "platform/CCPlatformMacros.h"
#include <string>
#include <vector>

NS_CC_BEGIN

class CC_DLL FileUtilsEmscripten : public FileUtils
{
    friend class FileUtils;
    FileUtilsEmscripten();

public:
    virtual bool init() override;
    virtual std::string getWritablePath() const override;

protected:
    virtual bool isFileExistInternal(const std::string& filePath) const override;
};

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#endif // __CC_FILEUTILS_EMSCRIPTEN_H__
