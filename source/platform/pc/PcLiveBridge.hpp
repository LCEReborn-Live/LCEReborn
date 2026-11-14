#ifndef PLATFORM_PC_PCLIVEBRIDGE_HPP
#define PLATFORM_PC_PCLIVEBRIDGE_HPP

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <stdio.h>
#endif

// The launcher and game share a private per-session directory. Keep its small
// file bridge available on every PC platform; the filenames passed here are
// fixed ASCII names owned by the client.
namespace PcLiveBridge
{
inline bool Enabled()
{
#ifdef _WIN32
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    return directory != NULL && *directory != L'\0';
#else
    const char* directory = std::getenv("LCE_LIVE_DIR");
    return directory != NULL && *directory != '\0';
#endif
}

inline FILE* Open(const wchar_t* name, const wchar_t* mode)
{
#ifdef _WIN32
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    if (directory == NULL || *directory == L'\0' || name == NULL || mode == NULL)
        return NULL;
    std::wstring path(directory);
    if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
        path += L'\\';
    path += name;
    FILE* file = NULL;
    return _wfopen_s(&file, path.c_str(), mode) == 0 ? file : NULL;
#else
    const char* directory = std::getenv("LCE_LIVE_DIR");
    if (directory == NULL || *directory == '\0' || name == NULL || mode == NULL)
        return NULL;
    std::string narrowName;
    std::string narrowMode;
    for (const wchar_t* at = name; *at != L'\0'; ++at)
    {
        if (*at > 0x7f)
            return NULL;
        narrowName.push_back(static_cast<char>(*at));
    }
    for (const wchar_t* at = mode; *at != L'\0'; ++at)
    {
        if (*at > 0x7f)
            return NULL;
        narrowMode.push_back(static_cast<char>(*at));
    }
    std::string path(directory);
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    path += narrowName;
    return std::fopen(path.c_str(), narrowMode.c_str());
#endif
}

inline bool NarrowName(const wchar_t* name, std::string& result)
{
    if (name == NULL)
        return false;
    result.clear();
    for (const wchar_t* at = name; *at != L'\0'; ++at)
    {
        if (*at > 0x7f)
            return false;
        result.push_back(static_cast<char>(*at));
    }
    return true;
}

inline bool Remove(const wchar_t* name)
{
    if (name == NULL)
        return false;
#ifdef _WIN32
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    if (directory == NULL || *directory == L'\0')
        return false;
    std::wstring path(directory);
    if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
        path += L'\\';
    path += name;
    return _wremove(path.c_str()) == 0;
#else
    const char* directory = std::getenv("LCE_LIVE_DIR");
    std::string narrowName;
    if (directory == NULL || *directory == '\0' || !NarrowName(name, narrowName))
        return false;
    std::string path(directory);
    if (!path.empty() && path[path.size() - 1] != '/')
        path += '/';
    path += narrowName;
    return std::remove(path.c_str()) == 0;
#endif
}

inline bool Rename(const wchar_t* from, const wchar_t* to)
{
    if (from == NULL || to == NULL)
        return false;
#ifdef _WIN32
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    if (directory == NULL || *directory == L'\0')
        return false;
    std::wstring oldPath(directory);
    std::wstring newPath(directory);
    if (!oldPath.empty() && oldPath[oldPath.size() - 1] != L'\\' && oldPath[oldPath.size() - 1] != L'/')
    {
        oldPath += L'\\';
        newPath += L'\\';
    }
    oldPath += from;
    newPath += to;
    return _wrename(oldPath.c_str(), newPath.c_str()) == 0;
#else
    std::string oldName;
    std::string newName;
    if (!NarrowName(from, oldName) || !NarrowName(to, newName))
        return false;
    const char* directory = std::getenv("LCE_LIVE_DIR");
    if (directory == NULL || *directory == '\0')
        return false;
    std::string oldPath(directory);
    std::string newPath(directory);
    if (!oldPath.empty() && oldPath[oldPath.size() - 1] != '/')
    {
        oldPath += '/';
        newPath += '/';
    }
    oldPath += oldName;
    newPath += newName;
    return std::rename(oldPath.c_str(), newPath.c_str()) == 0;
#endif
}
}

#endif
