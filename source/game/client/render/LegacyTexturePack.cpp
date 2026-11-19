#include "game/client/render/LegacyTexturePack.hpp"

LegacyTexturePack::LegacyTexturePack(unsigned int id) : DefaultTexturePack(id) {}

std::wstring LegacyTexturePack::getName()
{
    return L"Minecraft";
}

std::wstring LegacyTexturePack::getDesc1()
{
    return L"The classic Minecraft textures, icons and user interface!";
}
