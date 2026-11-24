#ifndef PLATFORM_BUILTINPACKS_HPP
#define PLATFORM_BUILTINPACKS_HPP

#include <algorithm>
#include <cctype>
#include <string>

inline bool IsOriginalCandyPackPath(std::string path)
{
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char character) {
        return character == '\\' ? '/' : static_cast<char>(std::tolower(character));
    });
    path = '/' + path + '/';
    return path.find("/candy texture pack/") != std::string::npos
           || path.find("/candy texture pack.pck/") != std::string::npos;
}

static const char* const BUILT_IN_PACKS[] = {
    "/vol/content/WiiU/DLC/Tutorial World",
    "/vol/content/WiiU/DLC/Battle Map Pack 0",
    "/vol/content/WiiU/DLC/MG02 Map Pack 0",
    "/vol/content/WiiU/DLC/MG02 Map Pack 1",
    "/vol/content/WiiU/DLC/MG02 Map Pack 2",
    "/vol/content/WiiU/DLC/MG02 Map Pack 3",
    "/vol/content/WiiU/DLC/Glide Map Pack 0",
    "/vol/content/WiiU/DLC/Battle & Beasts",
    "/vol/content/WiiU/DLC/Battle & Beasts 2",
    "/vol/content/WiiU/DLC/Festive",
    "/vol/content/WiiU/DLC/Fantasy Texture Pack",
    "/vol/content/WiiU/DLC/City Texture Pack",
    "/vol/content/WiiU/DLC/Natural Texture Pack",
    "/vol/content/WiiU/CU/DLC/1st Birthday Skin Pack",
    "/vol/content/WiiU/CU/DLC/2nd Birthday Skin Pack",
    "/vol/content/WiiU/CU/DLC/Minecon 2016 Skin Pack",
    "/vol/content/WiiU/CU/DLC/Minecon Earth 2017",
};

static const unsigned int BUILT_IN_PACK_COUNT = sizeof(BUILT_IN_PACKS) / sizeof(BUILT_IN_PACKS[0]);

#endif
