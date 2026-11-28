#include "game/world/generation/ModernVillagePieces.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include "game/core/BoundingBox.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/Direction.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/biome/BiomeSource.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/CauldronBlock.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/DoorBlock.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/block/StairBlock.hpp"
#include "game/world/chunk/ChunkPos.hpp"
#include "game/world/generation/StructureFeatureIO.hpp"
#include "game/world/generation/StructurePiece.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/ZombieVillager.hpp"

namespace
{
enum class Style { Plains, Desert, Savanna, Taiga, Snow };
enum class Plot { Square, Road, House, Farm, Library, Forge, Fisher, Butcher, Mason, Cartographer, Weaver, Chapel,
                  Fletcher, Tannery };

struct Materials
{
    const BlockState* foundation;
    const BlockState* wall;
    const BlockState* beam;
    const BlockState* boards;
    Block* fence;
    Block* stair;
    DoorBlock* door;
};

Style getVillageStyle(const Biome* biome)
{
    if (biome == Biome::DESERT) return Style::Desert;
    if (biome == Biome::SAVANNA) return Style::Savanna;
    if (biome == Biome::TAIGA) return Style::Taiga;
    if (biome == Biome::ICE_PLAINS || biome == Biome::COLD_TAIGA) return Style::Snow;
    return Style::Plains;
}

Materials materialsFor(Style style)
{
    auto* stone = Blocks::cobblestone->defaultBlockState();
    auto* spruce = Blocks::planks->getBlockState(1);
    auto* spruceLog = Blocks::log->getBlockState(1);
    switch (style)
    {
    case Style::Desert:
        return {Blocks::sandstone->defaultBlockState(), Blocks::sandstone->getBlockState(2),
                Blocks::sandstone->getBlockState(1), Blocks::planks->getBlockState(2),
                Blocks::birch_fence, Blocks::sandstone_stairs, static_cast<DoorBlock*>(Blocks::birch_door)};
    case Style::Savanna:
        return {stone, Blocks::hardened_clay->defaultBlockState(), Blocks::log2->getBlockState(0),
                Blocks::planks->getBlockState(4), Blocks::acacia_fence, Blocks::acacia_stairs,
                static_cast<DoorBlock*>(Blocks::acacia_door)};
    case Style::Taiga:
        return {stone, spruce, spruceLog, spruce, Blocks::spruce_fence, Blocks::spruce_stairs,
                static_cast<DoorBlock*>(Blocks::spruce_door)};
    case Style::Snow:
        return {Blocks::stonebrick->defaultBlockState(), Blocks::snow->defaultBlockState(), spruceLog,
                spruce, Blocks::spruce_fence, Blocks::spruce_stairs, static_cast<DoorBlock*>(Blocks::spruce_door)};
    case Style::Plains:
        return {stone, Blocks::planks->defaultBlockState(), Blocks::log->defaultBlockState(),
                Blocks::planks->defaultBlockState(), Blocks::fence, Blocks::oak_stairs,
                static_cast<DoorBlock*>(Blocks::wooden_door)};
    }
    return {};
}

int residentProfession(Plot plot)
{
    switch (plot)
    {
    case Plot::Library:
    case Plot::Cartographer:
        return 1;
    case Plot::Chapel:
        return 2;
    case Plot::Forge:
    case Plot::Mason:
        return 3;
    case Plot::Butcher:
    case Plot::Tannery:
        return 4;
    default:
        return 0;
    }
}

class VillagePlot : public StructurePiece
{
public:
    VillagePlot() = default;

    VillagePlot(Plot plot, Style style, bool abandoned, int variant, const BlockPos& front,
                const Direction* direction, int width, int length)
        : StructurePiece(0), mPlot(plot), mStyle(style), mVariant(variant), mAbandoned(abandoned)
    {
        setOrientation(direction);
        mBoundingBox = BoundingBox::orientBox(front.x, 64, front.z, -(width / 2), 0, 0, width, 12, length, direction);
    }

    static StructurePiece* create() { return new VillagePlot(); }
    EStructurePiece GetType() override { return eStructurePiece_ModernVillagePlot; }

    void addAdditionalSaveData(DataOutputStream* out) override
    {
        out->writeByte(static_cast<unsigned char>(mPlot));
        out->writeByte(static_cast<unsigned char>(mStyle));
        out->writeByte(static_cast<unsigned char>(mVariant));
        out->writeBoolean(mAbandoned);
        out->writeBoolean(mGrounded);
        out->writeInt(static_cast<int>(mFinishedChunks.size()));
        for (long long chunk : mFinishedChunks)
            out->writeLong(chunk);
    }

