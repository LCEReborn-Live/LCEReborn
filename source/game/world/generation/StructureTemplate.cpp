#include "game/world/generation/StructureTemplate.hpp"
#include "game/world/block/BlockId.hpp"
#include "game/core/EntityType.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

#include <boost/shared_ptr.hpp>

#include "game/core/BoundingBox.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/Direction.hpp"
#include "game/core/DyeColor.hpp"
#include "game/core/AnonymousDataWalker.hpp"
#include "game/core/DataWalker.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/item/BannerItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/data/DataFixerUpper.hpp"
#include "game/data/DoubleTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/entity/mob/Mob.hpp"
#include "game/item/Container.hpp"
#include "game/util/Vec3.hpp"
#include "game/util/NbtUtils.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/block/BlockRotProcessor.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/generation/StructurePlaceSettings.hpp"
#include "game/world/generation/StructureProcessor.hpp"

static const std::wstring PALETTE_TAG = L"palette";
static const std::wstring ENTITIES_TAG = L"entities";
static const std::wstring BLOCKS_TAG = L"blocks";
static const std::wstring BLOCK_POS_TAG = L"pos";
static const std::wstring BLOCK_STATE_TAG = L"state";
static const std::wstring BLOCK_NBT_TAG = L"nbt";
static const std::wstring ENTITY_POS_TAG = L"pos";
static const std::wstring ENTITY_BLOCK_POS_TAG = L"blockPos";
static const std::wstring ENTITY_NBT_TAG = L"nbt";
static const std::wstring SIZE_TAG = L"size";
static const std::wstring AUTHOR_TAG = L"author";

namespace
{
const BlockState* readFinalState(const std::wstring& value, bool requireKnownBlock = false,
                                 std::wstring* problem = nullptr)
{
    if (value.empty())
        return Blocks::air->defaultBlockState();
    CompoundTag state;
    const auto bracket = value.find(L'[');
    state.putString(L"Name", value.substr(0, bracket));
    if (requireKnownBlock && bracket != std::wstring::npos && value.back() != L']')
    {
        if (problem)
            *problem = L"malformed final_state";
        return nullptr;
    }
    if (bracket != std::wstring::npos && value.back() == L']')
    {
        auto properties = std::make_unique<CompoundTag>();
        auto start = bracket + 1;
        while (start < value.size() - 1)
        {
            const auto sep = value.find(L'=', start);
            const auto end = value.find_first_of(L",]", start);
            if (sep >= end)
            {
                if (requireKnownBlock)
                {
                    if (problem)
                        *problem = L"malformed final_state property";
                    return nullptr;
                }
                break;
            }
            properties->putString(value.substr(start, sep - start), value.substr(sep + 1, end - sep - 1));
            start = end + 1;
        }
        state.put(L"Properties", properties.release());
    }
    return NbtUtils::readBlockState(&state, true, requireKnownBlock, problem);
}

const Direction* localConnectorFacing(CompoundTag* palette)
{
    auto* properties = palette->getCompound(L"Properties");
    auto facing = properties->getString(L"facing");
    if (facing.empty())
    {
        const auto orientation = properties->getString(L"orientation");
        facing = orientation.substr(0, orientation.find(L'_'));
    }
    for (const auto* direction : Direction::VALUES)
        if (direction->getSerializedName() == facing)
            return direction;
    return nullptr;
}

bool insideTemplate(const BlockPos& pos, const BlockPos& size)
{
    return pos.x >= 0 && pos.x < size.x && pos.y >= 0 && pos.y < size.y && pos.z >= 0 && pos.z < size.z;
}
}

StructureTemplate::StructureBlockInfo::~StructureBlockInfo()
{
    delete mNbt;
}

StructureTemplate::StructureEntityInfo::~StructureEntityInfo()
{
    delete mPos;
    delete mNbt;
}

StructureTemplate::StructureTemplate()
{
    mSize = BlockPos::ZERO;
    mAuthor = L"?";
}

