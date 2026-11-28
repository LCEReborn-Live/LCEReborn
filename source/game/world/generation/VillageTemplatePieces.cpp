#include "game/world/generation/VillageTemplatePieces.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "game/core/BoundingBox.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/Direction.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/data/LevelStorage.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/Mob.hpp"
#include "game/util/Random.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/GenericOverworldLevelSource.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/biome/BiomeSource.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/chunk/ChunkPos.hpp"
#include "game/world/generation/StructureFeatureIO.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructurePiece.hpp"
#include "game/world/generation/StructurePlaceSettings.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include "game/world/generation/StructureTerrain.hpp"
#include "game/world/generation/NativeVillageRegistry.hpp"
#include "game/world/generation/Villager.hpp"

namespace
{
using Column = std::pair<int, int>;
using Columns = std::set<Column>;

const wchar_t* biomeFolder(const Biome* biome)
{
    if (biome == Biome::DESERT) return L"desert";
    if (biome == Biome::SAVANNA) return L"savanna";
    if (biome == Biome::TAIGA) return L"taiga";
    if (biome == Biome::ICE_PLAINS || biome == Biome::COLD_TAIGA) return L"snowy";
    return L"plains";
}

class VillageTemplatePiece : public StructurePiece
{
public:
    VillageTemplatePiece() = default;
    VillageTemplatePiece(const std::wstring& feature, const BlockPos& origin, int depth)
        : StructurePiece(depth), mPath(feature), mOrigin(origin), mRotation(Rotation::NONE)
    {
        mBoundingBox = BoundingBox::createProper(origin.x, origin.y, origin.z, origin.x, origin.y, origin.z);
    }
    VillageTemplatePiece(const std::wstring& path, const StructureTemplate* structure, const BlockPos& origin,
                         Rotation* rotation, int depth, bool street)
        : StructurePiece(depth), mPath(path), mTemplate(structure), mOrigin(origin), mRotation(rotation),
          mStreet(street)
    {
        mBoundingBox = structure->getTransformedBounds(origin, Mirror::NONE, rotation);
        for (const auto* entity : structure->getEntities())
        {
            const auto pos = worldPos(entity->mBlockPos);
            mBoundingBox->x0 = std::min(mBoundingBox->x0, pos.x);
            mBoundingBox->y0 = std::min(mBoundingBox->y0, pos.y);
            mBoundingBox->z0 = std::min(mBoundingBox->z0, pos.z);
            mBoundingBox->x1 = std::max(mBoundingBox->x1, pos.x);
            mBoundingBox->y1 = std::max(mBoundingBox->y1, pos.y);
            mBoundingBox->z1 = std::max(mBoundingBox->z1, pos.z);
        }
    }

    static StructurePiece* create() { return new VillageTemplatePiece(); }
    EStructurePiece GetType() override { return eStructurePiece_VillageTemplate; }

    BlockPos worldPos(const BlockPos& pos) const
    {
        return StructureTemplate::transform(pos, Mirror::NONE, mRotation).offset(mOrigin);
    }

    BlockPos connection(const StructureTemplate::Connector& connector) const
    {
        const auto pos = worldPos(connector.pos);
        return pos.above(offsetAt(pos));
    }

    const Direction* facing(const StructureTemplate::Connector& connector) const
    {
        return mRotation->rotate(connector.facing);
    }

    const StructureTemplate* structure() const { return mTemplate; }

    bool hasVillageResident() const
    {
        if (!mTemplate)
            return false;
        const auto& entities = mTemplate->getEntities();
        return std::any_of(entities.begin(), entities.end(), [](const auto* entity) {
            const auto type = entity->mNbt->getString(L"id");
            return type == L"minecraft:villager" || type == L"minecraft:zombie_villager";
        });
    }

    bool hasTerrainBlocks() const
    {
        return std::any_of(mTemplate->getBlocks().begin(), mTemplate->getBlocks().end(), [](const auto* block) {
            return block->mState->getBlock() != Blocks::air && block->mState->getBlock() != Blocks::structure_block;
        });
    }

    Columns footprint() const
    {
        Columns result;
        if (!hasTerrainBlocks())
            return result;
        if (mStreet)
        {
            for (const auto* block : mTemplate->getBlocks())
            {
                if (block->mState->getBlock() != Blocks::air)
                {
                    auto pos = worldPos(block->mPos);
                    result.emplace(pos.x, pos.z);
                }
            }
        }
        else
        {
            for (int x = mBoundingBox->x0; x <= mBoundingBox->x1; ++x)
                for (int z = mBoundingBox->z0; z <= mBoundingBox->z1; ++z)
                    result.emplace(x, z);
        }
        return result;
    }

