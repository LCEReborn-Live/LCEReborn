#ifndef GAME_WORLD_GENERATION_PILLAGEROUTPOSTFEATURE_HPP
#define GAME_WORLD_GENERATION_PILLAGEROUTPOSTFEATURE_HPP

#include "game/world/biome/Biome.hpp"
#include "game/world/generation/StructureFeature.hpp"

class GenericOverworldLevelSource;
class LevelChunk;

class PillagerOutpostFeature : public StructureFeature
{
public:
    static constexpr int PatrolRadius = 32;
    PillagerOutpostFeature(GenericOverworldLevelSource* terrain, bool villagesEnabled);

    static void registerPieces();
    std::wstring getFeatureName() override;
    bool isFeatureChunk(int chunkX, int chunkZ, bool flatWorld) override;
    StructureStart* createStructureStart(int chunkX, int chunkZ) override;
    BlockPos* getNearestGeneratedFeature(Level* level, const BlockPos& pos, bool skipExistingChunks) override;
    void addFeature(Level* level, int chunkX, int chunkZ, int originChunkX, int originChunkZ,
                    ChunkPrimer* primer) override;
    bool isPatrolArea(Level* level, const BlockPos& pos);
    void updateLoadedBanners(Level* level, LevelChunk& chunk);
    std::vector<Biome::MobSpawnerData*>* getEnemies();

private:
    bool isLocatableChunk(int chunkX, int chunkZ) override;
    GenericOverworldLevelSource* mTerrain;
    bool mVillagesEnabled;
};

#endif