    void readAdditionalSaveData(DataInputStream* in, StructureManager*) override
    {
        mPlot = static_cast<Plot>(in->readByte());
        mStyle = static_cast<Style>(in->readByte());
        mVariant = in->readByte();
        mAbandoned = in->readBoolean();
        mGrounded = in->readBoolean();
        const int count = in->readInt();
        for (int i = 0; i < count; ++i)
            mFinishedChunks.push_back(in->readLong());
    }

    bool postProcess(Level* level, Random&, BoundingBox* clip) override
    {
        const long long chunk = ChunkPos::asLong(clip->x0 >> 4, clip->z0 >> 4);
        if (std::find(mFinishedChunks.begin(), mFinishedChunks.end(), chunk) != mFinishedChunks.end())
            return true;

        const auto materials = materialsFor(mStyle);
        if (mPlot == Plot::Road)
            buildRoad(level, clip, materials);
        else
        {
            const bool alongZ = getOrientation()->getAxis() == Direction::Axis::Z;
            const int width = alongZ ? mBoundingBox->getXSpan() : mBoundingBox->getZSpan();
            const int length = alongZ ? mBoundingBox->getZSpan() : mBoundingBox->getXSpan();
            const int anchorZ = mPlot == Plot::Square ? length / 2 : 0;
            if (!mGrounded)
            {
                BlockPos anchor(getWorldX(width / 2, anchorZ), 0, getWorldZ(width / 2, anchorZ));
                if (!level->hasChunkAt(anchor))
                {
                    anchor.x = std::max(clip->x0, std::min(anchor.x, clip->x1));
                    anchor.z = std::max(clip->z0, std::min(anchor.z, clip->z1));
                }
                const auto grade = level->getTopSolidBlockPos(anchor).below();
                const int height = std::max(1, std::min(grade.y, level->getMaxBuildHeight() - 12));
                mBoundingBox->move(0, height - mBoundingBox->y0, 0);
                mGrounded = true;
            }

            preparePlot(level, clip, materials, width, length);
            if (mPlot == Plot::Square)
                buildSquare(level, clip, materials, width);
            else if (mPlot == Plot::Farm)
                buildFarm(level, clip, materials, width, length);
            else
                buildHouse(level, clip, materials, width, length);
        }
        mFinishedChunks.push_back(chunk);
        setDirty();
        return true;
    }

private:
    const BlockState* path() const
    {
        return mStyle == Style::Desert ? Blocks::sandstone->getBlockState(2) : Blocks::grass_path->defaultBlockState();
    }

    void buildRoad(Level* level, BoundingBox* clip, const Materials& materials)
    {
        for (int x = std::max(mBoundingBox->x0, clip->x0); x <= std::min(mBoundingBox->x1, clip->x1); ++x)
        {
            for (int z = std::max(mBoundingBox->z0, clip->z0); z <= std::min(mBoundingBox->z1, clip->z1); ++z)
            {
                BlockPos top = level->getTopSolidBlockPos(BlockPos(x, 0, z));
                const bool water = level->getBlockState(top)->getMaterial() == Material::WATER;
                if (water)
                    top.y = std::max(top.y, level->getSeaLevel());
                if (top.y > 1)
                    level->setBlock(top.below(), water ? materials.boards : path(), Block::UPDATE_CLIENTS, false);
                if (!water && (level->getBlock(top) == Blocks::tallgrass || level->getBlock(top) == Blocks::snow_layer))
                    level->setBlock(top, Blocks::air->defaultBlockState(), Block::UPDATE_CLIENTS, false);
            }
        }
    }

