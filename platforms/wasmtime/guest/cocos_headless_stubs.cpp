// Minimal Cocos2d symbols required by core_visual_module in the headless
// Wasmtime guest. These do not create a GL context or a window.

#include "base/CCRef.h"
#include "math/CCGeometry.h"
#include "renderer/CCTexture2D.h"

namespace cocos2d {

const Size Size::ZERO = Size(0.0f, 0.0f);

Size::Size() : width(0.0f), height(0.0f) {}
Size::Size(float w, float h) : width(w), height(h) {}
Size::Size(const Size &other) = default;
Size &Size::operator=(const Size &other) = default;

Ref::Ref() : _referenceCount(1) {}
Ref::~Ref() = default;
void Ref::retain() { ++_referenceCount; }
void Ref::release() {
    if(_referenceCount > 0)
        --_referenceCount;
}
Ref *Ref::autorelease() { return this; }
unsigned int Ref::getReferenceCount() const { return _referenceCount; }

Texture2D::Texture2D() :
    _pixelFormat(PixelFormat::RGBA8888), _pixelsWide(0), _pixelsHigh(0),
    _name(0), _maxS(0.0f), _maxT(0.0f), _contentSize(Size::ZERO),
    _hasPremultipliedAlpha(false), _hasMipmaps(false), _shaderProgram(nullptr),
    _antialiasEnabled(false), _ninePatchInfo(nullptr), _valid(false),
    _alphaTexture(nullptr) {}

Texture2D::~Texture2D() = default;

bool Texture2D::updateWithData(const void *, int, int, int, int) {
    return true;
}

int Texture2D::getPixelsWide() const { return _pixelsWide; }
int Texture2D::getPixelsHigh() const { return _pixelsHigh; }
GLuint Texture2D::getName() const { return _name; }

} // namespace cocos2d
