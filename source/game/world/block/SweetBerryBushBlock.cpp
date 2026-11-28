#include "game/world/block/SweetBerryBushBlock.hpp"

#include <cmath>
#include "game/core/DamageSource.hpp"
#include "game/core/DyeColor.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/IconRegister.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
auto* AGE = static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"age", 0, 3));
constexpr int Grown = 3;

int harvestCount(Random& random, int age)
{
    return 1 + random.nextInt(2) + (age == Grown ? 1 : 0);
}
}

SweetBerryBushBlock::SweetBerryBushBlock() : Bush(Material::PLANT)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(AGE, 0));
    setTicking(true);
}

void SweetBerryBushBlock::tick(Level* level, const BlockPos& pos, const BlockState* state, Random* random)
{
    Bush::tick(level, pos, state, random);
    if (!level->mIsClientSide && level->getBlockState(pos) == state && state->getValue(AGE) < Grown
        && random->nextInt(5) == 0 && level->getRawBrightness(pos.above()) >= 9)
        performBonemeal(level, random, pos, state);
}

bool SweetBerryBushBlock::TestUse(Level*, const BlockPos&, const BlockState* state, boost::shared_ptr<Player>)
{
    return state->getValue(AGE) >= 2;
}

bool SweetBerryBushBlock::use(Level* level, const BlockPos& pos, const BlockState* state,
                              boost::shared_ptr<Player> player, InteractionHand::EInteractionHand hand,
                              const Direction*, float, float, float, bool clientSide)
{
    const int age = state->getValue(AGE);
    const auto held = player->getItemInHand(hand);
    if (age < 2 || (age < Grown && held->getItem() == Items::dye
        && held->getAuxValue() == DyeColor::WHITE->getItemData()))
        return false;
    if (!clientSide && !level->mIsClientSide && level->setBlock(pos, state->setValue(AGE, 1), Block::UPDATE_ALL, false))
        popResource(level, pos, not_null_ptr<ItemInstance>(new ItemInstance(Items::sweetBerries,
                                                                           harvestCount(*level->mRandom, age))));
    return true;
}

void SweetBerryBushBlock::entityInside(Level* level, const BlockPos&, const BlockState* state,
                                       const boost::shared_ptr<Entity>& entity)
{
    if (!eTYPE_DERIVED_FROM(EntityType::LIVING_ENTITY, entity->GetType()))
        return;
    entity->mXd *= 0.8;
    entity->mZd *= 0.8;
    if (!level->mIsClientSide && state->getValue(AGE) > 0
        && (std::abs(entity->mX - entity->mXo) >= 0.003 || std::abs(entity->mZ - entity->mZo) >= 0.003))
        entity->hurt(DamageSource::CACTUS, 1.0f);
}

const AABB* SweetBerryBushBlock::getShape(const BlockState* state, LevelSource*, const BlockPos&)
{
    const int age = state->getValue(AGE);
    if (age == 0)
        return AABB::newTemp(0.25, 0, 0.25, 0.75, 0.5, 0.75);
    if (age < Grown)
        return AABB::newTemp(0.125, 0, 0.125, 0.875, 1, 0.875);
    return AABB::newTemp(0, 0, 0, 1, 1, 1);
}

void SweetBerryBushBlock::spawnResources(Level* level, const BlockPos& pos, const BlockState* state, float chance, int)
{
    if (level->mIsClientSide || !level->getGameRules()->getBoolean(GameRules::RULE_DOBLOCKDROPS))
        return;
    const int age = state->getValue(AGE);
    if (level->mRandom->nextFloat() <= chance)
        popResource(level, pos, not_null_ptr<ItemInstance>(new ItemInstance(Items::sweetBerries,
                                                   age >= 2 ? harvestCount(*level->mRandom, age) : 1)));
}

not_null_ptr<ItemInstance> SweetBerryBushBlock::getCloneItemInstance(Level*, const BlockPos&, const BlockState*)
{
    return not_null_ptr<ItemInstance>(new ItemInstance(Items::sweetBerries));
}

bool SweetBerryBushBlock::isValidBonemealTarget(Level*, const BlockPos&, const BlockState* state, bool)
{
    return state->getValue(AGE) < Grown;
}

bool SweetBerryBushBlock::isBonemealSuccess(Level*, Random*, const BlockPos&, const BlockState*)
{
    return true;
}

void SweetBerryBushBlock::performBonemeal(Level* level, Random*, const BlockPos& pos, const BlockState* state)
{
    const int age = state->getValue(AGE);
    if (age < Grown)
        level->setBlock(pos, state->setValue(AGE, age + 1), Block::UPDATE_ALL, false);
}

const BlockState* SweetBerryBushBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(AGE, data & 3);
}

int SweetBerryBushBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(AGE);
}

BlockStateDefinition* SweetBerryBushBlock::createBlockStateDefinition()
{
    const Property* properties[] = {AGE};
    return new BlockStateDefinition(this, properties, 1);
}

TextureAtlasSprite* SweetBerryBushBlock::getTexture(const Direction*, const BlockState* state)
{
    return mStageTextures[state->getValue(AGE)];
}

void SweetBerryBushBlock::registerIcons(IconRegister* icons)
{
    const wchar_t* textures[] = {L"sweet_berry_bush_stage0", L"sweet_berry_bush_stage1", L"sweet_berry_bush_stage2",
                                 L"sweet_berry_bush_stage3"};
    for (size_t i = 0; i < mStageTextures.size(); ++i)
        mStageTextures[i] = icons->registerIcon(textures[i]);
    mIcon = mStageTextures[0];
}
