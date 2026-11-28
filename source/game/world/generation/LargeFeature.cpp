#include "game/world/generation/LargeFeature.hpp"

#include "game/world/Level.hpp"

const std::wstring LargeFeature::STRONGHOLD(L"StrongHold");
const std::wstring LargeFeature::VILLAGE(L"Village");
const std::wstring LargeFeature::MONUMENT(L"Monument");
const std::wstring LargeFeature::MANSION(L"Mansion");
const std::wstring LargeFeature::END_CITY(L"EndCity");
const std::wstring LargeFeature::TEMPLE(L"Temple");
const std::wstring LargeFeature::MINESHAFT(L"Mineshaft");
const std::wstring LargeFeature::FORTRESS(L"Fortress");
const std::wstring LargeFeature::BURIED_TREASURE(L"BuriedTreasure");
const std::wstring LargeFeature::SHIPWRECK(L"Shipwreck");
const std::wstring LargeFeature::RUIN(L"Ruin");
const std::wstring LargeFeature::PILLAGER_OUTPOST(L"PillagerOutpost");

LargeFeature::LargeFeature()
{
    range = 8;
    random = Random();
}

LargeFeature::~LargeFeature()
{
}

long long LargeFeature::setupChunkSeed(long long baseSeed, Random& random, int chunkX, int chunkZ)
{
    random.setSeed(baseSeed);
    const auto xFactor = static_cast<unsigned long long>(random.nextLong());
    const auto zFactor = static_cast<unsigned long long>(random.nextLong());
    const auto seed = static_cast<long long>(xFactor * chunkX ^ zFactor * chunkZ ^ baseSeed);
    random.setSeed(seed);
    return seed;
}

void LargeFeature::apply(Level* level, int chunkX, int chunkZ, ChunkPrimer* primer)
{
    this->level = level;

    const int featureRange = range;

    random.setSeed(level->getSeed());

    const auto xSeedFactor = static_cast<unsigned long long>(random.nextLong());
    const auto zSeedFactor = static_cast<unsigned long long>(random.nextLong());

    for (int x = chunkX - featureRange; x <= chunkX + featureRange; ++x)
    {
        for (int z = chunkZ - featureRange; z <= chunkZ + featureRange; ++z)
        {
            const auto seed = xSeedFactor * x ^ zSeedFactor * z ^ level->getSeed();
            random.setSeed(static_cast<long long>(seed));
            addFeature(level, x, z, chunkX, chunkZ, primer);
        }
    }
}

void LargeFeature::addFeature(Level* level, int chunkX, int chunkZ, int originChunkX, int originChunkZ,
                              ChunkPrimer* primer)
{
}
