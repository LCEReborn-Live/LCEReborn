#ifndef PLATFORM_WIIU_PLATFORMTIMING_HPP
#define PLATFORM_WIIU_PLATFORMTIMING_HPP

union _LARGE_INTEGER
{
    struct
    {
        unsigned int LowPart;
        int HighPart;
    } u;
    long long QuadPart;
};

unsigned int QueryPerformanceCounter(_LARGE_INTEGER* value);
unsigned long long TicksToUnits(long long ticks, unsigned long long unitsPerSecond);
unsigned int QueryPerformanceFrequency(_LARGE_INTEGER* value);

#endif
