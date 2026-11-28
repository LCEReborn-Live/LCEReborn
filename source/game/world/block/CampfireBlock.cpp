#include "game/world/block/CampfireBlock.hpp"

#include <algorithm>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/client/render/ParticleType.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/Direction.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/item/FurnaceRecipes.hpp"
#include "game/item/Items.hpp"
#include "game/item/ShovelItem.hpp"
#include "game/network/ClientboundBlockEntityDataPacket.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/DimensionType.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BooleanProperty.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
auto* LIT = static_cast<AbstractProperty<bool>*>(BooleanProperty::create(L"lit"));
const AABB* CAMPFIRE_SHAPE = AABB::newPermanent(0, 0, 0, 1, 7.0 / 16, 1);

ItemInstance* cook(not_null_ptr<ItemInstance> item)
{
    return FurnaceRecipes::getInstance()->getCookingResult(item, FurnaceKind::Smoker);
}

std::vector<CuboidBlockModel::Part> fallback(bool lit)
{
    using CuboidBlockModel::Part;
    using CuboidBlockModel::Point;
    const auto log = [](const Point& low, const Point& high, int texture)
    {
        Part part{{low, high}, {}};
        for (auto& face : part.faces)
            face = {{0, 0, 16, 16}, texture, -1};
        return part;
    };
    std::vector<Part> parts{log({1, 0, 0}, {5, 4, 16}, lit ? 1 : 0), log({11, 0, 0}, {15, 4, 16}, lit ? 1 : 0),
                            log({0, 3, 1}, {16, 7, 5}, lit ? 1 : 0), log({0, 3, 11}, {16, 7, 15}, lit ? 1 : 0)};
    if (lit)
    {
        Part first{{{{0, 1, 8}, {16, 17, 8}}}, {}, 45};
        Part second{{{{8, 1, 0}, {8, 17, 16}}}, {}, 45};
        first.faces[2] = first.faces[3] = {{0, 0, 16, 16}, 2, -1};
        second.faces[4] = second.faces[5] = {{0, 0, 16, 16}, 2, -1};
        parts.push_back(first);
        parts.push_back(second);
    }
    return parts;
}
}

CampfireBlockEntity::CampfireBlockEntity()
{
    mFood.fill(ItemInstance::EMPTY);
}

BlockEntity* CampfireBlockEntity::create() { return new CampfireBlockEntity(); }
unsigned long long CampfireBlockEntity::GetType() { return BlockEntityType::CAMPFIRE; }

boost::shared_ptr<BlockEntity> CampfireBlockEntity::clone()
{
    auto copy = boost::shared_ptr<CampfireBlockEntity>(new CampfireBlockEntity());
    BlockEntity::clone(copy);
    copy->mFood = mFood;
    copy->mProgress = mProgress;
    return copy;
}

void CampfireBlockEntity::load(CompoundTag* tag)
{
    BlockEntity::load(tag);
    for (size_t i = 0; i < mFood.size(); ++i)
    {
        const auto suffix = std::to_wstring(i);
        const auto key = L"Food" + suffix;
        mFood[i] = tag->contains(key, Tag::TAG_COMPOUND)
            ? not_null_ptr<ItemInstance>(new ItemInstance(tag->getCompound(key))) : ItemInstance::EMPTY;
        mProgress[i] = std::max(0, std::min(int(CookingTime), tag->getInt(L"Cook" + suffix)));
    }
}

CompoundTag* CampfireBlockEntity::save(CompoundTag* tag)
{
    BlockEntity::save(tag);
    for (size_t i = 0; i < mFood.size(); ++i)
    {
        if (!mFood[i]->isEmpty())
        {
            const auto suffix = std::to_wstring(i);
            tag->put(L"Food" + suffix, mFood[i]->save(new CompoundTag()));
            tag->putInt(L"Cook" + suffix, mProgress[i]);
        }
    }
    return tag;
}

bool CampfireBlockEntity::hasSpace() const
{
    return std::any_of(mFood.begin(), mFood.end(), [](const auto& food) { return food->isEmpty(); });
}

CompoundTag* CampfireBlockEntity::getUpdateTag()
{
    return save(new CompoundTag());
}

boost::shared_ptr<Packet> CampfireBlockEntity::getUpdatePacket()
{
    return boost::shared_ptr<ClientboundBlockEntityDataPacket>(new ClientboundBlockEntityDataPacket(
        mPos, UpdatePacketType, mLevel->mDimension->getType()->getId(), getUpdateTag()));
}

