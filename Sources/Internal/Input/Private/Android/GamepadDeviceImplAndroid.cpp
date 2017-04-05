#include "Input/Private/Android/GamepadDeviceImplAndroid.h"

#if defined(__DAVAENGINE_COREV2__)
#if defined(__DAVAENGINE_ANDROID__)

#include "Engine/Private/Dispatcher/MainDispatcherEvent.h"
#include "Input/GamepadDevice.h"
#include "Input/InputElements.h"
#include "Input/Private/DigitalElement.h"

#include <android/input.h>
#include <android/keycodes.h>

namespace DAVA
{
namespace Private
{
GamepadDeviceImpl::GamepadDeviceImpl(GamepadDevice* gamepadDevice)
    : gamepadDevice(gamepadDevice)
{
}

void GamepadDeviceImpl::Update()
{
}

void GamepadDeviceImpl::HandleGamepadMotion(const MainDispatcherEvent& e)
{
    uint32 axis = e.gamepadEvent.axis;
    float32 value = e.gamepadEvent.value;

    if (axis == AMOTION_EVENT_AXIS_HAT_X || axis == AMOTION_EVENT_AXIS_HAT_Y)
    {
        HandleAxisHat(axis, value);
        return;
    }

    // On game pads with two analog joysticks, axis AXIS_RX is often reinterpreted as absolute X position and
    // axis AXIS_RZ is reinterpreted as absolute Y position of the second joystick instead.
    eInputElements element = eInputElements::NONE;
    switch (axis)
    {
    case AMOTION_EVENT_AXIS_X:
        element = eInputElements::GAMEPAD_LTHUMB_X;
        break;
    case AMOTION_EVENT_AXIS_Y:
        element = eInputElements::GAMEPAD_LTHUMB_Y;
        break;
    case AMOTION_EVENT_AXIS_Z:
    case AMOTION_EVENT_AXIS_RX:
        element = eInputElements::GAMEPAD_RTHUMB_X;
        break;
    case AMOTION_EVENT_AXIS_RY:
    case AMOTION_EVENT_AXIS_RZ:
        element = eInputElements::GAMEPAD_RTHUMB_Y;
        break;
    case AMOTION_EVENT_AXIS_LTRIGGER:
    case AMOTION_EVENT_AXIS_BRAKE:
        element = eInputElements::GAMEPAD_LTRIGGER;
        break;
    case AMOTION_EVENT_AXIS_RTRIGGER:
    case AMOTION_EVENT_AXIS_GAS:
        element = eInputElements::GAMEPAD_RTRIGGER;
        break;
    default:
        return;
    }

    // Android joystick Y-axis position is normalized to a range [-1, 1] where -1 for up or far and 1 for down or near.
    // Historically dava.engine's clients expect Y-axis value -1 for down or near and 1 for up and far so negate Y-axes.
    // The same applies to so called 'hats'.
    switch (axis)
    {
    case AMOTION_EVENT_AXIS_Y:
    case AMOTION_EVENT_AXIS_RY:
    case AMOTION_EVENT_AXIS_RZ:
        value = -value;
        break;
    default:
        break;
    }

    uint32 index = element - eInputElements::GAMEPAD_FIRST_AXIS;
    if (gamepadDevice->axises[index].x != value)
    {
        gamepadDevice->axises[index].x = value;
        gamepadDevice->axisChangedMask.set(index);
    }
}

void GamepadDeviceImpl::HandleGamepadButton(const MainDispatcherEvent& e)
{
    eInputElements element = eInputElements::NONE;
    switch (e.gamepadEvent.button)
    {
    case AKEYCODE_BACK:
        element = eInputElements::GAMEPAD_BACK;
        break;
    case AKEYCODE_DPAD_UP:
        element = eInputElements::GAMEPAD_DPAD_UP;
        break;
    case AKEYCODE_DPAD_DOWN:
        element = eInputElements::GAMEPAD_DPAD_DOWN;
        break;
    case AKEYCODE_DPAD_LEFT:
        element = eInputElements::GAMEPAD_DPAD_LEFT;
        break;
    case AKEYCODE_DPAD_RIGHT:
        element = eInputElements::GAMEPAD_DPAD_RIGHT;
        break;
    case AKEYCODE_BUTTON_A:
        element = eInputElements::GAMEPAD_A;
        break;
    case AKEYCODE_BUTTON_B:
        element = eInputElements::GAMEPAD_B;
        break;
    case AKEYCODE_BUTTON_X:
        element = eInputElements::GAMEPAD_X;
        break;
    case AKEYCODE_BUTTON_Y:
        element = eInputElements::GAMEPAD_Y;
        break;
    case AKEYCODE_BUTTON_L1:
    case AKEYCODE_BUTTON_L2:
        element = eInputElements::GAMEPAD_LSHOUDER;
        break;
    case AKEYCODE_BUTTON_R1:
    case AKEYCODE_BUTTON_R2:
        element = eInputElements::GAMEPAD_RSHOUDER;
        break;
    case AKEYCODE_BUTTON_THUMBL:
        element = eInputElements::GAMEPAD_LTHUMB;
        break;
    case AKEYCODE_BUTTON_THUMBR:
        element = eInputElements::GAMEPAD_RTHUMB;
        break;
    case AKEYCODE_BUTTON_START:
        element = eInputElements::GAMEPAD_START;
        break;
    default:
        return;
    }

    bool pressed = e.type == MainDispatcherEvent::GAMEPAD_BUTTON_DOWN;
    size_t index = element - eInputElements::GAMEPAD_FIRST_BUTTON;
    DigitalInputElement button(gamepadDevice->buttons[index]);
    pressed ? button.Press() : button.Release();
    gamepadDevice->buttonChangedMask.set(index);
}

void GamepadDeviceImpl::HandleAxisHat(int axis, float value)
{
    // Some controllers report D-pad presses as axis movement AXIS_HAT_X and AXIS_HAT_Y

    static const eInputElements elemX[] = { eInputElements::GAMEPAD_DPAD_LEFT, eInputElements::GAMEPAD_DPAD_RIGHT };
    static const eInputElements elemY[] = { eInputElements::GAMEPAD_DPAD_UP, eInputElements::GAMEPAD_DPAD_DOWN };

    bool pressed = value != 0.f;
    const eInputElements* elem = axis == AMOTION_EVENT_AXIS_HAT_X ? elemX : elemY;
    if (pressed)
    {
        uint32 index = elem[value > 0.f] - eInputElements::GAMEPAD_FIRST_BUTTON;
        DigitalInputElement dpadElem(gamepadDevice->buttons[index]);

        dpadElem.Press();
        gamepadDevice->buttonChangedMask.set(index);
    }
    else
    {
        for (uint32 i = 0; i < 2; ++i)
        {
            uint32 index = elem[i] - eInputElements::GAMEPAD_FIRST_BUTTON;
            DigitalInputElement dpadElem(gamepadDevice->buttons[index]);
            if (dpadElem.IsPressed())
            {
                dpadElem.Release();
                gamepadDevice->buttonChangedMask.set(index);
                break;
            }
        }
    }
}

bool GamepadDeviceImpl::HandleGamepadAdded(uint32 id)
{
    if (gamepadId == 0)
    {
        gamepadId = id;
        gamepadDevice->profile = eGamepadProfiles::EXTENDED;
    }
    return gamepadId != 0;
}

bool GamepadDeviceImpl::HandleGamepadRemoved(uint32 id)
{
    if (gamepadId == id)
    {
        gamepadId = 0;
    }
    return gamepadId != 0;
}

} // namespace Private
} // namespace DAVA

#endif // __DAVAENGINE_ANDROID__
#endif // __DAVAENGINE_COREV2__
