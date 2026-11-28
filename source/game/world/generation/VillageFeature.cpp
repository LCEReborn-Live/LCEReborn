#include "game/world/generation/VillageFeature.hpp"

#include <cwchar>
#include <vector>

#include "game/core/BoundingBox.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/SuperflatConfig.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelGenerationOptions.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/biome/BiomeSource.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/chunk/ChunkPos.hpp"
#include "game/world/generation/FlatGeneratorInfo.hpp"
#include "game/world/generation/VillageTemplatePieces.hpp"

std::vector<Biome*> VillageFeature::allowedBiomes;

VillageFeature::VillageFeature(int size) : StructureFeature()
{
    _init(size);
}

VillageFeature::VillageFeature(SuperflatConfig* config, int size) : StructureFeature()
{
    FlatGeneratorInfo generatorInfo(config, false);
    _init(size);
    mVillageSize = generatorInfo.getVillageSize();
    mSpacing = generatorInfo.getVillageDistance();
}

void VillageFeature::staticCtor()
{
    allowedBiomes.push_back(Biome::PLAINS);
    allowedBiomes.push_back(Biome::DESERT);
    allowedBiomes.push_back(Biome::SAVANNA);
    allowedBiomes.push_back(Biome::TAIGA);
    allowedBiomes.push_back(Biome::COLD_TAIGA);
    allowedBiomes.push_back(Biome::ICE_PLAINS);
}

void VillageFeature::_init(int size)
{
    mVillageSize = 0;
    mSpacing = 0x20;
    mSeparation = 8;
    mSize = size << 4;
}

std::wstring VillageFeature::getFeatureName()
{
    return std::wstring(L"Village");
}

bool VillageFeature::isFeatureChunk(int chunkX, int chunkZ, bool flatWorld)
{
    int spacing = mSpacing;
    if (!flatWorld)
        spacing = 16;
    int originalX = chunkX;
    int originalZ = chunkZ;
    if (chunkX < 0)
        chunkX += 1 - spacing;
    if (chunkZ < 0)
        chunkZ += 1 - spacing;
    int centerX = chunkX / spacing;
    int centerZ = chunkZ / spacing;
    Random* chunkRandom = level->getRandomFor(centerX, centerZ, 10387312);
    centerX *= spacing;
    centerZ *= spacing;
    centerX += chunkRandom->nextInt(spacing - mSeparation);
    centerZ += chunkRandom->nextInt(spacing - mSeparation);
    chunkX = originalX;
    chunkZ = originalZ;
    LevelGenerationOptions* options = gApp.getLevelGenerationOptions();
    if ((options != NULL && options->isFeatureChunk(chunkX, chunkZ, eFeatureType_Village, NULL))
        || (chunkX == centerX && chunkZ == centerZ))
    {
        if (level->getBiomeSource()->containsOnly(chunkX * 16 + 8, chunkZ * 16 + 8, 0, allowedBiomes))
        {
            return true;
        }
    }
    return false;
}

BlockPos* VillageFeature::getNearestGeneratedFeature(Level* levelIn, const BlockPos& pos, bool skipExistingChunks)
{
    level = levelIn;
    return StructureFeature::findNearestFeaturePositionBySpacing(levelIn, this, pos, mSpacing, mSeparation, 10387312,
                                                                 false, 100, skipExistingChunks);
}

StructureStart* VillageFeature::createStructureStart(int chunkX, int chunkZ)
{
    gApp.AddTerrainFeaturePosition((_eTerrainFeatureType)3, chunkX, chunkZ);
    return new VillageStart(level, random, chunkX, chunkZ, mVillageSize, mSize, mTerrain);
}

bool VillageFeature::isLocatableChunk(int chunkX, int chunkZ)
{
    const auto saved = mStructureMap.find(ChunkPos::asLong(chunkX, chunkZ));
    if (saved != mStructureMap.end())
        return saved->second->isValid();

    Random candidateRandom(level->getSeed());
    setupChunkSeed(level->getSeed(), candidateRandom, chunkX, chunkZ);
    candidateRandom.nextInt();
    VillageStart candidate(level, candidateRandom, chunkX, chunkZ, mVillageSize, mSize, mTerrain);
    return candidate.isValid();
}

VillageFeature::VillageStart::VillageStart(Level* levelIn, Random& random, int chunkX, int chunkZ, int size,
                                           int distance, GenericOverworldLevelSource* terrain)
    : StructureStart(chunkX, chunkZ),
      mValid(false),
      mDistance(distance)
{
    VillageTemplatePieces::generate(levelIn, terrain, random, chunkX, chunkZ, size, distance, mPieces);
    calculateBoundingBox();
    mValid = mPieces.size() >= 6;
}

VillageFeature::VillageStart::VillageStart() : StructureStart(), mValid(false), mDistance(0)
{
}

StructureStart* VillageFeature::VillageStart::Create()
{
    return new VillageStart();
}

bool VillageFeature::VillageStart::isValid()
{
    if (!mValid)
        return false;
    if (mBoundingBox->x0 < -mDistance / 2 || mBoundingBox->x1 > mDistance / 2 || mBoundingBox->z0 < -mDistance / 2
        || mBoundingBox->z1 > mDistance / 2)
    {
        mValid = false;
    }
    return mValid;
}

void VillageFeature::VillageStart::load(Level* level, DataInputStream* input)
{
    mDistance = level->getLevelData()->getXZSize() * 16;
    StructureStart::load(level, input);
}

void VillageFeature::VillageStart::addAdditionalSaveData(DataOutputStream* output)
{
    StructureStart::addAdditionalSaveData(output);
    output->writeBoolean(mValid);
}

void VillageFeature::VillageStart::readAdditionalSaveData(DataInputStream* input)
{
    StructureStart::readAdditionalSaveData(input);
    mValid = input->readBoolean();
}

EStructureStart VillageFeature::VillageStart::GetType()
{
    return eStructureStart_Village;
}
