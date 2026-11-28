#pragma once

#include "game/data/SavedData.hpp"
#include "game/world/block/BlockPos.hpp"

class Level;
class ServerLevel;

// Village & Pillage (Java 1.14.4) patrol attempts, separate from biome mob caps.
class PatrolSpawner : public SavedData
{
public:
    explicit PatrolSpawner(const std::wstring& id = L"patrols");
    int tick(ServerLevel& level, bool spawnEnemies = true);
    int spawnGroup(ServerLevel& level, const BlockPos& origin);
    static bool canSpawnAt(Level& level, const BlockPos& pos);
    static bool getSurface(Level& level, const BlockPos& column, BlockPos& surface);
    void load(CompoundTag* tag) override;
    CompoundTag* save(CompoundTag* tag) override;

private:
    int mNextTick = 0;
};
