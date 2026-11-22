#include "platform/switch/SwitchRuntime.hpp"
#include "platform/pc/Log.hpp"

#include <cstdio>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

bool SwitchEnterGameDirectory()
{
    // Mount before changing directories so libnx can resolve a relative NRO path.
    if (R_FAILED(romfsInit()))
        romfsMountFromCurrentProcess("romfs");

    // A single-file install may be launched directly from /switch.
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/LCEReborn", 0777);
    return chdir("sdmc:/switch/LCEReborn") == 0;
}

void SwitchLogStartupStage(const char* stage)
{
    static const Logger log("Switch startup");
    LOG_INFO(log, "%s", stage);
    Log::Flush();
}

void SwitchShowStartupError(const std::string& message)
{
    consoleInit(nullptr);
    std::printf("LCEReborn\n\n%s\n\nPress A to exit.\n", message.c_str());
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_A)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

bool SwitchMemoryUsage(unsigned long long* used, unsigned long long* total)
{
    u64 processUsed;
    u64 processTotal;
    if (R_FAILED(svcGetInfo(&processUsed, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))
        || R_FAILED(svcGetInfo(&processTotal, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
        return false;
    *used = processUsed;
    *total = processTotal;
    return true;
}
