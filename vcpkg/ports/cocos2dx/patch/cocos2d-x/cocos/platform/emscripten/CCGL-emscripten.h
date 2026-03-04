#ifndef __CCGL_EMSCRIPTEN_H__
#define __CCGL_EMSCRIPTEN_H__

#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#define CC_GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8

#define glClearDepth glClearDepthf

#define glDeleteVertexArraysOES glDeleteVertexArrays
#define glGenVertexArraysOES glGenVertexArrays
#define glBindVertexArrayOES glBindVertexArray

#define glMapBuffer glMapBufferOES
#define glUnmapBuffer glUnmapBufferOES

#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif

#ifndef GL_WRITE_ONLY_OES
#define GL_WRITE_ONLY_OES GL_WRITE_ONLY
#endif

#ifndef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8_OES GL_DEPTH24_STENCIL8
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

typedef char GLchar;

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#endif // __CCGL_EMSCRIPTEN_H__
