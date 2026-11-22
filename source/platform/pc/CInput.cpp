#include "platform/pc/CInput.hpp"
#include "platform/pc/InputBindings.hpp"
#include "platform/pc/PcLaunchOptions.hpp"
#include "platform/pc/PcPaths.hpp"
#include "platform/pc/PcPromptArt.hpp"
#include "platform/pc/Renderer.hpp"
#if LCE_PLATFORM_SWITCH
#include "platform/switch/SwitchRuntime.hpp"
#include <switch.h>
#endif

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>

struct _STRING_VERIFY_RESPONSE
{
    unsigned short count;
    unsigned short reserved;
    int* results;
};

int XEnableGuestSignin(int enable)
{
    (void)enable;
    return 0;
}

namespace
{
const float kSecondsPerNanosecond = 1.0e-9f;
const int kStickMaximum = 32767;
const int kTriggerThreshold = 3855;
const unsigned int kTouchInputButtons = 0x00c03cff;
const int kKeyboardJoypad = 0;
const int kKeyboardTextLimit = 256;
const float kTouchSpaceWidth = 1280.0f;
const float kTouchSpaceHeight = 720.0f;

int ClampStick(int value)
{
    if (value > kStickMaximum)
    {
        return kStickMaximum;
    }
    if (value < -kStickMaximum)
    {
        return -kStickMaximum;
    }
    return value;
}

int ApplyDeadzone(int value, int deadzone)
{
    if (value > deadzone)
    {
        return value - deadzone;
    }
    if (value < -deadzone)
    {
        return value + deadzone;
    }
    return 0;
}

float NormaliseStick(int value, float range)
{
    float result = static_cast<float>(value) / range;
    if (result > -0.15f && result < 0.15f)
    {
        return 0.0f;
    }
    return result;
}

unsigned char TriggerByte(int value)
{
    if (value <= 0)
    {
        return 0;
    }
    if (value >= kStickMaximum)
    {
        return 255;
    }
    return static_cast<unsigned char>(value * 255 / kStickMaximum);
}

unsigned int StickBits(int leftX, int leftY, int rightX, int rightY, int maximum)
{
    unsigned int bits = 0;
    if (leftX > maximum)
        bits |= EPcButton_LeftStickRight;
    if (leftX < -maximum)
        bits |= EPcButton_LeftStickLeft;
    if (rightY < -maximum)
        bits |= EPcButton_RightStickDown;
    if (rightY > maximum)
        bits |= EPcButton_RightStickUp;
    if (rightX > maximum)
        bits |= EPcButton_RightStickRight;
    if (rightX < -maximum)
        bits |= EPcButton_RightStickLeft;
    if (leftY < -maximum)
        bits |= EPcButton_LeftStickDown;
    if (leftY > maximum)
        bits |= EPcButton_LeftStickUp;
    return bits;
}

int LargerMagnitude(int first, int second)
{
    int firstMagnitude = first < 0 ? -first : first;
    int secondMagnitude = second < 0 ? -second : second;
    return secondMagnitude > firstMagnitude ? second : first;
}

void AppendUtf8(std::string& text, unsigned int codepoint)
{
    if (codepoint < 0x80)
    {
        text += static_cast<char>(codepoint);
    }
    else if (codepoint < 0x800)
    {
        text += static_cast<char>(0xc0 | (codepoint >> 6));
        text += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint < 0x10000)
    {
        text += static_cast<char>(0xe0 | (codepoint >> 12));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else
    {
        text += static_cast<char>(0xf0 | (codepoint >> 18));
        text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
}

std::string WideToUtf8(const std::wstring& text)
{
    std::string result;
    for (size_t index = 0; index < text.size(); ++index)
    {
        unsigned int codepoint = static_cast<unsigned int>(text[index]);
        if (sizeof(wchar_t) == 2 && codepoint >= 0xd800 && codepoint < 0xdc00 && index + 1 < text.size())
        {
            unsigned int low = static_cast<unsigned int>(text[index + 1]);
            if (low >= 0xdc00 && low < 0xe000)
            {
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            }
        }
        if ((codepoint >= 0xd800 && codepoint < 0xe000) || codepoint > 0x10ffff)
        {
            codepoint = 0xfffd;
        }
        AppendUtf8(result, codepoint);
    }
    return result;
}
}

CInput* g_pInputInstance = 0;
CInput* g_pInput = new (std::nothrow) CInput;

void PcInputHandleEvent(const SDL_Event& event)
{
    if (g_pInputInstance != 0)
    {
        g_pInputInstance->HandleEvent(event);
    }
}

bool PcInputGetKeyboardText(std::wstring* title, std::wstring* text)
{
    if (g_pInputInstance == 0 || g_pInputInstance->keyboardState != CInput::EKeyboardState_Active)
    {
        return false;
    }
    *title = g_pInputInstance->keyboardTitle;
    text->assign(g_pInputInstance->keyboardEditText.begin(), g_pInputInstance->keyboardEditText.end());
    return true;
}

CInput::CInput()
{
    g_pInputInstance = this;
    std::memset(joypads, 0, sizeof(joypads));
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        joypad.sensitivity = 1.0f;
        joypad.lockedController = -1;
        joypad.leftXSource = &joypad.leftX;
        joypad.leftYSource = &joypad.leftY;
        joypad.rightXSource = &joypad.rightX;
        joypad.rightYSource = &joypad.rightY;
        joypad.leftTriggerSource = &joypad.leftTrigger;
        joypad.rightTriggerSource = &joypad.rightTrigger;
        gameJoypadMaps[pad] = 0;
        alternateJoypadMaps[pad] = 0;
        idleStartTimes[pad] = 0;
    }
    for (int pad = 0; pad < 4; ++pad)
    {
        padRemap[pad] = -1;
        menuDisplayed[pad] = 0;
    }
    controllerCount = 0;
    lastInputJoypad = kKeyboardJoypad;
    menuDrawsPointer = false;
    cursorShown = true;
    deadzone = 10000;
    maximumInput = 10000;
    movementRange = 0x7fff;
    movementSpan = 0x58ef;
    movementSpanFloat = 22767.0f;
    controllerConfiguration[0] = 0;
    controllerConfiguration[1] = 0;
    controllerConfiguration[2] = 0;
    joypadMapsReady = false;
    initialised = false;
    inputTick = 0;
    initialRepeatDelay = 0.5f;
    repeatDelay = 0.8f;
    circleCrossSwapped = false;
    std::memset(buttonTimes, 0, sizeof(buttonTimes));
    debugSequenceCallback = 0;
    debugSequenceContext = 0;
    localMultiplayer = false;

    mouseLookEnabled = false;
    std::memset(touchData, 0, sizeof(touchData));
    mouseCaptured = false;
    pointerPlacementPending = false;
    pointerPlacementX = 0.0f;
    pointerPlacementY = 0.0f;
    windowFocused = true;
    lastWindowId = 0;
    textCapture = false;
    textCaptureWindowId = 0;

    keyboardState = EKeyboardState_Idle;
    keyboardCallback = 0;
    keyboardContext = 0;
    keyboardMaximumLength = 0;
    keyboardPad = 0;
    keyboardMode = EKeyboardMode_0;
    std::memset(keyboardText, 0, sizeof(keyboardText));
    keyboardAccepted = false;
    keyboardCancelled = false;
    destroyKeyboardRequested = false;
    keyboardWindowId = 0;

    errorViewerState = EErrorViewerState_None;
    errorViewerCallback = 0;
    errorViewerContext = 0;
    errorViewerController = 0;
    errorViewerCode = 0;
    errorViewerHasText = false;
}

CInput::~CInput()
{
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        if (joypads[pad].gamepad != 0)
        {
            SDL_CloseGamepad(joypads[pad].gamepad);
            joypads[pad].gamepad = 0;
        }
        delete[] gameJoypadMaps[pad];
        delete[] alternateJoypadMaps[pad];
        gameJoypadMaps[pad] = 0;
        alternateJoypadMaps[pad] = 0;
    }
    if (g_pInputInstance == this)
    {
        g_pInputInstance = 0;
    }
}

void CInput::Joypad::setConnected(bool value)
{
    if (connected != value)
    {
        connected = value;
        newlyConnected = value;
        disconnected = !value;
        return;
    }
    disconnected = false;
    newlyConnected = false;
}

