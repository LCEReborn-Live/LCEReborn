#ifndef PLATFORM_SWITCH_RUNTIME_HPP
#define PLATFORM_SWITCH_RUNTIME_HPP

#include <cstdint>
#include <string>

bool SwitchEnterGameDirectory();
void SwitchLogStartupStage(const char* stage);
void SwitchShowStartupError(const std::string& message);
bool SwitchMemoryUsage(unsigned long long* used, unsigned long long* total);
void SwitchRecordFrame(std::uint64_t frameNs, std::uint64_t presentNs);
void SwitchRecordChunkMesh(std::uint64_t elapsedNs);
void SwitchRecordChunkBatch(std::uint64_t elapsedNs, unsigned int passes);

#endif
