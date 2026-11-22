#include "platform/pc/PcCrashReport.hpp"
#include "platform/pc/PcDiscord.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace PcCrashReport
{
namespace
{
std::string reportPath;
std::vector<std::pair<std::string, SectionWriter>> sections;
}

void Install(const std::string& logDirectory)
{
    reportPath = logDirectory + "/switch-crash.txt";
}

void AddSection(const char* title, SectionWriter writer)
{
    sections.emplace_back(title, writer);
}

void NameCurrentThread(const char*) {}

std::string Write(const char* reason)
{
    std::fprintf(stderr, "LCEReborn: %s\n", reason);
    FILE* file = std::fopen(reportPath.c_str(), "wb");
    if (file == nullptr)
        return std::string();
    std::fprintf(file, "LCEReborn: %s\n", reason);
    for (const auto& section : sections)
    {
        std::string text;
        section.second(text);
        std::fprintf(file, "\n%s\n%s\n", section.first.c_str(), text.c_str());
    }
    std::fclose(file);
    return reportPath;
}
}

void PcDiscordStart(const std::string&) {}
void PcDiscordSetPresence(const std::wstring&) {}
