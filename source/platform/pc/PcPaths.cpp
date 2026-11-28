#include "platform/pc/PcPaths.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include <SDL3/SDL.h>
#include <zlib.h>

#include "game/core/StringConversion.hpp"
#include "platform/pc/PcLaunchOptions.hpp"
#include "platform/pc/Log.hpp"

static std::string RootFromOption(const std::string& option, const char* fallback)
{
#if LCE_PLATFORM_SWITCH
    // devoptab handles paths relative to the SD working directory and sdmc:/ device paths.
    return option.empty() ? fallback : option;
#else
    if (!option.empty())
        return std::filesystem::absolute(std::filesystem::u8path(option)).generic_string();
    return (std::filesystem::current_path() / fallback).generic_string();
#endif
}

static std::string ResourceRootFromOption(const std::string& option)
{
    std::string root = RootFromOption(option, "resources");
    std::error_code error;
#if LCE_PLATFORM_SWITCH
    if (option.empty() && std::filesystem::is_directory("romfs:/resources/ui", error))
        return "romfs:/resources";
#endif
    if (std::filesystem::is_directory(std::filesystem::path(root) / "pc", error))
        root += "/pc";
    return root;
}

static std::string& ContentRoot()
{
#if LCE_PLATFORM_SWITCH
    static std::string root = []() {
        const std::string& option = PcGetLaunchOptions().contentDir;
        std::error_code error;
        if (option.empty() && std::filesystem::is_regular_file("romfs:/assets/Common/Media/MediaWiiU.arc", error))
            return std::string("romfs:/assets");
        return RootFromOption(option, "assets");
    }();
#else
    static std::string root = RootFromOption(PcGetLaunchOptions().contentDir, "assets");
#endif
    return root;
}

static std::string& SaveRoot()
{
    static std::string root = RootFromOption(PcGetLaunchOptions().saveDir, "saves");
    return root;
}

void PcSetContentRoot(const std::string& path)
{
    ContentRoot() = std::filesystem::absolute(path).generic_string();
}

void PcSetSaveRoot(const std::string& path)
{
    SaveRoot() = std::filesystem::absolute(path).generic_string();
}

const std::string& PcContentRoot()
{
    return ContentRoot();
}

const std::string& PcSaveRoot()
{
    return SaveRoot();
}

const std::string& PcPacksRoot()
{
    static std::string root = RootFromOption(PcGetLaunchOptions().packsDir, "packs");
    return root;
}

const std::string& PcBundledPacksRoot()
{
    static std::string root = []() {
        std::filesystem::path installRoot = std::filesystem::u8path(PcResourceRoot());
        if (installRoot.filename() == "pc")
            installRoot = installRoot.parent_path();
        if (installRoot.filename() == "resources")
            installRoot = installRoot.parent_path();
        return (installRoot / "packs").lexically_normal().generic_string();
    }();
    return root;
}

const std::string& PcResourceRoot()
{
    static std::string root = ResourceRootFromOption(PcGetLaunchOptions().resourceDir);
    return root;
}

std::wstring PcVillageTemplateRoot()
{
    const auto& configured = PcGetLaunchOptions().villageTemplatesDir;
    if (configured.empty())
        return (std::filesystem::u8path(PcContentRoot()) / "custom-villages").lexically_normal().generic_wstring();
    std::error_code error;
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(configured), error);
    if (error)
        return std::filesystem::u8path(configured).generic_wstring();
    return absolute.lexically_normal().generic_wstring();
}

std::vector<std::wstring> PcListVillageTemplates(const std::wstring& root)
{
    std::vector<std::wstring> names;
    std::error_code error;
    constexpr Logger log("VillageTemplates");
    const auto rootUtf8 = convWstringToString(root);
    if (!std::filesystem::is_directory(root, error))
    {
        if (!PcGetLaunchOptions().villageTemplatesDir.empty())
            LOG_WARN(log, "Template directory is unavailable: %s", rootUtf8.c_str());
        return names;
    }
    error.clear();
    const std::filesystem::path directory(root);
    for (std::filesystem::recursive_directory_iterator file(directory,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && file != end; file.increment(error))
    {
        std::error_code statusError;
        const auto status = file->symlink_status(statusError);
        if (statusError || !std::filesystem::is_regular_file(status))
            continue;
        auto extension = file->path().extension().wstring();
        for (auto& ch : extension)
            if (ch >= L'A' && ch <= L'Z')
                ch += L'a' - L'A';
        if (extension == L".nbt")
            names.push_back(file->path().lexically_relative(directory).generic_wstring());
    }
    if (error)
        LOG_WARN(log, "Could not finish scanning template directory %s: %s", rootUtf8.c_str(), error.message().c_str());
    else if (names.empty() && !PcGetLaunchOptions().villageTemplatesDir.empty())
        LOG_INFO(log, "No NBT templates found in %s", rootUtf8.c_str());
    return names;
}

std::string PcReadVillagePoolConfiguration(const std::wstring& root)
{
    constexpr Logger log("VillageTemplates");
    const auto path = std::filesystem::path(root) / "village-pools.tsv";
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < 0 || size > 1024 * 1024)
    {
        LOG_WARN(log, "Cannot read village-pools.tsv in %s", convWstringToString(root).c_str());
        return {};
    }
    std::string text(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!text.empty() && !file.read(&text[0], size))
    {
        LOG_WARN(log, "Cannot read village-pools.tsv in %s", convWstringToString(root).c_str());
        return {};
    }
    return text;
}

