#include "platform/wiiu/PlatformTiming.hpp"

#include <coreinit/systeminfo.h>
#include <coreinit/time.h>

unsigned long long TicksToUnits(long long ticks, unsigned long long unitsPerSecond)
{
    const unsigned long long rate = OSTimerClockSpeed;
    const unsigned long long value = static_cast<unsigned long long>(ticks);
    return value / rate * unitsPerSecond + value % rate * unitsPerSecond / rate;
}

unsigned int QueryPerformanceCounter(_LARGE_INTEGER* value)
{
    value->QuadPart = TicksToUnits(OSGetTime(), 1000000ULL);
    return 1;
}

unsigned int QueryPerformanceFrequency(_LARGE_INTEGER* value)
{
    value->QuadPart = 1000000;
    return 0;
}
