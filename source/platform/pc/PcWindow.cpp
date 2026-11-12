#include "platform/pc/PcWindow.hpp"

#include <SDL3/SDL.h>
#include <stb_image.h>

#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>

#include "BuildInfo.hpp"
#include "platform/CInput.hpp"
#include "platform/pc/Log.hpp"
#include "platform/pc/PcApplication.hpp"
#include "platform/pc/PcInputEvents.hpp"
#include "platform/pc/PcPaths.hpp"

namespace
{
const Logger sLog("Window");
SDL_Window* sWindow = NULL;
SDL_GLContext sContext = NULL;
bool sShown = false;
bool sFullscreen = false;
bool sVSync = true;
PcDropHandler sDropHandler = NULL;
void* sDropContext = NULL;

std::string WindowTitle()
{
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::string title = std::string("LCEReborn - ") + LCE_BUILD_COMMIT + (LCE_BUILD_DIRTY ? " (modified)" : "");
    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(LCE_BUILD_DATE, "%d-%d-%d", &year, &month, &day) == 3 && month >= 1 && month <= 12)
    {
        char date[32];
        snprintf(date, sizeof(date), ", %d %s %d", day, kMonths[month - 1], year);
        title += date;
    }
    return title;
}

std::string DisplaySettingsPath()
{
    return PcSaveRoot() + "/display.txt";
}

void LoadDisplaySettings()
{
    std::ifstream file(DisplaySettingsPath().c_str());
    std::string line;
    while (std::getline(file, line))
    {
        std::string::size_type equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::istringstream name(line.substr(0, equals));
        std::istringstream value(line.substr(equals + 1));
        std::string key;
        int setting = 0;
        if (!(name >> key) || !(value >> setting))
            continue;
        if (key == "Fullscreen")
            sFullscreen = setting != 0;
        else if (key == "VSync")
            sVSync = setting != 0;
    }
}

void SaveDisplaySettings()
{
    std::ofstream file(DisplaySettingsPath().c_str(), std::ios::trunc);
    if (!file)
    {
        LOG_WARN(sLog, "Display settings could not be written to %s", DisplaySettingsPath().c_str());
        return;
    }
    file << "Fullscreen = " << (sFullscreen ? 1 : 0) << "\n";
    file << "VSync = " << (sVSync ? 1 : 0) << "\n";
}

void ApplyVSync()
{
    if (sContext != NULL && !SDL_GL_SetSwapInterval(sVSync ? 1 : 0))
        LOG_WARN(sLog, "SDL_GL_SetSwapInterval failed: %s", SDL_GetError());
}

bool IsFullscreenToggle(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return false;
    if (event.key.scancode == SDL_SCANCODE_F11)
        return true;
    return (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER)
           && (event.key.mod & SDL_KMOD_ALT) != 0;
}

void SetWindowIcon()
{
    const std::string path = PcResourceRoot() + "/icon.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (pixels == NULL)
    {
        LOG_WARN(sLog, "Window icon %s could not be loaded", path.c_str());
        return;
    }
    SDL_Surface* icon = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
    if (icon == NULL || !SDL_SetWindowIcon(sWindow, icon))
        LOG_WARN(sLog, "The window icon could not be set: %s", SDL_GetError());
    SDL_DestroySurface(icon);
    stbi_image_free(pixels);
}
}

bool PcWindowCreate(int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        LOG_FATAL(sLog, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    sWindow = SDL_CreateWindow(WindowTitle().c_str(), width, height,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
                                   | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (sWindow == NULL)
    {
        LOG_FATAL(sLog, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SetWindowIcon();

    static const int kVersions[][2] = {{4, 6}, {4, 5}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, {3, 3}};
    for (const int* version : kVersions)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, version[0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, version[1]);
        sContext = SDL_GL_CreateContext(sWindow);
        if (sContext != NULL)
            break;
    }
    if (sContext == NULL)
    {
        LOG_FATAL(sLog, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(sWindow);
        sWindow = NULL;
        return false;
    }

    SDL_GL_MakeCurrent(sWindow, sContext);
    LoadDisplaySettings();
    ApplyVSync();
    if (sFullscreen && !SDL_SetWindowFullscreen(sWindow, true))
    {
        LOG_WARN(sLog, "SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        sFullscreen = false;
    }
    sShown = false;
    return true;
}

void PcWindowDestroy()
{
    if (sContext != NULL)
    {
        SDL_GL_DestroyContext(sContext);
        sContext = NULL;
    }

    if (sWindow != NULL)
    {
        SDL_DestroyWindow(sWindow);
        sWindow = NULL;
    }
}

void PcWindowShow()
{
    if (sWindow != NULL && !sShown)
    {
        SDL_ShowWindow(sWindow);
        sShown = true;
    }
}

void PcWindowSwap()
{
    if (sWindow != NULL)
    {
        SDL_GL_SwapWindow(sWindow);
    }
}

void PcWindowGetDrawableSize(int* width, int* height)
{
    int drawableWidth = 0;
    int drawableHeight = 0;

    if (sWindow != NULL)
    {
        SDL_GetWindowSizeInPixels(sWindow, &drawableWidth, &drawableHeight);
    }

    *width = drawableWidth;
    *height = drawableHeight;
}

void* PcWindowGetProcAddress(const char* name)
{
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

void PcPumpEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            PcRequestExit();
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            sFullscreen = true;
            break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            sFullscreen = false;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (g_pInput != NULL)
            {
                g_pInput->AcquiredForeground();
            }
            break;
        case SDL_EVENT_DROP_FILE:
            if (sDropHandler != NULL && event.drop.data != NULL)
                sDropHandler(event.drop.data, sDropContext);
            break;
        case SDL_EVENT_DROP_COMPLETE:
            if (sDropHandler != NULL)
                sDropHandler(NULL, sDropContext);
            break;
        default:
            break;
        }

        if (IsFullscreenToggle(event))
        {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
                PcWindowSetFullscreen(!sFullscreen);
            continue;
        }

        PcInputHandleEvent(event);
    }
}

bool PcWindowIsFullscreen()
{
    return sFullscreen;
}

void PcWindowSetFullscreen(bool fullscreen)
{
    if (sWindow == NULL)
        return;
    if (!SDL_SetWindowFullscreen(sWindow, fullscreen))
    {
        LOG_WARN(sLog, "SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        return;
    }
    sFullscreen = fullscreen;
    SaveDisplaySettings();
}

bool PcWindowIsVSync()
{
    return sVSync;
}

void PcWindowSetVSync(bool enabled)
{
    sVSync = enabled;
    ApplyVSync();
    SaveDisplaySettings();
}

SDL_Window* PcWindowHandle()
{
    return sWindow;
}

void PcWindowSetDropHandler(PcDropHandler handler, void* context)
{
    sDropHandler = handler;
    sDropContext = context;
}
