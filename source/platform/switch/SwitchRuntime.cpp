#include "platform/switch/SwitchRuntime.hpp"

#include <cstdio>
#include <switch.h>
#include <unistd.h>

bool SwitchEnterGameDirectory()
{
    return chdir("sdmc:/switch/LCEReborn") == 0;
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
