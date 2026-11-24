#include "game/ui/UILiveAvatar.hpp"
#include "game/ui/UIScene.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/System.hpp"
#include "game/world/generation/Player.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
#include "platform/wiiu/WiiULiveCosmetics.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>
#include <png.h>
namespace {
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
struct FacePng
{
    unsigned char bytes[512];
    size_t size;
};

void WriteFacePng(png_structp png, png_bytep bytes, png_size_t size)
{
    FacePng& output = *static_cast<FacePng*>(png_get_io_ptr(png));
    if (size > sizeof(output.bytes) - output.size)
        png_error(png, "Face PNG is too large");
    std::memcpy(output.bytes + output.size, bytes, size);
    output.size += size;
}

bool EncodeFace(unsigned char* rgba, FacePng& output)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) return false;
    png_infop info = png_create_info_struct(png);
    if (info == NULL || setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_set_write_fn(png, &output, WriteFacePng, NULL);
    png_set_IHDR(png, info, 8, 8, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int row = 0; row < 8; ++row)
        png_write_row(png, rgba + row * 8 * 4);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return true;
}
#else
bool ReadSkin(const std::wstring& name, std::vector<unsigned char>& data)
{
    FILE* file = PcLiveBridge::Open(name.c_str(), L"rb");
    if (file == NULL) return false;
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length <= 0 || length > 512 * 1024 || std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }
    data.resize(static_cast<size_t>(length));
    const bool ok = std::fread(&data[0], 1, data.size(), file) == data.size();
    std::fclose(file);
    return ok;
}
#endif
bool ExtractFace(BufferedImage& skin, unsigned char* rgba)
{
    const unsigned int scale = skin.getWidth() / 64;
    if (skin.getData() == NULL || scale == 0 || scale > 8
        || skin.getWidth() != scale * 64 || skin.getHeight() < scale * 16)
        return false;
    const unsigned int size = 8 * scale;
    std::vector<int> basePixels(size * size), hatPixels(size * size);
    arrayWithLength<int> base(basePixels.data(), size * size), hat(hatPixels.data(), size * size);
    skin.getRGB(size, size, size, size, base, 0, size, 0);
    skin.getRGB(size * 5, size, size, size, hat, 0, size, 0);
    for (int pixel = 0; pixel < 64; ++pixel)
    {
        const unsigned int sample = (pixel / 8 * size + pixel % 8) * scale;
        const unsigned int under = static_cast<unsigned int>(base[sample]);
        const unsigned int over = static_cast<unsigned int>(hat[sample]);
        const unsigned int alpha = over >> 24;
        const unsigned int underAlpha = under >> 24;
        const unsigned int outAlpha = alpha + (underAlpha * (255 - alpha) + 127) / 255;
        unsigned int channels[3] = {(under >> 16) & 255, (under >> 8) & 255, under & 255};
        const unsigned int top[3] = {(over >> 16) & 255, (over >> 8) & 255, over & 255};
        if (outAlpha != 0)
            for (int channel = 0; channel < 3; ++channel)
                channels[channel] = (top[channel] * alpha + channels[channel] * underAlpha * (255 - alpha) / 255
                                     + outAlpha / 2) / outAlpha;
        rgba[pixel * 4 + 0] = static_cast<unsigned char>(channels[0]);
        rgba[pixel * 4 + 1] = static_cast<unsigned char>(channels[1]);
        rgba[pixel * 4 + 2] = static_cast<unsigned char>(channels[2]);
        rgba[pixel * 4 + 3] = static_cast<unsigned char>(outAlpha);
    }
    return true;
}

std::string SelectedFaceHex(unsigned int skinId)
{
    std::unique_ptr<BufferedImage> skin;
    if (skinId < 19)
    {
        skin.reset(Minecraft::GetInstance()->mTextures->readIndexedImage(Player::getDefaultSkinTexture(skinId)));
    }
    else
    {
        unsigned char* bytes = NULL;
        unsigned int size = 0;
        gApp.GetMemFileDetails(CMinecraftApp::getSkinPathFromId(skinId), &bytes, &size);
        if (bytes != NULL && size != 0 && size <= 512 * 1024)
            skin.reset(new BufferedImage(bytes, size));
    }
    unsigned char rgba[8 * 8 * 4];
    if (!skin || !ExtractFace(*skin, rgba))
        return std::string();
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(sizeof(rgba) * 2);
    for (const unsigned char channel : rgba)
    {
        hex.push_back(digits[channel >> 4]);
        hex.push_back(digits[channel & 15]);
    }
    return hex;
}
}

