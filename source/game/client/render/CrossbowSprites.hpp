#ifndef GAME_CLIENT_RENDER_CROSSBOWSPRITES_HPP
#define GAME_CLIENT_RENDER_CROSSBOWSPRITES_HPP

#include <cstdlib>

namespace CrossbowSprites
{
inline void draw(int* atlas, int width, int height)
{
    const char body[16][17] = {
        "................",
        "................",
        "....oo..........",
        "..oohhoo........",
        "..omawhsooo.....",
        "...ooowstsoo....",
        "......osaasoo...",
        ".....oahwoahwo..",
        "....oawwo.owhwo.",
        "...oahwo...owso.",
        "..owwho.....oso.",
        "..ohwoo.....oho.",
        "..owo.......owo.",
        "...oo........o..",
        "................",
        "................",
    };
    constexpr int frameCount = 5;
    const unsigned int outline = 0xff38261f;
    const unsigned int woodDark = 0xff805237;
    const unsigned int wood = 0xffaa7746;
    const unsigned int woodLight = 0xffd9aa67;
    const unsigned int steel = 0xff6e747b;
    const unsigned int steelLight = 0xffafb9bf;
    const unsigned int bronze = 0xffba9063;
    const unsigned int string = 0xffcdb990;
    const unsigned int rocketRed = 0xffd14a43;
    const unsigned int rocketPaper = 0xffe8d7b6;

    if (width < 16 || height < 17)
        return;
    const int cellWidth = width / 16;
    const int cellHeight = height / 17;
    for (int frame = 0; frame < frameCount; ++frame)
    {
        unsigned int pixels[16][16] = {};
        const auto line = [&pixels](int x0, int y0, int x1, int y1, unsigned int colour)
        {
            const int dx = x1 - x0;
            const int dy = y1 - y0;
            const int steps = std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
            for (int step = 0; step <= steps; ++step)
                pixels[y0 + dy * step / steps][x0 + dx * step / steps] = colour;
        };
        const int pull = frame == 0 ? 0 : (frame == 1 ? 2 : 4);
        line(2, 4, 8 - pull, 7 + pull, string);
        line(8 - pull, 7 + pull, 13, 13, string);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                switch (body[y][x])
                {
                case 'o': pixels[y][x] = outline; break;
                case 'h': pixels[y][x] = woodDark; break;
                case 'w': pixels[y][x] = wood; break;
                case 'a': pixels[y][x] = woodLight; break;
                case 's': pixels[y][x] = steel; break;
                case 'm': pixels[y][x] = steelLight; break;
                case 't': pixels[y][x] = bronze; break;
                }
            }
        }
        if (frame == 3)
        {
            line(4, 11, 12, 3, rocketPaper);
            pixels[3][12] = steelLight;
            pixels[3][13] = steel;
            pixels[4][13] = steelLight;
            pixels[10][4] = steelLight;
            pixels[11][5] = steel;
        }
        else if (frame == 4)
        {
            line(4, 11, 11, 4, rocketPaper);
            line(7, 7, 11, 3, rocketPaper);
            line(8, 8, 12, 4, rocketRed);
            pixels[5][10] = rocketRed;
            pixels[4][11] = rocketRed;
            pixels[3][12] = rocketRed;
        }
        // Keep the stock and muzzle in the same direction as the PNG overrides.
        for (int y = 0; y < cellHeight; ++y)
            for (int x = 0; x < cellWidth; ++x)
                atlas[(16 * cellHeight + y) * width + (2 + frame) * cellWidth + x] =
                    static_cast<int>(pixels[y * 16 / cellHeight][15 - x * 16 / cellWidth]);
    }
}
}

#endif