    void alignStreet(StructureTerrain& terrain, const BlockPos& joint)
    {
        const int xSpan = mBoundingBox->getXSpan();
        const int zSpan = mBoundingBox->getZSpan();
        mOffsets.resize(xSpan * zSpan);
        for (int x = mBoundingBox->x0; x <= mBoundingBox->x1; ++x)
        {
            for (int z = mBoundingBox->z0; z <= mBoundingBox->z1; ++z)
            {
                const int dist = std::abs(x - joint.x) + std::abs(z - joint.z);
                const int maxRise = std::min(9, std::max(0, dist - 1));
                const int rise = terrain.surface(x, z).ground - mOrigin.y;
                mOffsets[index(x, z)] = std::max(-maxRise, std::min(maxRise, rise));
            }
        }
        // Limit grades to one block per step while keeping the entrance pinned.
        for (int x = mBoundingBox->x0; x <= mBoundingBox->x1; ++x)
        {
            for (int z = mBoundingBox->z0; z <= mBoundingBox->z1; ++z)
            {
                auto& value = mOffsets[index(x, z)];
                if (x > mBoundingBox->x0) value = std::min<int>(value, mOffsets[index(x - 1, z)] + 1);
                if (z > mBoundingBox->z0) value = std::min<int>(value, mOffsets[index(x, z - 1)] + 1);
            }
        }
        for (int x = mBoundingBox->x1; x >= mBoundingBox->x0; --x)
        {
            for (int z = mBoundingBox->z1; z >= mBoundingBox->z0; --z)
            {
                auto& value = mOffsets[index(x, z)];
                if (x < mBoundingBox->x1) value = std::min<int>(value, mOffsets[index(x + 1, z)] + 1);
                if (z < mBoundingBox->z1) value = std::min<int>(value, mOffsets[index(x, z + 1)] + 1);
            }
        }
        const auto extremes = std::minmax_element(mOffsets.begin(), mOffsets.end());
        mBoundingBox->y0 += *extremes.first;
        mBoundingBox->y1 += *extremes.second;
    }

    bool fitsTerrain(StructureTerrain& terrain) const
    {
        // A connected plot must fit without a deep trench or a tall artificial platform.
        for (const auto* entry : mTemplate->getBlocks())
        {
            const auto* block = entry->mState->getBlock();
            if (entry->mPos.y != 0 || block == Blocks::air || block == Blocks::structure_block)
                continue;
            const auto pos = worldPos(entry->mPos);
            const auto surface = terrain.surface(pos.x, pos.z);
            const int difference = pos.y + offsetAt(pos) - surface.ground;
            if (surface.flooded || difference < -2 || difference > 4)
                return false;
        }
        return true;
    }

    void addAdditionalSaveData(DataOutputStream* out) override
    {
        out->writeUTF(mPath);
        out->writeInt(mOrigin.x);
        out->writeInt(mOrigin.y);
        out->writeInt(mOrigin.z);
        out->writeByte(mRotation->getType());
        out->writeBoolean(mStreet);
        out->writeInt(static_cast<int>(mOffsets.size()));
        for (auto offset : mOffsets)
            out->writeByte(offset);
        out->writeInt(static_cast<int>(mFinishedChunks.size()));
        for (auto chunk : mFinishedChunks)
            out->writeLong(chunk);
    }

    void readAdditionalSaveData(DataInputStream* in, StructureManager* manager) override
    {
        mPath = in->readUTF();
        const int x = in->readInt();
        const int y = in->readInt();
        const int z = in->readInt();
        mOrigin = BlockPos(x, y, z);
        const auto rotation = in->readByte();
        mRotation = Rotation::VALUES[rotation & 3];
        mStreet = in->readBoolean();
        const int count = in->readInt();
        for (int i = 0; i < count; ++i)
            mOffsets.push_back(static_cast<signed char>(in->readByte()));
        const int completed = in->readInt();
        for (int i = 0; i < completed; ++i)
            mFinishedChunks.insert(in->readLong());
        mTemplate = manager ? manager->getNamed(mPath) : nullptr;
    }

