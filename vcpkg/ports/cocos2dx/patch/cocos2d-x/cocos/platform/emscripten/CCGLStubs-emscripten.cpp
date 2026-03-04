#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include <GLES2/gl2.h>

extern "C" {

void *glMapBufferOES(GLenum target, GLenum access) {
    (void)target;
    (void)access;
    return nullptr;
}

GLboolean glUnmapBufferOES(GLenum target) {
    (void)target;
    return GL_TRUE;
}

}

#endif
