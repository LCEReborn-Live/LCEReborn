#include "game/world/generation/NativeVillageRegistry.hpp"

#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/TreeFeature.hpp"
#include "game/world/generation/PineFeature.hpp"
#include "game/world/generation/SpruceFeature.hpp"
#include "game/world/generation/SavannaTreeFeature.hpp"
#include "game/world/generation/CactusFeature.hpp"
#include "game/world/generation/FlowerFeature.hpp"
#include "game/world/generation/TallgrassFeature.hpp"

namespace NativeVillageRegistry
{
bool isFeature(const std::wstring& id)
{
    static const wchar_t* names[] = {L"oak_tree", L"pine_tree", L"spruce_tree", L"acacia_tree",
        L"plain_flower", L"taiga_grass", L"cactus", L"sweet_berry_bush", L"hay_pile", L"snow_pile",
        L"ice_pile", L"melon_pile", L"pumpkin_pile"};
    for (const auto* name : names)
        if (id == std::wstring(L"NativeFeature/v1/") + name) return true;
    return false;
}

bool placeFeature(const std::wstring& id, Level* level, Random& random, const BlockPos& pos)
{
    const auto name = id.substr(std::wstring(L"NativeFeature/v1/").size());
    if (name == L"oak_tree") return TreeFeature(false).place(level, random, pos);
    if (name == L"pine_tree") return PineFeature().place(level, random, pos);
    if (name == L"spruce_tree") return SpruceFeature(false).place(level, random, pos);
    if (name == L"acacia_tree") return SavannaTreeFeature(false).place(level, random, pos);
    if (name == L"cactus") return CactusFeature().place(level, random, pos);
    if (name == L"taiga_grass")
        return TallgrassFeature(random.nextInt(5) == 0 ? TallGrassBlock::Variant::GRASS
                                                      : TallGrassBlock::Variant::FERN).place(level, random, pos);
    if (name == L"plain_flower")
    {
        auto* flower = level->getBiome(pos)->getRandomFlower(random, pos);
        return FlowerFeature(static_cast<FlowerBlock*>(flower->getCategory()->getBlock()), flower).place(level, random, pos);
    }
    if (name == L"sweet_berry_bush")
    {
        for (int i = 0; i < 64; ++i)
        {
            const auto p = pos.offset(random.nextInt(8) - random.nextInt(8),
                                      random.nextInt(4) - random.nextInt(4),
                                      random.nextInt(8) - random.nextInt(8));
            if (level->isEmptyBlock(p) && Blocks::sweet_berry_bush->canSurvive(level, p, nullptr))
                level->setBlock(p, Blocks::sweet_berry_bush->getBlockState(3), ::Block::UPDATE_CLIENTS, false);
        }
        return true;
    }
    if (name.find(L"_pile") == std::wstring::npos || pos.y < 5) return false;
    const int rx = 2 + random.nextInt(2), rz = 2 + random.nextInt(2);
    // The source pile feature visits x first, then y, then z. Keep this order
    // and use a piece-local RNG so chunk population order cannot change it.
    for (int z = -rz; z <= rz; ++z)
        for (int y = 0; y <= 1; ++y)
            for (int x = -rx; x <= rx; ++x)
            {
                if (x * x + z * z > random.nextFloat() * 10 - random.nextFloat() * 6
                    && random.nextFloat() >= 0.031f) continue;
                const auto p = pos.offset(x, y, z);
                if (!level->isEmptyBlock(p)) continue;
                const auto* below = level->getBlockState(p.below());
                if (below->getBlock() == Blocks::grass_path ? !random.nextBoolean()
                                                          : !below->getMaterial()->blocksMotion()) continue;
                const BlockState* state = nullptr;
                if (name == L"hay_pile") state = Blocks::hay_block->getBlockState(random.nextInt(3) * 4);
                else if (name == L"snow_pile") state = Blocks::snow->defaultBlockState();
                else if (name == L"ice_pile") state = (random.nextInt(7) == 0 ? Blocks::blue_ice : Blocks::packed_ice)->defaultBlockState();
                else if (name == L"melon_pile") state = Blocks::melon_block->defaultBlockState();
                else if (name == L"pumpkin_pile") state = (random.nextFloat() < 0.95f ? Blocks::pumpkin : Blocks::lit_pumpkin)->defaultBlockState();
                if (state) level->setBlock(p, state, ::Block::UPDATE_CLIENTS, false);
            }
    return true;
}
}
