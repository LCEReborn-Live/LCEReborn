#include "game/world/generation/PillagerOutpostFeature.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>

#include "game/core/BoundingBox.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Pillager.hpp"
#include "game/core/Rotation.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/LevelStorage.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/MobGroupData.hpp"
#include "game/item/BannerItem.hpp"
#include "game/util/NbtUtils.hpp"
#include "game/world/Level.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/biome/BiomeSource.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BannerBlockEntity.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/ChestBlockEntity.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/chunk/ChunkPos.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/generation/StructureFeatureIO.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructurePlaceSettings.hpp"
#include "game/world/generation/StructureStart.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include "game/world/generation/StructureTerrain.hpp"
#include "game/world/generation/VillagerGolem.hpp"
#include "game/world/generation/VillageFeature.hpp"

namespace
{
constexpr int spacing = 24;
constexpr int separation = 6;
constexpr int salt = 165745296;
const std::wstring localPrefix = L"LocalVillage/pillager_outpost/";
const std::wstring archivePrefix = L"Structures\\pillageroutpost\\";

bool isTower(const std::wstring& name)
{
    return name == L"watchtower" || name == L"watchtower_overgrown";
}

const StructureTemplate* getTemplate(StructureManager& manager, const std::wstring& name)
{
    if (const auto* custom = manager.getNamed(localPrefix + name))
        return custom;
    return manager.getNamed(archivePrefix + name);
}

bool spawnResident(Level& world, BlockPos pos, unsigned long long type, bool captain)
{
    auto mob = boost::dynamic_pointer_cast<Mob>(EntityIO::newEntity(type, &world));
    if (!mob)
        return false;

    // Both the local NBT entity positions and the TU80 marker blocks can sit
    // one block above the floor.
    if (!world.getBlockState(pos.below())->getMaterial()->blocksMotion()
        && world.getBlockState(pos.below(2))->getMaterial()->blocksMotion())
        pos = pos.below();
    mob->moveTo(pos.x + 0.5, pos.y, pos.z + 0.5, 0.0f, 0.0f);
    if (!mob->checkSpawnObstruction())
        return false;

    delete mob->finalizeMobSpawn(world.getCurrentDifficultyAt(pos), nullptr, L"");
    mob->setPersistenceRequired(true);
    if (const auto pillager = boost::dynamic_pointer_cast<Pillager>(mob))
        pillager->setCaptain(captain);
    return world.addEntity(mob);
}

class OutpostPiece : public StructurePiece
{
public:
    OutpostPiece() = default;
    OutpostPiece(std::wstring name, const StructureTemplate* structure, const BlockPos& center,
                 Rotation* rotation)
        : StructurePiece(0), mName(std::move(name)), mTemplate(structure), mRotation(rotation)
    {
        const auto size = mTemplate->getSize();
        mOrigin = center.subtract(StructureTemplate::transform(BlockPos(size.x / 2, 0, size.z / 2),
                                                                 Mirror::NONE, mRotation));
        mBoundingBox = mTemplate->getTransformedBounds(mOrigin, Mirror::NONE, mRotation);
    }

    static StructurePiece* create() { return new OutpostPiece(); }
    EStructurePiece GetType() override { return eStructurePiece_OutpostTemplate; }

    void addAdditionalSaveData(DataOutputStream* output) override
    {
        output->writeUTF(mName);
        output->writeBlockPos(mOrigin);
        output->writeByte(static_cast<unsigned char>(mRotation->getType()));
    }

    void readAdditionalSaveData(DataInputStream* input, StructureManager* manager) override
    {
        mName = input->readUTF();
        mOrigin = input->readBlockPos();
        const unsigned int id = input->readUnsignedByte();
        mRotation = id < 4 ? Rotation::VALUES[id] : Rotation::NONE;
        mTemplate = getTemplate(*manager, mName);
    }