    bool postProcess(Level* level, Random&, BoundingBox* clip) override
    {
        const long long chunk = ChunkPos::asLong(clip->x0 >> 4, clip->z0 >> 4);
        if (NativeVillageRegistry::isFeature(mPath))
        {
            if (!mFinishedChunks.count(chunk) && clip->isInside(mOrigin) && !level->mIsClientSide)
            {
                unsigned long long seed = static_cast<unsigned long long>(level->getSeed())
                    ^ static_cast<unsigned long long>(mOrigin.compressBlockPos());
                for (auto ch : mPath) seed = seed * 31 + static_cast<unsigned int>(ch);
                Random featureRandom(static_cast<long long>(seed));
                NativeVillageRegistry::placeFeature(mPath, level, featureRandom, mOrigin);
                mFinishedChunks.insert(chunk);
                setDirty();
            }
            return true;
        }
        if (!mTemplate || mFinishedChunks.count(chunk))
            return true;

        prepareTerrain(level, clip);
        StructurePlaceSettings settings;
        settings.setBoundingBox(clip)->setRotation(mRotation)->setIgnoreEntities(level->mIsClientSide);
        if (mStreet || !hasTerrainBlocks())
            settings.setIgnoreBlock(Blocks::air);
        if (mStreet)
        {
            settings.verticalOffset = [this](const BlockPos& pos) { return offsetAt(pos); };
        }
        settings.findSafeMobPosition = mPath.find(L"NativeVillage/") == 0;
        mTemplate->placeInWorld(level, mOrigin, &settings, Block::UPDATE_CLIENTS);
        if (!level->mIsClientSide)
            for (const auto& marker : mTemplate->getMarkers(mOrigin, &settings))
                spawnMarker(level, marker.first, marker.second);

        mFinishedChunks.insert(chunk);
        setDirty();
        return true;
    }

private:
    int index(int x, int z) const
    {
        return (x - mBoundingBox->x0) * mBoundingBox->getZSpan() + z - mBoundingBox->z0;
    }

    int offsetAt(const BlockPos& pos) const
    {
        return mOffsets.empty() ? 0 : mOffsets[index(pos.x, pos.z)];
    }

    void prepareTerrain(Level* level, BoundingBox* clip) const
    {
        const auto* air = Blocks::air->defaultBlockState();
        const bool desert = mPath.find(L"\\desert\\") != std::wstring::npos
            || mPath.find(L"LocalVillage/desert/") == 0 || mPath.find(L"NativeVillage/v1/desert/") == 0;
        const auto* foundation = (desert ? Blocks::sandstone : Blocks::dirt)->defaultBlockState();
        Columns prepared;
        for (const auto* entry : mTemplate->getBlocks())
        {
            const BlockPos pos = worldPos(entry->mPos);
            auto* block = entry->mState->getBlock();
            if (entry->mPos.y != 0 || block == Blocks::air || block == Blocks::structure_block || !clip->isInside(pos)
                || !prepared.emplace(pos.x, pos.z).second)
                continue;
            const auto floor = pos.above(offsetAt(pos));
            if (mStreet && block == Blocks::grass_path)
                for (int y = 1; y <= 2; ++y)
                    if (!level->getBlockState(floor.above(y))->getMaterial()->isLiquid())
                        level->setBlock(floor.above(y), air, Block::UPDATE_CLIENTS, false);
            for (auto below = floor.below(); below.y > 0 && below.y >= floor.y - 4; below = below.below())
            {
                auto* material = level->getBlockState(below)->getMaterial();
                if (material->blocksMotion() || material->isLiquid())
                    break;
                level->setBlock(below, foundation, Block::UPDATE_CLIENTS, false);
            }
        }
    }

    static void spawnMarker(Level* level, const BlockPos& pos, const std::wstring& marker)
    {
        unsigned long long type = 0;
        if (marker.find(L"minecraft:villager_v2") == 0) type = EntityType::VILLAGER;
        else if (marker.find(L"minecraft:iron_golem") == 0) type = EntityType::VILLAGER_GOLEM;
        else if (marker.find(L"minecraft:cat") == 0) type = EntityType::CAT;
        else if (marker == L"minecraft:horse") type = EntityType::HORSE;
        else if (marker == L"minecraft:pig") type = EntityType::PIG;
        else if (marker == L"minecraft:cow") type = EntityType::COW;
        else if (marker == L"minecraft:sheep") type = EntityType::SHEEP;
        if (!type)
            return;

        auto mob = boost::dynamic_pointer_cast<Mob>(EntityIO::newEntity(type, level));
        if (!mob)
            return;
        const auto feet = level->isEmptyBlock(pos.below()) ? pos.below() : pos;
        mob->moveTo(feet.x + 0.5, feet.y, feet.z + 0.5, 0, 0);
        mob->finalizeMobSpawn(level->getCurrentDifficultyAt(feet), nullptr, L"");
        mob->setPersistenceRequired(true);
        level->addEntity(mob);
    }