void CInput::Initialise(int count, unsigned char gameMapCount, unsigned char menuMapCount)
{
    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "CInput: gamepad subsystem unavailable: %s", SDL_GetError());
    }
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        if (joypads[pad].gamepad != 0)
        {
            SDL_CloseGamepad(joypads[pad].gamepad);
        }
    }
    std::memset(joypads, 0, sizeof(joypads));

    controllerCount = 0;
    deadzone = 10000;
    maximumInput = 10000;
    movementRange = 0x7fff;
    movementSpan = 0x58ef;
    movementSpanFloat = 22767.0f;
    unsigned long long now = SDL_GetTicksNS();
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        joypad.sensitivity = 1.0f;
        joypad.lockedController = -1;
        joypad.leftXSource = &joypad.leftX;
        joypad.leftYSource = &joypad.leftY;
        joypad.rightXSource = &joypad.rightX;
        joypad.rightYSource = &joypad.rightY;
        joypad.leftTriggerSource = &joypad.leftTrigger;
        joypad.rightTriggerSource = &joypad.rightTrigger;
        delete[] gameJoypadMaps[pad];
        delete[] alternateJoypadMaps[pad];
        gameJoypadMaps[pad] = new unsigned int[gameMapCount];
        alternateJoypadMaps[pad] = new unsigned int[gameMapCount];
        for (unsigned int map = 0; map < gameMapCount; ++map)
        {
            gameJoypadMaps[pad][map] = 0;
            alternateJoypadMaps[pad][map] = 0;
        }
        idleStartTimes[pad] = now;
    }
    controllerConfiguration[0] = static_cast<unsigned char>(count);
    controllerConfiguration[1] = gameMapCount;
    controllerConfiguration[2] = menuMapCount;
    joypadMapsReady = false;
    for (int pad = 0; pad < 4; ++pad)
    {
        padRemap[pad] = -1;
    }
    initialised = true;
    keyBindingsPath = PcSaveRoot() + "/keybinds.txt";
    keyboardMouse.LoadBindings(keyBindingsPath);

    int gamepadCount = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);
#if LCE_PLATFORM_SWITCH
    char inputStatus[80];
    std::snprintf(inputStatus, sizeof(inputStatus), "SDL detected %d gamepad(s)", gamepadCount);
    SwitchLogStartupStage(inputStatus);
#endif
    if (gamepads != 0)
    {
        for (int index = 0; index < gamepadCount; ++index)
        {
            AddGamepad(gamepads[index]);
        }
        SDL_free(gamepads);
    }
}

void CInput::AddGamepad(unsigned int gamepadId)
{
    if (!initialised)
    {
        return;
    }
    int freeSlot = -1;
    for (int pad = 1; pad < JOYPAD_COUNT; ++pad)
    {
        if (joypads[pad].gamepad != 0)
        {
            if (joypads[pad].gamepadId == gamepadId)
            {
                return;
            }
        }
        else if (freeSlot < 0)
        {
            freeSlot = pad;
        }
    }
    if (freeSlot < 0)
    {
        return;
    }
    SDL_Gamepad* gamepad = SDL_OpenGamepad(static_cast<SDL_JoystickID>(gamepadId));
    if (gamepad == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "CInput: could not open gamepad %u: %s", gamepadId, SDL_GetError());
        return;
    }
    joypads[freeSlot].gamepad = gamepad;
    joypads[freeSlot].gamepadId = gamepadId;
    ResolveFaceButtons(joypads[freeSlot]);
#if LCE_PLATFORM_SWITCH
    SwitchLogStartupStage("Opened a Switch gamepad");
#endif
}

void CInput::ResolveFaceButtons(Joypad& joypad)
{
    int faceCount = 0;
    const int* faceButtons = PcInputGamepadFaceButtons(faceCount);
    int labelCount = 0;
    const PcGamepadLabelBinding* labels = PcInputGamepadLabelBindings(labelCount);
    for (int face = 0; face < 4; ++face)
    {
        joypad.padFaceButtons[face] = 0;
        if (face >= faceCount || joypad.gamepad == 0)
        {
            continue;
        }
        SDL_GamepadButton button = static_cast<SDL_GamepadButton>(faceButtons[face]);
        if (button == SDL_GAMEPAD_BUTTON_NORTH)
        {
            joypad.padFaceButtons[face] = EPcButton_Face4;
            continue;
        }
        if (button == SDL_GAMEPAD_BUTTON_WEST)
        {
            joypad.padFaceButtons[face] = EPcButton_Face3;
            continue;
        }
        SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabel(joypad.gamepad, button);
        if (label == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN)
        {
            label = SDL_GetGamepadButtonLabelForType(SDL_GAMEPAD_TYPE_STANDARD, button);
        }
        for (int index = 0; index < labelCount; ++index)
        {
            if (labels[index].label == static_cast<int>(label))
            {
                joypad.padFaceButtons[face] = labels[index].buttons;
                break;
            }
        }
    }
}

void CInput::RemoveGamepad(unsigned int gamepadId)
{
    for (int pad = 1; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        if (joypad.gamepad != 0 && joypad.gamepadId == gamepadId)
        {
            SDL_CloseGamepad(joypad.gamepad);
            joypad.gamepad = 0;
            joypad.gamepadId = 0;
            joypad.padButtons = 0;
            std::memset(joypad.padFaceButtons, 0, sizeof(joypad.padFaceButtons));
            joypad.padLeftX = 0;
            joypad.padLeftY = 0;
            joypad.padRightX = 0;
            joypad.padRightY = 0;
            joypad.padLeftTrigger = 0;
            joypad.padRightTrigger = 0;
            return;
        }
    }
}

void CInput::HandleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        AddGamepad(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        RemoveGamepad(event.gdevice.which);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.windowID != 0)
        {
            lastWindowId = event.key.windowID;
        }
        if (keyboardState == EKeyboardState_Active)
        {
            HandleKeyboardKey(static_cast<int>(event.key.scancode), event.key.mod, event.key.repeat);
        }
        else if (textCapture)
        {
            HandleTextCaptureKey(static_cast<int>(event.key.scancode), event.key.mod, event.key.repeat);
        }
        else
        {
            keyboardMouse.HandleEvent(event, KeyboardContext(), mouseCaptured);
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (keyboardState == EKeyboardState_Active)
        {
            HandleKeyboardTextInput(event.text.text);
        }
        else if (textCapture)
        {
            AppendCapturedText(event.text.text);
        }
        break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        keyboardMouse.HandleEvent(event, KeyboardContext(), mouseCaptured);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        if (textCapture)
        {
            if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f)
            {
                capturedKeys.push_back(event.wheel.y > 0.0f ? ETextKey_PageUp : ETextKey_PageDown);
            }
        }
        else if (keyboardState != EKeyboardState_Active)
        {
            keyboardMouse.HandleEvent(event, KeyboardContext(), mouseCaptured);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        windowFocused = true;
        lastWindowId = event.window.windowID;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        windowFocused = false;
        ReleaseAllKeys();
        break;
    default:
        break;
    }
}

void CInput::BeginTextCapture(int pad)
{
    (void)pad;
    if (textCapture)
    {
        return;
    }
    textCapture = true;
    capturedText.clear();
    capturedKeys.clear();
    keyboardMouse.ReleaseAll();
    textCaptureWindowId = 0;
    SDL_Window* window = LocateWindow();
    if (window != 0 && SDL_StartTextInput(window))
    {
        textCaptureWindowId = SDL_GetWindowID(window);
    }
}

void CInput::EndTextCapture()
{
    if (!textCapture)
    {
        return;
    }
    if (textCaptureWindowId != 0)
    {
        SDL_Window* window = SDL_GetWindowFromID(textCaptureWindowId);
        if (window != 0)
        {
            SDL_StopTextInput(window);
        }
        textCaptureWindowId = 0;
    }
    textCapture = false;
    capturedText.clear();
    capturedKeys.clear();
}

bool CInput::ConsumeCapturedText(std::wstring* text)
{
    if (capturedText.empty())
    {
        return false;
    }
    text->swap(capturedText);
    capturedText.clear();
    return true;
}

int CInput::ConsumeTextKey()
{
    if (capturedKeys.empty())
    {
        return ETextKey_None;
    }
    int key = capturedKeys.front();
    capturedKeys.erase(capturedKeys.begin());
    return key;
}

void CInput::AppendCapturedText(const char* text)
{
    if (text == 0)
    {
        return;
    }
    size_t length = std::strlen(text);
    const char* cursor = text;
    while (length > 0)
    {
        unsigned int codepoint = SDL_StepUTF8(&cursor, &length);
        if (codepoint == 0)
        {
            break;
        }
        if (codepoint == 0x0a || codepoint == 0x0d || codepoint == 0x09)
        {
            codepoint = ' ';
        }
        if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0) || codepoint == SDL_INVALID_UNICODE_CODEPOINT
            || (codepoint >= 0xd800 && codepoint < 0xe000) || codepoint > 0x10ffff)
        {
            continue;
        }
        if (sizeof(wchar_t) == 2 && codepoint > 0xffff)
        {
            unsigned int offset = codepoint - 0x10000;
            capturedText.push_back(static_cast<wchar_t>(0xd800 + (offset >> 10)));
            capturedText.push_back(static_cast<wchar_t>(0xdc00 + (offset & 0x3ff)));
        }
        else
        {
            capturedText.push_back(static_cast<wchar_t>(codepoint));
        }
    }
}

