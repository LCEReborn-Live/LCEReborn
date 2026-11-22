#ifndef PLATFORM_SWITCH_RUNTIME_HPP
#define PLATFORM_SWITCH_RUNTIME_HPP

#include <string>

bool SwitchEnterGameDirectory();
void SwitchLogStartupStage(const char* stage);
void SwitchShowStartupError(const std::string& message);
bool SwitchMemoryUsage(unsigned long long* used, unsigned long long* total);

#endif
