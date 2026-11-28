#include "platform/pc/PcLaunchOptions.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

static PcLaunchOptions sOptions;

static std::vector<std::string> CommandLineArguments(int argc, char** argv)
{
    std::vector<std::string> arguments;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == NULL)
        return arguments;
    for (int index = 1; index < count; ++index)
    {
        int length = WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, NULL, 0, NULL, NULL);
        std::string argument(length > 0 ? length - 1 : 0, '\0');
        if (length > 1)
            WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, &argument[0], length, NULL, NULL);
        arguments.push_back(argument);
    }
    LocalFree(wide);
#else
    for (int index = 1; index < argc; ++index)
        arguments.push_back(argv[index]);
#endif
    return arguments;
}

static void PrintUsage(std::FILE* stream, PcProgram program)
{
    if (program == PcProgram::Server)
    {
        std::fputs("Usage: LCERebornServer [options]\n"
                   "  --content-dir <dir>         game content (default: assets in the working folder)\n"
                   "  --village-templates-dir <dir>  local NBT and village-pools.tsv (default: custom-villages in content)\n"
                   "  --save-dir <dir>            worlds, server.properties and server-icon.png (default: saves in the\n"
                   "                              working folder)\n"
                   "  --packs-dir <dir>           packs to load (default: packs in the working folder)\n"
                   "  --log-dir <dir>             logs (default: logs in the working folder)\n"
                   "  --help                      show this message\n",
                   stream);
        return;
    }
    std::fputs("Usage: LCEReborn [options]\n"
               "  --username <name>           player name\n"
               "  --uuid <uuid>               player identity, 32 hex digits (default: made from the name)\n"
               "  --content-dir <dir>         game content (default: assets in the working folder)\n"
               "  --village-templates-dir <dir>  local NBT and village-pools.tsv (default: custom-villages in content)\n"
               "  --save-dir <dir>            saves and settings (default: saves in the working folder)\n"
               "  --packs-dir <dir>           packs to load (default: packs in the working folder)\n"
               "  --resource-dir <dir>        PC interface art (default: resources in the working folder)\n"
               "  --language <code>           language, such as en-GB or ja (default: the system language)\n"
               "  --log-dir <dir>             logs (default: logs in the working folder)\n"
               "  --split-screen <on|off>     other controllers can join as more players (default: on)\n"
               "  --join-prompt <on|off>      prompt other controllers to join when a world loads (default: off)\n"
               "  --world <folder>            load this save from the saves folder's games folder at start\n"
               "  --join <address>            join the game at this address (host or host:port) at start\n"
               "  --discord-app <id>          show what you are doing in Discord, through this Discord application\n"
               "  --profiles <file>           the launcher's profiles, offered to split-screen players who join\n",
               stream);
#if LCE_DEBUG_BUILD
    std::fputs("  --position <x,y,z>          move the player here once a world has loaded\n"
               "  --rotation <yaw,pitch>      turn the player to face this way once a world has loaded\n"
               "  --capture-frames <count>    save this many frames as PNG files, then quit\n"
               "  --capture-delay <seconds>   wait this long in the world before capturing (default: 10)\n"
               "  --capture-dir <dir>         where captured frames go (default: capture in the working folder)\n",
               stream);
#endif
    std::fputs("  --help                      show this message\n", stream);
}