    bool postProcess(Level* world, Random&, BoundingBox* clip) override
    {
        if (!mTemplate)
            return true;

        StructurePlaceSettings settings;
        const bool embeddedGolem = mName == L"feature_cage1" && !mTemplate->getEntities().empty();
        settings.setRotation(mRotation)->setBoundingBox(clip)->setIgnoreEntities(!embeddedGolem)
            ->setIgnoreBlock(Blocks::structure_void);

        for (const auto* entry : mTemplate->getBlocks())
        {
            if (entry->mPos.y != 0 || !entry->mState->getMaterial()->blocksMotion())
                continue;
            auto under = worldPos(entry->mPos).below();
            if (!clip->isInside(under))
                continue;
            const auto* fill = (isTower(mName) ? Blocks::cobblestone : Blocks::dirt)->defaultBlockState();
            for (int depth = 0; depth < 12 && under.y > 1; ++depth, under = under.below())
            {
                if (world->getBlockState(under)->getMaterial()->blocksMotion())
                    break;
                world->setBlock(under, fill, Block::UPDATE_CLIENTS, false);
            }
        }

        mTemplate->placeInWorld(world, mOrigin, &settings, Block::UPDATE_CLIENTS);
        if (isTower(mName))
        {
            updateBanners(*world->getChunk(clip->x0 >> 4, clip->z0 >> 4));
            populateTower(*world, *clip);
        }
        else if (mName == L"feature_cage1" && !embeddedGolem)
            populateCage(*world, settings);
        return true;
    }

    void updateBanners(LevelChunk& chunk)
    {
        const int x = chunk.x * 16, z = chunk.z * 16;
        if (!mTemplate || !isTower(mName) || !mBoundingBox->intersects(x, z, x + 15, z + 15))
            return;

        const auto canonical = BannerItem::makeIllagerBanner();
        for (const auto* entry : mTemplate->getBlocks())
        {
            if (entry->mState->getBlock() != Blocks::wall_banner || !entry->mNbt)
                continue;
            const auto pos = worldPos(entry->mPos);
            if (pos.x < x || pos.x >= x + 16 || pos.z < z || pos.z >= z + 16
                || chunk.getBlockState(pos.x & 15, pos.y, pos.z & 15)->getBlock() != Blocks::wall_banner)
                continue;
            auto banner = boost::dynamic_pointer_cast<BannerBlockEntity>(
                chunk.getBlockEntity(pos, LevelChunk::NO_CREATE));
            if (!banner || banner->isIllagerBanner())
                continue;

            // In old chunks, only replace the pattern that came from this template.
            CompoundTag saved;
            banner->save(&saved);
            if (saved.getString(L"CustomName") != entry->mNbt->getString(L"CustomName")
                || saved.getInt(BannerBlockEntity::TAG_BASE) != entry->mNbt->getInt(BannerBlockEntity::TAG_BASE)
                || !NbtUtils::compareNbt(saved.get(BannerBlockEntity::TAG_PATTERNS),
                                        entry->mNbt->get(BannerBlockEntity::TAG_PATTERNS), false))
                continue;

            banner->setItemValues(canonical, false);
            chunk.markUnsaved();
        }
    }

private:
    BlockPos worldPos(const BlockPos& pos) const
    {
        return StructureTemplate::transform(pos, Mirror::NONE, mRotation).offset(mOrigin);
    }

