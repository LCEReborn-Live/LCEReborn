#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>

#include "platform/wiiu/WiiU_Runtime.hpp"
#include "platform/wiiu/PlatformTiming.hpp"
#include "game/core/CriticalSection.hpp"
#include "platform/wiiu/WiiU_FileCache.hpp"

FSClient* gFileSystemClient = 0;
FSClient* gFileCacheClient = 0;
static char s_packsFolder[160];

void stateChangeCallback(FSClient*, FSVolumeState, void*);

unsigned int FileTimeToSystemTime(const FILETIME* fileTime, SYSTEMTIME* systemTime)
{
    (void)fileTime;
    (void)systemTime;
    return 0;
}

static void MountPacksFolder()
{
    FSCmdBlock commandBlock;
    FSInitCmdBlock(&commandBlock);
    FSMountSource source;
    char mountPath[128];
    if (FSGetMountSource(gFileSystemClient, &commandBlock, FS_MOUNT_SOURCE_SD, &source, FS_ERROR_FLAG_ALL) != FS_STATUS_OK
        || FSMount(gFileSystemClient, &commandBlock, &source, mountPath, sizeof(mountPath), FS_ERROR_FLAG_ALL)
               != FS_STATUS_OK)
        return;

    static const char* const folders[] = {"/wiiu", "/wiiu/LCEReborn", "/wiiu/LCEReborn/packs"};
    char path[160];
    for (unsigned int index = 0; index < sizeof(folders) / sizeof(folders[0]); ++index)
    {
        std::snprintf(path, sizeof(path), "%s%s", mountPath, folders[index]);
        FSMakeDir(gFileSystemClient, &commandBlock, path, FS_ERROR_FLAG_ALL);
    }
    FSStat stat;
    if (FSGetStat(gFileSystemClient, &commandBlock, path, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK
        && (stat.flags & FS_STAT_DIRECTORY) != 0)
        std::strcpy(s_packsFolder, path);
}

const char* WiiUPacksFolder()
{
    return s_packsFolder;
}

static bool ListFilesUnder(FSCmdBlock* commandBlock, const std::string& folder, const std::string& prefix,
                           std::vector<std::string>& paths, std::vector<unsigned long long>& sizes)
{
    FSDirectoryHandle directory;
    if (FSOpenDir(gFileSystemClient, commandBlock, folder.c_str(), &directory, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return false;
    std::vector<std::string> names;
    std::vector<bool> folders;
    std::vector<unsigned long long> fileSizes;
    FSDirectoryEntry entry;
    while (FSReadDir(gFileSystemClient, commandBlock, directory, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
    {
        if (std::strcmp(entry.name, ".") == 0 || std::strcmp(entry.name, "..") == 0)
            continue;
        names.push_back(entry.name);
        folders.push_back((entry.info.flags & FS_STAT_DIRECTORY) != 0);
        fileSizes.push_back(entry.info.size);
    }
    FSCloseDir(gFileSystemClient, commandBlock, directory, FS_ERROR_FLAG_ALL);

    std::vector<size_t> order(names.size());
    for (size_t index = 0; index < order.size(); ++index)
        order[index] = index;
    for (size_t index = 1; index < order.size(); ++index)
        for (size_t other = index; other > 0 && names[order[other]] < names[order[other - 1]]; --other)
            std::swap(order[other], order[other - 1]);

    for (size_t position = 0; position < order.size(); ++position)
    {
        const size_t index = order[position];
        const std::string relative = prefix.empty() ? names[index] : prefix + "/" + names[index];
        if (folders[index])
        {
            if (!ListFilesUnder(commandBlock, folder + "/" + names[index], relative, paths, sizes))
                return false;
        }
        else
        {
            paths.push_back(relative);
            sizes.push_back(fileSizes[index]);
        }
    }
    return true;
}

bool WiiUListFiles(const char* root, std::vector<std::string>& paths, std::vector<unsigned long long>& sizes)
{
    FSCmdBlock commandBlock;
    FSInitCmdBlock(&commandBlock);
    paths.clear();
    sizes.clear();
    return ListFilesUnder(&commandBlock, root, std::string(), paths, sizes);
}

static bool RemoveAll(FSCmdBlock* commandBlock, const std::string& path)
{
    FSStat stat;
    if (FSGetStat(gFileSystemClient, commandBlock, path.c_str(), &stat, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return true;
    if ((stat.flags & FS_STAT_DIRECTORY) != 0)
    {
        FSDirectoryHandle directory;
        std::vector<std::string> children;
        if (FSOpenDir(gFileSystemClient, commandBlock, path.c_str(), &directory, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        {
            FSDirectoryEntry entry;
            while (FSReadDir(gFileSystemClient, commandBlock, directory, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
                if (std::strcmp(entry.name, ".") != 0 && std::strcmp(entry.name, "..") != 0)
                    children.push_back(path + "/" + entry.name);
            FSCloseDir(gFileSystemClient, commandBlock, directory, FS_ERROR_FLAG_ALL);
        }
        for (size_t index = 0; index < children.size(); ++index)
            if (!RemoveAll(commandBlock, children[index]))
                return false;
    }
    return FSRemove(gFileSystemClient, commandBlock, path.c_str(), FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
}

bool WiiURemoveAll(const char* path)
{
    FSCmdBlock commandBlock;
    FSInitCmdBlock(&commandBlock);
    return RemoveAll(&commandBlock, path);
}

bool WiiURename(const char* from, const char* to)
{
    FSCmdBlock commandBlock;
    FSInitCmdBlock(&commandBlock);
    return FSRename(gFileSystemClient, &commandBlock, from, to, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
}

bool WiiUCreateDirectories(const char* path)
{
    FSCmdBlock commandBlock;
    FSInitCmdBlock(&commandBlock);
    const std::string full(path);
    for (size_t slash = full.find('/', 1); ; slash = full.find('/', slash + 1))
    {
        const std::string part = slash == std::string::npos ? full : full.substr(0, slash);
        FSMakeDir(gFileSystemClient, &commandBlock, part.c_str(), FS_ERROR_FLAG_ALL);
        if (slash == std::string::npos)
            break;
    }
    FSStat stat;
    return FSGetStat(gFileSystemClient, &commandBlock, path, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK
           && (stat.flags & FS_STAT_DIRECTORY) != 0;
}

void WiiUInitialise()
{
    FSInit();
    gFileSystemClient = static_cast<FSClient*>(std::malloc(0x1700));
    FSAddClient(gFileSystemClient, FS_ERROR_FLAG_NONE);
    MountPacksFolder();
    gFileCacheClient = gFileSystemClient;
    FSStateChangeParams notification = {stateChangeCallback, 0, 0};
    FSSetStateChangeNotification(gFileSystemClient, &notification);
    s_writeBuffer = static_cast<unsigned char*>(MEMAllocFromDefaultHeapEx(FILE_CACHE_BUFFER_SIZE, 64));
    InitializeCriticalSection(&s_writeMutex);
    InitializeCriticalSection(&s_commandBlockMutex);
}

#if defined(__POWERPC__) || defined(__powerpc__) || defined(__PPC__)
void __debugbreak()
{
}
#endif

unsigned int GetLastFileErrorCodeForViewer()
{
    return static_cast<unsigned int>(FSGetLastErrorCodeForViewer(gFileSystemClient));
}

unsigned int GetLastError()
{
    return 0;
}

void GlobalMemoryStatus(MEMORYSTATUS* status)
{
    status->dwTotalPhys = 0;
    status->dwAvailPhys = 0;
    status->dwAvailVirtual = 0;
}

void GlobalMemoryStatus(_MEMORYSTATUS* status)
{
    status->dwTotalPhys = 0;
    status->dwAvailPhys = 0;
    status->dwAvailVirtual = 0;
}

unsigned int GetTickCount()
{
    return static_cast<unsigned int>(TicksToUnits(OSGetSystemTime(), 1000ULL));
}

void GetSystemTime(SYSTEMTIME* systemTime)
{
    OSCalendarTime calendar;
    OSTicksToCalendarTime(OSGetTime(), &calendar);
    systemTime->year = static_cast<unsigned short>(calendar.tm_year);
    systemTime->month = static_cast<unsigned short>(calendar.tm_mon + 1);
    systemTime->dayOfWeek = static_cast<unsigned short>(calendar.tm_wday);
    systemTime->day = static_cast<unsigned short>(calendar.tm_mday);
    systemTime->hour = static_cast<unsigned short>(calendar.tm_hour);
    systemTime->minute = static_cast<unsigned short>(calendar.tm_min);
    systemTime->second = static_cast<unsigned short>(calendar.tm_sec);
    systemTime->milliseconds = static_cast<unsigned short>(calendar.tm_msec);
}

void GetLocalTime(SYSTEMTIME* systemTime)
{
    GetSystemTime(systemTime);
}

void EnterCriticalSection(OSFastMutex* criticalSection)
{
    OSFastMutex_Lock(criticalSection);
}

void LeaveCriticalSection(OSFastMutex* criticalSection)
{
    OSFastMutex_Unlock(criticalSection);
}

void InitializeCriticalSection(OSFastMutex* criticalSection)
{
    OSFastMutex_Init(criticalSection, "");
}

void InitializeCriticalSectionAndSpinCount(OSFastMutex* criticalSection, unsigned long)
{
    InitializeCriticalSection(criticalSection);
}

void DeleteCriticalSection(OSFastMutex*)
{
}

int TryEnterCriticalSection(OSFastMutex* criticalSection)
{
    return OSFastMutex_TryLock(criticalSection);
}
