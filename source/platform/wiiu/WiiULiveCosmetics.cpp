#include "platform/wiiu/WiiULiveCosmetics.hpp"

#include <cstdlib>
#include <cstring>
#include <map>

#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/System.hpp"
#if LCE_PLATFORM_SWITCH
#include "game/world/generation/Player.hpp"
#endif
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"

namespace WiiULiveCosmetics
{
namespace
{
const size_t kMaxSkinBytes = 512 * 1024;
const size_t kMaxAvatars = 192;
const size_t kMaxAvatarBytes = 4 * 1024 * 1024;

struct Avatar
{
    std::string png;
    unsigned int revision = 0;
    long long usedAt = 0;
    long long refreshAt = 0;
};

std::map<unsigned int, Avatar> avatars;
size_t avatarBytes = 0;
unsigned int generation = 0;
unsigned int requestedAvatar = 0;
bool loadingOwnSkin = false;
unsigned int ownSkin = 0;
#if LCE_PLATFORM_SWITCH
unsigned int ownCape = 0;
bool loadingOwnCape = false;
#endif
long long nextOwnCheck = 0;
long long nextAvatarCheck = 0;

const JsonValue& Field(const JsonValue& root, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = root.members.find(name);
    return found == root.members.end() ? empty : found->second;
}

bool IsTexture(const std::string& bytes, char height)
{
    const char header[] = "\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x40\0\0\0";
    return bytes.size() >= 33 && bytes.size() <= kMaxSkinBytes
        && bytes.compare(0, sizeof(header) - 1, header, sizeof(header) - 1) == 0
        && bytes[sizeof(header) - 1] == height;
}

bool IsSkin(const std::string& bytes)
{
    return IsTexture(bytes, 64);
}

unsigned int CosmeticId(unsigned int user)
{
    const unsigned int hash = static_cast<unsigned int>((user * 2654435761ULL) & 0x7fffffffULL);
    return 0x80000000u | (hash ? hash : 1u);
}

bool EvictAvatar(unsigned int keep)
{
    auto oldest = avatars.end();
    for (auto current = avatars.begin(); current != avatars.end(); ++current)
        if (current->first != keep && current->first != requestedAvatar
            && (oldest == avatars.end() || current->second.usedAt < oldest->second.usedAt))
            oldest = current;
    if (oldest == avatars.end())
        return false;
    avatarBytes -= oldest->second.png.size();
    avatars.erase(oldest);
    return true;
}

void DownloadOwnSkin(unsigned int user)
{
    const std::string path = "/users/" + std::to_string(user) + "/skin";
    loadingOwnSkin = WiiULiveSignIn::RequestApi(path.c_str(), NULL, [user](long status, const std::string& response) {
        loadingOwnSkin = false;
        if (status == 404)
            nextOwnCheck = System::processTimeInMilliSecs() + 60000;
        if (status != 200 || !IsSkin(response))
            return;
        const unsigned int id = CosmeticId(user);
        const std::wstring texture = CMinecraftApp::getSkinPathFromId(id);
        unsigned char* data = NULL;
        unsigned int length = 0;
        gApp.GetMemFileDetails(texture, &data, &length);
        if (length == 0)
        {
            unsigned char* owned = new unsigned char[response.size()];
            std::memcpy(owned, response.data(), response.size());
            gApp.AddMemoryTextureFile(texture, owned, static_cast<unsigned int>(response.size()));
        }
#if LCE_PLATFORM_SWITCH
        _SkinAdjustments adjustments;
        adjustments.mAnimFlags = 0x40000u;
        gApp.SetSkinAdjustments(id, adjustments);
#endif
        ownSkin = id;
        GameSettings::SetPlayerSkin(0, id, false);
    });
}

#if LCE_PLATFORM_SWITCH
void DownloadOwnCape(unsigned int user)
{
    const std::string path = "/users/" + std::to_string(user) + "/cape";
    loadingOwnCape = WiiULiveSignIn::RequestApi(path.c_str(), NULL, [user](long status, const std::string& response) {
        loadingOwnCape = false;
        if (status != 200 || !IsTexture(response, 32))
            return;
        const unsigned int id = CosmeticId(user);
        const std::wstring texture = Player::getCapePathFromId(id);
        if (!gApp.IsFileInMemoryTextures(texture))
        {
            unsigned char* bytes = new unsigned char[response.size()];
            std::memcpy(bytes, response.data(), response.size());
            gApp.AddMemoryTextureFile(texture, bytes, static_cast<unsigned int>(response.size()));
        }
        ownCape = id;
        GameSettings::SetPlayerCape(0, id, false);
    });
}
#endif
}

void Tick()
{
    if (!WiiULiveSignIn::IsSignedIn())
    {
        ownSkin = 0;
#if LCE_PLATFORM_SWITCH
        ownCape = 0;
#endif
        nextOwnCheck = 0;
        return;
    }
    const long long now = System::processTimeInMilliSecs();
#if LCE_PLATFORM_SWITCH
    const bool needsCosmetics = (!ownSkin || !ownCape) && !loadingOwnSkin && !loadingOwnCape;
#else
    const bool needsCosmetics = !ownSkin && !loadingOwnSkin;
#endif
    if (needsCosmetics && now >= nextOwnCheck)
    {
        loadingOwnSkin = WiiULiveSignIn::RequestApi("/me/cosmetics", NULL, [](long status, const std::string& response) {
            loadingOwnSkin = false;
            JsonValue json;
            if (status != 200 || !PcJsonParse(response, json))
                return;
#if !LCE_PLATFORM_SWITCH
            if (Field(json, L"skin").text != L"true")
            {
                nextOwnCheck = System::processTimeInMilliSecs() + 60000;
                return;
            }
#endif
            const JsonValue& value = Field(json, L"user_id");
            if (value.type != JsonValue::Number || value.text.empty() || value.text.size() > 10
                || value.text.find_first_not_of(L"0123456789") != std::wstring::npos)
                return;
            const std::string digits(value.text.begin(), value.text.end());
            const unsigned long long user = std::strtoull(digits.c_str(), NULL, 10);
            if (user == 0 || user > 0xffffffffULL)
                return;
#if LCE_PLATFORM_SWITCH
            nextOwnCheck = System::processTimeInMilliSecs() + 60000;
            if (!ownCape && Field(json, L"cape").text == L"true")
                DownloadOwnCape(static_cast<unsigned int>(user));
            if (!ownSkin && Field(json, L"skin").text == L"true")
#endif
                DownloadOwnSkin(static_cast<unsigned int>(user));
        });
        if (loadingOwnSkin)
            nextOwnCheck = now + 10000;
    }
    if (requestedAvatar || now < nextAvatarCheck)
        return;
    for (auto& item : avatars)
    {
        if (now < item.second.refreshAt || now - item.second.usedAt > 30000)
            continue;
        const unsigned int user = item.first;
        const std::string path = "/users/" + std::to_string(user) + "/skin";
        if (!WiiULiveSignIn::RequestApi(path.c_str(), NULL, [user](long status, const std::string& response) {
            requestedAvatar = 0;
            Avatar& avatar = avatars.at(user);
            const long long now = System::processTimeInMilliSecs();
            avatar.refreshAt = now + (status == 200 || status == 404 ? 60000 : 10000);
            if (status != 404 && (status != 200 || !IsSkin(response)))
                return;
            const std::string png = status == 200 ? response : std::string();
            if (avatar.png == png)
                return;
            avatarBytes -= avatar.png.size();
            while (avatarBytes + png.size() > kMaxAvatarBytes && EvictAvatar(user)) {}
            avatar.png = png;
            avatarBytes += png.size();
            avatar.revision = ++generation;
        }))
            return;
        requestedAvatar = user;
        nextAvatarCheck = now + 250;
        return;
    }
}

unsigned int PlayerSkinId() { return ownSkin; }
#if LCE_PLATFORM_SWITCH
unsigned int PlayerCapeId() { return ownCape; }
#endif
unsigned int AvatarGeneration() { return generation; }

const std::string* AvatarSkin(unsigned int user, std::string& revision)
{
    auto found = avatars.find(user);
    if (found == avatars.end())
    {
        if (avatars.size() >= kMaxAvatars && !EvictAvatar(user))
            return NULL;
        found = avatars.emplace(user, Avatar()).first;
    }
    Avatar& avatar = found->second;
    avatar.usedAt = System::processTimeInMilliSecs();
    if (avatar.png.empty())
        return NULL;
    revision = std::to_string(avatar.revision);
    return &avatar.png;
}
}