bool PcParseLaunchOptions(int argc, char** argv, int& exitCode, PcProgram program)
{
    exitCode = 1;
    const std::vector<std::string> arguments = CommandLineArguments(argc, argv);

    for (std::vector<std::string>::size_type index = 0; index < arguments.size(); ++index)
    {
        std::string name = arguments[index];
        std::string value;
        bool hasValue = false;

        const std::string::size_type equals = name.find('=');
        if (name.compare(0, 2, "--") == 0 && equals != std::string::npos)
        {
            value = name.substr(equals + 1);
            name.resize(equals);
            hasValue = true;
        }

        if (name == "--help" || name == "-h")
        {
            PrintUsage(stdout, program);
            exitCode = 0;
            return false;
        }

        if (program == PcProgram::Server && name != "--content-dir" && name != "--save-dir" && name != "--packs-dir"
            && name != "--log-dir" && name != "--village-templates-dir")
        {
            std::fprintf(stderr, "Unknown option: %s\n", arguments[index].c_str());
            PrintUsage(stderr, program);
            return false;
        }

        std::string* target = NULL;
        bool* flag = NULL;
#if LCE_DEBUG_BUILD
        int* count = NULL;
        double* seconds = NULL;
        std::vector<double>* list = NULL;
        size_t listSize = 0;
#endif
        if (name == "--split-screen")
            flag = &sOptions.splitScreen;
        else if (name == "--join-prompt")
            flag = &sOptions.joinPrompt;
        else if (name == "--username")
            target = &sOptions.username;
        else if (name == "--content-dir")
            target = &sOptions.contentDir;
        else if (name == "--village-templates-dir")
            target = &sOptions.villageTemplatesDir;
        else if (name == "--save-dir")
            target = &sOptions.saveDir;
        else if (name == "--packs-dir")
            target = &sOptions.packsDir;
        else if (name == "--resource-dir")
            target = &sOptions.resourceDir;
        else if (name == "--language")
            target = &sOptions.language;
        else if (name == "--log-dir")
            target = &sOptions.logDir;
        else if (name == "--uuid")
            target = &sOptions.uuid;
        else if (name == "--world")
            target = &sOptions.world;
        else if (name == "--join")
            target = &sOptions.join;
        else if (name == "--discord-app")
            target = &sOptions.discordApp;
        else if (name == "--profiles")
            target = &sOptions.profiles;
#if LCE_DEBUG_BUILD
        else if (name == "--position")
        {
            list = &sOptions.position;
            listSize = 3;
        }
        else if (name == "--rotation")
        {
            list = &sOptions.rotation;
            listSize = 2;
        }
        else if (name == "--capture-dir")
            target = &sOptions.captureDir;
        else if (name == "--capture-frames")
            count = &sOptions.captureFrames;
        else if (name == "--capture-delay")
            seconds = &sOptions.captureDelay;
#endif
        else
        {
            std::fprintf(stderr, "Unknown option: %s\n", arguments[index].c_str());
            PrintUsage(stderr, program);
            return false;
        }

        if (!hasValue)
        {
            if (index + 1 >= arguments.size())
            {
                std::fprintf(stderr, "Missing value for %s\n", name.c_str());
                PrintUsage(stderr, program);
                return false;
            }
            value = arguments[++index];
        }

        if (value.empty())
        {
            std::fprintf(stderr, "Empty value for %s\n", name.c_str());
            return false;
        }

        if (flag != NULL)
        {
            if (value == "on")
                *flag = true;
            else if (value == "off")
                *flag = false;
            else
            {
                std::fprintf(stderr, "%s must be on or off, not %s\n", name.c_str(), value.c_str());
                return false;
            }
            continue;
        }

#if LCE_DEBUG_BUILD
        if (list != NULL)
        {
            list->clear();
            const char* cursor = value.c_str();
            for (;;)
            {
                char* end = NULL;
                const double number = std::strtod(cursor, &end);
                if (end == cursor)
                    break;
                list->push_back(number);
                cursor = end;
                if (*cursor != ',')
                    break;
                ++cursor;
            }
            if (list->size() != listSize || *cursor != '\0')
            {
                std::fprintf(stderr, "%s must be %u numbers separated by commas, not %s\n", name.c_str(),
                             static_cast<unsigned int>(listSize), value.c_str());
                return false;
            }
            continue;
        }

        if (count != NULL || seconds != NULL)
        {
            char* end = NULL;
            const double number = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || number < 0.0 || (count != NULL && number != (int)number))
            {
                std::fprintf(stderr, "%s must be a %s, not %s\n", name.c_str(),
                             count != NULL ? "whole number" : "number of seconds", value.c_str());
                return false;
            }
            if (count != NULL)
                *count = (int)number;
            else
                *seconds = number;
            continue;
        }
#endif

        *target = value;
    }

    if (!sOptions.uuid.empty())
    {
        std::string digits;
        for (char character : sOptions.uuid)
        {
            if (character == '-')
                continue;
            if (!std::isxdigit(static_cast<unsigned char>(character)))
            {
                digits.clear();
                break;
            }
            digits += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (digits.size() != 32)
        {
            std::fprintf(stderr, "--uuid must be 32 hexadecimal digits, not %s\n", sOptions.uuid.c_str());
            return false;
        }
        sOptions.uuid = digits;
    }

    return true;
}

const PcLaunchOptions& PcGetLaunchOptions()
{
    return sOptions;
}
