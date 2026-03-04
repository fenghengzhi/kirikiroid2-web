#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#ifndef __AUDIO_ENGINE_EMSCRIPTEN_H_
#define __AUDIO_ENGINE_EMSCRIPTEN_H_

#include <functional>
#include <string>
#include <map>
#include "audio/include/AudioEngine.h"
#include "base/CCRef.h"

NS_CC_BEGIN
namespace experimental {

#define MAX_AUDIOINSTANCES 32

class CC_DLL AudioEngineImpl : public cocos2d::Ref
{
public:
    AudioEngineImpl();
    ~AudioEngineImpl();

    bool init();
    int play2d(const std::string &fileFullPath, bool loop, float volume);
    void setVolume(int audioID, float volume);
    void setLoop(int audioID, bool loop);
    bool pause(int audioID);
    bool resume(int audioID);
    bool stop(int audioID);
    void stopAll();
    float getDuration(int audioID);
    float getCurrentTime(int audioID);
    bool setCurrentTime(int audioID, float time);
    void setFinishCallback(int audioID, const std::function<void(int, const std::string &)> &callback);

    void uncache(const std::string &filePath);
    void uncacheAll();

    int preload(const std::string &filePath, std::function<void(bool isSuccess)> callback);

    void update(float dt);
};

}
NS_CC_END

#endif // __AUDIO_ENGINE_EMSCRIPTEN_H_
#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