std::vector<unsigned char> PcReadVillageTemplate(const std::wstring& root, const std::wstring& relativePath)
{
    constexpr unsigned int maxSize = 8 * 1024 * 1024;
    std::ifstream file(std::filesystem::path(root) / relativePath, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > maxSize)
        return {};

    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};
    if (bytes.size() < 2 || bytes[0] != 0x1f || bytes[1] != 0x8b)
        return bytes;
    if (bytes.size() < 18)
        return {};

    const auto* footer = bytes.data() + bytes.size() - 4;
    const auto originalSize = static_cast<unsigned int>(footer[0]) | (static_cast<unsigned int>(footer[1]) << 8)
        | (static_cast<unsigned int>(footer[2]) << 16) | (static_cast<unsigned int>(footer[3]) << 24);
    if (originalSize == 0 || originalSize > maxSize)
        return {};

    std::vector<unsigned char> raw(originalSize);
    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
        return {};
    stream.next_in = bytes.data();
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = raw.data();
    stream.avail_out = originalSize;
    const bool complete = inflate(&stream, Z_FINISH) == Z_STREAM_END && stream.avail_in == 0
        && stream.total_out == originalSize;
    inflateEnd(&stream);
    return complete ? std::move(raw) : std::vector<unsigned char>{};
}

static std::string DisplayPath(const std::string& path)
{
    return std::filesystem::path(path).make_preferred().u8string();
}

bool PcCheckGameFolders(std::string& message, bool needInterfaceArt)
{
    std::error_code error;
    const PcLaunchOptions& options = PcGetLaunchOptions();
    const std::string workingFolder = DisplayPath(std::filesystem::current_path(error).generic_string());

    const std::filesystem::path content(PcContentRoot());
    if (!std::filesystem::is_directory(content, error))
    {
#if LCE_PLATFORM_SWITCH
        if (options.contentDir.empty())
        {
            message = "This NRO needs the prepared base game and Update 688. "
                      "Copy assets/ to /switch/LCEReborn on the SD card, or use a full NRO.";
            return false;
        }
#endif
        message = "The game content folder was not found:\n" + DisplayPath(PcContentRoot()) + "\n\n";
        if (options.contentDir.empty())
            message += "With no --content-dir, the game looks for assets in the working folder (" + workingFolder
                       + "). Start it from the folder that holds assets, or pass --content-dir <folder>.";
        else
            message += "Check the folder given to --content-dir.";
        message += "\nThe content is made from your own game dumps by tools/prepare.py.";
        return false;
    }

    const std::filesystem::path archive = content / "Common" / "Media" / "MediaWiiU.arc";
    if (!std::filesystem::is_regular_file(archive, error))
    {
        message = "The game content folder is missing Common/Media/MediaWiiU.arc:\n" + DisplayPath(PcContentRoot())
                  + "\n\nThis folder is not prepared game content, or it is incomplete. Run tools/prepare.py with "
                    "your base game and update dumps.";
        return false;
    }

    const std::filesystem::path interfaceArt = std::filesystem::path(PcResourceRoot()) / "ui";
    if (needInterfaceArt && !std::filesystem::is_directory(interfaceArt, error))
    {
        message = "The PC interface art was not found:\n" + DisplayPath(interfaceArt.generic_string()) + "\n\n";
        if (options.resourceDir.empty())
            message += "With no --resource-dir, the game looks for resources in the working folder (" + workingFolder
                       + "). Start it from the folder that holds resources, or pass --resource-dir <folder>.";
        else
            message += "Check the folder given to --resource-dir.";
        return false;
    }

    const std::filesystem::path saves(PcSaveRoot());
    std::filesystem::create_directories(saves, error);
    if (error || !std::filesystem::is_directory(saves, error))
    {
        message = "The save folder could not be created:\n" + DisplayPath(PcSaveRoot()) + "\n\n" + error.message()
                  + "\nPass --save-dir <folder> to save somewhere else.";
        return false;
    }

    return true;
}

void PcOpenFolder(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::u8path(path);
    std::filesystem::create_directories(folder, error);
    const std::string url = "file:///" + folder.generic_u8string();
    SDL_OpenURL(url.c_str());
}
