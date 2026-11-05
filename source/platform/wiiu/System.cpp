#include "game/core/System.hpp"
#include "platform/Platform.hpp"

#if LCE_PLATFORM_WIIU
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include "platform/wiiu/PlatformTiming.hpp"
#elif LCE_PLATFORM_PC
#include <chrono>
#include <ctime>

static const long long kUnixSecondsAt2000 = 946684800LL;

static long long LocalOffsetSeconds()
{
    const std::time_t seconds = std::time(NULL);
    std::tm local;
    std::tm utc;
#if defined(_WIN32)
    localtime_s(&local, &seconds);
    gmtime_s(&utc, &seconds);
#else
    localtime_r(&seconds, &local);
    gmtime_r(&seconds, &utc);
#endif
    utc.tm_isdst = local.tm_isdst;
    return static_cast<long long>(std::mktime(&local) - std::mktime(&utc));
}

static unsigned long long LocalNanosecondsSince2000()
{
    static const long long offsetSeconds = LocalOffsetSeconds();
    const long long unixNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    return static_cast<unsigned long long>(unixNanoseconds + (offsetSeconds - kUnixSecondsAt2000) * 1000000000LL);
}
#endif

void System::arraycopy(arrayWithLength<unsigned char> src, unsigned int srcPos, arrayWithLength<unsigned char>* dest,
                       unsigned int destPos, unsigned int length)
{
    std::memcpy(dest->data + destPos, src.data + srcPos, length);
}

void System::arraycopy(arrayWithLength<Node*> src, unsigned int srcPos, arrayWithLength<Node*>* dest,
                       unsigned int destPos, unsigned int length)
{
    System::arraycopy<Node*>(src, srcPos, dest, destPos, length);
}

void System::arraycopy(arrayWithLength<Biome*> src, unsigned int srcPos, arrayWithLength<Biome*>* dest,
                       unsigned int destPos, unsigned int length)
{
    System::arraycopy<Biome*>(src, srcPos, dest, destPos, length);
}

void System::arraycopy(arrayWithLength<int> src, unsigned int srcPos, arrayWithLength<int>* dest, unsigned int destPos,
                       unsigned int length)
{
    std::memcpy(dest->data + destPos, src.data + srcPos, length * sizeof(int));
}

void System::arraycopy(arrayWithLength<unsigned short> src, unsigned int srcPos, arrayWithLength<unsigned short>* dest,
                       unsigned int destPos, unsigned int length)
{
    std::memcpy(dest->data + destPos, src.data + srcPos, length * sizeof(unsigned short));
}

uint64_t System::processTime(ETimeGranularity granularity)
{
#if LCE_PLATFORM_WIIU
    switch (granularity)
    {
    case eTimeGranularity_MilliSecs:
        return TicksToUnits(OSGetTime(), 1000ULL);
    case eTimeGranularity_MicroSecs:
        return TicksToUnits(OSGetTime(), 1000000ULL);
    case eTimeGranularity_NanoSecs:
        return TicksToUnits(OSGetTime(), 1000000000ULL);
    }
    return 0;
#elif LCE_PLATFORM_PC
    switch (granularity)
    {
    case eTimeGranularity_MilliSecs:
        return LocalNanosecondsSince2000() / 1000000ULL;
    case eTimeGranularity_MicroSecs:
        return LocalNanosecondsSince2000() / 1000ULL;
    case eTimeGranularity_NanoSecs:
        return LocalNanosecondsSince2000();
    }
    return 0;
#endif
}

uint64_t System::processTimeInSeconds()
{
    return (long long)processTime(eTimeGranularity_MilliSecs) / 1000;
}

long long System::processTimeInMilliSecs()
{
    return processTime(eTimeGranularity_MilliSecs);
}

uint64_t System::processTimeInNanoSecs()
{
    return processTime(eTimeGranularity_NanoSecs);
}

uint64_t System::currentRealTimeMillis()
{
#if LCE_PLATFORM_WIIU
    return TicksToUnits(OSGetTime(), 1000ULL);
#elif LCE_PLATFORM_PC
    return LocalNanosecondsSince2000() / 1000000ULL;
#endif
}

uint64_t System::currentUnixEpochTimeSeconds()
{
    return (long long)currentRealTimeMillis() / 1000;
}

void System::ReverseUSHORT(unsigned short* value)
{
    unsigned short original = *value;
    unsigned char* source = (unsigned char*)&original;
    unsigned char* destination = (unsigned char*)value;
    destination[0] = source[1];
    destination[1] = source[0];
}

void System::ReverseSHORT(short* value)
{
    unsigned short original = (unsigned short)*value;
    unsigned char* source = (unsigned char*)&original;
    unsigned char* destination = (unsigned char*)value;
    destination[0] = source[1];
    destination[1] = source[0];
}

void System::ReverseULONG(unsigned int* value)
{
    unsigned int original = *value;
    unsigned char* source = (unsigned char*)&original;
    unsigned char* destination = (unsigned char*)value;
    destination[0] = source[3];
    destination[1] = source[2];
    destination[2] = source[1];
    destination[3] = source[0];
}

void System::ReverseINT(int* value)
{
    unsigned int original = (unsigned int)*value;
    unsigned char* source = (unsigned char*)&original;
    unsigned char* destination = (unsigned char*)value;
    destination[0] = source[3];
    destination[1] = source[2];
    destination[2] = source[1];
    destination[3] = source[0];
}

void System::ReverseULONGLONG(long long* value)
{
    unsigned long long original = (unsigned long long)*value;
    unsigned char* source = (unsigned char*)&original;
    unsigned char* destination = (unsigned char*)value;
    destination[0] = source[7];
    destination[1] = source[6];
    destination[2] = source[5];
    destination[3] = source[4];
    destination[4] = source[3];
    destination[5] = source[2];
    destination[6] = source[1];
    destination[7] = source[0];
}

void System::ReverseCHAR16A(char16_t* text, int count)
{
    for (int i = 0; i < count; i++)
    {
        ReverseUSHORT((unsigned short*)&text[i]);
    }
}
