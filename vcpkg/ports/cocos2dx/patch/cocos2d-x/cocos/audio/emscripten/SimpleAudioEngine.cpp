#include "platform/CCPlatformConfig.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "audio/include/SimpleAudioEngine.h"

using namespace CocosDenshion;

static SimpleAudioEngine *s_pEngine = nullptr;

SimpleAudioEngine *SimpleAudioEngine::getInstance() {
    if (!s_pEngine) s_pEngine = new SimpleAudioEngine();
    return s_pEngine;
}

void SimpleAudioEngine::end() {
    delete s_pEngine;
    s_pEngine = nullptr;
}

SimpleAudioEngine::SimpleAudioEngine() {}
SimpleAudioEngine::~SimpleAudioEngine() {}

void SimpleAudioEngine::preloadBackgroundMusic(const char *pszFilePath) {}
void SimpleAudioEngine::playBackgroundMusic(const char *pszFilePath, bool bLoop) {}
void SimpleAudioEngine::stopBackgroundMusic(bool bReleaseData) {}
void SimpleAudioEngine::pauseBackgroundMusic() {}
void SimpleAudioEngine::resumeBackgroundMusic() {}
void SimpleAudioEngine::rewindBackgroundMusic() {}
bool SimpleAudioEngine::willPlayBackgroundMusic() { return false; }
bool SimpleAudioEngine::isBackgroundMusicPlaying() { return false; }

float SimpleAudioEngine::getBackgroundMusicVolume() { return 1.0f; }
void SimpleAudioEngine::setBackgroundMusicVolume(float volume) {}

float SimpleAudioEngine::getEffectsVolume() { return 1.0f; }
void SimpleAudioEngine::setEffectsVolume(float volume) {}

unsigned int SimpleAudioEngine::playEffect(const char *pszFilePath, bool bLoop, float pitch, float pan, float gain) { return 0; }
void SimpleAudioEngine::pauseEffect(unsigned int nSoundId) {}
void SimpleAudioEngine::pauseAllEffects() {}
void SimpleAudioEngine::resumeEffect(unsigned int nSoundId) {}
void SimpleAudioEngine::resumeAllEffects() {}
void SimpleAudioEngine::stopEffect(unsigned int nSoundId) {}
void SimpleAudioEngine::stopAllEffects() {}
void SimpleAudioEngine::preloadEffect(const char *pszFilePath) {}
void SimpleAudioEngine::unloadEffect(const char *pszFilePath) {}

#endif
