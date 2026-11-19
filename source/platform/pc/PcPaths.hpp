#ifndef PLATFORM_PC_PCPATHS_HPP
#define PLATFORM_PC_PCPATHS_HPP

#include <string>

void PcSetContentRoot(const std::string& path);
void PcSetSaveRoot(const std::string& path);
const std::string& PcContentRoot();
const std::string& PcSaveRoot();
const std::string& PcPacksRoot();
const std::string& PcBundledPacksRoot();
const std::string& PcResourceRoot();
bool PcCheckGameFolders(std::string& message, bool needInterfaceArt = true);
void PcOpenFolder(const std::string& path);

#endif