    void populateTower(Level& world, BoundingBox& clip)
    {
        for (const auto* entry : mTemplate->getBlocks())
        {
            if (entry->mState->getBlock() != Blocks::chest)
                continue;
            const auto pos = worldPos(entry->mPos);
            if (clip.isInside(pos))
                if (auto chest = boost::dynamic_pointer_cast<ChestBlockEntity>(world.getBlockEntity(pos)))
                {
                    Random lootRandom(world.getSeed() ^ pos.compressBlockPos());
                    const auto seed = lootRandom.nextLong();
                    chest->setLootTable(&BuiltInLootTables::PILLAGER_OUTPOST, seed == 0 ? 1 : seed);
                }
        }

        if (world.getDifficulty() == Difficulty::PEACEFUL)
            return;

        const auto captain = worldPos(BlockPos(7, 14, 3));
        if (clip.isInside(captain))
            spawnResident(world, captain, EntityType::PILLAGER, true);

        for (const BlockPos& local : {BlockPos(4, 6, 4), BlockPos(7, 1, 7)})
        {
            const auto guard = worldPos(local);
            if (clip.isInside(guard))
                spawnResident(world, guard, EntityType::PILLAGER, false);
        }
    }

    void populateCage(Level& world, StructurePlaceSettings& settings)
    {
        for (const auto& marker : mTemplate->getMarkers(mOrigin, &settings))
            if (marker.second == L"cage")
                spawnResident(world, marker.first, EntityType::VILLAGER_GOLEM, false);
    }