void CampfireBlockEntity::notifyFoodChanged()
{
    if (!mLevel)
        return;
    setChanged();
    const auto* state = mLevel->getBlockState(mPos);
    mLevel->sendBlockUpdated(mPos, state, state, Block::UPDATE_CLIENTS, true);
}

bool CampfireBlockEntity::placeFood(not_null_ptr<ItemInstance> item)
{
    auto slot = std::find_if(mFood.begin(), mFood.end(), [](const auto& food) { return food->isEmpty(); });
    if (slot == mFood.end() || item->isEmpty() || cook(item)->isEmpty())
        return false;
    *slot = item->copy();
    (*slot)->setCount(1);
    mProgress[slot - mFood.begin()] = 0;
    notifyFoodChanged();
    return true;
}

void CampfireBlockEntity::tick()
{
    if (!mLevel)
        return;
    const auto* state = mLevel->getBlockState(mPos);
    if (state->getBlock() != Blocks::campfire)
        return;
    const bool wet = mLevel->getExtraBlockState(mPos)->getMaterial() == Material::WATER;
    const bool lit = CampfireBlock::isLit(state);
    if (mLevel->mIsClientSide)
    {
        if (!lit || wet)
        {
            mSmokeDelay = 0;
            return;
        }
        // Random block animation ticks can leave seconds-long gaps. Each loaded
        // campfire keeps its own client-side timer: 40-60 ticks at 20 ticks/sec.
        if (--mSmokeDelay <= 0)
        {
            Random* random = mLevel->mRandom;
            mSmokeDelay = 40 + random->nextInt(21);
            const bool signal = mLevel->getBlockState(mPos.below())->getBlock() == Blocks::hay_block;
            const auto* smoke = signal ? PARTICLE_CAMPFIRE_SIGNAL_SMOKE : PARTICLE_CAMPFIRE_SMOKE;
            mLevel->addParticle(smoke, true, mPos.x + .27 + random->nextDouble() * .46, mPos.y + .75,
                                mPos.z + .27 + random->nextDouble() * .46, 0, 0, 0, arrayWithLength<int>());
        }
        return;
    }
    if (lit && wet)
    {
        mLevel->setBlock(mPos, state->setValue(LIT, false), Block::UPDATE_ALL, false);
        return;
    }
    bool changed = false;
    bool finishedFood = false;
    for (size_t i = 0; i < mFood.size(); ++i)
    {
        if (mFood[i]->isEmpty())
            continue;
        if (!lit)
        {
            changed |= mProgress[i] > 0;
            mProgress[i] = std::max(0, mProgress[i] - 2);
            continue;
        }
        changed = true;
        if (++mProgress[i] >= CookingTime)
        {
            auto* result = cook(mFood[i]);
            Block::popResource(mLevel, mPos, result->isEmpty() ? mFood[i] : result->copy());
            mFood[i] = ItemInstance::EMPTY;
            mProgress[i] = 0;
            finishedFood = true;
        }
    }
    if (finishedFood)
        notifyFoodChanged();
    else if (changed)
        mLevel->blockEntityChanged(mPos, shared_from_this());
}

void CampfireBlockEntity::dropFood()
{
    for (auto& food : mFood)
    {
        if (!food->isEmpty())
            Block::popResource(mLevel, mPos, food);
        food = ItemInstance::EMPTY;
    }
    mProgress.fill(0);
}

CampfireBlock::CampfireBlock() : BaseEntityBlock(Material::WOOD)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::NORTH)
                                              ->setValue(LIT, true));
    setLightBlock(0);
    mLitModel = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/campfire_model.bin", mTextures.size());
    mUnlitModel = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/campfire_off_model.bin", mTextures.size());
    if (mLitModel.empty())
        mLitModel = fallback(true);
    if (mUnlitModel.empty())
        mUnlitModel = fallback(false);
}

bool CampfireBlock::isLit(const BlockState* state) { return state->getValue(LIT); }
int CampfireBlock::getLightEmission(const BlockState* state) { return isLit(state) ? 15 : 0; }

boost::shared_ptr<BlockEntity> CampfireBlock::newBlockEntity(Level* level, int)
{
    auto entity = boost::shared_ptr<CampfireBlockEntity>(new CampfireBlockEntity());
    entity->setLevel(level);
    return entity;
}

EBlockFaceShape CampfireBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*)
{
    return BLOCK_FACE_UNDEFINED;
}

const AABB* CampfireBlock::getShape(const BlockState*, LevelSource*, const BlockPos&)
{
    return CAMPFIRE_SHAPE;
}