void CInput::HandleTextCaptureKey(int scancode, unsigned short modifiers, bool repeat)
{
    const bool control = (modifiers & SDL_KMOD_CTRL) != 0;
    int key = ETextKey_None;
    switch (scancode)
    {
    case SDL_SCANCODE_BACKSPACE:
        key = control ? ETextKey_WordBackspace : ETextKey_Backspace;
        break;
    case SDL_SCANCODE_DELETE:
        key = ETextKey_Delete;
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        key = repeat ? ETextKey_None : ETextKey_Enter;
        break;
    case SDL_SCANCODE_ESCAPE:
        key = repeat ? ETextKey_None : ETextKey_Escape;
        break;
    case SDL_SCANCODE_TAB:
        key = ETextKey_Tab;
        break;
    case SDL_SCANCODE_UP:
        key = ETextKey_Up;
        break;
    case SDL_SCANCODE_DOWN:
        key = ETextKey_Down;
        break;
    case SDL_SCANCODE_LEFT:
        key = control ? ETextKey_WordLeft : ETextKey_Left;
        break;
    case SDL_SCANCODE_RIGHT:
        key = control ? ETextKey_WordRight : ETextKey_Right;
        break;
    case SDL_SCANCODE_HOME:
        key = ETextKey_Home;
        break;
    case SDL_SCANCODE_END:
        key = ETextKey_End;
        break;
    case SDL_SCANCODE_PAGEUP:
        key = ETextKey_PageUp;
        break;
    case SDL_SCANCODE_PAGEDOWN:
        key = ETextKey_PageDown;
        break;
    case SDL_SCANCODE_V:
        if (control && !repeat)
        {
            char* clipboard = SDL_GetClipboardText();
            if (clipboard != 0)
            {
                AppendCapturedText(clipboard);
                SDL_free(clipboard);
            }
        }
        break;
    default:
        break;
    }
    if (key != ETextKey_None)
    {
        capturedKeys.push_back(key);
    }
}

void CInput::ReleaseAllKeys()
{
    keyboardMouse.ReleaseAll();
}

int CInput::KeyboardPlayer() const
{
    return joypads[kKeyboardJoypad].lockedController;
}

bool CInput::IsKeyboardInMenu() const
{
    int player = KeyboardPlayer();
    if (player < 0 || player >= 4)
    {
        for (int pad = 0; pad < 4; ++pad)
        {
            if (menuDisplayed[pad] != 0)
            {
                return true;
            }
        }
        return false;
    }
    return menuDisplayed[player] != 0;
}

EPcInputContext CInput::KeyboardContext() const
{
    return IsKeyboardInMenu() ? EPcInputContext_Menu : EPcInputContext_Game;
}

bool CInput::IsMenuShown(int pad) const
{
    return static_cast<unsigned int>(pad) < 4 && menuDisplayed[pad] != 0;
}

SDL_Window* CInput::LocateWindow() const
{
    SDL_Window* window = SDL_GetKeyboardFocus();
    if (window == 0 && lastWindowId != 0)
    {
        window = SDL_GetWindowFromID(lastWindowId);
    }
    if (window == 0)
    {
        int windowCount = 0;
        SDL_Window** windows = SDL_GetWindows(&windowCount);
        if (windows != 0)
        {
            if (windowCount > 0)
            {
                window = windows[0];
            }
            SDL_free(windows);
        }
    }
    return window;
}

bool CInput::ConsumeMouseLook(int pad, float* x, float* y)
{
    if (!mouseLookEnabled || pad != KeyboardPlayer())
    {
        return false;
    }
    return keyboardMouse.ConsumeLook(x, y);
}

void CInput::UpdateMouseCapture()
{
    bool desired = windowFocused && keyboardState == EKeyboardState_Idle
                   && errorViewerState == EErrorViewerState_None && KeyboardPlayer() >= 0 && !IsKeyboardInMenu();
    mouseLookEnabled = desired;
    bool showCursor = !desired && !(menuDrawsPointer && keyboardState == EKeyboardState_Idle)
                      && lastInputJoypad == kKeyboardJoypad;
    if (showCursor != cursorShown && (showCursor ? SDL_ShowCursor() : SDL_HideCursor()))
    {
        cursorShown = showCursor;
    }
    if (desired == mouseCaptured)
    {
        return;
    }
    keyboardMouse.DiscardRelativeInput();
    SDL_Window* window = LocateWindow();
    if (window != 0)
    {
        if (SDL_SetWindowRelativeMouseMode(window, desired))
        {
            mouseCaptured = desired;
            if (!desired && windowFocused)
            {
                if (pointerPlacementPending)
                {
                    WarpPointer(window, pointerPlacementX, pointerPlacementY);
                }
                else
                {
                    int width = 0;
                    int height = 0;
                    if (SDL_GetWindowSize(window, &width, &height))
                        WarpPointer(window, width * 0.5f, height * 0.5f);
                }
            }
            pointerPlacementPending = false;
        }
    }
    else if (!desired)
    {
        mouseCaptured = false;
    }
}

void CInput::WarpPointer(SDL_Window* window, float x, float y)
{
    SDL_WarpMouseInWindow(window, x, y);
    keyboardMouse.SetPointer(x, y);
}

void CInput::PlaceMenuPointer(int pad, float x, float y)
{
    if (!IsKeyboardPad(pad))
    {
        return;
    }
    SDL_Window* window = LocateWindow();
    int width = 0;
    int height = 0;
    if (window == 0 || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
    {
        return;
    }
    float areaLeft;
    float areaTop;
    float areaWidth;
    float areaHeight;
    gRenderer->GetInterfaceArea(&areaLeft, &areaTop, &areaWidth, &areaHeight);
    const float windowX = (x / kTouchSpaceWidth * areaWidth + areaLeft) * static_cast<float>(width);
    const float windowY = (y / kTouchSpaceHeight * areaHeight + areaTop) * static_cast<float>(height);
    if (mouseCaptured)
    {
        pointerPlacementPending = true;
        pointerPlacementX = windowX;
        pointerPlacementY = windowY;
    }
    else if (windowFocused)
    {
        WarpPointer(window, windowX, windowY);
    }
}

void CInput::ReadGamepad(Joypad& joypad)
{
    joypad.padButtons = 0;
    joypad.padLeftX = 0;
    joypad.padLeftY = 0;
    joypad.padRightX = 0;
    joypad.padRightY = 0;
    joypad.padLeftTrigger = 0;
    joypad.padRightTrigger = 0;
    if (joypad.gamepad == 0)
    {
        return;
    }
    int count = 0;
    const PcGamepadBinding* bindings = PcInputGamepadBindings(count);
    for (int index = 0; index < count; ++index)
    {
        if (SDL_GetGamepadButton(joypad.gamepad, static_cast<SDL_GamepadButton>(bindings[index].button)))
        {
            joypad.padButtons |= bindings[index].buttons;
        }
    }
    int faceCount = 0;
    const int* faceButtons = PcInputGamepadFaceButtons(faceCount);
    for (int face = 0; face < faceCount && face < 4; ++face)
    {
        if (SDL_GetGamepadButton(joypad.gamepad, static_cast<SDL_GamepadButton>(faceButtons[face])))
        {
            joypad.padButtons |= joypad.padFaceButtons[face];
        }
    }
#if LCE_PLATFORM_SWITCH
    static bool reportedGamepadInput = false;
    if (joypad.padButtons != 0 && !reportedGamepadInput)
    {
        reportedGamepadInput = true;
        SwitchLogStartupStage("Received the first Switch gamepad button press");
    }
#endif
    joypad.padLeftX = ClampStick(SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    joypad.padLeftY = ClampStick(-static_cast<int>(SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_LEFTY)));
    joypad.padRightX = ClampStick(SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    joypad.padRightY = ClampStick(-static_cast<int>(SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_RIGHTY)));
    int leftTrigger = SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    int rightTrigger = SDL_GetGamepadAxis(joypad.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if (leftTrigger > kTriggerThreshold)
    {
        joypad.padButtons |= EPcButton_LeftTrigger;
    }
    if (rightTrigger > kTriggerThreshold)
    {
        joypad.padButtons |= EPcButton_RightTrigger;
    }
    joypad.padLeftTrigger = TriggerByte(leftTrigger);
    joypad.padRightTrigger = TriggerByte(rightTrigger);
}

void CInput::ReadAllPads()
{
    bool recentlyForegrounded = inputTick != 0;
    if (recentlyForegrounded)
    {
        --inputTick;
    }
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        joypad.previousConsumed = joypad.consumed;
        joypad.consumed = false;
        bool connected = pad == kKeyboardJoypad;
        if (pad != kKeyboardJoypad)
        {
            ReadGamepad(joypad);
            connected = joypad.gamepad != 0;
            if (!connected && recentlyForegrounded && joypad.connected)
            {
                connected = true;
            }
        }
        joypad.setConnected(connected);
    }
}

void CInput::ConsumeAllPads()
{
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        joypads[pad].consumed = true;
    }
}

void CInput::ClearJoypadValues(Joypad* joypad)
{
    joypad->touchValue = 0;
    joypad->rawValue = 0;
    joypad->pressedValue = 0;
    joypad->releasedValue = 0;
    joypad->leftX = 0.0f;
    joypad->leftY = 0.0f;
    joypad->rightX = 0.0f;
    joypad->rightY = 0.0f;
    joypad->leftTrigger = 0;
    joypad->rightTrigger = 0;
}

void CInput::ApplyStickValues(Joypad* joypad, int leftX, int leftY, int rightX, int rightY)
{
    joypad->rawValue |= StickBits(leftX, leftY, rightX, rightY, static_cast<int>(maximumInput));
    int zone = static_cast<int>(deadzone);
    joypad->leftX = NormaliseStick(ApplyDeadzone(leftX, zone), movementSpanFloat);
    joypad->leftY = NormaliseStick(ApplyDeadzone(leftY, zone), movementSpanFloat);
    joypad->rightX = NormaliseStick(ApplyDeadzone(rightX, zone), movementSpanFloat);
    joypad->rightY = NormaliseStick(ApplyDeadzone(rightY, zone), movementSpanFloat);
}