    std::wstring mPath;
    const StructureTemplate* mTemplate = nullptr;
    BlockPos mOrigin;
    Rotation* mRotation = nullptr;
    bool mStreet = false;
    std::vector<signed char> mOffsets;
    std::set<long long> mFinishedChunks;
};

struct OpenConnection
{
    BlockPos block;
    const Direction* facing;
    std::wstring type;
    std::wstring pool;
    int depth;
};

template <typename Visitor>
bool tryWeighted(Random& random, const std::vector<LocalVillageTemplateEntry>& entries, Visitor visit)
{
    std::vector<size_t> remaining;
    remaining.reserve(entries.size());
    long long totalWeight = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        remaining.push_back(i);
        totalWeight += entries[i].weight;
    }

    while (!remaining.empty())
    {
        auto ticket = std::min(totalWeight - 1, static_cast<long long>(random.nextDouble() * totalWeight));
        auto selected = remaining.begin();
        while (ticket >= entries[*selected].weight)
        {
            ticket -= entries[*selected].weight;
            ++selected;
        }
        const auto& entry = entries[*selected];
        if (visit(entry))
            return true;
        totalWeight -= entry.weight;
        remaining.erase(selected);
    }
    return false;
}

using PoolLookup = std::function<std::vector<LocalVillageTemplateEntry>(const std::wstring&)>;

bool assembleVillage(Level* level, StructureTerrain& terrain, Random& random, StructureManager* manager,
                     const BlockPos& center, int size, int worldWidth, const std::wstring& startPath,
                     const PoolLookup& templatesFor, std::vector<StructurePiece*>& pieces,
                     bool native = false)
{
    const auto* start = manager->getNamed(startPath);
    if (!start || start->getConnectors().empty())
        return false;

    const auto startSize = start->getSize();
    auto* rotation = Rotation::VALUES[random.nextInt(4)];
    const BlockPos pivot = StructureTemplate::transform(BlockPos(startSize.x / 2, 0, startSize.z / 2),
                                                       Mirror::NONE, rotation);
    const auto centerSurface = terrain.surface(center.x, center.z);
    if (native && centerSurface.flooded)
        return false;
    const BlockPos origin = center.subtract(pivot).above(std::max(1, centerSurface.ground));
    std::vector<std::unique_ptr<VillageTemplatePiece>> plan;
    Columns occupied;
    std::deque<OpenConnection> connections;
    const int extraSize = std::max(0, std::min(3, size));
    const int radius = 53 + extraSize * 8;
    const int limit = worldWidth / 2 - 1;
    const auto add = [&](std::unique_ptr<VillageTemplatePiece> piece, const StructureTemplate::Connector* entrance,
                         int depth) {
        const auto* bounds = piece->getBoundingBox();
        if (bounds->x0 < std::max(-limit, center.x - radius) || bounds->x1 > std::min(limit, center.x + radius)
            || bounds->z0 < std::max(-limit, center.z - radius) || bounds->z1 > std::min(limit, center.z + radius)
            || bounds->y0 < 1 || bounds->y1 >= level->getMaxBuildHeight())
            return false;
        auto footprint = piece->footprint();
        if (std::any_of(footprint.begin(), footprint.end(), [&](const auto& column) { return occupied.count(column); }))
            return false;
        if (native && !piece->fitsTerrain(terrain))
            return false;
        occupied.insert(footprint.begin(), footprint.end());
        for (const auto& connector : piece->structure()->getConnectors())
        {
            const bool vertical = connector.facing->getAxis() == Direction::Axis::Y;
            if (&connector == entrance || (!native && vertical))
                continue;
            if (connector.pool == L"lce:empty" || connector.pool == L"minecraft:empty")
                continue;
            connections.push_back({piece->connection(connector), piece->facing(connector),
                                   connector.target,
                                   connector.pool, depth + (vertical ? 0 : 1)});
        }
        plan.push_back(std::move(piece));
        return true;
    };
    if (!add(std::make_unique<VillageTemplatePiece>(startPath, start, origin, rotation, 0, false), nullptr, 0))
        return false;

    std::map<std::wstring, std::vector<LocalVillageTemplateEntry>> pools;
    const auto getPool = [&](const std::wstring& pool) -> const std::vector<LocalVillageTemplateEntry>& {
        const auto cached = pools.find(pool);
        if (cached != pools.end())
            return cached->second;
        return pools.emplace(pool, templatesFor(pool)).first->second;
    };
    const auto build = [&](const OpenConnection& open, const std::vector<LocalVillageTemplateEntry>& pool) {
        if (pool.empty())
            return false;
        const auto target = open.block.relative(open.facing);
        const auto tryPiece = [&](const LocalVillageTemplateEntry& entry) {
            const auto& path = entry.path;
            if (native && path.empty()) return true;
            if (native && NativeVillageRegistry::isFeature(path))
            {
                if (open.type != L"minecraft:bottom" || open.facing != Direction::UP) return false;
                const auto pos = entry.followTerrain ? BlockPos(target.x, terrain.surface(target.x, target.z).ground + 1, target.z)
                                                    : target;
                if (std::abs(pos.x - center.x) > radius || std::abs(pos.z - center.z) > radius
                    || pos.x < -limit || pos.x > limit || pos.z < -limit || pos.z > limit) return false;
                plan.emplace_back(new VillageTemplatePiece(path, pos, open.depth));
                return true;
            }
            const bool street = entry.followTerrain;
            const auto* structure = manager->getNamed(path);
            if (!structure)
                return false;
            for (const auto& entrance : structure->getConnectors())
            {
                if (entrance.type != open.type || (!native && entrance.facing->getAxis() == Direction::Axis::Y))
                    continue;
                for (auto* facing : Rotation::VALUES)
                {
                    if (facing->rotate(entrance.facing) != open.facing->getOpposite())
                        continue;
                    const auto corner = target.subtract(StructureTemplate::transform(entrance.pos, Mirror::NONE, facing));
                    if (!native && !street && std::abs(terrain.surface(target.x, target.z).ground - corner.y) > 8)
                        continue;
                    auto piece = std::make_unique<VillageTemplatePiece>(path, structure, corner, facing, open.depth, street);
                    if (street)
                        piece->alignStreet(terrain, target);
                    if (add(std::move(piece), &entrance, open.depth))
                        return true;
                }
            }
            return false;
        };
        return tryWeighted(random, pool, tryPiece);
    };
    while (!connections.empty() && plan.size() < (native ? 512u : 43u))
    {
        const auto open = connections.front();
        connections.pop_front();
        const auto pool = native ? open.pool : open.pool.substr(open.pool.find(L':') + 1);
        const bool built = open.depth <= 6 && build(open, getPool(pool));
        if (!built && native)
            build(open, getPool(NativeVillageRegistry::fallback(pool)));
    }
    if (plan.size() < 6 || (native && std::none_of(plan.begin(), plan.end(), [](const auto& piece) {
            return piece->hasVillageResident();
        })))
        return false;
    for (auto& piece : plan)
        pieces.push_back(piece.release());
    return true;
}
}

