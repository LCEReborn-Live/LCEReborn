#ifndef GAME_WORLD_GENERATION_VILLAGEFEATURE_HPP
#define GAME_WORLD_GENERATION_VILLAGEFEATURE_HPP

#include <string>
#include <vector>

#include <boost/unordered_map.hpp>

#include "game/world/generation/StructureFeature.hpp"
#include "game/world/generation/StructureStart.hpp"

class Biome;
class BlockPos;
class BoundingBox;
class DataInputStream;
class DataOutputStream;
class GenericOverworldLevelSource;
class Level;
class Random;
class SuperflatConfig;

class VillageFeature : public StructureFeature
{
public:
    class VillageStart : public StructureStart
    {
    public:
        VillageStart(Level* level, Random& random, int chunkX, int chunkZ, int size, int distance,
                     GenericOverworldLevelSource* terrain = nullptr);
        VillageStart();

        static StructureStart* Create();
        void load(Level* level, DataInputStream* input) override;
        virtual bool isValid();
        virtual void addAdditionalSaveData(DataOutputStream* output);
        virtual void readAdditionalSaveData(DataInputStream* input);
        virtual EStructureStart GetType();

    private:
        bool mValid;
        int mDistance;
    };

    VillageFeature(int size);
    VillageFeature(SuperflatConfig* config, int size);
    VillageFeature(boost::unordered_map<std::wstring, std::wstring> options, int size);

    static std::vector<Biome*> allowedBiomes;

    static void staticCtor();
    void _init(int size);
    virtual std::wstring getFeatureName();
    virtual bool isFeatureChunk(int chunkX, int chunkZ, bool flatWorld);
    virtual BlockPos* getNearestGeneratedFeature(Level* level, const BlockPos& pos, bool skipExistingChunks);
    virtual StructureStart* createStructureStart(int chunkX, int chunkZ);
    void setTerrainSource(GenericOverworldLevelSource* terrain) { mTerrain = terrain; }

private:
    bool isLocatableChunk(int chunkX, int chunkZ) override;
    int mVillageSize;
    int mSpacing;
    int mSeparation;
    int mSize;
    GenericOverworldLevelSource* mTerrain = nullptr;
};

#endif
