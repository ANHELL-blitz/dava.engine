#include "Input/Gamepad.h"

#include <algorithm>

#include "Debug/DVAssert.h"
#include "Engine/Engine.h"
#include "Engine/Private/EngineBackend.h"
#include "Engine/Private/Dispatcher/MainDispatcher.h"
#include "Engine/Private/Dispatcher/MainDispatcherEvent.h"
#include "Input/InputEvent.h"
#include "Input/InputSystem.h"
#include "Input/Private/DIElementWrapper.h"

#if defined(__DAVAENGINE_ANDROID__)
#include "Input/Private/Android/GamepadImplAndroid.h"
#elif defined(__DAVAENGINE_WIN_UAP__)
#include "Input/Private/Win10/GamepadImplWin10.h"
#elif defined(__DAVAENGINE_WIN32__)
#include "Input/Private/Win32/GamepadImplWin32.h"
#elif defined(__DAVAENGINE_MACOS__)
#include "Input/Private/Mac/GamepadImplMac.h"
#elif defined(__DAVAENGINE_IPHONE__)
#include "Input/Private/Ios/GamepadImplIos.h"
#else
#error "GamepadDevice: unknown platform"
#endif

namespace DAVA
{
Gamepad::Gamepad(uint32 id)
    : InputDevice(id)
    , inputSystem(GetEngineContext()->inputSystem)
    , impl(new Private::GamepadImpl(this))
    , buttons{}
    , axes{}
{
    endFrameConnectionToken = Engine::Instance()->endFrame.Connect(this, &Gamepad::OnEndFrame);
}

Gamepad::~Gamepad()
{
    Engine::Instance()->endFrame.Disconnect(endFrameConnectionToken);
}

bool Gamepad::IsElementSupported(eInputElements elementId) const
{
    DVASSERT(IsGamepadAxisInputElement(elementId) || IsGamepadButtonInputElement(elementId));
    return supportedElements[elementId - eInputElements::GAMEPAD_FIRST];
}

eDigitalElementStates Gamepad::GetDigitalElementState(eInputElements elementId) const
{
    DVASSERT(IsGamepadButtonInputElement(elementId));
    return buttons[elementId - eInputElements::GAMEPAD_FIRST_BUTTON];
}

AnalogElementState Gamepad::GetAnalogElementState(eInputElements elementId) const
{
    DVASSERT(IsGamepadAxisInputElement(elementId));
    return axes[elementId - eInputElements::GAMEPAD_FIRST_AXIS];
}

void Gamepad::Update()
{
    impl->Update();

    Window* window = GetPrimaryWindow();
    for (uint32 i = eInputElements::GAMEPAD_FIRST_BUTTON; i <= eInputElements::GAMEPAD_LAST_BUTTON; ++i)
    {
        uint32 index = i - eInputElements::GAMEPAD_FIRST_BUTTON;
        if (buttonChangedMask[index])
        {
            InputEvent inputEvent;
            inputEvent.window = window;
            inputEvent.deviceType = eInputDeviceTypes::CLASS_GAMEPAD;
            inputEvent.deviceId = GetId();
            inputEvent.elementId = static_cast<eInputElements>(i);
            inputEvent.digitalState = buttons[index];
            inputSystem->DispatchInputEvent(inputEvent);
        }
    }
    buttonChangedMask.reset();

    for (uint32 i = eInputElements::GAMEPAD_FIRST_AXIS; i <= eInputElements::GAMEPAD_LAST_AXIS; ++i)
    {
        uint32 index = i - eInputElements::GAMEPAD_FIRST_AXIS;
        if (axisChangedMask[index])
        {
            InputEvent inputEvent;
            inputEvent.window = window;
            inputEvent.deviceType = eInputDeviceTypes::CLASS_GAMEPAD;
            inputEvent.deviceId = GetId();
            inputEvent.elementId = static_cast<eInputElements>(i);
            inputEvent.analogState = axes[index];
            inputSystem->DispatchInputEvent(inputEvent);
        }
    }
    axisChangedMask.reset();
}

void Gamepad::OnEndFrame()
{
    for (DIElementWrapper di : buttons)
    {
        di.OnEndFrame();
    }
}

void Gamepad::HandleGamepadAdded(const Private::MainDispatcherEvent& e)
{
    uint32 deviceId = e.gamepadEvent.deviceId;
    impl->HandleGamepadAdded(deviceId);
}

void Gamepad::HandleGamepadRemoved(const Private::MainDispatcherEvent& e)
{
    uint32 deviceId = e.gamepadEvent.deviceId;
    impl->HandleGamepadRemoved(deviceId);
}

void Gamepad::HandleGamepadMotion(const Private::MainDispatcherEvent& e)
{
    impl->HandleGamepadMotion(e);
}

void Gamepad::HandleGamepadButton(const Private::MainDispatcherEvent& e)
{
    impl->HandleGamepadButton(e);
}

void Gamepad::HandleButtonPress(eInputElements element, bool pressed)
{
    uint32 index = element - eInputElements::GAMEPAD_FIRST_BUTTON;
    DIElementWrapper di(buttons[index]);
    if (di.IsPressed() != pressed)
    {
        pressed ? di.Press() : di.Release();
        buttonChangedMask.set(index);
    }
}

void Gamepad::HandleBackButtonPress(bool pressed)
{
    using namespace DAVA::Private;

    DIElementWrapper di(backButton);
    if (di.IsPressed() != pressed)
    {
        pressed ? di.Press() : di.Release();

        if ((backButton & eDigitalElementStates::JUST_PRESSED) == eDigitalElementStates::JUST_PRESSED)
        {
            EngineBackend::Instance()->GetDispatcher()->PostEvent(MainDispatcherEvent(MainDispatcherEvent::BACK_NAVIGATION));
        }
    }
}

void Gamepad::HandleAxisMovement(eInputElements element, float32 newValue, bool horizontal)
{
    // TODO: use some threshold for comparisons below

    uint32 index = element - eInputElements::GAMEPAD_FIRST_AXIS;
    if (horizontal)
    {
        if (newValue != axes[index].x)
        {
            axes[index].x = newValue;
            axisChangedMask.set(index);
        }
    }
    else
    {
        if (newValue != axes[index].y)
        {
            axes[index].y = newValue;
            axisChangedMask.set(index);
        }
    }
}

} // namespace DAVA
