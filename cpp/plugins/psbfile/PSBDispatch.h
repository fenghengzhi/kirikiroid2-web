#pragma once

#include "PSBRawFile.h"
#include "tjs.h"

namespace PSB {
    tjs_int32 GetPSBValueClassID();
    iTJSDispatch2 *CreatePSBValueDispatch(PSBRawNode value);
    tTJSVariant CreatePSBValueVariant(PSBRawNode value);
} // namespace PSB
