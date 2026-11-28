#include "game/world/block/CuboidBlockModel.hpp"

#include <memory>

#include "game/core/InputStream.hpp"

std::vector<CuboidBlockModel::Part> CuboidBlockModel::read(const wchar_t* path, int textureCount)
{
    std::unique_ptr<InputStream> input(InputStream::getResourceAsStream(path));
    if (!input)
        return {};

    std::array<int, 4> magic;
    for (auto& byte : magic)
        byte = input->read();
    const bool legacy = magic == std::array<int, 4>{'G', 'S', 'J', '1'};
    const bool hasPitch = magic == std::array<int, 4>{'C', 'B', 'M', '4'};
    const bool hasFaceRotation = hasPitch || magic == std::array<int, 4>{'C', 'B', 'M', '3'};
    const bool rotatable = hasFaceRotation || magic == std::array<int, 4>{'C', 'B', 'M', '2'};
    if (!legacy && !rotatable && magic != std::array<int, 4>{'C', 'B', 'M', '1'})
        return {};

    const int count = input->read();
    if (count < 1 || count > 16)
        return {};

    std::vector<Part> parts(count);
    for (auto& part : parts)
    {
        if (rotatable)
        {
            const int angle = input->read();
            if (angle < 0 || angle > 180)
                return {};
            part.yRotation = angle - 90;
        }
        if (hasPitch)
        {
            const int pitch = input->read();
            if (pitch < 0 || pitch > 180)
                return {};
            part.xRotation = (pitch - 90) * .5f;
        }
        const int factor = rotatable ? 2 : 1;
        for (auto& point : part.bounds)
            for (auto& coord : point)
            {
                const int value = input->read();
                if (value < 0 || value > (hasFaceRotation ? 17 : 16) * factor)
                    return {};
                coord = float(value) / factor;
            }
        for (size_t axis = 0; axis < part.bounds[0].size(); ++axis)
            if (part.bounds[0][axis] > part.bounds[1][axis])
                return {};
        const int faceCount = input->read();
        if (faceCount < 1 || faceCount > 6)
            return {};
        for (int i = 0; i < faceCount; ++i)
        {
            const int id = input->read();
            const int side = legacy ? id & 0x7f : id;
            const int texture = input->read();
            const int cullSide = legacy ? ((id & 0x80) ? side : -1) : input->read() - 1;
            if (side < 0 || side >= 6 || cullSide < -1 || cullSide >= 6 || texture < 0
                || texture >= textureCount || part.faces[side].texture >= 0)
                return {};
            const int normalAxis = side < 2 ? 1 : side < 4 ? 2 : 0;
            for (int axis = 0; axis < 3; ++axis)
                if (axis != normalAxis && part.bounds[0][axis] == part.bounds[1][axis])
                    return {};
            auto& face = part.faces[side];
            face.texture = texture;
            face.cullSide = cullSide;
            if (hasFaceRotation)
            {
                face.uvRotation = input->read();
                if (face.uvRotation < 0 || face.uvRotation > 3)
                    return {};
            }
            for (auto& coord : face.uv)
            {
                const int value = input->read();
                if (value < 0 || value > 16)
                    return {};
                coord = float(value);
            }
        }
    }
    return parts;
}