    std::wstring mName;
    const StructureTemplate* mTemplate = nullptr;
    BlockPos mOrigin;
    Rotation* mRotation = Rotation::NONE;
};

std::unique_ptr<OutpostPiece> makePiece(StructureManager& manager, StructureTerrain& terrain,
                                       Level& world, const std::wstring& name,
                                       BlockPos center, Rotation* rotation)
{
    const auto* structure = getTemplate(manager, name);
    if (!structure || structure->getBlocks().empty())
        return nullptr;
    auto piece = std::make_unique<OutpostPiece>(name, structure, center, rotation);
    const auto* box = piece->getBoundingBox();
    const int limit = world.getLevelData()->getXZSize() * 8;
    if (box->x0 < -limit + 2 || box->z0 < -limit + 2 || box->x1 >= limit - 2 || box->z1 >= limit - 2)
        return nullptr;

    std::array<int, 9> ground;
    size_t index = 0;
    for (const int x : {box->x0, center.x, box->x1})
        for (const int z : {box->z0, center.z, box->z1})
        {
            const auto surface = terrain.surface(x, z);
            if (surface.flooded || surface.ground < 5)
                return nullptr;
            ground[index++] = surface.ground;
        }

    std::sort(ground.begin(), ground.end());
    if (ground.back() - ground.front() > (isTower(name) ? 10 : 5))
        return nullptr;
    center.y = ground[ground.size() / 2];
    if (center.y + structure->getSize().y > world.getMaxBuildHeight())
        return nullptr;
    return std::make_unique<OutpostPiece>(name, structure, center, rotation);
}

class OutpostStart : public StructureStart
{
public:
    OutpostStart() = default;
    OutpostStart(Level& world, GenericOverworldLevelSource* source, Random& random, int cx, int cz)
        : StructureStart(cx, cz)
    {
        auto* manager = world.getLevelStorage()->getStructureManager();
        StructureTerrain terrain(&world, source);
        const BlockPos center(cx * 16 + 8, 0, cz * 16 + 8);
        auto* rotation = Rotation::VALUES[random.nextInt(4)];
        const bool overgrown = random.nextInt(5) == 0;
        auto tower = makePiece(*manager, terrain, world, overgrown ? L"watchtower_overgrown" : L"watchtower",
                               center, rotation);
        if (!tower)
            return;
        mPieces.push_back(tower.release());

        // The two layout NBTs define the clearing and the depth of a wing.
        const auto* base = manager->getNamed(localPrefix + L"base_plate");
        const auto* wing = manager->getNamed(localPrefix + L"feature_plate");
        const int distance = std::min(24, std::max(18, (base ? base->getSize().x : 16) / 2
                                                        + (wing ? wing->getSize().z : 32) / 3));
        std::array<const wchar_t*, 6> buildings {L"feature_cage1", L"feature_cage2", L"feature_targets",
                                                L"feature_logs", L"feature_tent1", L"feature_tent2"};
        for (size_t i = buildings.size() - 1; i > 0; --i)
            std::swap(buildings[i], buildings[random.nextInt(static_cast<int>(i + 1))]);
        const int firstSide = random.nextInt(4);
        constexpr int sides[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        const int wanted = 2 + random.nextInt(3);
        int added = 0;
        for (int step = 0; step < 4 && added < wanted; ++step)
        {
            const auto& side = sides[(step + firstSide) % 4];
            const auto plot = center.offset(side[0] * distance, 0, side[1] * distance);
            auto piece = makePiece(*manager, terrain, world, buildings[added], plot,
                                   Rotation::VALUES[random.nextInt(4)]);
            if (piece)
            {
                mPieces.push_back(piece.release());
                ++added;
            }
        }
        calculateBoundingBox();
    }

    static StructureStart* create() { return new OutpostStart(); }
    EStructureStart GetType() override { return eStructureStart_PillagerOutpost; }
    bool isValid() override { return !mPieces.empty(); }

    bool isValidForPostProcess(ChunkPos* pos) override
    {
        return !mProcessed.count(ChunkPos::asLong(pos->x, pos->z));
    }

    void notifyPostProcessAt(ChunkPos* pos) override
    {
        mProcessed.insert(ChunkPos::asLong(pos->x, pos->z));
        setDirty();
    }

    void addAdditionalSaveData(DataOutputStream* output) override
    {
        output->writeInt(static_cast<int>(mProcessed.size()));
        for (const auto key : mProcessed)
            output->writeLong(key);
    }

    void readAdditionalSaveData(DataInputStream* input) override
    {
        const int count = std::min(64, input->readInt());
        for (int i = 0; i < count; ++i)
            mProcessed.insert(input->readLong());
    }

    bool activeNear(const BlockPos& pos)
    {
        if (!isValid())
            return false;
        const int cx = static_cast<int>(getChunkX()), cz = static_cast<int>(getChunkZ());
        if (!mProcessed.count(ChunkPos::asLong(cx, cz)))
            return false;
        const auto* tower = mPieces.front()->getBoundingBox();
        const int dx = pos.x - (cx * 16 + 8), dz = pos.z - (cz * 16 + 8);
        return dx * dx + dz * dz <= PillagerOutpostFeature::PatrolRadius * PillagerOutpostFeature::PatrolRadius
            && pos.y >= tower->y0 - 4 && pos.y <= tower->y1 + 4;
    }

    void updateLoadedBanners(LevelChunk& chunk)
    {
        if (isValid() && mProcessed.count(ChunkPos::asLong(chunk.x, chunk.z)))
            static_cast<OutpostPiece*>(mPieces.front())->updateBanners(chunk);
    }

private:
    std::set<long long> mProcessed;
};

int regionAt(int chunk, int size)
{
    return (chunk < 0 ? chunk - size + 1 : chunk) / size;
}
}

PillagerOutpostFeature::PillagerOutpostFeature(GenericOverworldLevelSource* terrain, bool villagesEnabled)
    : mTerrain(terrain), mVillagesEnabled(villagesEnabled)
{
    level = nullptr;
    range = 3;
}

void PillagerOutpostFeature::registerPieces()
{
    StructureFeatureIO::setStartId(eStructureStart_PillagerOutpost, OutpostStart::create, LargeFeature::PILLAGER_OUTPOST);
    StructureFeatureIO::setPieceId(eStructurePiece_OutpostTemplate, OutpostPiece::create, L"OutpostTemplate");
}

std::wstring PillagerOutpostFeature::getFeatureName()
{
    return LargeFeature::PILLAGER_OUTPOST;
}

bool PillagerOutpostFeature::isFeatureChunk(int chunkX, int chunkZ, bool flatWorld)
{
    if (flatWorld || !level)
        return false;
    const int halfSize = level->getLevelData()->getXZSize() / 2;
    if (chunkX < -halfSize + 2 || chunkZ < -halfSize + 2 || chunkX >= halfSize - 2 || chunkZ >= halfSize - 2)
        return false;

    const int rx = regionAt(chunkX, spacing), rz = regionAt(chunkZ, spacing);
    auto* candidates = level->getRandomFor(rx, rz, salt);
    const int x = rx * spacing + candidates->nextInt(spacing - separation);
    const int z = rz * spacing + candidates->nextInt(spacing - separation);
    if (x != chunkX || z != chunkZ)
        return false;
    const auto& biomes = VillageFeature::allowedBiomes;
    if (!level->getBiomeSource()->containsOnly(x * 16 + 8, z * 16 + 8, 0, biomes))
        return false;

    if (mVillagesEnabled)
    {
        constexpr int villageSpacing = 16;
        constexpr int buffer = 5;
        for (int vx = regionAt(x - buffer, villageSpacing); vx <= regionAt(x + buffer, villageSpacing); ++vx)
            for (int vz = regionAt(z - buffer, villageSpacing); vz <= regionAt(z + buffer, villageSpacing); ++vz)
            {
                auto* villageRandom = level->getRandomFor(vx, vz, 10387312);
                const int cx = vx * villageSpacing + villageRandom->nextInt(8);
                const int cz = vz * villageSpacing + villageRandom->nextInt(8);
                if (std::abs(x - cx) <= buffer && std::abs(z - cz) <= buffer
                    && level->getBiomeSource()->containsOnly(cx * 16 + 8, cz * 16 + 8, 0, biomes))
                    return false;
            }
    }
    return true;
}

void PillagerOutpostFeature::addFeature(Level* world, int x, int z, int originX, int originZ, ChunkPrimer* primer)
{
    if (!isFeatureChunk(x, z, false) || world->isChunkGeneratedAt(x, z))
        return;
    StructureFeature::addFeature(world, x, z, originX, originZ, primer);
}

StructureStart* PillagerOutpostFeature::createStructureStart(int x, int z)
{
    return new OutpostStart(*level, mTerrain, random, x, z);
}

BlockPos* PillagerOutpostFeature::getNearestGeneratedFeature(Level* world, const BlockPos& pos, bool skipExistingChunks)
{
    level = world;
    const int maxDistance = world->getLevelData()->getXZSize();
    return findNearestFeaturePositionBySpacing(world, this, pos, spacing, separation, salt,
                                               false, maxDistance, skipExistingChunks);
}

bool PillagerOutpostFeature::isLocatableChunk(int x, int z)
{
    const auto entry = mStructureMap.find(ChunkPos::asLong(x, z));
    if (entry != mStructureMap.end())
        return entry->second->isValid();
    if (level->isChunkGeneratedAt(x, z))
        return false;
    Random seeded(level->getSeed());
    setupChunkSeed(level->getSeed(), seeded, x, z);
    seeded.nextInt();
    OutpostStart candidate(*level, mTerrain, seeded, x, z);
    return candidate.isValid();
}

bool PillagerOutpostFeature::isPatrolArea(Level* world, const BlockPos& pos)
{
    restoreSavedData(world);
    for (const auto& entry : mStructureMap)
        if (static_cast<OutpostStart*>(entry.second)->activeNear(pos))
            return true;
    return false;
}

void PillagerOutpostFeature::updateLoadedBanners(Level* world, LevelChunk& chunk)
{
    restoreSavedData(world);
    for (const auto& entry : mStructureMap)
        static_cast<OutpostStart*>(entry.second)->updateLoadedBanners(chunk);
}

std::vector<Biome::MobSpawnerData*>* PillagerOutpostFeature::getEnemies()
{
    static Biome::MobSpawnerData pillagers(1, EntityType::PILLAGER, 1, 1);
    static std::vector<Biome::MobSpawnerData*> enemies {&pillagers};
    return &enemies;
}
