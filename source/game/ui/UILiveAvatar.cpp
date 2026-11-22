#include "game/ui/UILiveAvatar.hpp"
#include "game/ui/UIScene.hpp"
#include "game/core/BufferedImage.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
#include "platform/wiiu/WiiULiveCosmetics.hpp"
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
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

#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    FacePng output = {};
    if (!EncodeFace(rgba.data(), output)) return std::wstring();
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
