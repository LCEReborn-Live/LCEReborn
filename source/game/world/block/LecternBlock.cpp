#include "game/world/block/LecternBlock.hpp"

#include <algorithm>

#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/Direction.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/data/ListTag.hpp"
#include "game/item/Items.hpp"
#include "game/item/WrittenBookItem.hpp"
#include "game/network/ClientboundBlockEntityDataPacket.hpp"
#include "game/util/AABB.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/DimensionType.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/BooleanProperty.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/LocalPlayer.hpp"

namespace
{
auto* HAS_BOOK = static_cast<AbstractProperty<bool>*>(BooleanProperty::create(L"has_book"));
auto* POWERED = static_cast<AbstractProperty<bool>*>(BooleanProperty::create(L"powered"));
constexpr int PulseTicks = 2;

bool isBook(const not_null_ptr<ItemInstance>& item)
{
    return item->getItem() == Items::writtenBook || item->getItem() == Items::writableBook;
}

std::vector<CuboidBlockModel::Part> fallbackModel()
{
    using CuboidBlockModel::Part;
    Part base{{{{0, 0, 0}, {16, 2, 16}}}, {}};
    Part pillar{{{{4, 2, 4}, {12, 15, 12}}}, {}};
    Part stand{{{{0, 12, 3}, {16, 16, 16}}}, {}};
    stand.xRotation = -22.5f;
    for (auto& face : base.faces) face = {{0, 0, 16, 16}, 1, -1};
    for (auto& face : pillar.faces) face = {{0, 0, 16, 16}, 3, -1};
    for (auto& face : stand.faces) face = {{0, 0, 16, 16}, 3, -1};
    base.faces[0].texture = 0;
    stand.faces[1].texture = 4;
    pillar.faces[2].texture = 2;
    return {base, pillar, stand};
}
}

const wchar_t* const LecternBlock::PageMessage = L"LCE|LecternPage";

BlockEntity* LecternBlockEntity::create() { return new LecternBlockEntity(); }
unsigned long long LecternBlockEntity::GetType() { return BlockEntityType::LECTERN; }

boost::shared_ptr<BlockEntity> LecternBlockEntity::clone()
{
    auto copy = boost::shared_ptr<LecternBlockEntity>(new LecternBlockEntity());
    BlockEntity::clone(copy);
    copy->mBook = mBook->copy();
    copy->mPage = mPage;
    return copy;
}

void LecternBlockEntity::load(CompoundTag* tag)
{
    BlockEntity::load(tag);
    mBook = tag->contains(L"Book", Tag::TAG_COMPOUND)
        ? not_null_ptr<ItemInstance>(new ItemInstance(tag->getCompound(L"Book"))) : ItemInstance::EMPTY;
    if (!mBook->isEmpty() && !isBook(mBook))
        mBook = ItemInstance::EMPTY;
    mPage = std::max(0, std::min(pageCount() - 1, tag->getInt(L"Page")));
}

CompoundTag* LecternBlockEntity::save(CompoundTag* tag)
{
    BlockEntity::save(tag);
    if (!mBook->isEmpty())
        tag->put(L"Book", mBook->save(new CompoundTag()));
    tag->putInt(L"Page", mPage);
    return tag;
}

CompoundTag* LecternBlockEntity::getUpdateTag() { return save(new CompoundTag()); }

boost::shared_ptr<Packet> LecternBlockEntity::getUpdatePacket()
{
    return boost::shared_ptr<Packet>(new ClientboundBlockEntityDataPacket(
        mPos, UpdatePacketType, mLevel->mDimension->getType()->getId(), getUpdateTag()));
}

int LecternBlockEntity::pageCount() const
{
    auto* tag = mBook->getTag();
    const auto* pages = tag && tag->contains(WrittenBookItem::TAG_PAGES, Tag::TAG_LIST)
        ? tag->getList(WrittenBookItem::TAG_PAGES, Tag::TAG_STRING) : nullptr;
    return pages ? pages->size() : 0;
}

void LecternBlockEntity::setBook(not_null_ptr<ItemInstance> book)
{
    mBook = book;
    mPage = 0;
    setChanged();
}