void VillageTemplatePieces::registerPieces()
{
    StructureFeatureIO::setPieceId(eStructurePiece_VillageTemplate, VillageTemplatePiece::create, L"LCEViTemplate");
}

bool VillageTemplatePieces::generate(Level* level, GenericOverworldLevelSource* source, Random& random, int chunkX,
                                     int chunkZ, int size, int worldWidth, std::vector<StructurePiece*>& pieces)
{
    auto* manager = level->getLevelStorage()->getStructureManager();
    if (!manager)
        return false;

    const BlockPos center(chunkX * 16 + 8, 0, chunkZ * 16 + 8);
    const std::wstring biome = biomeFolder(level->getBiomeSource()->getBiome(center));
    StructureTerrain terrain(level, source);
    const auto localPools = [&](const std::wstring& pool) { return manager->getLocalVillageTemplates(biome, pool); };
    const auto localCenters = manager->getLocalVillageStarts(biome);
    if (!localCenters.empty())
    {
        Random localRandom(level->getSeed() ^ ChunkPos::asLong(chunkX, chunkZ));
        if (tryWeighted(localRandom, localCenters, [&](const LocalVillageTemplateEntry& entry) {
                return assembleVillage(level, terrain, localRandom, manager, center, size, worldWidth,
                                       entry.path, localPools, pieces);
            }))
            return true;
    }

    const auto centers = NativeVillageRegistry::entries(L"minecraft:village/" + biome + L"/town_centers");
    return tryWeighted(random, centers, [&](const LocalVillageTemplateEntry& entry) {
        return assembleVillage(level, terrain, random, manager, center, size, worldWidth,
                               entry.path, NativeVillageRegistry::entries, pieces, true);
    });
}
