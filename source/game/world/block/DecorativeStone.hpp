#ifndef GAME_WORLD_BLOCK_DECORATIVESTONE_HPP
#define GAME_WORLD_BLOCK_DECORATIVESTONE_HPP

namespace DecorativeStone
{
constexpr int SlabTypesPerBlock = 7;
constexpr int StairCount = 14;
constexpr int MaterialCount = 20;

struct MaterialEntry
{
    int blockId;
    int data;
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* russian;
    bool wall;
};

extern const MaterialEntry materials[MaterialCount];
void registerBlocks();
}

#endif
