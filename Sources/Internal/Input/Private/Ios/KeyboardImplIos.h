#pragma once

#if defined(__DAVAENGINE_IPHONE__)

#include "Input/InputElements.h"

namespace DAVA
{
namespace Private
{
class KeyboardImpl final
{
public:
    eInputElements ConvertNativeScancodeToDavaScancode(uint32 nativeScancode);
    WideString TranslateElementToWideString(eInputElements elementId);
};

} // namespace Private
} // namespace DAVA

#endif // __DAVAENGINE_IPHONE__
