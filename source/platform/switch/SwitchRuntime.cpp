#include "platform/switch/SwitchRuntime.hpp"
#include "game/core/System.hpp"
#include "game/core/VideoSettings.hpp"
#include "platform/pc/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
const Logger kPerformanceLog("Switch performance");
constexpr std::uint64_t kReportIntervalNs = 10000000000ULL;
constexpr double kNsPerMs = 1000000.0;
// Written and reported by the chunk update thread.
unsigned int sRebuiltChunks = 0;
std::uint64_t sMeshTotal = 0, sMeshMax = 0;
}

void SwitchRecordFrame(std::uint64_t frameNs, std::uint64_t presentNs)
{
    // Called only by the main thread.
    constexpr unsigned int lastBucket = 100;
    static unsigned int frameBuckets[lastBucket + 1] = {};
    static std::uint64_t frameTotal = 0, frameMax = 0, presentTotal = 0;
    static unsigned int samples = 0, over20Ms = 0, over33Ms = 0;
    static int distance = VideoSettings::GetRenderDistance();
    const int currentDistance = VideoSettings::GetRenderDistance();
    if (distance != currentDistance)
    {
        distance = currentDistance;
        frameTotal = frameMax = presentTotal = 0;
        samples = over20Ms = over33Ms = 0;
        std::fill_n(frameBuckets, lastBucket + 1, 0);
    }

    ++samples;
    if (frameNs > 20000000ULL)
        ++over20Ms;
    if (frameNs > 33333333ULL)
        ++over33Ms;
    const auto bucket = std::min<std::uint64_t>((frameNs + 999999) / 1000000, lastBucket);
    ++frameBuckets[bucket];
    frameTotal += frameNs;
    frameMax = std::max(frameMax, frameNs);
    presentTotal += presentNs;
    if (frameTotal < kReportIntervalNs)
        return;

    const auto threshold = (samples * 95 + 99) / 100;
    unsigned int cumulative = 0;
    unsigned int p95 = 0;
    while (p95 < lastBucket && (cumulative += frameBuckets[p95]) < threshold)
        ++p95;

    LOG_INFO(kPerformanceLog,
             "distance=%d frames=%u fps=%.1f over_20ms=%u over_33ms=%u frame_ms_avg=%.2f frame_ms_p95_bucket=%u "
             "frame_ms_max=%.2f work_ms_avg=%.2f present_ms_avg=%.2f",
             distance, samples, samples * 1000000000.0 / frameTotal, over20Ms, over33Ms,
             frameTotal / (samples * kNsPerMs), p95, frameMax / kNsPerMs,
             (frameTotal - presentTotal) / (samples * kNsPerMs), presentTotal / (samples * kNsPerMs));
    frameTotal = frameMax = presentTotal = 0;
    samples = over20Ms = over33Ms = 0;
    std::fill_n(frameBuckets, lastBucket + 1, 0);
}

void SwitchRecordChunkMesh(std::uint64_t elapsedNs)
{
    ++sRebuiltChunks;
    sMeshTotal += elapsedNs;
    sMeshMax = std::max(sMeshMax, elapsedNs);
}

void SwitchRecordChunkBatch(std::uint64_t elapsedNs, unsigned int passes)
{
    // Called only by the chunk update thread.
    static std::uint64_t reportAt = System::processTimeInNanoSecs() + kReportIntervalNs;
    static std::uint64_t total = 0, maximum = 0;
    static unsigned int batches = 0, totalPasses = 0;
    ++batches;
    totalPasses += passes;
    total += elapsedNs;
    maximum = std::max(maximum, elapsedNs);
    const auto now = System::processTimeInNanoSecs();
    if (now < reportAt)
        return;
    LOG_INFO(kPerformanceLog,
             "chunk_batches=%u passes=%u rebuilt=%u batch_ms_avg=%.2f batch_ms_max=%.2f mesh_ms_avg=%.2f mesh_ms_max=%.2f",
             batches, totalPasses, sRebuiltChunks, total / (batches * kNsPerMs), maximum / kNsPerMs,
             sRebuiltChunks ? sMeshTotal / (sRebuiltChunks * kNsPerMs) : 0.0, sMeshMax / kNsPerMs);
    reportAt = now + kReportIntervalNs;
    total = maximum = 0;
    batches = totalPasses = sRebuiltChunks = 0;
    sMeshTotal = sMeshMax = 0;
}

bool SwitchEnterGameDirectory()
{
    // Mount before changing directories so libnx can resolve a relative NRO path.
    if (R_FAILED(romfsInit()))
        romfsMountFromCurrentProcess("romfs");

    // A single-file install may be launched directly from /switch.
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/LCEReborn", 0777);
    return chdir("sdmc:/switch/LCEReborn") == 0;
}

void SwitchLogStartupStage(const char* stage)
{
    static const Logger log("Switch startup");
    LOG_INFO(log, "%s", stage);
    Log::Flush();
}

void SwitchShowStartupError(const std::string& message)
{
    consoleInit(nullptr);
    std::printf("LCEReborn\n\n%s\n\nPress A to exit.\n", message.c_str());
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_A)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

bool SwitchMemoryUsage(unsigned long long* used, unsigned long long* total)
{
    u64 processUsed;
    u64 processTotal;
    if (R_FAILED(svcGetInfo(&processUsed, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))
        || R_FAILED(svcGetInfo(&processTotal, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
        return false;
    *used = processUsed;
    *total = processTotal;
    return true;
}