bool CampfireBlock::use(Level* level, const BlockPos& pos, const BlockState* state, boost::shared_ptr<Player> player,
                        InteractionHand::EInteractionHand hand, const Direction*, float, float, float, bool clientSide)
{
    auto held = player->getItemInHand(hand);
    if (held->isEmpty())
        return false;
    const bool lit = isLit(state);
    const bool ignite = !lit && held->getItem() == Items::flintAndSteel
        && level->getExtraBlockState(pos)->getMaterial() != Material::WATER;
    const bool extinguish = lit && dynamic_cast<ShovelItem*>(held->getItem()) != nullptr;
    if (ignite || extinguish)
    {
        if (!clientSide && !level->mIsClientSide
            && level->setBlock(pos, state->setValue(LIT, ignite), Block::UPDATE_ALL, false))
        {
            level->playSound({}, pos, ignite ? SoundEvents::FLINTANDSTEEL_USE : SoundEvents::FIRE_EXTINGUISH,
                             SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, 16.0f);
            if (!player->mAbilities.mInstabuild)
                held->hurtAndBreak(1, player);
        }
        return true;
    }
    if (!lit || cook(held)->isEmpty())
        return false;
    if (clientSide || level->mIsClientSide)
        return true;
    auto campfire = boost::dynamic_pointer_cast<CampfireBlockEntity>(level->getBlockEntity(pos));
    if (!campfire || !campfire->placeFood(held))
        return false;
    if (!player->mAbilities.mInstabuild)
        held->shrink(1);
    return true;
}

void CampfireBlock::entityInside(Level* level, const BlockPos&, const BlockState* state, const boost::shared_ptr<Entity>& entity)
{
    if (!level->mIsClientSide && isLit(state) && !entity->isFireImmune()
        && eTYPE_DERIVED_FROM(EntityType::LIVING_ENTITY, entity->GetType()))
        entity->hurt(DamageSource::IN_FIRE, 1.0f);
}

void CampfireBlock::animateTick(const BlockState* state, Level* level, const BlockPos& pos, Random* random)
{
    if (!isLit(state))
        return;
    if (random->nextInt(10) == 0)
        level->playLocalSound(pos.x + .5, pos.y + .5, pos.z + .5, SoundEvents::CAMPFIRE_CRACKLE,
                              SoundSource::eSoundSource_Blocks, .8f, 1.0f, false, 16.0f);
}

void CampfireBlock::onRemove(Level* level, const BlockPos& pos, const BlockState* state)
{
    if (!level->mIsClientSide && level->getGameRules()->getBoolean(GameRules::RULE_DOBLOCKDROPS))
        if (auto fire = boost::dynamic_pointer_cast<CampfireBlockEntity>(level->getBlockEntity(pos)))
            fire->dropFood();
    BaseEntityBlock::onRemove(level, pos, state);
}

Item* CampfireBlock::getResource(const BlockState*, Random*, int) { return Items::coal; }

const BlockState* CampfireBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                                      int, boost::shared_ptr<LivingEntity> placer)
{
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY,
                                         placer ? placer->getDirection() : Direction::NORTH);
}

const BlockState* CampfireBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::from2DDataValue(data & 3))
                              ->setValue(LIT, (data & 4) == 0);
}

int CampfireBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY)->get2DDataValue() | (isLit(state) ? 0 : 4);
}

const BlockState* CampfireBlock::rotate(const BlockState* state, Rotation* rotation)
{
    const auto* direction = state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY);
    return state->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, rotation->rotate(direction));
}

const BlockState* CampfireBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY)));
}

BlockStateDefinition* CampfireBlock::createBlockStateDefinition()
{
    const Property* properties[] = {HorizontalDirectionalBlock::FACING_PROPERTY, LIT};
    return new BlockStateDefinition(this, properties, 2);
}

CuboidBlockModel::Point CampfireBlock::orientPoint(const BlockState* state, CuboidBlockModel::Point point)
{
    const auto* direction = state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY);
    if (direction->getAxis() == Direction::Axis::X)
        return {16 - point[2], point[1], point[0]};
    return point;
}

void CampfireBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"campfire_log"), icons->registerIcon(L"campfire_log_lit"),
                 icons->registerIcon(L"campfire")};
    mIcon = mTextures[0];
}

const std::vector<CuboidBlockModel::Part>& CampfireBlock::getModel(const BlockState* state) const
{
    return isLit(state) ? mLitModel : mUnlitModel;
}
