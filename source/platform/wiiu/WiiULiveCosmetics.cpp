#include "platform/wiiu/WiiULiveCosmetics.hpp"

#include <cstdlib>
#include <cstring>
#include <map>

#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/System.hpp"
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"

namespace WiiULiveCosmetics
{
namespace
{
const size_t kMaxSkinBytes = 512 * 1024;
const unsigned int kClassic64x64SkinModel = 0x40000u;
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
long long nextOwnCheck = 0;
long long nextAvatarCheck = 0;

const JsonValue& Field(const JsonValue& root, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = root.members.find(name);
    return found == root.members.end() ? empty : found->second;
}

bool IsSkin(const std::string& bytes)
{
    const char header[] = "\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x40\0\0\0\x40";
    return bytes.size() >= 33 && bytes.size() <= kMaxSkinBytes && bytes.compare(0, sizeof(header) - 1, header, sizeof(header) - 1) == 0;
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
        const unsigned int hash = static_cast<unsigned int>((user * 2654435761ULL) & 0x7fffffffULL);
        const unsigned int id = 0x80000000u | (hash ? hash : 1u);
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
        // The texture and geometry reply carries this flag to the other players.
        _SkinAdjustments adjustments;
        adjustments.mAnimFlags = kClassic64x64SkinModel;
        gApp.SetSkinAdjustments(id, adjustments);
        ownSkin = id;
        GameSettings::SetPlayerSkin(0, id, false);
    });
}
}

void Tick()
{
    if (!WiiULiveSignIn::IsSignedIn())
    {
        ownSkin = 0;
        nextOwnCheck = 0;
        return;
    }
    const long long now = System::processTimeInMilliSecs();
    if (!ownSkin && !loadingOwnSkin && now >= nextOwnCheck)
    {
        loadingOwnSkin = WiiULiveSignIn::RequestApi("/me/cosmetics", NULL, [](long status, const std::string& response) {
            loadingOwnSkin = false;
            JsonValue json;
            if (status != 200 || !PcJsonParse(response, json))
                return;
            if (Field(json, L"skin").text != L"true")
            {
                nextOwnCheck = System::processTimeInMilliSecs() + 60000;
                return;
            }
            const JsonValue& value = Field(json, L"user_id");
            if (value.type != JsonValue::Number || value.text.empty() || value.text.size() > 10
                || value.text.find_first_not_of(L"0123456789") != std::wstring::npos)
                return;
            const std::string digits(value.text.begin(), value.text.end());
            const unsigned long long user = std::strtoull(digits.c_str(), NULL, 10);
            if (user > 0 && user <= 0xffffffffULL)
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