bool LecternBlockEntity::setPage(int page)
{
    if (page < 0 || page >= pageCount() || page == mPage)
        return false;
    mPage = page;
    setChanged();
    return true;
}

LecternBlock::LecternBlock() : BaseEntityBlock(Material::WOOD)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::NORTH)
                                              ->setValue(HAS_BOOK, false)->setValue(POWERED, false));
    setLightBlock(0);
    mModel = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/lectern_model.bin", mTextures.size());
    if (mModel.empty())
        mModel = fallbackModel();
}

boost::shared_ptr<BlockEntity> LecternBlock::newBlockEntity(Level* level, int)
{
    auto entity = boost::shared_ptr<LecternBlockEntity>(new LecternBlockEntity());
    entity->setLevel(level);
    return entity;
}

EBlockFaceShape LecternBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*)
{
    return BLOCK_FACE_UNDEFINED;
}

const AABB* LecternBlock::getShape(const BlockState*, LevelSource*, const BlockPos&)
{
    return AABB::newTemp(0, 0, 0, 1, 1, 1);
}

void LecternBlock::addCollisionAABBs(const BlockState*, Level*, const BlockPos& pos, const AABB* entityBox,
                                     std::vector<AABB*>* collisions, boost::shared_ptr<Entity>, bool)
{
    addCollisionAABB(pos, entityBox, collisions, AABB::newTemp(0, 0, 0, 1, 2.0 / 16, 1));
    addCollisionAABB(pos, entityBox, collisions, AABB::newTemp(.25, 2.0 / 16, .25, .75, 14.0 / 16, .75));
    addCollisionAABB(pos, entityBox, collisions, AABB::newTemp(0, 14.0 / 16, 0, 1, 1, 1));
}

bool LecternBlock::use(Level* level, const BlockPos& pos, const BlockState* state, boost::shared_ptr<Player> player,
                        InteractionHand::EInteractionHand hand, const Direction*, float, float, float, bool clientSide)
{
    const auto lectern = boost::dynamic_pointer_cast<LecternBlockEntity>(level->getBlockEntity(pos));
    if (!lectern)
        return false;
    const auto held = player->getItemInHand(hand);
    if (state->getValue(HAS_BOOK))
    {
        if (lectern->book()->isEmpty())
            return true;
        if (player->isSneaking())
        {
            if (!clientSide && !level->mIsClientSide && !player->isSpectator())
            {
                auto book = lectern->book();
                const int page = lectern->page();
                lectern->setBook(ItemInstance::EMPTY);
                if (!level->setBlock(pos, state->setValue(HAS_BOOK, false)->setValue(POWERED, false), Block::UPDATE_ALL, false))
                {
                    lectern->setBook(book);
                    lectern->setPage(page);
                    return true;
                }
                if (state->getValue(POWERED))
                    level->updateNeighborsAt(pos.above(), this, false);
                if (!player->addItem(book))
                    player->drop(book, false);
            }
        }
        else if (level->mIsClientSide)
        {
            const auto* localPlayer = dynamic_cast<LocalPlayer*>(player.get());
            if (localPlayer)
                gApp.LoadWritingBookMenu(localPlayer->mUserIndex, lectern->book()->copy(), false, &pos, lectern->page());
        }
        return true;
    }
    if (held->isEmpty() || !isBook(held) || player->isSpectator())
        return false;
    if (!clientSide && !level->mIsClientSide)
    {
        auto book = held->copy();
        book->setCount(1);
        lectern->setBook(book);
        if (!level->setBlock(pos, state->setValue(HAS_BOOK, true), Block::UPDATE_ALL, false))
            lectern->setBook(ItemInstance::EMPTY);
        else if (!player->mAbilities.mInstabuild)
            held->shrink(1);
    }
    return true;
}

void LecternBlock::onRemove(Level* level, const BlockPos& pos, const BlockState* state)
{
    auto lectern = boost::dynamic_pointer_cast<LecternBlockEntity>(level->getBlockEntity(pos));
    if (!level->mIsClientSide && lectern && !lectern->book()->isEmpty()
        && level->getGameRules()->getBoolean(GameRules::RULE_DOBLOCKDROPS))
        popResource(level, pos, lectern->book());
    BaseEntityBlock::onRemove(level, pos, state);
}

