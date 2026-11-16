#include "game/client/render/LegacyTexturePack.hpp"

LegacyTexturePack::LegacyTexturePack(unsigned int id) : DefaultTexturePack(id) {}

std::wstring LegacyTexturePack::getName()
{
    return L"Old Textures";
}

std::wstring LegacyTexturePack::getDesc1()
{
    return L"The original LCEReborn textures.";
}