void CInput::SetJoypadValues(int pad, Joypad* joypad)
{
    int leftX = joypad->padLeftX;
    int leftY = joypad->padLeftY;
    int rightX = joypad->padRightX;
    int rightY = joypad->padRightY;
    unsigned char leftTrigger = joypad->padLeftTrigger;
    unsigned char rightTrigger = joypad->padRightTrigger;
    joypad->rawValue |= joypad->padButtons;
    ApplyStickValues(joypad, leftX, leftY, rightX, rightY);
    joypad->leftTrigger = leftTrigger;
    joypad->rightTrigger = rightTrigger;
}

void CInput::GetStartTime(int pad, int button)
{
    ButtonTime& time = buttonTimes[pad][button];
    unsigned long long now = SDL_GetTicksNS();
    time.lastUpdate = now;
    time.heldTime = 0.0f;
    time.repeatTime = 0.0f;
    time.repeated = false;
    idleStartTimes[pad] = now;
}

void CInput::UpdateTime(int pad, int button)
{
    ButtonTime& time = buttonTimes[pad][button];
    unsigned long long now = SDL_GetTicksNS();
    unsigned long long elapsed = now - time.lastUpdate;
    time.lastUpdate = now;
    time.heldTime += static_cast<float>(elapsed) * kSecondsPerNanosecond;
    idleStartTimes[pad] = now;
}

void CInput::UpdateKeyboardMouse(Joypad& joypad)
{
    if (joypad.consumed)
    {
        keyboardMouse.ReleaseAll();
    }
    keyboardMouse.Update(KeyboardContext(), initialRepeatDelay, repeatDelay);
    joypad.previousValue = 0;
    joypad.rawValue = 0;
    joypad.pressedValue = 0;
    joypad.releasedValue = 0;
    joypad.leftX = keyboardMouse.MoveX();
    joypad.leftY = keyboardMouse.MoveY();
    joypad.rightX = 0.0f;
    joypad.rightY = 0.0f;
    joypad.leftTrigger = 0;
    joypad.rightTrigger = 0;
    if (keyboardMouse.AnyInputPressed() || keyboardMouse.AnyInputDown())
    {
        idleStartTimes[kKeyboardJoypad] = SDL_GetTicksNS();
    }
    if (keyboardMouse.AnyInputPressed() || keyboardMouse.MouseMovedThisTick())
    {
        lastInputJoypad = kKeyboardJoypad;
    }
}

void CInput::UpdateJoypads()
{
    UpdateKeyboardMouse(joypads[kKeyboardJoypad]);

    int connected = 1;
    for (int pad = 1; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        joypad.previousValue = joypad.rawValue;
        joypad.rawValue = joypad.fakeValue;
        if (joypad.fakeTimer)
        {
            --joypad.fakeTimer;
        }
        else
        {
            joypad.fakeValue = 0;
        }
        joypad.leftTrigger = 0;
        joypad.rightTrigger = 0;
    }
    for (int pad = 1; pad < JOYPAD_COUNT; ++pad)
    {
        Joypad& joypad = joypads[pad];
        if (joypad.consumed)
        {
            ClearJoypadValues(&joypad);
            if (joypad.connected)
            {
                ++connected;
            }
            continue;
        }
        if (joypad.newlyConnected || joypad.disconnected)
        {
            ClearJoypadValues(&joypad);
        }
        if (joypad.connected)
        {
            SetJoypadValues(pad, &joypad);
            joypad.rawValue |= joypad.touchValue;
            joypad.touchValue = 0;
            if (!joypad.previousConsumed)
            {
                joypad.pressedValue = joypad.rawValue & ~joypad.previousValue;
                joypad.releasedValue = joypad.previousValue & ~joypad.rawValue;
            }
            else
            {
                joypad.pressedValue = 0;
                joypad.releasedValue = 0;
            }
            if (joypad.pressedValue != 0)
            {
                lastInputJoypad = pad;
            }
            for (int button = 0; button < BUTTON_COUNT; ++button)
            {
                unsigned int mask = 1u << button;
                if (joypad.pressedValue & mask)
                {
                    GetStartTime(pad, button);
                }
                else if (joypad.rawValue & mask)
                {
                    UpdateTime(pad, button);
                }
            }
            ++connected;
        }
    }
    controllerCount = static_cast<unsigned int>(connected);
}

void CInput::Tick()
{
    ReadAllPads();
    TickErrorViewer();
    TickKeyboard();
    TickVerifyStrings();
    UpdateMouseCapture();
    UpdateJoypads();
    if (!PcGetLaunchOptions().splitScreen)
        FollowDeviceInUse();
}

void CInput::FollowDeviceInUse()
{
    const int current = padRemap[0];
    const int next = lastInputJoypad;
    if (current < 0 || next == current || static_cast<unsigned int>(next) >= JOYPAD_COUNT
        || !joypads[next].connected || joypads[next].lockedController >= 0)
    {
        return;
    }
    const unsigned int heldButtons = joypads[current].rawValue;
    ClearJoypadValues(&joypads[current]);
    joypads[current].rawValue = heldButtons;
    joypads[current].lockedController = -1;
    padRemap[0] = next;
    joypads[next].lockedController = 0;
}

void CInput::SetDeadzoneAndMovementRange(unsigned int value, unsigned int maximum, unsigned int range)
{
    deadzone = value;
    maximumInput = maximum;
    movementRange = range;
    movementSpan = range - value;
    movementSpanFloat = static_cast<float>(movementSpan);
}

void CInput::SetGameControllerTypeJoypadMaps(int controllerType, unsigned char map, unsigned int value)
{
    unsigned int** maps = controllerType == 0 ? gameJoypadMaps : alternateJoypadMaps;
    for (int pad = 0; pad < JOYPAD_COUNT; ++pad)
    {
        if (maps[pad] != 0 && map < controllerConfiguration[1])
        {
            maps[pad][map] = value;
        }
    }
}

bool CInput::RemapIpad(int& pad)
{
    bool result = false;
    if (static_cast<unsigned int>(pad) < 4)
    {
        if (padRemap[pad] >= 0)
        {
            pad = padRemap[pad];
            result = true;
        }
    }
    else if (static_cast<unsigned int>(pad - 4) < 5)
    {
        pad -= 4;
        result = true;
    }
    return result;
}

void CInput::SetGameJoypadMaps(int pad, unsigned char map, unsigned int value)
{
    RemapIpad(pad);
    if (static_cast<unsigned int>(pad) >= JOYPAD_COUNT || gameJoypadMaps[pad] == 0 || map >= controllerConfiguration[1])
    {
        return;
    }
    gameJoypadMaps[pad][map] = value;
    alternateJoypadMaps[pad][map] = value;
}

unsigned int CInput::GetGameJoypadMaps(int pad, unsigned char map)
{
    int originalPad = pad;
    if (!RemapIpad(pad) || gameJoypadMaps[pad] == 0 || map >= controllerConfiguration[1])
    {
        return 0;
    }
    return IsPadClassicController(originalPad) ? alternateJoypadMaps[pad][map] : gameJoypadMaps[pad][map];
}

void CInput::SetJoypadMapArraySetupComplete()
{
    joypadMapsReady = true;
}

void CInput::SetJoypadSensitivity(int pad, float sensitivity)
{
    if (RemapIpad(pad))
    {
        joypads[pad].sensitivity = sensitivity;
    }
}

