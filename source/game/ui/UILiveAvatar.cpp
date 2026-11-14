#include "game/ui/UILiveAvatar.hpp"
#include "game/ui/UIScene.hpp"
#include "game/core/BufferedImage.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <png.h>
namespace {
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
}
std::wstring RegisterLiveFaceIcon(UIScene& scene, unsigned int id, const std::string& revision)
{
    wchar_t textureName[96];
    std::swprintf(textureName, 96, L"LiveFace_%llx_%u_%ls",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&scene)), id,
        std::wstring(revision.begin(), revision.end()).c_str());
    if (scene.hasRegisteredSubstitutionTexture(textureName)) return textureName;

    wchar_t fileName[64];
    std::swprintf(fileName, 64, L"friend-%u.skin.png", id);
    std::vector<unsigned char> bytes;
    if (!ReadSkin(fileName, bytes)) return std::wstring();
    BufferedImage skin(&bytes[0], static_cast<unsigned int>(bytes.size()));
    if (skin.getData() == NULL || skin.getWidth() < 64 || skin.getHeight() < 16) return std::wstring();
    arrayWithLength<int> base(64, false), hat(64, false);
    skin.getRGB(8, 8, 8, 8, base, 0, 8, 0);
    skin.getRGB(40, 8, 8, 8, hat, 0, 8, 0);
    std::vector<unsigned char> rgba(8 * 8 * 4);
    for (int pixel = 0; pixel < 64; ++pixel)
    {
        const unsigned int under = static_cast<unsigned int>(base[pixel]);
        const unsigned int over = static_cast<unsigned int>(hat[pixel]);
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
    return textureName;
}
