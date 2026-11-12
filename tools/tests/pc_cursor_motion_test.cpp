#include "platform/pc/PcCursorMotion.hpp"

#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static void CheckMovement(int fps, float stick, float viewportSpeed)
{
    for (int sign = -1; sign <= 1; sign += 2)
    {
        float held = 0.0f;
        float x = 1000.0f;
        float y = 1000.0f;
        for (int frame = 0; frame < fps; ++frame)
            PcCursorMotion::Advance(sign * stick, sign * stick, 60.0f / fps,
                                    viewportSpeed, held, x, y);
        const float expected = sign * stick * stick * 13.0f * viewportSpeed * (60.0f - 3.5f);
        Check(std::fabs((x - 1000.0f) - expected) < 0.05f, "Horizontal speed depends on FPS");
        Check(std::fabs((y - 1000.0f) + expected) < 0.05f, "Vertical speed depends on FPS");
        Check(std::fabs(x + y - 2000.0f) < 0.05f, "Axes are asymmetric");
    }
}

int main()
{
    for (int fps : {5, 10, 20, 30, 60, 120, 144, 240, 360, 1000})
        for (float stick : {0.16f, 0.5f, 1.0f})
            for (float viewportSpeed : {0.6f, 1.0f})
                CheckMovement(fps, stick, viewportSpeed);

    // Regression: both axes previously stuck/asymmetric in split-screen at 240 FPS.
    float held = 15.0f;
    float x = 100.0f, y = 100.0f;
    PcCursorMotion::Advance(1.0f, 1.0f, 0.25f, 0.6f, held, x, y);
    Check(x > 100.0f && y < 100.0f, "Subpixel movement was lost");

    // A 100 ms loading frame must account for 100 ms, not the old 50 ms cap.
    Check(std::fabs(PcCursorMotion::FrameScale(100000000LL) - 6.0f) < 0.0001f,
          "Loading frame time was truncated");
    Check(PcCursorMotion::FrameScale(-100LL) == 0.0f, "Negative elapsed time accepted");
    Check(std::fabs(PcCursorMotion::FrameScale(5000000000LL) - 15.0f) < 0.0001f,
          "Suspension jump is unbounded");

    // Uneven frames during loading must match the same total elapsed time.
    held = 0.0f;
    x = y = 1000.0f;
    for (int frame = 0; frame < 10; ++frame)
    {
        PcCursorMotion::Advance(0.5f, 0.5f, PcCursorMotion::FrameScale(5000000LL), 0.6f, held, x, y);
        PcCursorMotion::Advance(0.5f, 0.5f, PcCursorMotion::FrameScale(95000000LL), 0.6f, held, x, y);
    }
    const float expected = 0.25f * 13.0f * 0.6f * (60.0f - 3.5f);
    Check(std::fabs((x - 1000.0f) - expected) < 0.05f, "Uneven frames changed cursor speed");
    Check(std::fabs(x + y - 2000.0f) < 0.05f, "Uneven frames affected axes differently");
    std::cout << "Cursor movement: single/split, both directions, 5-1000 FPS, subpixel and loading checks passed\n";
}
