#include "game/world/GenericOverworldLevelSource.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/chunk/ChunkPos.hpp"
#include "game/world/chunk/ChunkPrimer.hpp"
#include "game/world/generation/StructureTerrain.hpp"

bool GenericOverworldLevelSource::isFlat()
{
    return false;
}

StructureTerrain::StructureTerrain(Level* level, GenericOverworldLevelSource* source)
    : mLevel(level), mSource(source)
{
}

StructureTerrain::~StructureTerrain() = default;

StructureTerrain::Surface StructureTerrain::surface(int x, int z)
{
    const auto key = std::make_pair(x, z);
    const auto existing = mSurfaces.find(key);
    if (existing != mSurfaces.end())
        return existing->second;
    return mSurfaces.emplace(key, sample(x, z)).first->second;
}

StructureTerrain::Surface StructureTerrain::sample(int x, int z)
{
    const BlockPos pos(x, 0, z);
    if (!mSource)
    {
        if (!mLevel->hasChunkAt(pos))
            return {mLevel->getSeaLevel(), false};
        const auto top = mLevel->getTopSolidBlockPos(pos);
        return {top.y - 1, mLevel->getBlockState(top)->getMaterial()->isLiquid()};
    }

    const ChunkPos chunk(pos);
    auto& primer = mPrimers[ChunkPos::asLong(chunk.x, chunk.z)];
    if (!primer)
    {
        primer = std::make_unique<ChunkPrimer>(true, arrayWithLength<unsigned short>(65536, true));
        mSource->prepareHeights(chunk.x, chunk.z, primer.get());
    }
    const int localX = x & 15;
    const int localZ = z & 15;
    const int ground = primer->getHighestSolidPos(localX, localZ);
    const int top = primer->getHighestNonAirPos(localX, localZ);
    return {ground, top > ground && primer->getState(localX, top, localZ)->getMaterial()->isLiquid()};
}