unsigned int CInput::only_one_bit_set(unsigned int value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

unsigned int CInput::allowInput(int pad, unsigned char map, unsigned int value)
{
    int mappedPad = pad;
    if (!RemapIpad(mappedPad))
    {
        return 0;
    }
    unsigned int mapping = GetGameJoypadMaps(pad, map);
    if (value == 0 || value != mapping)
    {
        return 0;
    }
    if (only_one_bit_set(mapping))
    {
        for (unsigned char other = controllerConfiguration[2] + 1; other < controllerConfiguration[1]; ++other)
        {
            unsigned int otherMapping = GetGameJoypadMaps(pad, other);
            if (!only_one_bit_set(otherMapping) && (mapping & otherMapping)
                && (joypads[mappedPad].rawValue & mapping & otherMapping)
                && (joypads[mappedPad].rawValue & otherMapping) == otherMapping)
            {
                return 0;
            }
        }
    }
    return 1;
}

unsigned int CInput::KeyboardActionValue(int pad, unsigned char map)
{
    unsigned int mapping = GetGameJoypadMaps(pad, map);
    return mapping != 0 ? mapping : 1u;
}

unsigned int CInput::GetValue(int pad, unsigned char map, bool repeated)
{
    bool menuOpen = IsMenuShown(pad);
    int mappedPad = pad;
    if (!RemapIpad(mappedPad) || (menuOpen && map > controllerConfiguration[2]))
    {
        return 0;
    }
    if (mappedPad == kKeyboardJoypad)
    {
        bool active = repeated ? keyboardMouse.ActionRepeat(map) : keyboardMouse.ActionDown(map);
        return active ? KeyboardActionValue(pad, map) : 0;
    }
    unsigned int value = joypads[mappedPad].rawValue & GetGameJoypadMaps(pad, map);
    if (map > controllerConfiguration[2] && !allowInput(pad, map, value))
    {
        return 0;
    }
    if (!repeated || value == 0)
    {
        return value;
    }
    int button = 0;
    unsigned int buttons = value;
    while ((buttons & 1) == 0)
    {
        ++button;
        buttons >>= 1;
    }
    ButtonTime& time = buttonTimes[mappedPad][button];
    ButtonTime* repeatTime = &time;
    if (!only_one_bit_set(buttons))
    {
        int secondButton = button + 1;
        unsigned int remainingButtons = buttons >> 1;
        while ((remainingButtons & 1) == 0)
        {
            ++secondButton;
            remainingButtons >>= 1;
        }
        ButtonTime& secondTime = buttonTimes[mappedPad][secondButton];
        if (time.heldTime <= secondTime.heldTime)
        {
            if (secondTime.heldTime - time.heldTime < 0.001f)
            {
                secondTime.heldTime = secondTime.heldTime + 0.002f;
            }
        }
        else
        {
            if (time.heldTime - secondTime.heldTime < 0.001f)
            {
                time.heldTime = time.heldTime + 0.002f;
            }
            repeatTime = &secondTime;
        }
    }
    if (repeatTime->heldTime >= initialRepeatDelay)
    {
        if (repeatTime->repeatTime == 0.0f)
        {
            repeatTime->repeatTime = initialRepeatDelay;
            return value;
        }
        if (repeatTime->heldTime - repeatTime->repeatTime > repeatDelay)
        {
            repeatTime->repeatTime += repeatDelay;
            return value;
        }
    }
    else if (!repeatTime->repeated)
    {
        repeatTime->repeated = true;
        return value;
    }
    return 0;
}

void CInput::SetFakePadValue(int pad, unsigned char map)
{
    bool menuOpen = IsMenuShown(pad);
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!menuOpen || map <= controllerConfiguration[2]))
    {
        if (mappedPad == kKeyboardJoypad)
        {
            keyboardMouse.SetFakeAction(map, 10);
            return;
        }
        joypads[mappedPad].fakeValue |= GetGameJoypadMaps(pad, map);
        joypads[mappedPad].fakeTimer = 10;
    }
}

bool CInput::ButtonPressed(int pad, unsigned char map, unsigned int mask)
{
    int mappedPad = pad;
    if (!RemapIpad(mappedPad))
    {
        return false;
    }
    if (mappedPad == kKeyboardJoypad)
    {
        if (map == 0xff)
        {
            return keyboardMouse.AnyInputPressed();
        }
        if (map > controllerConfiguration[2] && IsMenuShown(pad))
        {
            return false;
        }
        return keyboardMouse.ActionPressed(map);
    }
    if (map == 0xff)
    {
        return joypads[mappedPad].pressedValue != 0;
    }
    unsigned int mapping = GetGameJoypadMaps(pad, map);
    if (map <= controllerConfiguration[2])
    {
        return (joypads[mappedPad].pressedValue & mapping) != 0;
    }
    if (IsMenuShown(pad))
    {
        return false;
    }
    unsigned int value = joypads[mappedPad].pressedValue & mapping;
    if (!only_one_bit_set(mapping) && (joypads[mappedPad].rawValue & mask) != 0)
    {
        value |= mask;
    }
    return value == mapping && allowInput(pad, map, value);
}

bool CInput::ButtonReleased(int pad, unsigned char map)
{
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!IsMenuShown(pad) || map <= controllerConfiguration[2]))
    {
        if (mappedPad == kKeyboardJoypad)
        {
            return keyboardMouse.ActionReleased(map);
        }
        return (joypads[mappedPad].releasedValue & GetGameJoypadMaps(pad, map)) != 0;
    }
    return false;
}

bool CInput::ButtonDown(int pad, unsigned char map)
{
    int mappedPad = pad;
    if (!RemapIpad(mappedPad))
    {
        return false;
    }
    if (mappedPad == kKeyboardJoypad)
    {
        if (map == 0xff)
        {
            return keyboardMouse.AnyInputDown();
        }
        if (map > controllerConfiguration[2] && IsMenuShown(pad))
        {
            return false;
        }
        return keyboardMouse.ActionDown(map);
    }
    if (map == 0xff)
    {
        return joypads[mappedPad].rawValue != 0;
    }
    if (IsMenuShown(pad) && map > controllerConfiguration[2])
    {
        return false;
    }
    unsigned int mapping = GetGameJoypadMaps(pad, map);
    unsigned int value = joypads[mappedPad].rawValue & mapping;
    if (map <= controllerConfiguration[2])
    {
        return value != 0;
    }
    return value == mapping && allowInput(pad, map, value);
}

void CInput::SetJoypadStickAxisMap(int pad, unsigned int destination, unsigned int source)
{
    if (!RemapIpad(pad))
    {
        return;
    }
    Joypad& joypad = joypads[pad];
    float* sourceValue = 0;
    if (source == 0)
        sourceValue = &joypad.leftX;
    else if (source == 1)
        sourceValue = &joypad.leftY;
    else if (source == 2)
        sourceValue = &joypad.rightX;
    else if (source == 3)
        sourceValue = &joypad.rightY;
    if (sourceValue == 0)
    {
        return;
    }
    if (destination == 0)
        joypad.leftXSource = sourceValue;
    else if (destination == 1)
        joypad.leftYSource = sourceValue;
    else if (destination == 2)
        joypad.rightXSource = sourceValue;
    else if (destination == 3)
        joypad.rightYSource = sourceValue;
}

void CInput::SetJoypadStickTriggerMap(int pad, unsigned int destination, unsigned int source)
{
    if (!RemapIpad(pad))
    {
        return;
    }
    Joypad& joypad = joypads[pad];
    unsigned char* sourceValue = 0;
    if (source == 0)
        sourceValue = &joypad.leftTrigger;
    else if (source == 1)
        sourceValue = &joypad.rightTrigger;
    if (destination == 0)
        joypad.leftTriggerSource = sourceValue;
    else if (destination == 1)
        joypad.rightTriggerSource = sourceValue;
}

void CInput::SetKeyRepeatRate(float initialDelay, float delay)
{
    initialRepeatDelay = initialDelay;
    repeatDelay = delay;
}

void CInput::SetDebugSequence(const char* sequence, InputCallback callback, void* context)
{
    debugSequence = sequence != 0 ? sequence : "";
    debugSequenceCallback = callback;
    debugSequenceContext = context;
}

double CInput::GetIdleSeconds(int pad)
{
    int mappedPad = pad;
    if (!RemapIpad(mappedPad))
    {
        if (static_cast<unsigned int>(pad) >= JOYPAD_COUNT)
        {
            return 0.0;
        }
        mappedPad = pad;
    }
    unsigned long long now = SDL_GetTicksNS();
    return static_cast<float>(now - idleStartTimes[mappedPad]) * kSecondsPerNanosecond;
}

unsigned char CInput::IsPadConnected(int pad)
{
    if (RemapIpad(pad))
    {
        return joypads[pad].connected;
    }
    return static_cast<unsigned int>(pad) < controllerCount;
}

void CInput::SetCircleCrossSwapped(bool swapped)
{
    circleCrossSwapped = swapped;
}

unsigned char CInput::IsCircleCrossSwapped()
{
    return circleCrossSwapped;
}

void CInput::MapTouchInput(int pad, unsigned int mask)
{
    if (static_cast<unsigned int>(pad) < JOYPAD_COUNT)
    {
        joypads[pad].touchValue = (joypads[pad].touchValue & ~kTouchInputButtons) | mask;
    }
}

float CInput::GetJoypadStick_Menu_LX(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].leftX : 0.0f;
}

float CInput::GetJoypadStick_Menu_LY(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].leftY : 0.0f;
}

float CInput::GetJoypadStick_Menu_RX(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].rightX : 0.0f;
}

float CInput::GetJoypadStick_Menu_RY(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].rightY : 0.0f;
}

unsigned char CInput::GetJoypadLTrigger_Menu(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].leftTrigger : 0;
}

unsigned char CInput::GetJoypadRTrigger_Menu(int pad)
{
    int mappedPad = pad;
    return RemapIpad(mappedPad) ? joypads[mappedPad].rightTrigger : 0;
}

float CInput::GetJoypadStick_LX(int pad, bool checkMenu)
{
    bool menuShown = IsMenuShown(pad);
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!checkMenu || !menuShown))
    {
        return *joypads[mappedPad].leftXSource * joypads[mappedPad].sensitivity;
    }
    return 0.0f;
}

float CInput::GetJoypadStick_LY(int pad, bool checkMenu)
{
    bool menuShown = IsMenuShown(pad);
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!checkMenu || !menuShown))
    {
        return *joypads[mappedPad].leftYSource * joypads[mappedPad].sensitivity;
    }
    return 0.0f;
}

float CInput::GetJoypadStick_RX(int pad, bool checkMenu)
{
    bool menuShown = IsMenuShown(pad);
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!checkMenu || !menuShown))
    {
        return *joypads[mappedPad].rightXSource * joypads[mappedPad].sensitivity;
    }
    return 0.0f;
}

