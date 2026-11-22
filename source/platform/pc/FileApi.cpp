#include "platform/pc/FileApi.hpp"

#include "platform/pc/PcPaths.hpp"
#include "platform/pc/PcVirtualMemory.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include <share.h>
#include <stdio.h>
#else
#include <sys/types.h>
#endif

extern C4JStorage* gStorage;

namespace fs = std::filesystem;

enum class FileOpenMode
{
    Read,
    Append,
    Truncate
};

struct FileHandleEntry
{
    std::mutex mutex;
    std::FILE* stream;
    long long position;
    bool append;

    FileHandleEntry(std::FILE* fileStream, bool appendMode) : stream(fileStream), position(0), append(appendMode)
    {
    }

    ~FileHandleEntry()
    {
        std::fclose(stream);
    }

    FileHandleEntry(const FileHandleEntry&) = delete;
    FileHandleEntry& operator=(const FileHandleEntry&) = delete;
};

struct DirectoryHandleEntry
{
    std::mutex mutex;
    fs::directory_iterator iterator;

    explicit DirectoryHandleEntry(fs::directory_iterator directoryIterator) : iterator(std::move(directoryIterator))
    {
    }
};

struct FileHandleTable
{
    std::mutex mutex;
    std::unordered_map<int, std::shared_ptr<FileHandleEntry>> files;
    std::unordered_map<int, std::shared_ptr<DirectoryHandleEntry>> directories;
    int nextHandle = 1;
};

static FileHandleTable& Handles()
{
    static FileHandleTable table;
    return table;
}


static int AllocateHandle(FileHandleTable& table)
{
    for (;;)
    {
        int handle = table.nextHandle;
        table.nextHandle = handle == INT_MAX ? 1 : handle + 1;
        if (table.files.find(handle) == table.files.end()
            && table.directories.find(handle) == table.directories.end())
            return handle;
    }
}

static int AddFile(const std::shared_ptr<FileHandleEntry>& file)
{
    FileHandleTable& table = Handles();
    std::lock_guard<std::mutex> lock(table.mutex);
    int handle = AllocateHandle(table);
    table.files[handle] = file;
    return handle;
}

static int AddDirectory(const std::shared_ptr<DirectoryHandleEntry>& directory)
{
    FileHandleTable& table = Handles();
    std::lock_guard<std::mutex> lock(table.mutex);
    int handle = AllocateHandle(table);
    table.directories[handle] = directory;
    return handle;
}

static std::shared_ptr<FileHandleEntry> FindFile(int handle)
{
    FileHandleTable& table = Handles();
    std::lock_guard<std::mutex> lock(table.mutex);
    std::unordered_map<int, std::shared_ptr<FileHandleEntry>>::iterator it = table.files.find(handle);
    return it == table.files.end() ? std::shared_ptr<FileHandleEntry>() : it->second;
}

static std::shared_ptr<DirectoryHandleEntry> FindDirectory(int handle)
{
    FileHandleTable& table = Handles();
    std::lock_guard<std::mutex> lock(table.mutex);
    std::unordered_map<int, std::shared_ptr<DirectoryHandleEntry>>::iterator it = table.directories.find(handle);
    return it == table.directories.end() ? std::shared_ptr<DirectoryHandleEntry>() : it->second;
}

static bool MatchMount(const std::string& path, const char* mount, std::string& rest)
{
    const std::size_t length = std::strlen(mount);
    if (path.compare(0, length, mount) != 0)
        return false;
    if (path.size() != length && path[length] != '/')
        return false;
    rest = path.substr(length);
    return true;
}

#if defined(_WIN32)
static bool DecodeUtf8(const std::string& text, std::wstring& decoded)
{
    decoded.clear();
    std::size_t index = 0;
    while (index < text.size())
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        unsigned int codePoint;
        std::size_t length;
        if (lead < 0x80)
        {
            codePoint = lead;
            length = 1;
        }
        else if ((lead & 0xe0) == 0xc0)
        {
            codePoint = lead & 0x1f;
            length = 2;
        }
        else if ((lead & 0xf0) == 0xe0)
        {
            codePoint = lead & 0x0f;
            length = 3;
        }
        else if ((lead & 0xf8) == 0xf0)
        {
            codePoint = lead & 0x07;
            length = 4;
        }
        else
        {
            return false;
        }
        if (index + length > text.size())
            return false;
        for (std::size_t continuation = 1; continuation < length; ++continuation)
        {
            const unsigned char byte = static_cast<unsigned char>(text[index + continuation]);
            if ((byte & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (byte & 0x3f);
        }
        static const unsigned int minimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (codePoint < minimum[length] || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
        if (codePoint >= 0x10000)
        {
            codePoint -= 0x10000;
            decoded.push_back(static_cast<wchar_t>(0xd800 + (codePoint >> 10)));
            decoded.push_back(static_cast<wchar_t>(0xdc00 + (codePoint & 0x3ff)));
        }
        else
        {
            decoded.push_back(static_cast<wchar_t>(codePoint));
        }
        index += length;
    }
    return true;
}

static fs::path GamePath(const std::string& text)
{
    std::wstring wide;
    if (!DecodeUtf8(text, wide))
    {
        wide.clear();
        for (std::string::const_iterator it = text.begin(); it != text.end(); ++it)
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*it)));
    }
    return fs::path(wide);
}

