#ifndef GAME_WORLD_BLOCK_CUBOIDBLOCKMODEL_HPP
#define GAME_WORLD_BLOCK_CUBOIDBLOCKMODEL_HPP

#include <array>
#include <vector>

namespace CuboidBlockModel
{
using Point = std::array<float, 3>;
using Box = std::array<Point, 2>;

struct Face
{
    std::array<float, 4> uv{};
    int texture = -1;
    int cullSide = -1;
    int uvRotation = 0;
};

struct Part
{
    Box bounds;
    std::array<Face, 6> faces;
    int yRotation = 0;
    float xRotation = 0;
};

std::vector<Part> read(const wchar_t* path, int textureCount);
}

#endif