float CInput::GetJoypadStick_RY(int pad, bool checkMenu)
{
    bool menuShown = IsMenuShown(pad);
    int mappedPad = pad;
    if (RemapIpad(mappedPad) && (!checkMenu || !menuShown))
    {
        return *joypads[mappedPad].rightYSource * joypads[mappedPad].sensitivity;
    }
    return 0.0f;
}

unsigned char CInput::GetJoypadLTrigger(int pad, bool raw)
{
    if (raw && IsMenuShown(pad))
    {
        return 0;
    }
    if (RemapIpad(pad) && joypads[pad].leftTriggerSource != 0)
    {
        return *joypads[pad].leftTriggerSource;
    }
    return 0;
}

unsigned char CInput::GetJoypadRTrigger(int pad, bool raw)
{
    if (raw && IsMenuShown(pad))
    {
        return 0;
    }
    if (RemapIpad(pad) && joypads[pad].rightTriggerSource != 0)
    {
        return *joypads[pad].rightTriggerSource;
    }
    return 0;
}

bool CInput::IsKeyboardPad(int pad)
{
    return RemapIpad(pad) && pad == kKeyboardJoypad;
}

int CInput::LastInputPad()
{
    return lastInputJoypad + 4;
}

bool CInput::UsesKeyboard(int pad)
{
    return IsKeyboardPad(pad);
}

void CInput::BeginKeyCapture()
{
    keyboardMouse.BeginCapture();
}

void CInput::CancelKeyCapture()
{
    keyboardMouse.CancelCapture();
}

int CInput::ConsumeCapturedKey()
{
    return keyboardMouse.ConsumeCapture();
}

bool CInput::HasKeyBinding(int action)
{
    return !keyboardMouse.GetBindings(PcKeyboardMouse::ActionContext(action), action).empty();
}

bool CInput::SameKeyBinding(int action, int otherAction)
{
    const std::vector<int>& bindings = keyboardMouse.GetBindings(PcKeyboardMouse::ActionContext(action), action);
    return !bindings.empty()
           && bindings == keyboardMouse.GetBindings(PcKeyboardMouse::ActionContext(otherAction), otherAction);
}

void CInput::SetKeyBinding(int action, int input)
{
    std::vector<int> inputs;
    if (input >= 0)
        inputs.push_back(input);
    keyboardMouse.SetBindings(PcKeyboardMouse::ActionContext(action), action, inputs);
    keyboardMouse.SaveBindings(keyBindingsPath);
}

unsigned int CInput::GetPadButtons(int pad, bool pressedOnly)
{
    if (!RemapIpad(pad) || pad == kKeyboardJoypad || static_cast<unsigned int>(pad) >= JOYPAD_COUNT)
        return 0;
    return pressedOnly ? joypads[pad].pressedValue : joypads[pad].rawValue;
}

void CInput::ResetKeyBindings()
{
    keyboardMouse.SetDefaultBindings();
    keyboardMouse.SaveBindings(keyBindingsPath);
}

bool CInput::MenuPointerPosition(float* x, float* y)
{
    if (mouseCaptured || keyboardState != EKeyboardState_Idle)
    {
        return false;
    }
    SDL_Window* window = LocateWindow();
    int width = 0;
    int height = 0;
    if (window == 0 || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
    {
        return false;
    }
    float areaLeft;
    float areaTop;
    float areaWidth;
    float areaHeight;
    gRenderer->GetInterfaceArea(&areaLeft, &areaTop, &areaWidth, &areaHeight);
    float pointerX = (keyboardMouse.PointerX() / static_cast<float>(width) - areaLeft) / areaWidth * kTouchSpaceWidth;
    float pointerY = (keyboardMouse.PointerY() / static_cast<float>(height) - areaTop) / areaHeight * kTouchSpaceHeight;
    *x = pointerX < 0.0f ? 0.0f : (pointerX > kTouchSpaceWidth - 1.0f ? kTouchSpaceWidth - 1.0f : pointerX);
    *y = pointerY < 0.0f ? 0.0f : (pointerY > kTouchSpaceHeight - 1.0f ? kTouchSpaceHeight - 1.0f : pointerY);
    return true;
}

bool CInput::ConsumeMenuPointer(int pad, float* x, float* y)
{
    if (!keyboardMouse.PointerMoved() || !IsKeyboardPad(pad) || !MenuPointerPosition(x, y))
    {
        return false;
    }
    keyboardMouse.ClearPointerMoved();
    return true;
}

unsigned int CInput::ConsumeMenuClicks(int pad)
{
    if (!IsKeyboardPad(pad))
    {
        return 0;
    }
    return keyboardMouse.ConsumeClicks();
}

bool CInput::MenuQuickMoveHeld(int pad)
{
    return IsKeyboardPad(pad) && keyboardMouse.QuickMoveHeld();
}

bool CInput::MenuPointerHeld(int pad)
{
    return IsKeyboardPad(pad) && keyboardMouse.PrimaryHeld();
}

bool CInput::GetMenuPointer(int pad, float* x, float* y)
{
    return IsKeyboardPad(pad) && MenuPointerPosition(x, y);
}

void CInput::SetMenuDrawsPointer(bool drawsPointer)
{
    menuDrawsPointer = drawsPointer;
}

int CInput::PromptJoypad(int pad)
{
    int mappedPad = pad;
    if (RemapIpad(mappedPad))
    {
        return mappedPad;
    }
    return lastInputJoypad;
}

bool CInput::UseKeyboardPrompts(int pad)
{
    return PromptJoypad(pad) == kKeyboardJoypad;
}

int CInput::PadFamily(int joypad)
{
    SDL_Gamepad* gamepad = joypad > 0 && joypad < JOYPAD_COUNT ? joypads[joypad].gamepad : 0;
    switch (gamepad != 0 ? SDL_GetGamepadType(gamepad) : SDL_GAMEPAD_TYPE_UNKNOWN)
    {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return EPcPadFamily_Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
        return EPcPadFamily_PlayStation3;
    case SDL_GAMEPAD_TYPE_PS4:
        return EPcPadFamily_PlayStation4;
    case SDL_GAMEPAD_TYPE_PS5:
        return EPcPadFamily_PlayStation5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return EPcPadFamily_Nintendo;
    default:
        return EPcPadFamily_Generic;
    }
}

int CInput::PadElement(int joypad, unsigned int button)
{
    static const struct
    {
        unsigned int button;
        int element;
    } kElements[] = {{EPcButton_LeftBumper, EPcPadElement_LeftShoulder},
                     {EPcButton_RightBumper, EPcPadElement_RightShoulder},
                     {EPcButton_LeftTrigger, EPcPadElement_LeftTrigger},
                     {EPcButton_RightTrigger, EPcPadElement_RightTrigger},
                     {EPcButton_LeftStickPress, EPcPadElement_LeftStickPress},
                     {EPcButton_RightStickPress, EPcPadElement_RightStickPress},
                     {EPcButton_LeftStickUp | EPcButton_LeftStickDown | EPcButton_LeftStickLeft
                          | EPcButton_LeftStickRight,
                      EPcPadElement_LeftStick},
                     {EPcButton_RightStickUp | EPcButton_RightStickDown | EPcButton_RightStickLeft
                          | EPcButton_RightStickRight,
                      EPcPadElement_RightStick},
                     {EPcButton_Start, EPcPadElement_Start},
                     {EPcButton_Back, EPcPadElement_Back},
                     {EPcButton_DPadUp, EPcPadElement_DPadUp},
                     {EPcButton_DPadDown, EPcPadElement_DPadDown},
                     {EPcButton_DPadLeft, EPcPadElement_DPadLeft},
                     {EPcButton_DPadRight, EPcPadElement_DPadRight}};
    for (size_t index = 0; index < sizeof(kElements) / sizeof(kElements[0]); ++index)
    {
        if ((button & kElements[index].button) != 0)
        {
            return kElements[index].element;
        }
    }

    int family = PadFamily(joypad);
    SDL_Gamepad* gamepad = joypad > 0 && joypad < JOYPAD_COUNT ? joypads[joypad].gamepad : 0;
    SDL_GamepadButtonLabel label = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
    if (gamepad != 0)
    {
        int faceCount = 0;
        const int* faceButtons = PcInputGamepadFaceButtons(faceCount);
        for (int face = 0; face < faceCount && face < 4; ++face)
        {
            if ((joypads[joypad].padFaceButtons[face] & button) != 0)
            {
                label = SDL_GetGamepadButtonLabel(gamepad, static_cast<SDL_GamepadButton>(faceButtons[face]));
                break;
            }
        }
    }
    if (label == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN)
    {
        bool playStation = family == EPcPadFamily_PlayStation3 || family == EPcPadFamily_PlayStation4
                           || family == EPcPadFamily_PlayStation5;
        int labelCount = 0;
        const PcGamepadLabelBinding* labels = PcInputGamepadLabelBindings(labelCount);
        for (int index = 0; index < labelCount; ++index)
        {
            bool symbol = labels[index].label >= SDL_GAMEPAD_BUTTON_LABEL_CROSS;
            if ((labels[index].buttons & button) != 0 && symbol == playStation)
            {
                label = static_cast<SDL_GamepadButtonLabel>(labels[index].label);
                break;
            }
        }
    }
    switch (label)
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
        return EPcPadElement_FaceA;
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return EPcPadElement_FaceB;
    case SDL_GAMEPAD_BUTTON_LABEL_X:
        return EPcPadElement_FaceX;
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
        return EPcPadElement_FaceY;
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return EPcPadElement_FaceCross;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
        return EPcPadElement_FaceCircle;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        return EPcPadElement_FaceSquare;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        return EPcPadElement_FaceTriangle;
    default:
        return -1;
    }
}

