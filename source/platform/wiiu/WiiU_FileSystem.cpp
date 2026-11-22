#include "platform/wiiu/WiiU_FileSystem.hpp"

#include "platform/wiiu/CStorage.hpp"
#include "platform/wiiu/WiiU_Runtime.hpp"

#include <cstdio>
#include <string>

extern C4JStorage* gStorage;

void getFinalFilePath(char (&path)[256], const char* fileName)
{
    std::string mountedPath = gStorage->GetMountedPath(std::string(fileName));
    const char* filePath = mountedPath.empty() ? fileName : mountedPath.c_str();

    if (filePath[0] == '/')
        std::snprintf(path, sizeof(path), "%s", filePath);
    else
        std::snprintf(path, sizeof(path), "%s/%s", WiiUGameContentFolder(), filePath);
    for (char* character = path; *character != '\0'; ++character)
        if (*character == '\\')
            *character = '/';
}
