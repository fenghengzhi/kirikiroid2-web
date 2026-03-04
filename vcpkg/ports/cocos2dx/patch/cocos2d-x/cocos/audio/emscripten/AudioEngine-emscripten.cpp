#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "AudioEngine-emscripten.h"
#include "base/ccUtils.h"

NS_CC_BEGIN
namespace experimental {

AudioEngineImpl::AudioEngineImpl() {}
AudioEngineImpl::~AudioEngineImpl() {}

bool AudioEngineImpl::init() { return true; }

int AudioEngineImpl::play2d(const std::string &fileFullPath, bool loop, float volume)
{
    return AudioEngine::INVALID_AUDIO_ID;
}

void AudioEngineImpl::setVolume(int audioID, float volume) {}
void AudioEngineImpl::setLoop(int audioID, bool loop) {}
bool AudioEngineImpl::pause(int audioID) { return false; }
bool AudioEngineImpl::resume(int audioID) { return false; }
bool AudioEngineImpl::stop(int audioID) { return false; }
void AudioEngineImpl::stopAll() {}
float AudioEngineImpl::getDuration(int audioID) { return 0.0f; }
float AudioEngineImpl::getCurrentTime(int audioID) { return 0.0f; }
bool AudioEngineImpl::setCurrentTime(int audioID, float time) { return false; }

void AudioEngineImpl::setFinishCallback(int audioID, const std::function<void(int, const std::string &)> &callback) {}

void AudioEngineImpl::uncache(const std::string &filePath) {}
void AudioEngineImpl::uncacheAll() {}

int AudioEngineImpl::preload(const std::string &filePath, std::function<void(bool isSuccess)> callback)
{
    if (callback) callback(false);
    return 0;
}

void AudioEngineImpl::update(float dt) {}

}
NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