int CInput::GetPromptStyle(int pad)
{
    int joypad = PromptJoypad(pad);
    if (joypad != kKeyboardJoypad)
    {
        return EPromptStyle_Gamepad + PadFamily(joypad);
    }
    return menuDrawsPointer ? EPromptStyle_Pointer : EPromptStyle_Keyboard;
}

int CInput::GetPromptImages(int pad, const int* actions, int actionCount, unsigned int buttons, int* images,
                            int maxImages)
{
    int count = 0;
    int joypad = PromptJoypad(pad);
    if (joypad != kKeyboardJoypad)
    {
        EPcPadFamily family = static_cast<EPcPadFamily>(PadFamily(joypad));
        unsigned int handled = 0;
        for (int bit = 0; bit < 32 && count < maxImages; ++bit)
        {
            unsigned int button = 1u << bit;
            if ((buttons & button) == 0 || (handled & button) != 0)
            {
                continue;
            }
            int element = PadElement(joypad, button);
            if (element < 0)
            {
                continue;
            }
            if (element == EPcPadElement_LeftStick)
            {
                handled |= EPcButton_LeftStickUp | EPcButton_LeftStickDown | EPcButton_LeftStickLeft
                           | EPcButton_LeftStickRight;
            }
            else if (element == EPcPadElement_RightStick)
            {
                handled |= EPcButton_RightStickUp | EPcButton_RightStickDown | EPcButton_RightStickLeft
                           | EPcButton_RightStickRight;
            }
            images[count++] = PcPadPromptArt(family, static_cast<EPcPadElement>(element));
        }
        return count;
    }

    bool pointer = menuDrawsPointer;
    bool mouseShown = false;
    for (int index = 0; index < actionCount && count < maxImages; ++index)
    {
        int action = actions[index];
        int input = -1;
        int modifier = -1;
        if (pointer && PcKeyboardMouse::PointerPromptInput(action, &input, &modifier))
        {
            if (modifier >= 0 && count < maxImages)
            {
                images[count++] = modifier;
            }
        }
        else if (PcKeyboardMouse::IsLookAction(action))
        {
            if (mouseShown)
            {
                continue;
            }
            mouseShown = true;
            input = PC_PROMPT_ART_MOUSE;
        }
        else if (action >= 0 && action < PcKeyboardMouse::ACTION_COUNT)
        {
            const std::vector<int>& inputs =
                keyboardMouse.GetBindings(PcKeyboardMouse::ActionContext(action), action);
            if (!inputs.empty())
            {
                input = inputs[0];
            }
        }
        if (input >= 0 && count < maxImages)
        {
            images[count++] = input;
        }
    }
    return count;
}

int CInput::ConsumeMenuWheel(int pad)
{
    if (!IsKeyboardPad(pad) || !IsKeyboardInMenu())
    {
        return 0;
    }
    return keyboardMouse.ConsumeMenuWheel();
}

bool CInput::ConsumeHotbarInput(int pad, int* slot, int* scroll)
{
    if (!IsKeyboardPad(pad) || IsKeyboardInMenu())
    {
        return false;
    }
    return keyboardMouse.ConsumeHotbar(slot, scroll);
}

unsigned short* CInput::GetTouchPadData(int pad, ETouchType touchType, bool previous)
{
    (void)touchType;
    (void)previous;
    float x;
    float y;
    if (!IsKeyboardPad(pad) || !MenuPointerPosition(&x, &y))
    {
        return 0;
    }
    touchData[0] = static_cast<unsigned short>(x);
    touchData[1] = static_cast<unsigned short>(y);
    touchData[2] = (SDL_GetMouseState(0, 0) & SDL_BUTTON_LMASK) != 0 ? 1 : 0;
    touchData[3] = 0;
    return touchData;
}

void CInput::SetMenuDisplayed(int pad, bool displayed)
{
    if (static_cast<unsigned int>(pad) < 4)
    {
        menuDisplayed[pad] = displayed;
    }
}

unsigned int CInput::RequestKeyboard(const wchar_t* title, const wchar_t* initialText, unsigned int pad,
                                     unsigned int maximumLength, KeyboardCallback callback, void* context,
                                     EKeyboardMode mode)
{
    if (keyboardState != EKeyboardState_Idle)
    {
        return 1;
    }
    keyboardTitle = title != 0 ? title : L"";
    keyboardState = EKeyboardState_Requested;
    keyboardCallback = callback;
    keyboardMaximumLength = maximumLength > static_cast<unsigned int>(kKeyboardTextLimit)
                                ? kKeyboardTextLimit
                                : static_cast<int>(maximumLength);
    keyboardContext = context;
    keyboardPad = static_cast<int>(pad);
    keyboardMode = mode;
    RemapIpad(keyboardPad);
    std::memset(keyboardText, 0, sizeof(keyboardText));
    if (initialText != 0)
    {
        std::wcsncpy(keyboardText, initialText, static_cast<size_t>(keyboardMaximumLength));
        keyboardText[keyboardMaximumLength] = 0;
    }
    keyboardAccepted = false;
    keyboardCancelled = false;
    destroyKeyboardRequested = false;
    return 0;
}

void CInput::DestroyKeyboard()
{
    if (keyboardState == EKeyboardState_Active)
    {
        destroyKeyboardRequested = true;
    }
}

void CInput::GetText(wchar_t* text, int length)
{
    if (text == 0 || length <= 0)
    {
        return;
    }
    std::wcsncpy(text, keyboardText, static_cast<size_t>(length));
}

bool CInput::AppendKeyboardCodepoint(unsigned int codepoint)
{
    if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0) || codepoint == SDL_INVALID_UNICODE_CODEPOINT
        || (codepoint >= 0xd800 && codepoint < 0xe000) || codepoint > 0x10ffff)
    {
        return false;
    }
    switch (static_cast<int>(keyboardMode))
    {
    case 1:
    case 6:
    case 7:
        if (codepoint < '0' || codepoint > '9')
        {
            return false;
        }
        break;
    case 3:
    case 5:
        if (codepoint > 0xff)
        {
            return false;
        }
        break;
    default:
        break;
    }
    bool surrogatePair = sizeof(wchar_t) == 2 && codepoint > 0xffff;
    size_t units = surrogatePair ? 2 : 1;
    if (keyboardEditText.size() + units > static_cast<size_t>(keyboardMaximumLength))
    {
        return false;
    }
    if (surrogatePair)
    {
        unsigned int offset = codepoint - 0x10000;
        keyboardEditText.push_back(static_cast<wchar_t>(0xd800 + (offset >> 10)));
        keyboardEditText.push_back(static_cast<wchar_t>(0xdc00 + (offset & 0x3ff)));
    }
    else
    {
        keyboardEditText.push_back(static_cast<wchar_t>(codepoint));
    }
    return true;
}

void CInput::HandleKeyboardTextInput(const char* text)
{
    if (text == 0)
    {
        return;
    }
    size_t length = std::strlen(text);
    const char* cursor = text;
    while (length > 0)
    {
        unsigned int codepoint = SDL_StepUTF8(&cursor, &length);
        if (codepoint == 0)
        {
            break;
        }
        AppendKeyboardCodepoint(codepoint);
    }
}

void CInput::HandleKeyboardKey(int scancode, unsigned short modifiers, bool repeat)
{
    switch (scancode)
    {
    case SDL_SCANCODE_BACKSPACE:
        if (!keyboardEditText.empty())
        {
            unsigned int last = static_cast<unsigned int>(keyboardEditText.back());
            keyboardEditText.pop_back();
            if (sizeof(wchar_t) == 2 && last >= 0xdc00 && last < 0xe000 && !keyboardEditText.empty())
            {
                unsigned int high = static_cast<unsigned int>(keyboardEditText.back());
                if (high >= 0xd800 && high < 0xdc00)
                {
                    keyboardEditText.pop_back();
                }
            }
        }
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (!repeat)
        {
            keyboardAccepted = true;
        }
        break;
    case SDL_SCANCODE_ESCAPE:
        if (!repeat)
        {
            keyboardCancelled = true;
        }
        break;
    case SDL_SCANCODE_V:
        if ((modifiers & SDL_KMOD_CTRL) != 0 && !repeat)
        {
            char* clipboard = SDL_GetClipboardText();
            if (clipboard != 0)
            {
                HandleKeyboardTextInput(clipboard);
                SDL_free(clipboard);
            }
        }
        break;
    default:
        break;
    }
}