void PublishLiveGameAvatar(int profile)
{
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (!WiiULiveSignIn::IsSignedIn()) return;
#else
    if (!PcLiveBridge::Enabled()) return;
#endif
    static long long checkAt = 0;
    const long long now = System::processTimeInMilliSecs();
    if (now < checkAt) return;
    checkAt = now + 2000;

    const unsigned int skinId = GameSettings::GetSelectedPlayerSkinId(profile);
    static long long publishedSkin = -1;
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    static std::string publishedAccount;
    static bool pending = false;
    const std::string account = WiiULiveSignIn::PlayerUuid();
    if (account.empty() || pending || (account == publishedAccount && publishedSkin == skinId)) return;
#else
    if (publishedSkin == skinId) return;
#endif
    const std::string hex = SelectedFaceHex(skinId);
    if (hex.empty()) return;
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    const std::string body = "{\"pixels\":\"" + hex + "\"}";
    pending = WiiULiveSignIn::RequestApi("/me/avatar", body.c_str(), [account, skinId](long status, const std::string&) {
        pending = false;
        if (status == 200)
        {
            publishedSkin = skinId;
            publishedAccount = account;
        }
        else
        {
            checkAt = System::processTimeInMilliSecs() + 60000;
        }
    });
#else
    FILE* file = PcLiveBridge::Open(L"game-avatar.tmp", L"wb");
    if (file == NULL) return;
    const bool written = std::fwrite(hex.data(), 1, hex.size(), file) == hex.size();
    if (std::fclose(file) == 0 && written && PcLiveBridge::Rename(L"game-avatar.tmp", L"game-avatar.hex"))
        publishedSkin = skinId;
#endif
}

std::wstring RegisterLiveFaceIcon(UIScene& scene, unsigned int id, const std::string& revision)
{
    std::vector<unsigned char> bytes;
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    (void)revision;
    std::string skinRevision;
    const std::string* png = WiiULiveCosmetics::AvatarSkin(id, skinRevision);
    if (png == NULL) return std::wstring();
#else
    const std::string& skinRevision = revision;
#endif
    wchar_t textureName[96];
    std::swprintf(textureName, 96, L"LiveFace_%llx_%u_%ls",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&scene)), id,
        std::wstring(skinRevision.begin(), skinRevision.end()).c_str());
    if (scene.hasRegisteredSubstitutionTexture(textureName)) return textureName;

#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    bytes.assign(png->begin(), png->end());
#else
    wchar_t fileName[64];
    std::swprintf(fileName, 64, L"friend-%u.skin.png", id);
    if (!ReadSkin(fileName, bytes)) return std::wstring();
#endif
    BufferedImage skin(bytes.data(), static_cast<unsigned int>(bytes.size()));
    unsigned char rgba[8 * 8 * 4];
    if (!ExtractFace(skin, rgba)) return std::wstring();

#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    FacePng output = {};
    if (!EncodeFace(rgba, output)) return std::wstring();
    unsigned char* textureBytes = new unsigned char[output.size];
    std::memcpy(textureBytes, output.bytes, output.size);
    scene.registerSubstitutionTexture(textureName, textureBytes, static_cast<unsigned int>(output.size), true);
#else
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = 8;
    image.height = 8;
    image.format = PNG_FORMAT_RGBA;
    FILE* output = PcLiveBridge::Open(L"friend-face.tmp.png", L"wb");
    if (output == NULL) return std::wstring();
    const bool written = png_image_write_to_stdio(&image, output, 0, &rgba[0], 8 * 4, NULL, NULL, NULL) != 0;
    std::fclose(output);
    if (!written) { PcLiveBridge::Remove(L"friend-face.tmp.png"); return std::wstring(); }
    std::vector<unsigned char> encoded;
    const bool read = ReadSkin(L"friend-face.tmp.png", encoded);
    PcLiveBridge::Remove(L"friend-face.tmp.png");
    if (!read || encoded.empty()) return std::wstring();
    unsigned char* textureBytes = new unsigned char[encoded.size()];
    std::memcpy(textureBytes, &encoded[0], encoded.size());
    scene.registerSubstitutionTexture(textureName, textureBytes, static_cast<unsigned int>(encoded.size()), true);
#endif
    return textureName;
}