static std::string GameName(const fs::path& path)
{
    const std::wstring& wide = path.native();
    std::string encoded;
    for (std::size_t index = 0; index < wide.size(); ++index)
    {
        unsigned int codePoint = static_cast<unsigned int>(wide[index]);
        if (codePoint >= 0xd800 && codePoint <= 0xdbff && index + 1 < wide.size()
            && wide[index + 1] >= 0xdc00 && wide[index + 1] <= 0xdfff)
        {
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (static_cast<unsigned int>(wide[index + 1]) - 0xdc00);
            ++index;
        }
        else if (codePoint >= 0xd800 && codePoint <= 0xdfff)
        {
            codePoint = 0xfffd;
        }

        if (codePoint < 0x80)
        {
            encoded.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint < 0x800)
        {
            encoded.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            encoded.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
        else if (codePoint < 0x10000)
        {
            encoded.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            encoded.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
        else
        {
            encoded.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            encoded.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }
    return encoded;
}
#else
static fs::path GamePath(const std::string& text)
{
    return fs::path(text);
}

static std::string GameName(const fs::path& path)
{
    return path.native();
}
#endif

static fs::path UnderRoot(const std::string& root, const std::string& rest)
{
    std::string::size_type start = rest.find_first_not_of('/');
    if (start == std::string::npos)
        return fs::path(root);
    return fs::path(root) / GamePath(rest.substr(start));
}

#if defined(_WIN32)
static fs::path ContentPath(const fs::path& path)
{
    return path;
}
#else
static bool SameNameIgnoringCase(const std::string& first, const std::string& second)
{
    if (first.size() != second.size())
        return false;
    for (std::string::size_type index = 0; index < first.size(); ++index)
    {
        if (std::tolower(static_cast<unsigned char>(first[index]))
            != std::tolower(static_cast<unsigned char>(second[index])))
            return false;
    }
    return true;
}

// The game names its content files with the case-insensitive Wii U file system in mind.
static fs::path ContentPath(const fs::path& path)
{
    std::error_code error;
    if (fs::exists(path, error))
        return path;

    static std::mutex mutex;
    static std::unordered_map<std::string, fs::path> matches;
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_map<std::string, fs::path>::const_iterator known = matches.find(path.native());
    if (known != matches.end())
        return known->second;

    const fs::path root(PcContentRoot());
    fs::path resolved = root;
    bool found = true;
    for (const fs::path& part : path.lexically_relative(root))
    {
        fs::path next = resolved / part;
        if (found && !fs::exists(next, error))
        {
            found = false;
            for (fs::directory_iterator entry(resolved, error), end; !error && entry != end; entry.increment(error))
            {
                if (SameNameIgnoringCase(entry->path().filename().native(), part.native()))
                {
                    next = entry->path();
                    found = true;
                    break;
                }
            }
        }
        resolved = next;
    }
    matches.emplace(path.native(), found ? resolved : path);
    return found ? resolved : path;
}
#endif

static fs::path ResolvePath(const char* fileName)
{
    std::string path;
    if (gStorage != nullptr)
        path = gStorage->GetMountedPath(std::string(fileName));
    if (path.empty())
        path = fileName;
    std::replace(path.begin(), path.end(), '\\', '/');

    std::string rest;
    if (MatchMount(path, "/vol/content", rest))
        return ContentPath(UnderRoot(PcContentRoot(), rest));
    if (MatchMount(path, "/vol/save", rest))
        return UnderRoot(PcSaveRoot(), rest);
    if (MatchMount(path, "/vol/packs", rest))
        return UnderRoot(PcPacksRoot(), rest);

    const fs::path nativePath = GamePath(path);
#if LCE_PLATFORM_SWITCH
    if (path.compare(0, sizeof("sdmc:/") - 1, "sdmc:/") == 0)
        return nativePath;
#endif
    if ((!path.empty() && path[0] == '/') || nativePath.is_absolute())
        return nativePath;
    return ContentPath(UnderRoot(PcContentRoot(), path));
}

static std::FILE* OpenStream(const fs::path& path, FileOpenMode mode)
{
#if defined(_WIN32)
    const wchar_t* modeString = mode == FileOpenMode::Read ? L"rb" : mode == FileOpenMode::Append ? L"a+b" : L"w+b";
    return _wfsopen(path.c_str(), modeString, _SH_DENYNO);
#else
    const char* modeString = mode == FileOpenMode::Read ? "rb" : mode == FileOpenMode::Append ? "a+b" : "w+b";
    return std::fopen(path.c_str(), modeString);
#endif
}

static bool SeekStream(std::FILE* stream, long long offset, int origin)
{
#if defined(_WIN32)
    return _fseeki64(stream, offset, origin) == 0;
#elif LCE_PLATFORM_SWITCH
    static_assert(sizeof(long) >= sizeof(long long), "Switch file offsets require a 64-bit long");
    return std::fseek(stream, static_cast<long>(offset), origin) == 0;
#else
    return fseeko(stream, static_cast<off_t>(offset), origin) == 0;
#endif
}

static long long TellStream(std::FILE* stream)
{
#if defined(_WIN32)
    return _ftelli64(stream);
#elif LCE_PLATFORM_SWITCH
    return std::ftell(stream);
#else
    return static_cast<long long>(ftello(stream));
#endif
}

static long long StreamSize(std::FILE* stream)
{
    std::fflush(stream);
    if (!SeekStream(stream, 0, SEEK_END))
        return 0;
    long long size = TellStream(stream);
    return size < 0 ? 0 : size;
}

int CreateFileA(const char* fileName, unsigned int desiredAccess, unsigned int, void*, unsigned int creationDisposition,
                unsigned int, int)
{
    FileOpenMode mode;
    switch (creationDisposition)
    {
    case OPEN_EXISTING:
        if (desiredAccess == GENERIC_READ)
        {
            mode = FileOpenMode::Read;
            break;
        }
        [[fallthrough]];
    case OPEN_ALWAYS:
        mode = FileOpenMode::Append;
        break;
    case CREATE_NEW:
    case CREATE_ALWAYS:
    case TRUNCATE_EXISTING:
        mode = FileOpenMode::Truncate;
        break;
    default:
        return INVALID_HANDLE_VALUE;
    }

    const fs::path path = ResolvePath(fileName);
    std::error_code error;
    if (fs::is_directory(path, error))
        return INVALID_HANDLE_VALUE;

    std::FILE* stream = OpenStream(path, mode);
    if (stream == nullptr)
        return INVALID_HANDLE_VALUE;

    return AddFile(std::make_shared<FileHandleEntry>(stream, mode == FileOpenMode::Append));
}

void CloseHandle(int file)
{
    std::shared_ptr<FileHandleEntry> openFile;
    {
        FileHandleTable& table = Handles();
        std::lock_guard<std::mutex> lock(table.mutex);
        std::unordered_map<int, std::shared_ptr<FileHandleEntry>>::iterator it = table.files.find(file);
        if (it == table.files.end())
            return;
        openFile = it->second;
        table.files.erase(it);
    }
    std::lock_guard<std::mutex> lock(openFile->mutex);
    std::fflush(openFile->stream);
}

unsigned int GetFileSize(int file, unsigned int*)
{
    std::shared_ptr<FileHandleEntry> openFile = FindFile(file);
    if (!openFile)
        return 0;
    std::lock_guard<std::mutex> lock(openFile->mutex);
    return static_cast<unsigned int>(StreamSize(openFile->stream));
}

int ReadFile(int file, void* buffer, unsigned int bytesToRead, unsigned int* bytesRead, _OVERLAPPED*)
{
    std::shared_ptr<FileHandleEntry> openFile = FindFile(file);
    if (!openFile)
        return 1;
    std::lock_guard<std::mutex> lock(openFile->mutex);
    std::size_t total = 0;
    if (bytesToRead != 0 && SeekStream(openFile->stream, openFile->position, SEEK_SET))
    {
        total = std::fread(buffer, 1, bytesToRead, openFile->stream);
        std::clearerr(openFile->stream);
        openFile->position += static_cast<long long>(total);
    }
    if (bytesRead != nullptr)
        *bytesRead = static_cast<unsigned int>(total);
    return 1;
}

int WriteFile(int file, const void* buffer, unsigned int bytesToWrite, unsigned int* bytesWritten, _OVERLAPPED*)
{
    std::shared_ptr<FileHandleEntry> openFile = FindFile(file);
    if (!openFile)
        return 0;
    std::lock_guard<std::mutex> lock(openFile->mutex);
    const bool positioned = openFile->append ? SeekStream(openFile->stream, 0, SEEK_END)
                                             : SeekStream(openFile->stream, openFile->position, SEEK_SET);
    if (!positioned)
        return 0;
    const std::size_t written = bytesToWrite == 0 ? 0 : std::fwrite(buffer, 1, bytesToWrite, openFile->stream);
    if (written != bytesToWrite)
    {
        std::clearerr(openFile->stream);
        return 0;
    }
    if (!openFile->append)
        openFile->position += static_cast<long long>(written);
    if (bytesWritten != nullptr)
        *bytesWritten = static_cast<unsigned int>(written);
    return 1;
}

int SetFilePointer(int file, long distanceToMove, int*, unsigned int moveMethod)
{
    std::shared_ptr<FileHandleEntry> openFile = FindFile(file);
    if (!openFile)
        return -1;
    std::lock_guard<std::mutex> lock(openFile->mutex);
    const int fileSize = static_cast<int>(StreamSize(openFile->stream));
    int position;
    if (moveMethod == 2)
        position = fileSize + static_cast<int>(distanceToMove);
    else if (moveMethod == 1)
        position = static_cast<int>(openFile->position) + static_cast<int>(distanceToMove);
    else
        position = static_cast<int>(distanceToMove);
    if (position >= 0 && position < fileSize)
        openFile->position = position;
    return position;
}

unsigned int GetFileAttributesA(const char* fileName)
{
    std::error_code error;
    const fs::file_status status = fs::status(ResolvePath(fileName), error);
    if (error || !fs::exists(status))
        return INVALID_FILE_ATTRIBUTES;
    return fs::is_directory(status) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

int CreateDirectoryA(const char* pathName, void*)
{
    std::error_code error;
    const bool created = fs::create_directory(ResolvePath(pathName), error);
    return created && !error;
}

bool PcRemoveAll(const char* path)
{
    std::error_code error;
    fs::remove_all(ResolvePath(path), error);
    return !error;
}

bool PcRename(const char* from, const char* to)
{
    std::error_code error;
    fs::rename(ResolvePath(from), ResolvePath(to), error);
    return !error;
}

bool PcCreateDirectories(const char* path)
{
    std::error_code error;
    fs::create_directories(ResolvePath(path), error);
    return !error;
}

int OpenDir(const char* path, int* directory)
{
    std::error_code error;
    fs::directory_iterator iterator(ResolvePath(path), error);
    if (error)
        return 0;
    *directory = AddDirectory(std::make_shared<DirectoryHandleEntry>(std::move(iterator)));
    return 1;
}

int ReadDir(int* directory, FSDirEntry* entry)
{
    std::shared_ptr<DirectoryHandleEntry> openDirectory = FindDirectory(*directory);
    if (!openDirectory)
        return 0;
    std::lock_guard<std::mutex> lock(openDirectory->mutex);
    if (openDirectory->iterator == fs::directory_iterator())
        return 0;

    const fs::directory_entry& current = *openDirectory->iterator;
    const std::string name = GameName(current.path().filename());
    const std::size_t length = std::min(name.size(), sizeof(entry->name) - 1);
    std::memcpy(entry->name, name.data(), length);
    entry->name[length] = '\0';

    std::error_code error;
    entry->isDirectory = current.is_directory(error);
    entry->size = 0;
    if (!entry->isDirectory)
    {
        const std::uintmax_t size = current.file_size(error);
        if (!error)
            entry->size = static_cast<unsigned long long>(size);
    }

    openDirectory->iterator.increment(error);
    if (error)
        openDirectory->iterator = fs::directory_iterator();
    return 1;
}

int CloseDir(int* directory)
{
    FileHandleTable& table = Handles();
    std::lock_guard<std::mutex> lock(table.mutex);
    return table.directories.erase(*directory) != 0;
}

static const unsigned int kMemCommit = 0x1000;
static const unsigned int kMemReserve = 0x2000;
static const unsigned int kMemDecommit = 0x4000;
static const unsigned int kMemRelease = 0x8000;

void* VirtualAlloc(void* address, unsigned long size, unsigned int allocationType, unsigned int)
{
    const bool commit = (allocationType & kMemCommit) != 0;
    if (address == nullptr)
        return (allocationType & kMemReserve) != 0 || commit ? PcVirtualReserve(size, commit) : nullptr;
    if (commit && !PcVirtualCommit(address, size))
        return nullptr;
    return address;
}

int VirtualFree(void* address, unsigned long size, unsigned int freeType)
{
    if (address == nullptr)
        return 0;
    if ((freeType & kMemRelease) != 0)
        return PcVirtualRelease(address, size) ? 1 : 0;
    if ((freeType & kMemDecommit) != 0)
        return PcVirtualDecommit(address, size) ? 1 : 0;
    return 1;
}
