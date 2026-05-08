#pragma once

#include "tjs.h"

namespace motion {

    class SeparateLayerAdaptor;

    class PrivateMotionGLL {
    public:
        PrivateMotionGLL(const tTJSVariant &ownerVariant,
                         const tTJSVariant &targetLayerVariant);
        ~PrivateMotionGLL();

        iTJSDispatch2 *ensureLayerObject(iTJSDispatch2 *targetLayerObject,
                                         bool absolute);
        iTJSDispatch2 *layerObject() const;
        tTJSVariant layerVariant() const;
        void setSize(int width, int height);
        void setVisible(bool visible);
        void setAbsolute(bool absolute);
        void invalidate();

    private:
        tTJSVariant _ownerVariant;
        tTJSVariant _targetLayerVariant;
        tTJSVariant _layerObject;
    };

    iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
        SeparateLayerAdaptor &sla,
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject,
        int canvasWidth,
        int canvasHeight);

} // namespace motion
