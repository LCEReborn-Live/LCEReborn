#ifndef PLATFORM_PC_PCCURSORMOTION_HPP
#define PLATFORM_PC_PCCURSORMOTION_HPP

#include <algorithm>
#include <cmath>

namespace PcCursorMotion
{
inline float FrameScale(long long elapsedNanoseconds)
{
    // Compensate ordinary loading stalls, but avoid a large jump after suspension.
    return static_cast<float>(std::max(0LL, std::min(250000000LL, elapsedNanoseconds))) * 60.0e-9f;
}

inline float AccelerationIntegral(float ticks)
{
    return ticks < 7.0f ? ticks * ticks / 14.0f : ticks - 3.5f;
}

inline void Advance(float inputX, float inputY, float frameScale, float viewportSpeed,
                    float& heldTicks, float& x, float& y)
{
    // Integrate the acceleration ramp so a long frame covers the same distance
    // as several short frames. Keep fractional coordinates in the scene state.
    const float nextTicks = heldTicks + frameScale;
    const float distance = 13.0f * viewportSpeed
                           * (AccelerationIntegral(nextTicks) - AccelerationIntegral(heldTicks));
    heldTicks = std::min(nextTicks, 15.0f);
    x += inputX * std::fabs(inputX) * distance;
    y -= inputY * std::fabs(inputY) * distance;
}
}

#endif
