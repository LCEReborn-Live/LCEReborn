#include "platform/pc/PcSystemInfo.hpp"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#elif LCE_PLATFORM_SWITCH
#include "platform/switch/SwitchRuntime.hpp"
#else
#include <unistd.h>
#include <cstdio>
#include <fstream>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

namespace
{
std::string TrimSpaces(const std::string& name)
{
    const std::string::size_type first = name.find_first_not_of(" \t");
    const std::string::size_type last = name.find_last_not_of(" \t");
    return first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
}

#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
void CpuId(unsigned int leaf, unsigned int registers[4])
{
#ifdef _WIN32
    int values[4] = {0, 0, 0, 0};
    __cpuid(values, static_cast<int>(leaf));
    std::memcpy(registers, values, sizeof(values));
#else
    registers[0] = registers[1] = registers[2] = registers[3] = 0;
    __cpuid(leaf, registers[0], registers[1], registers[2], registers[3]);
#endif
}

std::string CpuBrand()
{
    unsigned int registers[4] = {0, 0, 0, 0};
    CpuId(0x80000000u, registers);
    if (registers[0] < 0x80000004u)
        return std::string();
    char brand[49];
    std::memset(brand, 0, sizeof(brand));
    for (unsigned int leaf = 0; leaf < 3; ++leaf)
    {
        CpuId(0x80000002u + leaf, registers);
        std::memcpy(brand + leaf * 16, registers, sizeof(registers));
    }
    return TrimSpaces(brand);
}
#endif
}

bool PcMemoryUsage(unsigned long long* used, unsigned long long* total)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters;
    std::memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return false;
    MEMORYSTATUSEX status;
    std::memset(&status, 0, sizeof(status));
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return false;
    *used = counters.WorkingSetSize;
    *total = status.ullTotalPhys;
    return true;
#elif LCE_PLATFORM_SWITCH
    return SwitchMemoryUsage(used, total);
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long pages = sysconf(_SC_PHYS_PAGES);
    if (pageSize <= 0 || pages <= 0)
        return false;
    std::FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr)
        return false;
    unsigned long long size = 0;
    unsigned long long resident = 0;
    const bool read = std::fscanf(statm, "%llu %llu", &size, &resident) == 2;
    std::fclose(statm);
    if (!read)
        return false;
    *used = resident * static_cast<unsigned long long>(pageSize);
    *total = static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(pageSize);
    return true;
#endif
}

std::string PcCpuName()
{
#if LCE_PLATFORM_SWITCH
    return "Nintendo Switch (ARM64)";
#else
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
    std::string brand = CpuBrand();
    if (!brand.empty())
        return brand;
#endif
#ifndef _WIN32
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line))
    {
        const std::string::size_type colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = TrimSpaces(line.substr(0, colon));
        if (key == "model name" || key == "Model" || key == "Hardware")
            return TrimSpaces(line.substr(colon + 1));
    }
#endif
    return std::string();
#endif
}