    void preparePlot(Level* level, BoundingBox* clip, const Materials& materials, int width, int length)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < length; ++z)
            {
                const BlockPos floor(getWorldX(x, z), getWorldY(0), getWorldZ(x, z));
                if (!clip->isInside(floor)) continue;
                const int ceiling = std::max(floor.y + 9, level->getTopSolidBlockPos(floor).y + 1);
                for (int y = floor.y + 1; y < std::min(ceiling, level->getMaxBuildHeight()); ++y)
                {
                    const BlockPos pos(floor.x, y, floor.z);
                    if (!level->isEmptyBlock(pos))
                        level->setBlock(pos, Blocks::air->defaultBlockState(), Block::UPDATE_CLIENTS, false);
                }
                placeBlock(level, materials.foundation, x, 0, z, clip);
                fillColumnDown(level, materials.foundation, x, -1, z, clip);
            }
        }
    }

    void buildSquare(Level* level, BoundingBox* clip, const Materials& materials, int width)
    {
        const int center = width / 2;
        const auto* paving = mStyle == Style::Desert ? materials.wall : Blocks::stonebrick->defaultBlockState();
        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < width; ++z)
            {
                const bool apron = x >= 2 && x < width - 2 && z >= 2 && z < width - 2;
                placeBlock(level, apron ? paving : path(), x, 0, z, clip);
            }
        }
        placeBlock(level, Blocks::bell->defaultBlockState(), center, 1, center, clip);
        for (int x : {2, width - 3})
        {
            for (int z : {2, width - 3})
            {
                placeBlock(level, materials.foundation, x, 1, z, clip);
                if (!mAbandoned)
                    placeBlock(level, Blocks::lantern->defaultBlockState(), x, 2, z, clip);
            }
        }
    }

    void buildFarm(Level* level, BoundingBox* clip, const Materials& materials, int width, int length)
    {
        const int middle = width / 2;
        const auto* water = Blocks::water->defaultBlockState();
        const auto* moistSoil = Blocks::farmland->getBlockState(7);
        Block* crops[] = {Blocks::wheat, Blocks::carrots, Blocks::potatoes};
        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < length; ++z)
            {
                if (x == 0 || x == width - 1 || z == length - 1)
                {
                    placeBlock(level, materials.beam, x, 0, z, clip);
                    placeBlock(level, materials.fence->defaultBlockState(), x, 1, z, clip);
                }
                else if (z < 2)
                {
                    placeBlock(level, path(), x, 0, z, clip);
                }
                else if (x == middle)
                {
                    placeBlock(level, water, x, 0, z, clip);
                }
                else
                {
                    placeBlock(level, moistSoil, x, 0, z, clip);
                    const int crop = (mVariant + (x > middle ? 1 : 0)) % 3;
                    placeBlock(level, crops[crop]->getBlockState(4 + (x + z + mVariant) % 4), x, 1, z, clip);
                }
            }
        }
        placeBlock(level, Blocks::composter->defaultBlockState(), width - 2, 1, 1, clip);
        placeSupplyChest(level, clip, 1, 1, &BuiltInLootTables::VILLAGE_FARM_SUPPLIES);
        spawnResident(level, clip, middle, 1);
    }

    void buildHouse(Level* level, BoundingBox* clip, const Materials& materials, int width, int length)
    {
        const int middle = width / 2;
        const int rear = length - 1;
        const auto* air = Blocks::air->defaultBlockState();
        const auto* glass = Blocks::glass_pane->defaultBlockState();
        const bool tall = mPlot == Plot::Chapel || mPlot == Plot::Library;
        const int wallTop = tall ? 5 : 3 + mVariant % 2;

        generateBox(level, clip, 1, 0, 1, width - 2, 0, rear - 1, materials.boards, materials.boards, false);
        generateBox(level, clip, 0, 1, 1, width - 1, wallTop, rear, materials.wall, air, false);
        for (int x : {0, width - 1})
        {
            for (int z : {1, rear})
                generateBox(level, clip, x, 1, z, x, wallTop, z, materials.beam, materials.beam, false);
            placeBlock(level, glass, x, 2, length / 2, clip);
            if (tall)
                placeBlock(level, glass, x, 3, length / 2, clip);
        }
        placeBlock(level, glass, middle - 2, 2, 1, clip);
        placeBlock(level, glass, middle + 2, 2, 1, clip);
        placeBlock(level, glass, middle, 2, rear, clip);
        for (int x = middle - 1; x <= middle + 1; ++x)
            placeBlock(level, path(), x, 0, 0, clip);

        if (mAbandoned)
        {
            placeBlock(level, air, middle, 1, 1, clip);
            placeBlock(level, air, middle, 2, 1, clip);
            placeBlock(level, Blocks::web->defaultBlockState(), 1, wallTop - 1, rear - 1, clip);
        }
        else
        {
            Random doorRandom(mVariant);
            createDoor(level, clip, doorRandom, middle, 1, 1, Direction::NORTH, materials.door);
            placeBlock(level, Blocks::lantern->defaultBlockState(), middle + 2, 1, 0, clip);
        }
        buildRoof(level, clip, materials, width, length, wallTop + 1);
        furnish(level, clip, width, length);
        spawnResident(level, clip, middle, 3);
    }

    void buildRoof(Level* level, BoundingBox* clip, const Materials& materials, int width, int length, int eave)
    {
        const int last = width - 1;
        const int rear = length - 1;
        if (mStyle == Style::Desert || mPlot == Plot::Forge)
        {
            generateBox(level, clip, 0, eave, 0, last, eave, rear, materials.foundation, materials.foundation, false);
            for (int x = 0; x <= last; x += 2)
            {
                placeBlock(level, materials.beam, x, eave + 1, 0, clip);
                placeBlock(level, materials.beam, x, eave + 1, rear, clip);
            }
            return;
        }
        auto* left = materials.stair->defaultBlockState()->setValue(StairBlock::FACING, Direction::EAST);
        auto* right = materials.stair->defaultBlockState()->setValue(StairBlock::FACING, Direction::WEST);
        const int roofStart = mStyle == Style::Savanna ? 1 : 0;
        for (int i = 0; i < last / 2; ++i)
        {
            const int y = eave + i;
            for (int z = roofStart; z <= rear; ++z)
            {
                placeBlock(level, left, i, y, z, clip);
                placeBlock(level, right, last - i, y, z, clip);
            }
            for (int x = i + 1; x < last - i; ++x)
            {
                placeBlock(level, materials.wall, x, y, 1, clip);
                placeBlock(level, materials.wall, x, y, rear, clip);
            }
        }
        for (int z = roofStart; z <= rear; ++z)
            placeBlock(level, materials.beam, last / 2, eave + last / 2, z, clip);
    }

    void furnish(Level* level, BoundingBox* clip, int width, int length)
    {
        const int x = width - 2;
        const int rear = length - 2;
        const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::NORTH);
        placeBlock(level, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 1, 1, rear - 1, clip);
        placeBlock(level, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 1, 1, rear, clip);

        switch (mPlot)
        {
        case Plot::Library:
            placeBlock(level, Blocks::lectern->defaultBlockState(), x, 1, 2, clip);
            generateBox(level, clip, x, 1, rear - 1, x, 2, rear, Blocks::bookshelf->defaultBlockState(),
                        Blocks::bookshelf->defaultBlockState(), false);
            break;
        case Plot::Forge:
            placeBlock(level, Blocks::blast_furnace->defaultBlockState(), x, 1, 2, clip);
            placeBlock(level, Blocks::smithing_table->defaultBlockState(), x, 1, 3, clip);
            placeBlock(level, Blocks::grindstone->defaultBlockState(), x, 1, 4, clip);
            placeBlock(level, Blocks::stonebrick->defaultBlockState(), x, 2, 2, clip);
            break;
        case Plot::Fisher:
            placeBlock(level, Blocks::barrel->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Butcher:
            placeBlock(level, Blocks::smoker->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Mason:
            placeBlock(level, Blocks::stonecutter->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Cartographer:
            placeBlock(level, Blocks::cartography_table->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Weaver:
            placeBlock(level, Blocks::loom->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Chapel:
            placeBlock(level, Blocks::stonebrick->defaultBlockState(), x, 1, 2, clip);
            placeBlock(level, Blocks::brewing_stand->defaultBlockState(), x, 2, 2, clip);
            break;
        case Plot::Fletcher:
            placeBlock(level, Blocks::fletching_table->defaultBlockState(), x, 1, 2, clip);
            break;
        case Plot::Tannery:
            placeBlock(level, Blocks::cauldron->defaultBlockState(), x, 1, 2, clip);
            break;
        default:
            break;
        }
        auto* loot = mPlot == Plot::House ? &BuiltInLootTables::VILLAGE_HOME_SUPPLIES
                                        : &BuiltInLootTables::VILLAGE_WORKSHOP_SUPPLIES;
        placeSupplyChest(level, clip, x - 1, rear, loot);
    }

    void placeSupplyChest(Level* level, BoundingBox* clip, int x, int z, ResourceLocation* table)
    {
        const BlockPos position(getWorldX(x, z), getWorldY(1), getWorldZ(x, z));
        Random lootSeed(level->getSeed() ^ position.compressBlockPos());
        createChest(level, clip, lootSeed, x, 1, z, table);
    }

    void spawnResident(Level* level, BoundingBox* clip, int x, int z)
    {
        const BlockPos pos(getWorldX(x, z), getWorldY(1), getWorldZ(x, z));
        if (level->mIsClientSide || !clip->isInside(pos))
            return;
        if (mAbandoned)
        {
            auto zombie = boost::shared_ptr<ZombieVillager>(new ZombieVillager(level));
            zombie->moveTo(pos.x + 0.5, pos.y, pos.z + 0.5, 0, 0);
            zombie->finalizeMobSpawn(level->getCurrentDifficultyAt(pos), nullptr, L"");
            zombie->setProfession(residentProfession(mPlot));
            zombie->setPersistenceRequired(true);
            level->addEntity(zombie);
        }
        else
        {
            auto villager = boost::shared_ptr<Villager>(new Villager(level));
            villager->moveTo(pos.x + 0.5, pos.y, pos.z + 0.5, 0, 0);
            villager->setProfession(residentProfession(mPlot));
            villager->finalizeMobSpawn(level->getCurrentDifficultyAt(pos), nullptr, false, L"");
            villager->setPersistenceRequired(true);
            level->addEntity(villager);
        }
    }

    Plot mPlot = Plot::Square;
    Style mStyle = Style::Plains;
    int mVariant = 0;
    bool mAbandoned = false;
    bool mGrounded = false;
    std::vector<long long> mFinishedChunks;
};
}

void ModernVillagePieces::registerPieces()
{
    StructureFeatureIO::setPieceId(eStructurePiece_ModernVillagePlot, VillagePlot::create, L"LCEViPlot");
}

void ModernVillagePieces::generate(Level* level, Random& random, int chunkX, int chunkZ, int size, int worldWidth,
                                  std::vector<StructurePiece*>& pieces)
{
    const BlockPos center(chunkX * 16 + 8, 64, chunkZ * 16 + 8);
    const Style style = getVillageStyle(level->getBiomeSource()->getBiome(center));
    const bool abandoned = random.nextInt(50) == 0;
    const auto addPlot = [&](Plot plot, const BlockPos& front, const Direction* away, int width, int length) {
        auto piece = std::unique_ptr<VillagePlot>(new VillagePlot(plot, style, abandoned, random.nextInt(6), front,
                                                                  away, width, length));
        auto* box = piece->getBoundingBox();
        const int limit = worldWidth / 2 - 1;
        if (box->x0 < -limit || box->x1 > limit || box->z0 < -limit || box->z1 > limit)
            return false;
        if (StructurePiece::findCollisionPiece(&pieces, box))
            return false;
        pieces.push_back(piece.release());
        return true;
    };
    if (!addPlot(Plot::Square, center.north(5), Direction::SOUTH, 11, 11))
        return;

    const std::array<const Direction*, 4> arms = {Direction::NORTH, Direction::EAST, Direction::SOUTH, Direction::WEST};
    const std::array<std::array<Plot, 4>, 4> neighborhoods = {{
        {Plot::House, Plot::Library, Plot::Farm, Plot::House},
        {Plot::Forge, Plot::House, Plot::Fisher, Plot::Butcher},
        {Plot::Mason, Plot::House, Plot::Farm, Plot::Cartographer},
        {Plot::Tannery, Plot::Weaver, Plot::Chapel, Plot::Fletcher},
    }};
    const int rotation = random.nextInt(4);
    const bool reverse = random.nextBoolean();
    for (int arm = 0; arm < 4; ++arm)
    {
        const auto* direction = arms[(rotation + arm) % 4];
        const auto* side = arms[(rotation + arm + (reverse ? 3 : 1)) % 4];
        const int length = 26 + std::min(4, size) * 2 + random.nextInt(3);
        if (!addPlot(Plot::Road, center.relative(direction, 6), direction, 3, length))
            continue;
        for (int slot = 0; slot < 4; ++slot)
        {
            const Plot plot = neighborhoods[arm][slot];
            const auto* outward = slot % 2 == 0 ? side : side->getOpposite();
            const auto front = center.relative(direction, slot < 2 ? 15 : 26).relative(outward, 2);
            const int width = plot == Plot::House ? 7 : 9;
            const int depth = plot == Plot::Farm ? 9 : (plot == Plot::House ? 7 : 8);
            addPlot(plot, front, outward, width, depth);
        }
    }
}
