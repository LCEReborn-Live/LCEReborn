#ifndef PLATFORM_WIIU_LIVE_COSMETICS_HPP
#define PLATFORM_WIIU_LIVE_COSMETICS_HPP

#include <string>

namespace WiiULiveCosmetics
{
void Tick();
unsigned int PlayerSkinId();
#if LCE_PLATFORM_SWITCH
unsigned int PlayerCapeId();
#endif
unsigned int AvatarGeneration();
const std::string* AvatarSkin(unsigned int user, std::string& revision);
}

#endif
