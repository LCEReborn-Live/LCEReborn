#ifndef PLATFORM_PC_PCWINDOW_HPP
#define PLATFORM_PC_PCWINDOW_HPP

struct SDL_Window;

typedef void (*PcDropHandler)(const char* path, void* context);

bool PcWindowCreate(int width, int height);
void PcWindowDestroy();
void PcWindowShow();
void PcWindowSwap();
void PcWindowGetDrawableSize(int* width, int* height);
void* PcWindowGetProcAddress(const char* name);
void PcPumpEvents();
bool PcWindowIsFullscreen();
void PcWindowSetFullscreen(bool fullscreen);
bool PcWindowIsVSync();
void PcWindowSetVSync(bool enabled);
SDL_Window* PcWindowHandle();
void PcWindowSetDropHandler(PcDropHandler handler, void* context);

#endif
