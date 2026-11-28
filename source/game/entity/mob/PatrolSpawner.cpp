#include "game/entity/mob/PatrolSpawner.hpp"

#include <algorithm>
#include <cmath>

#include "game/core/DifficultyInstance.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/Pillager.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/NaturalMobSpawner.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/util/Random.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/DimensionType.hpp"
#include "game/world/WorldBorder.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villages.hpp"

PatrolSpawner::PatrolSpawner(const std::wstring& id) : SavedData(id) {}

int PatrolSpawner::tick(ServerLevel& level, bool spawnEnemies)
{
    if (level.mIsClientSide || level.mDimension->getType() != DimensionType::OVERWORLD
        || !spawnEnemies || level.getDifficulty() == Difficulty::PEACEFUL
        || !level.getGameRules()->getBoolean(GameRules::RULE_DOMOBSPAWNING))
        return 0;

    setDirty();
    if (--mNextTick > 0)
        return 0;
    mNextTick += 12000 + level.mRandom->nextInt(1200);
    if (level.getDayTime() / 24000 < 5 || !level.isDay() || level.mRandom->nextInt(5) != 0
        || level.mPlayers.empty())
        return 0;

    const auto player = level.mPlayers[level.mRandom->nextInt(static_cast<int>(level.mPlayers.size()))];
    if (!player->isAlive() || player->isSpectator()
        || level.getVillages()->getClosestVillage(BlockPos(player), 0))
        return 0;

    const int dx = (24 + level.mRandom->nextInt(24)) * (level.mRandom->nextBoolean() ? -1 : 1);
    const int dz = (24 + level.mRandom->nextInt(24)) * (level.mRandom->nextBoolean() ? -1 : 1);
    const auto origin = BlockPos(player).offset(dx, 0, dz);
    // A patrol attempt must never load or generate terrain on its own.
    if (!level.hasChunksAt(origin.x - 10, 0, origin.z - 10,
                            origin.x + 10, level.getHeight() - 1, origin.z + 10, false))
        return 0;
    return spawnGroup(level, origin);
}

bool PatrolSpawner::getSurface(Level& level, const BlockPos& column, BlockPos& surface)
{
    if (!level.getWorldBorder()->isWithinBounds(column) || !level.hasChunkAt(column, false))
        return false;
    // Equivalent to MOTION_BLOCKING_NO_LEAVES: fluids count, leaves do not.
    // LCE's getTopSolidBlockPos ignores fluids, which would put patrols underwater.
    const int top = std::min(level.getHeight(), level.getChunkAt(column)->getHighestSectionPosition() + 16);
    for (int y = top - 1; y >= 0; --y)
    {
        auto* material = level.getBlockState(BlockPos(column.x, y, column.z))->getMaterial();
        if (material != Material::LEAVES && (material->blocksMotion() || material->isLiquid()))
        {
            surface = BlockPos(column.x, y + 1, column.z);
            return surface.y < level.getHeight();
        }
    }
    return false;
}

bool PatrolSpawner::canSpawnAt(Level& level, const BlockPos& pos)
{
    return !level.mIsClientSide && level.mDimension->getType() == DimensionType::OVERWORLD
           && level.getDifficulty() != Difficulty::PEACEFUL
           && pos.y > 0 && pos.y + 1 < level.getHeight()
           && level.hasChunkAt(pos, false) && level.getWorldBorder()->isWithinBounds(pos)
           && level.getBiome(pos)->getBaseClass() != Biome::eBiomeBaseClass_MushroomIsland
           && level.getBrightness(LightLayer::BLOCK, pos) <= 8
           && NaturalMobSpawner::isSpawnPositionOk(Mob::SPAWN_PLACEMENT_TYPE_ON_GROUND, &level, pos, 1, 1);
}

int PatrolSpawner::spawnGroup(ServerLevel& level, const BlockPos& origin)
{
    if (!level.hasChunkAt(origin, false) || !level.getWorldBorder()->isWithinBounds(origin)
        || level.getBiome(origin)->getBaseClass() == Biome::eBiomeBaseClass_MushroomIsland)
        return 0;
    const int size = static_cast<int>(std::ceil(level.getCurrentDifficultyAt(origin).getEffectiveDifficulty())) + 1;
    auto column = origin;
    int spawned = 0;
    for (int i = 0; i < size; ++i)
    {
        BlockPos pos;
        bool added = false;
        if (getSurface(level, column, pos) && canSpawnAt(level, pos))
        {
            auto member = boost::static_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, &level));
            member->moveTo(pos.x + 0.5, pos.y, pos.z + 0.5, 0.0f, 0.0f);
            if (member->checkSpawnObstruction())
            {
                member->preparePatrol(i == 0);
                delete member->finalizeMobSpawn(level.getCurrentDifficultyAt(pos), nullptr, L"");
                added = level.addEntity(member);
                if (added)
                    ++spawned;
            }
        }
        if (i == 0 && !added)
            break;
        const int dx = level.mRandom->nextInt(5) - level.mRandom->nextInt(5);
        const int dz = level.mRandom->nextInt(5) - level.mRandom->nextInt(5);
        column = column.offset(dx, 0, dz);
    }
    return spawned;
}

void PatrolSpawner::load(CompoundTag* tag)
{
    mNextTick = std::max(0, tag->getInt(L"NextAttemptTicks"));
}

CompoundTag* PatrolSpawner::save(CompoundTag* tag)
{
    tag->putInt(L"NextAttemptTicks", mNextTick);
    return tag;
}