void CInput::FinishKeyboard(bool accepted)
{
    if (accepted)
    {
        size_t length = keyboardEditText.size();
        if (length > static_cast<size_t>(keyboardMaximumLength))
        {
            length = static_cast<size_t>(keyboardMaximumLength);
        }
        std::memset(keyboardText, 0, sizeof(keyboardText));
        for (size_t index = 0; index < length; ++index)
        {
            keyboardText[index] = keyboardEditText[index];
        }
        keyboardText[keyboardMaximumLength] = 0;
    }
    if (keyboardCallback != 0)
    {
        keyboardCallback(keyboardContext, accepted);
    }
    if (keyboardWindowId != 0)
    {
        SDL_Window* window = SDL_GetWindowFromID(keyboardWindowId);
        if (window != 0)
        {
            SDL_StopTextInput(window);
        }
        keyboardWindowId = 0;
    }
    keyboardEditText.clear();
    keyboardAccepted = false;
    keyboardCancelled = false;
    destroyKeyboardRequested = false;
    keyboardState = EKeyboardState_Idle;
}

void CInput::TickKeyboard()
{
    if (keyboardState == EKeyboardState_Requested)
    {
#if LCE_PLATFORM_SWITCH
        SwkbdConfig config;
        if (R_SUCCEEDED(swkbdCreate(&config, 0)))
        {
            swkbdConfigMakePresetDefault(&config);
            const std::string title = WideToUtf8(keyboardTitle);
            const std::string initial = WideToUtf8(keyboardText);
            swkbdConfigSetHeaderText(&config, title.c_str());
            swkbdConfigSetInitialText(&config, initial.c_str());
            swkbdConfigSetStringLenMax(&config, keyboardMaximumLength);
            char text[kKeyboardTextLimit * 4 + 1] = {};
            const Result result = swkbdShow(&config, text, sizeof(text));
            swkbdClose(&config);
            keyboardEditText.clear();
            if (R_SUCCEEDED(result))
                HandleKeyboardTextInput(text);
            FinishKeyboard(R_SUCCEEDED(result));
            return;
        }
#endif
        keyboardEditText.assign(keyboardText, keyboardText + std::wcslen(keyboardText));
        keyboardAccepted = false;
        keyboardCancelled = false;
        keyboardWindowId = 0;
        SDL_Window* window = LocateWindow();
        if (window != 0 && SDL_StartTextInput(window))
        {
            keyboardWindowId = SDL_GetWindowID(window);
        }
        keyboardState = EKeyboardState_Active;
        return;
    }
    if (keyboardState != EKeyboardState_Active)
    {
        return;
    }
    if (static_cast<unsigned int>(keyboardPad) < JOYPAD_COUNT)
    {
        joypads[keyboardPad].consumed = true;
    }
    if (keyboardAccepted)
    {
        FinishKeyboard(true);
    }
    else if (keyboardCancelled || destroyKeyboardRequested)
    {
        FinishKeyboard(false);
    }
}

void CInput::RequestErrorViewer(unsigned int value, unsigned int type, ErrorViewerCallback callback, void* context)
{
    if (errorViewerState == EErrorViewerState_None)
    {
        errorViewerState = EErrorViewerState_Requested;
        errorViewerController = value;
        errorViewerContext = context;
        errorViewerCode = type;
        errorViewerTitle.clear();
        errorViewerMessage.clear();
        errorViewerHasText = false;
        errorViewerCallback = callback;
    }
}

void CInput::RequestErrorViewer(unsigned int value, wchar_t* title, wchar_t* message, ErrorViewerCallback callback,
                                void* context)
{
    if (errorViewerState == EErrorViewerState_None)
    {
        errorViewerState = EErrorViewerState_Requested;
        errorViewerContext = context;
        errorViewerController = value;
        errorViewerTitle = title != 0 ? title : L"";
        errorViewerMessage = message != 0 ? message : L"";
        errorViewerHasText = title != 0;
        errorViewerCallback = callback;
    }
}

void CInput::DestroyErrorViewer()
{
    if (errorViewerState == EErrorViewerState_Active)
    {
        errorViewerState = EErrorViewerState_Destroyed;
    }
}

void CInput::TickErrorViewer()
{
    if (errorViewerState != EErrorViewerState_Requested)
    {
        return;
    }
    errorViewerState = EErrorViewerState_Active;
    if (mouseCaptured)
    {
        SDL_Window* captured = LocateWindow();
        if (captured != 0)
        {
            SDL_SetWindowRelativeMouseMode(captured, false);
        }
        mouseCaptured = false;
    }
    mouseLookEnabled = false;
    ReleaseAllKeys();

    std::string title;
    std::string message;
    if (errorViewerHasText)
    {
        title = WideToUtf8(errorViewerTitle);
        message = WideToUtf8(errorViewerMessage);
    }
    else
    {
        char code[64];
        std::snprintf(code, sizeof(code), "Error Code: %03u-%04u", errorViewerCode / 10000, errorViewerCode % 10000);
        title = "Error";
        message = code;
    }
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), LocateWindow()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "CInput: %s: %s (%s)", title.c_str(), message.c_str(), SDL_GetError());
    }
    ConsumeAllPads();

    bool destroyed = errorViewerState == EErrorViewerState_Destroyed;
    errorViewerState = EErrorViewerState_None;
    if (!destroyed && errorViewerCallback != 0)
    {
        errorViewerCallback(errorViewerContext);
    }
}

void CInput::SetLocalMultiplayer(bool enabled)
{
    localMultiplayer = enabled;
}

unsigned int CInput::IsLocalMultiplayerAvailable()
{
    return PcGetLaunchOptions().splitScreen ? 1 : 0;
}

bool CInput::ShowsJoinPromptOnLoad()
{
    return PcGetLaunchOptions().splitScreen && PcGetLaunchOptions().joinPrompt;
}

unsigned int CInput::UsingRemoteVita()
{
    return 0;
}

unsigned int CInput::VerifyStrings(wchar_t** strings, int count, int (*callback)(void*, _STRING_VERIFY_RESPONSE*),
                                   void* context)
{
    (void)strings;
    if (callback != 0)
    {
        VerifyRequest request;
        request.callback = callback;
        request.context = context;
        request.count = count < 0 ? 0 : count;
        verifyRequests.push_back(request);
    }
    return 1;
}

void CInput::TickVerifyStrings()
{
    if (verifyRequests.empty())
    {
        return;
    }
    std::vector<VerifyRequest> requests;
    requests.swap(verifyRequests);
    for (size_t index = 0; index < requests.size(); ++index)
    {
        std::vector<int> results(static_cast<size_t>(requests[index].count), 0);
        _STRING_VERIFY_RESPONSE response;
        response.count = static_cast<unsigned short>(requests[index].count);
        response.reserved = 0;
        response.results = results.empty() ? 0 : &results[0];
        requests[index].callback(requests[index].context, &response);
    }
}

void CInput::CancelQueuedVerifyStrings(int (*callback)(void*, _STRING_VERIFY_RESPONSE*), void* context)
{
    for (std::vector<VerifyRequest>::iterator request = verifyRequests.begin(); request != verifyRequests.end();)
    {
        if (request->callback == callback && request->context == context)
        {
            request = verifyRequests.erase(request);
        }
        else
        {
            ++request;
        }
    }
}

void CInput::CancelAllVerifyInProgress()
{
    verifyRequests.clear();
}

unsigned int CInput::LockPad(int pad, int controllerId)
{
    if (!RemapIpad(pad) || static_cast<unsigned int>(controllerId) >= 4)
    {
        return 0;
    }
    int previousPad = padRemap[controllerId];
    if (previousPad != pad)
    {
        if (previousPad >= 0)
        {
            joypads[previousPad].lockedController = -1;
        }
        int previousController = joypads[pad].lockedController;
        if (previousController >= 0)
        {
            padRemap[previousController] = -1;
        }
        padRemap[controllerId] = pad;
        joypads[pad].lockedController = controllerId;
        if (previousPad >= 0)
        {
            for (int controller = 0; controller < 4; ++controller)
            {
                if (padRemap[controller] == -1)
                {
                    padRemap[controller] = previousPad;
                    joypads[previousPad].lockedController = controller;
                    break;
                }
            }
        }
    }
    return 1;
}

int CInput::UnlockPad(int pad)
{
    if (static_cast<unsigned int>(pad) < 4)
    {
        int mappedPad = padRemap[pad];
        if (mappedPad >= 0)
        {
            joypads[mappedPad].lockedController = -1;
            padRemap[pad] = -1;
            unsigned int previousButtons = joypads[mappedPad].rawValue;
            ClearJoypadValues(&joypads[mappedPad]);
            joypads[mappedPad].rawValue = previousButtons;
            return mappedPad + 4;
        }
    }
    return -1;
}

unsigned int CInput::IsPadLocked(int pad)
{
    if (RemapIpad(pad))
    {
        return joypads[pad].lockedController >= 0;
    }
    return 0;
}

int CInput::GetControllerID(int pad)
{
    return RemapIpad(pad) ? pad + 4 : -1;
}

unsigned char CInput::IsPadDRCController(int pad)
{
    (void)pad;
    return 0;
}

unsigned int CInput::IsPadClassicController(int pad)
{
    (void)pad;
    return 0;
}

unsigned char CInput::IsDRCConnected()
{
    return 0;
}

void CInput::AcquiredForeground()
{
    inputTick = 20;
}
