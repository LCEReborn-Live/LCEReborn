#ifndef GAME_WORLD_GENERATION_STRUCTURETERRAIN_HPP
#define GAME_WORLD_GENERATION_STRUCTURETERRAIN_HPP

#include <map>
#include <memory>
#include <utility>

class ChunkPrimer;
class GenericOverworldLevelSource;
class Level;

class StructureTerrain
{
public:
    struct Surface
    {
        int ground;
        bool flooded;
    };

    StructureTerrain(Level* level, GenericOverworldLevelSource* source);
    ~StructureTerrain();
    Surface surface(int x, int z);

private:
    Surface sample(int x, int z);

    Level* mLevel;
    GenericOverworldLevelSource* mSource;
    std::map<long long, std::unique_ptr<ChunkPrimer>> mPrimers;
    std::map<std::pair<int, int>, Surface> mSurfaces;
};

#endif