StructureTemplate::~StructureTemplate()
{
    for (std::vector<StructureBlockInfo*>::iterator block = mBlocks.begin(); block != mBlocks.end(); ++block)
    {
        delete *block;
    }

    for (std::vector<StructureEntityInfo*>::iterator entity = mEntities.begin(); entity != mEntities.end(); ++entity)
    {
        delete *entity;
    }
}

BlockPos StructureTemplate::getSize() const
{
    return mSize;
}

BoundingBox* StructureTemplate::getTransformedBounds(const BlockPos& origin, Mirror* mirror, Rotation* rotation) const
{
    const auto last = transform(BlockPos(mSize.x - 1, mSize.y - 1, mSize.z - 1), mirror, rotation).offset(origin);
    return BoundingBox::createProper(origin.x, origin.y, origin.z, last.x, last.y, last.z);
}

void StructureTemplate::setNative(const BlockPos& size, std::vector<StructureBlockInfo*> blocks,
                                 std::vector<StructureEntityInfo*> entities, std::vector<Connector> connectors)
{
    for (auto* block : mBlocks) delete block;
    for (auto* entity : mEntities) delete entity;
    mPalette.clear();
    mSize = size;
    mBlocks = std::move(blocks);
    mEntities = std::move(entities);
    mConnectors = std::move(connectors);
    std::sort(mBlocks.begin(), mBlocks.end(), StructureBlockInfo::byColumnSorter());
}

BlockPos StructureTemplate::transform(const BlockPos& pos, Mirror* mirror, Rotation* rotation)
{
    int x = pos.x;
    int y = pos.y;
    int z = pos.z;

    bool mirrored = true;

    switch (mirror->getType())
    {
    case Mirror::eTYPE_LeftRight:
        z = -z;
        break;

    case Mirror::eTYPE_FrontBack:
        x = -x;
        break;

    default:
        mirrored = false;
        break;
    }

    switch (rotation->getType())
    {
    case Rotation::eTYPE_Clockwise90:
        return BlockPos(-z, y, x);

    case Rotation::eTYPE_Clockwise180:
        return BlockPos(-x, y, -z);

    case Rotation::eTYPE_CounterClockwise90:
        return BlockPos(z, y, -x);

    default:
        return mirrored ? BlockPos(x, y, z) : pos;
    }
}

BlockPos StructureTemplate::calculateRelativePosition(const StructurePlaceSettings* settings, const BlockPos& pos)
{
    return transform(pos, settings->getMirror(), settings->getRotation());
}

StructureTemplate::MarkerMap StructureTemplate::getMarkers(const BlockPos& pos, StructurePlaceSettings* settings) const
{
    MarkerMap markers;

    BoundingBox* boundingBox = settings->getBoundingBox();

    for (std::vector<StructureBlockInfo*>::const_iterator it = mBlocks.begin(); it != mBlocks.end(); ++it)
    {
        StructureBlockInfo* blockInfo = *it;

        BlockPos blockPos = calculateRelativePosition(settings, blockInfo->mPos).offset(pos);
        if (settings->verticalOffset)
            blockPos.y += settings->verticalOffset(blockPos);

        if (boundingBox != NULL && !boundingBox->isInside(blockPos))
        {
            continue;
        }

        if (blockInfo->mState->getBlock() == Blocks::structure_block && blockInfo->mNbt != NULL)
        {
            markers.insert(std::make_pair(blockPos, blockInfo->mNbt->getString(L"metadata")));
        }
    }

    return markers;
}

BlockPos StructureTemplate::calculateConnectedPosition(const StructurePlaceSettings* settings, const BlockPos& pos,
                                                       const StructurePlaceSettings* connectedSettings,
                                                       const BlockPos& connectedPos) const
{
    return calculateRelativePosition(settings, pos)
        .subtract(calculateRelativePosition(connectedSettings, connectedPos));
}

