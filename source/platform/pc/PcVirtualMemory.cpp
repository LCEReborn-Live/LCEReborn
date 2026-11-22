#include "platform/pc/PcVirtualMemory.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif LCE_PLATFORM_SWITCH
#include <cstdlib>
#include <cstring>
#else
#include <sys/mman.h>
#endif

void* PcVirtualReserve(std::size_t size, bool commit)
{
#if defined(_WIN32)
    return ::VirtualAlloc(NULL, size, commit ? MEM_RESERVE | MEM_COMMIT : MEM_RESERVE, PAGE_READWRITE);
#elif LCE_PLATFORM_SWITCH
    (void)commit;
    return std::calloc(1, size);
#else
    void* memory = mmap(NULL, size, commit ? PROT_READ | PROT_WRITE : PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? NULL : memory;
#endif
}

bool PcVirtualCommit(void* address, std::size_t size)
{
#if defined(_WIN32)
    return ::VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#elif LCE_PLATFORM_SWITCH
    (void)size;
    return address != nullptr;
#else
    return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

bool PcVirtualDecommit(void* address, std::size_t size)
{
#if defined(_WIN32)
    return ::VirtualFree(address, size, MEM_DECOMMIT) != 0;
#elif LCE_PLATFORM_SWITCH
    std::memset(address, 0, size);
    return true;
#else
    madvise(address, size, MADV_DONTNEED);
    return mprotect(address, size, PROT_NONE) == 0;
#endif
}

bool PcVirtualRelease(void* address, std::size_t size)
{
#if defined(_WIN32)
    (void)size;
    return ::VirtualFree(address, 0, MEM_RELEASE) != 0;
#elif LCE_PLATFORM_SWITCH
    (void)size;
    std::free(address);
    return true;
#else
    return munmap(address, size) == 0;
#endif
}
