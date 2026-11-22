#include "platform/pc/PcPaths.hpp"

#include <filesystem>

#include <SDL3/SDL.h>

#include "platform/pc/PcLaunchOptions.hpp"

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