Vec3* StructureTemplate::transform(Vec3* pos, Mirror* mirror, Rotation* rotation)
{
    double x = pos->x;
    double y = pos->y;
    double z = pos->z;

    bool mirrored = true;

    switch (mirror->getType())
    {
    case Mirror::eTYPE_LeftRight:
        z = 1.0 - z;
        break;

    case Mirror::eTYPE_FrontBack:
        x = 1.0 - x;
        break;

    default:
        mirrored = false;
        break;
    }

    switch (rotation->getType())
    {
    case Rotation::eTYPE_Clockwise90:
        return Vec3::newTemp(1.0 - z, y, x);

    case Rotation::eTYPE_Clockwise180:
        return Vec3::newTemp(1.0 - x, y, 1.0 - z);

    case Rotation::eTYPE_CounterClockwise90:
        return Vec3::newTemp(z, y, 1.0 - x);

    default:
        return mirrored ? Vec3::newTemp(x, y, z) : pos;
    }
}

void StructureTemplate::placeEntities(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const
{
    auto* mirror = settings->getMirror();
    auto* rotation = settings->getRotation();
    auto* boundingBox = settings->getBoundingBox();
    for (std::vector<StructureEntityInfo*>::const_iterator it = mEntities.begin(); it != mEntities.end(); it++)
    {
        StructureEntityInfo* entityInfo = *it;

        BlockPos blockPos = transform(entityInfo->mBlockPos, mirror, rotation).offset(pos);
        const int offset = settings->verticalOffset ? settings->verticalOffset(blockPos) : 0;
        blockPos.y += offset;

        if (boundingBox != NULL && !boundingBox->isInside(blockPos))
        {
            continue;
        }

        std::unique_ptr<CompoundTag> nbt(entityInfo->mNbt->copy());
        // Each placement creates a new entity with its own world identity.
        nbt->remove(L"UUID");
        nbt->remove(L"UUIDMost");
        nbt->remove(L"UUIDLeast");

        Vec3* entityPos =
            transform(entityInfo->mPos, mirror, rotation)->add(pos.x, pos.y + offset, pos.z);

        ListTag* posTag = new ListTag();
        posTag->add(new DoubleTag(entityPos->x));
        posTag->add(new DoubleTag(entityPos->y));
        posTag->add(new DoubleTag(entityPos->z));

        nbt->put(L"Pos", posTag);

        boost::shared_ptr<Entity> entity;
        entity = EntityIO::newEntity(nbt.get(), level);

        if (entity != NULL)
        {
            float yRot = entity->mirror(mirror);
            yRot = yRot + (entity->mYRot - entity->rotate(rotation));

            entity->moveTo(entityPos->x, entityPos->y, entityPos->z, yRot, entity->mXRot);

            if (settings->findSafeMobPosition)
            {
                auto mob = boost::dynamic_pointer_cast<Mob>(entity);
                if (mob)
                {
                    bool safe = false;
                    for (int dy : {0, -1, 1, -2, 2, -3, 3})
                    {
                        const auto feet = BlockPos(entityPos->x, entityPos->y + dy, entityPos->z);
                        if (feet.y < 1 || feet.y + 2 >= level->getMaxBuildHeight()
                            || !level->getBlockState(feet.below())->getMaterial()->blocksMotion())
                            continue;
                        mob->moveTo(entityPos->x, entityPos->y + dy, entityPos->z, yRot, entity->mXRot);
                        if (!level->hasBlockCubes(mob->getBoundingBox())
                            && !level->containsAnyLiquid_NoLoad(mob->getBoundingBox()))
                        {
                            safe = true;
                            break;
                        }
                    }
                    if (!safe)
                        continue;
                    mob->setPersistenceRequired(true);
                }
            }
            level->addEntity(entity);
        }
    }
}

void StructureTemplate::placeInWorld(Level* level, const BlockPos& pos, StructureProcessor* processor,
                                     StructurePlaceSettings* settings, int flags) const
{
    if ((mBlocks.empty() && (settings->isIgnoreEntities() || mEntities.empty())) || mSize.x < 1 || mSize.y < 1
        || mSize.z < 1)
    {
        delete processor;
        return;
    }

    Block* ignoreBlock = settings->getIgnoreBlock();
    BoundingBox* boundingBox = settings->getBoundingBox();

    BlockPos lastColumn(INT_MAX, INT_MAX, INT_MAX);

    for (std::vector<StructureBlockInfo*>::const_iterator it = mBlocks.begin(); it != mBlocks.end(); ++it)
    {
        StructureBlockInfo* templateInfo = *it;

        BlockPos blockPos = calculateRelativePosition(settings, templateInfo->mPos).offset(pos);
        if (settings->verticalOffset)
            blockPos.y += settings->verticalOffset(blockPos);

        StructureBlockInfo* blockInfo = processor != NULL ? processor->processBlock(level, blockPos, templateInfo)
                                                          : templateInfo;

        if (lastColumn.x != blockPos.x || lastColumn.z != blockPos.z)
        {
            if (!level->hasChunkAt(blockPos) || level->getChunkAt(blockPos)->isEmpty())
            {
                continue;
            }

            lastColumn = blockPos;
        }

        if (blockInfo == NULL)
        {
            continue;
        }

        if (boundingBox != NULL && !boundingBox->isInside(blockPos))
        {
            continue;
        }

        const BlockState* state = blockInfo->mState->getBlock()->mirror(blockInfo->mState, settings->getMirror());
        state = state->getBlock()->rotate(state, settings->getRotation());
        state = settings->getSwappedBlock(state);

        Block* block = state->getBlock();

        if ((ignoreBlock != NULL && ignoreBlock == block)
            || (settings->isIgnoreStructureBlocks() && block == Blocks::structure_block))
        {
            continue;
        }

        if (settings->isPlacingWaterBelowSeaLevel() && blockPos.y < level->getSeaLevel())
        {
            if (Block::getId(block) == BlockId::AIR)
            {
                block = Blocks::water;
                state = block->defaultBlockState();
            }
            else if (block->canContainLiquid(state))
            {
                level->setExtraBlock(blockPos, Blocks::flowing_water->defaultBlockState(), Block::UPDATE_INVISIBLE,
                                     false);
            }
        }

        if (blockInfo->mNbt != NULL)
        {
            boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(blockPos);

            if (blockEntity)
            {
                if (eTYPE_DERIVED_FROM(BlockEntityType::CONTAINER, blockEntity->GetType()))
                {
                    boost::dynamic_pointer_cast<Container>(blockEntity)->clearContent();
                }

                level->setBlock(blockPos, Blocks::barrier->defaultBlockState(), Block::UPDATE_INVISIBLE, false);
            }
        }

        if (level->setBlockAndData(blockPos.x, blockPos.y, blockPos.z, Block::getId(block),
                                   block->convertBlockStateToLegacyData(state), flags)
            && blockInfo->mNbt != NULL)
        {
            boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(blockPos);

            if (blockEntity)
            {
                std::unique_ptr<CompoundTag> nbt(blockInfo->mNbt->copy());
                nbt->putInt(L"x", blockPos.x);
                nbt->putInt(L"y", blockPos.y);
                nbt->putInt(L"z", blockPos.z);
                if (nbt->contains(L"LootTable", Tag::TAG_STRING) && nbt->getLong(L"LootTableSeed") == 0)
                {
                    Random lootRandom(level->getSeed() ^ blockPos.compressBlockPos());
                    const auto seed = lootRandom.nextLong();
                    nbt->putLong(L"LootTableSeed", seed == 0 ? 1 : seed);
                }

                blockEntity->load(nbt.get());
                blockEntity->mirror(settings->getMirror());
                blockEntity->rotate(settings->getRotation());
            }
        }
        if (blockInfo->mExtraState)
            level->setExtraBlock(blockPos, blockInfo->mExtraState, flags, false);
    }

    for (std::vector<StructureBlockInfo*>::const_iterator it = mBlocks.begin(); it != mBlocks.end(); ++it)
    {
        StructureBlockInfo* blockInfo = *it;

        if (ignoreBlock != NULL && ignoreBlock == blockInfo->mState->getBlock())
        {
            continue;
        }

        BlockPos blockPos = calculateRelativePosition(settings, blockInfo->mPos).offset(pos);
        if (settings->verticalOffset)
            blockPos.y += settings->verticalOffset(blockPos);

        if (boundingBox != NULL && !boundingBox->isInside(blockPos))
        {
            continue;
        }

        if (settings->isIgnoreStructureBlocks() && blockInfo->mState->getBlock() == Blocks::structure_block)
            continue;
        level->blockUpdated(blockPos, blockInfo->mState->getBlock(), false);

        if (blockInfo->mNbt != NULL)
        {
            boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(blockPos);

            if (blockEntity)
            {
                blockEntity->setChanged();
            }
        }
    }

    if (!settings->isIgnoreEntities())
    {
        placeEntities(level, pos, settings);
    }

    delete processor;
}

void StructureTemplate::placeInWorld(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const
{
    placeInWorld(level, pos, new BlockRotProcessor(settings, pos), settings, 2);
}

void StructureTemplate::placeInWorldChunk(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const
{
    settings->updateBoundingBoxFromChunkPos();

    placeInWorld(level, pos, settings);
}

void StructureTemplate::placeInWorld(Level* level, const BlockPos& pos, StructurePlaceSettings* settings,
                                     int flags) const
{
    placeInWorld(level, pos, new BlockRotProcessor(settings, pos), settings, flags);
}

BlockPos StructureTemplate::getSize(Rotation* rotation) const
{
    switch (rotation->getType())
    {
    case Rotation::eTYPE_Clockwise90:
    case Rotation::eTYPE_CounterClockwise90:
        return BlockPos(mSize.z, mSize.y, mSize.x);

    default:
        return mSize;
    }
}

BlockPos StructureTemplate::getZeroPositionWithTransform(const BlockPos& pos, Mirror* mirror, Rotation* rotation,
                                                         int sizeX, int sizeZ)
{
    --sizeX;
    --sizeZ;

    int mirroredX = mirror == Mirror::FRONT_BACK ? sizeX : 0;
    int mirroredZ = mirror == Mirror::LEFT_RIGHT ? sizeZ : 0;

    BlockPos zeroPos = pos;

    switch (rotation->getType())
    {
    case Rotation::eTYPE_None:
        zeroPos = pos.offset(mirroredX, 0, mirroredZ);
        break;

    case Rotation::eTYPE_Clockwise90:
        zeroPos = pos.offset(sizeZ - mirroredZ, 0, mirroredX);
        break;

    case Rotation::eTYPE_Clockwise180:
        zeroPos = pos.offset(sizeX - mirroredX, 0, sizeZ - mirroredZ);
        break;

    case Rotation::eTYPE_CounterClockwise90:
        zeroPos = pos.offset(mirroredZ, 0, sizeX - mirroredX);
        break;

    default:
        break;
    }

    return zeroPos;
}

BlockPos StructureTemplate::getZeroPositionWithTransform(const BlockPos& pos, Mirror* mirror, Rotation* rotation) const
{
    return getZeroPositionWithTransform(pos, mirror, rotation, getSize().x, getSize().z);
}

void StructureTemplate::addWalkers(DataFixerUpper* fixerUpper)
{
    fixerUpper->addWalker(DataFixTypes::eDataFixTypes_Structure, (DataWalker*)new AnonymousDataWalker());
}

bool StructureTemplate::loadLocalVillage(CompoundTag* tag, std::wstring& problem)
{
    const auto fail = [&](const wchar_t* message) {
        problem = message;
        return false;
    };
    auto* dimensions = tag->getList(SIZE_TAG, Tag::TAG_INT);
    if (dimensions->size() != 3)
        return fail(L"size must contain three integers");
    const BlockPos size(dimensions->getInt(0), dimensions->getInt(1), dimensions->getInt(2));
    if (size.x < 1 || size.x > 64 || size.y < 1 || size.y > 64 || size.z < 1 || size.z > 64)
        return fail(L"each template dimension must be between 1 and 64");

    auto* palette = tag->getList(PALETTE_TAG, Tag::TAG_COMPOUND);
    auto* blocks = tag->getList(BLOCKS_TAG, Tag::TAG_COMPOUND);
    if ((palette->size() == 0 && blocks->size() != 0) || blocks->size() > 2 * size.x * size.y * size.z)
        return fail(L"invalid palette or blocks list");
    for (int index = 0; index < palette->size(); ++index)
    {
        auto* entry = palette->getCompound(index);
        if (entry->getString(L"Name") != L"minecraft:jigsaw"
            && !NbtUtils::readBlockState(entry, true, true, &problem))
        {
            problem = L"palette entry " + std::to_wstring(index) + L": " + problem;
            return false;
        }
    }

    for (int index = 0; index < blocks->size(); ++index)
    {
        auto* block = blocks->getCompound(index);
        auto* pos = block->getList(BLOCK_POS_TAG, Tag::TAG_INT);
        if (pos->size() != 3 || !block->contains(BLOCK_STATE_TAG, Tag::TAG_INT))
            return fail(L"block entry is missing its position or palette index");
        const BlockPos relative(pos->getInt(0), pos->getInt(1), pos->getInt(2));
        const int paletteIndex = block->getInt(BLOCK_STATE_TAG);
        if (!insideTemplate(relative, size)
            || paletteIndex < 0 || paletteIndex >= palette->size())
            return fail(L"block entry has an invalid position or palette index");

        auto* entry = palette->getCompound(paletteIndex);
        if (entry->getString(L"Name") == L"minecraft:jigsaw")
        {
            auto* nbt = block->getCompound(BLOCK_NBT_TAG);
            const bool legacy = nbt->contains(L"attachement_type", Tag::TAG_STRING);
            const auto pool = nbt->getString(legacy ? L"target_pool" : L"pool");
            const auto name = nbt->getString(legacy ? L"attachement_type" : L"name");
            const auto validId = [](const std::wstring& id) {
                const auto colon = id.find(L':');
                return colon != std::wstring::npos && colon > 0 && colon + 1 < id.size()
                    && id.find(L"..") == std::wstring::npos;
            };
            if (!localConnectorFacing(entry) || !validId(name) || !validId(pool)
                || (!legacy && pool != L"lce:empty" && pool != L"minecraft:empty"
                    && !validId(nbt->getString(L"target"))))
                return fail(L"Jigsaw has invalid facing, name, target or pool identifiers");
            if (!readFinalState(nbt->getString(L"final_state"), true, &problem))
            {
                problem = L"Jigsaw final_state at block entry " + std::to_wstring(index) + L": " + problem;
                return false;
            }
        }
    }

    auto* entities = tag->getList(ENTITIES_TAG, Tag::TAG_COMPOUND);
    for (int index = 0; index < entities->size(); ++index)
    {
        auto* entity = entities->getCompound(index);
        auto* pos = entity->getList(ENTITY_BLOCK_POS_TAG, Tag::TAG_INT);
        auto* exact = entity->getList(ENTITY_POS_TAG, Tag::TAG_DOUBLE);
        if (pos->size() != 3
            || exact->size() != 3 || !entity->contains(ENTITY_NBT_TAG, Tag::TAG_COMPOUND))
            return fail(L"entity entry has an invalid position or missing NBT");
        const double x = exact->getDouble(0), y = exact->getDouble(1), z = exact->getDouble(2);
        // Java captures entities intersecting the selection, including their
        // centres just outside it. Keep those positions rather than clamping.
        if (!(std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
            || x < -1 || x > size.x + 1 || y < -1 || y > size.y + 1 || z < -1 || z > size.z + 1)
            return fail(L"entity position is outside the selection margin");
    }

    load(tag, true, true);
    return true; // Empty terminators are valid structure templates.
}

void StructureTemplate::load(CompoundTag* tag, bool flattened, bool localVillage)
{
    for (std::vector<StructureBlockInfo*>::iterator block = mBlocks.begin(); block != mBlocks.end(); ++block)
    {
        delete *block;
    }

    for (std::vector<StructureEntityInfo*>::iterator entity = mEntities.begin(); entity != mEntities.end(); ++entity)
    {
        delete *entity;
    }

    mEntities.clear();
    mPalette.clear();
    mBlocks.clear();
    mConnectors.clear();

    ListTag* sizeTag = tag->getList(SIZE_TAG, Tag::TAG_INT);
    mSize = BlockPos(sizeTag->getInt(0), sizeTag->getInt(1), sizeTag->getInt(2));

    mAuthor = tag->getString(AUTHOR_TAG);

    ListTag* paletteTag = tag->getList(PALETTE_TAG, Tag::TAG_COMPOUND);

    for (int i = 0; i < paletteTag->size(); ++i)
    {
        const BlockState* state = NbtUtils::readBlockState(paletteTag->getCompound(i), flattened);
        mPalette.push_back(state);
    }

    ListTag* blocksTag = tag->getList(BLOCKS_TAG, Tag::TAG_COMPOUND);

    for (int i = 0; i < blocksTag->size(); ++i)
    {
        CompoundTag* blockTag = blocksTag->getCompound(i);

        ListTag* posTag = blockTag->getList(BLOCK_POS_TAG, Tag::TAG_INT);
        BlockPos blockPos(posTag->getInt(0), posTag->getInt(1), posTag->getInt(2));

        unsigned int paletteIndex = (unsigned int)blockTag->getInt(BLOCK_STATE_TAG);

        const BlockState* state = paletteIndex < mPalette.size() ? mPalette.at(paletteIndex)
                                                                 : Blocks::air->defaultBlockState();

        CompoundTag* blockNbt = blockTag->contains(BLOCK_NBT_TAG) ? blockTag->getCompound(BLOCK_NBT_TAG)->copy() : NULL;
          const BlockState* extraState = flattened ? Blocks::air->defaultBlockState() : nullptr;
        if (flattened && paletteIndex < mPalette.size())
        {
            auto* palette = paletteTag->getCompound(paletteIndex);
            const std::wstring name = palette->getString(L"Name");
              if (palette->getCompound(L"Properties")->getString(L"waterlogged") == L"true")
                  extraState = Blocks::water->defaultBlockState();
            if (name == L"minecraft:jigsaw" && blockNbt)
            {
                if (localVillage)
                {
                    const bool legacy = blockNbt->contains(L"attachement_type", Tag::TAG_STRING);
                    const auto name = blockNbt->getString(legacy ? L"attachement_type" : L"name");
                    mConnectors.push_back({blockPos, localConnectorFacing(palette), name,
                                           blockNbt->getString(legacy ? L"target_pool" : L"pool"),
                                           legacy ? name : blockNbt->getString(L"target")});
                }
                else
                {
                    const auto facing = palette->getCompound(L"Properties")->getString(L"facing");
                    const auto direction = std::find_if(Direction::VALUES.begin(), Direction::VALUES.end(),
                        [&](const Direction* d) { return d->getSerializedName() == facing; });
                    if (direction != Direction::VALUES.end())
                        mConnectors.push_back({blockPos, *direction, blockNbt->getString(L"attachement_type"),
                                               blockNbt->getString(L"target_pool"), L""});
                }
                state = readFinalState(blockNbt->getString(L"final_state"));
                  if (blockNbt->getString(L"final_state").find(L"waterlogged=true") != std::wstring::npos)
                      extraState = Blocks::water->defaultBlockState();
                delete blockNbt;
                blockNbt = nullptr;
            }
            else if (state->getBlock() == Blocks::bed || state->getBlock() == Blocks::wall_banner)
            {
                if (!blockNbt)
                    blockNbt = new CompoundTag();
                const bool bed = state->getBlock() == Blocks::bed;
                if (name == L"minecraft:illager_captain_wall_banner")
                {
                    const auto banner = BannerItem::makeIllagerBanner();
                    if (auto* bannerTag = banner->getTagElement(L"BlockEntityTag"))
                    {
                        delete blockNbt;
                        blockNbt = static_cast<CompoundTag*>(bannerTag->copy());
                    }
                }
                for (const auto* color : DyeColor::COLORS)
                {
                      const auto colorName = color == DyeColor::SILVER ? std::wstring(L"light_gray")
                                                                     : color->getSerializedName();
                      if (name == L"minecraft:" + colorName + (bed ? L"_bed" : L"_wall_banner"))
                    {
                        blockNbt->putInt(bed ? L"color" : L"Base", bed ? color->getBlockData() : color->getItemData());
                        break;
                    }
                }
            }
            if (!localVillage && state->getBlock() == Blocks::chest && blockNbt && blockNbt->contains(L"LootTable", Tag::TAG_STRING))
            {
                const auto id = blockNbt->getString(L"LootTable");
                if (id.find(L"minecraft:chests/village/") == 0)
                {
                    const auto& table = id.find(L"_house") != std::wstring::npos
                        ? BuiltInLootTables::VILLAGE_HOME_SUPPLIES : BuiltInLootTables::VILLAGE_WORKSHOP_SUPPLIES;
                    blockNbt->putString(L"LootTable", table.toString());
                }
            }
        }

        if (localVillage && state->getBlock() == Blocks::structure_void)
            delete blockNbt;
        else
            mBlocks.push_back(new StructureBlockInfo(blockPos, state, blockNbt, extraState));
    }

    std::sort(mBlocks.begin(), mBlocks.end(), StructureBlockInfo::byColumnSorter());

    ListTag* entitiesTag = tag->getList(ENTITIES_TAG, Tag::TAG_COMPOUND);

    for (int i = 0; i < entitiesTag->size(); ++i)
    {
        CompoundTag* entityTag = entitiesTag->getCompound(i);
        if (!entityTag->contains(ENTITY_NBT_TAG, Tag::TAG_COMPOUND))
            continue;

        ListTag* posTag = entityTag->getList(ENTITY_POS_TAG, Tag::TAG_DOUBLE);
        ListTag* blockPosTag = entityTag->getList(ENTITY_BLOCK_POS_TAG, Tag::TAG_INT);
        if (posTag->size() != 3 || blockPosTag->size() != 3)
            continue;

        const double x = posTag->getDouble(0), y = posTag->getDouble(1), z = posTag->getDouble(2);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;

        const BlockPos blockPos(blockPosTag->getInt(0), blockPosTag->getInt(1), blockPosTag->getInt(2));
        auto* entityNbt = entityTag->getCompound(ENTITY_NBT_TAG)->copy();
        if (flattened)
        {
            const auto id = entityNbt->getString(L"id");
            if (id == L"minecraft:iron_golem") entityNbt->putString(L"id", L"minecraft:villager_golem");
            if (id == L"minecraft:cat")
            {
                // Java's tabby is zero; LCE retained the old ocelot cat variants.
                static const int variants[] = {4, 1, 2, 3, 8, 9, 7, 6, 5, 10, 11};
                const int variant = entityNbt->getInt(L"CatType");
                if (variant >= 0 && variant < 11) entityNbt->putInt(L"CatType", variants[variant]);
            }
            if (entityNbt->contains(L"VillagerData", Tag::TAG_COMPOUND))
            {
                const auto profession = entityNbt->getCompound(L"VillagerData")->getString(L"profession");
                entityNbt->putInt(L"Profession", profession == L"minecraft:nitwit" ? 5 : 0);
            }
        }
        mEntities.push_back(new StructureEntityInfo(Vec3::newPermanent(x, y, z), blockPos, entityNbt));
    }
}