bool LecternBlock::turnPage(Level* level, const BlockPos& pos, int page)
{
    const auto* state = level->getBlockState(pos);
    if (level->mIsClientSide || state->getBlock() != this || !state->getValue(HAS_BOOK))
        return false;
    const auto lectern = boost::dynamic_pointer_cast<LecternBlockEntity>(level->getBlockEntity(pos));
    if (!lectern || !lectern->setPage(page))
        return false;
    if (!state->getValue(POWERED))
    {
        level->setBlock(pos, state->setValue(POWERED, true), Block::UPDATE_ALL, false);
        level->updateNeighborsAt(pos.above(), this, false);
        level->addToTickNextTick(pos, this, PulseTicks);
    }
    else
    {
        level->sendBlockUpdated(pos, state, state, Block::UPDATE_CLIENTS, true);
        level->updateNeighbourForOutputSignal(pos, this);
    }
    return true;
}

void LecternBlock::tick(Level* level, const BlockPos& pos, const BlockState* state, Random*)
{
    if (!level->mIsClientSide && state->getValue(POWERED))
    {
        level->setBlock(pos, state->setValue(POWERED, false), Block::UPDATE_ALL, false);
        level->updateNeighborsAt(pos.above(), this, false);
    }
}

int LecternBlock::getSignal(const BlockState* state, LevelSource*, const BlockPos&, const Direction*)
{
    return state->getValue(POWERED) ? SIGNAL_MAX : SIGNAL_NONE;
}

int LecternBlock::getDirectSignal(const BlockState* state, LevelSource* level, const BlockPos& pos, const Direction* face)
{
    return face == Direction::DOWN ? getSignal(state, level, pos, face) : SIGNAL_NONE;
}

int LecternBlock::getAnalogOutputSignal(const BlockState* state, Level* level, const BlockPos& pos)
{
    if (!state->getValue(HAS_BOOK))
        return 0;
    const auto lectern = boost::dynamic_pointer_cast<LecternBlockEntity>(level->getBlockEntity(pos));
    if (!lectern || lectern->book()->isEmpty())
        return 0;
    const int lastPage = lectern->pageCount() - 1;
    return lastPage > 0 ? 1 + 14 * lectern->page() / lastPage : 15;
}

const BlockState* LecternBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                                     int, boost::shared_ptr<LivingEntity> placer)
{
    const auto* direction = placer ? placer->getDirection()->getOpposite() : Direction::NORTH;
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, direction);
}

const BlockState* LecternBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::from2DDataValue(data & 3))
                              ->setValue(POWERED, (data & 4) != 0)->setValue(HAS_BOOK, (data & 8) != 0);
}

int LecternBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return facing(state)->get2DDataValue() | (state->getValue(POWERED) ? 4 : 0) | (state->getValue(HAS_BOOK) ? 8 : 0);
}

const Direction* LecternBlock::facing(const BlockState* state)
{
    return state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY);
}

const BlockState* LecternBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, rotation->rotate(facing(state)));
}

const BlockState* LecternBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(facing(state)));
}

BlockStateDefinition* LecternBlock::createBlockStateDefinition()
{
    const Property* properties[] = {HorizontalDirectionalBlock::FACING_PROPERTY, POWERED, HAS_BOOK};
    return new BlockStateDefinition(this, properties, 3);
}

CuboidBlockModel::Point LecternBlock::orientPoint(const BlockState* state, CuboidBlockModel::Point point)
{
    const auto* direction = facing(state);
    if (direction == Direction::EAST) return {16 - point[2], point[1], point[0]};
    if (direction == Direction::SOUTH) return {16 - point[0], point[1], 16 - point[2]};
    if (direction == Direction::WEST) return {point[2], point[1], 16 - point[0]};
    return point;
}

void LecternBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"planks_oak"), icons->registerIcon(L"lectern_base"),
                 icons->registerIcon(L"lectern_front"), icons->registerIcon(L"lectern_sides"),
                 icons->registerIcon(L"lectern_top")};
    mIcon = mTextures[3];
}
