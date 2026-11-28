#include "game/core/Ocelot.hpp"
#include "game/entity/mob/EntityEvent.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/EntityType.hpp"

#include <typeinfo>

#include "game/client/audio/SoundEvents.hpp"
#include "game/core/AttributeInstance.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/Cat.hpp"
#include "game/core/Chicken.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/RiderPositionOverrides.hpp"
#include "game/core/Turtle.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/AvoidEntityGoal.hpp"
#include "game/entity/ai/BreedGoal.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/FollowOwnerGoal.hpp"
#include "game/entity/ai/LeapAtTargetGoal.hpp"
#include "game/entity/ai/MoveControl.hpp"
#include "game/entity/ai/NonTameRandomTargetGoal.hpp"
#include "game/entity/ai/OcelotAttackGoal.hpp"
#include "game/entity/ai/SitGoal.hpp"
#include "game/entity/ai/TemptGoal.hpp"
#include "game/entity/ai/WaterAvoidingRandomStrollGoal.hpp"
#include "game/entity/mob/AgableMob.hpp"
#include "game/entity/mob/Animal.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/EntityDataSerializers.hpp"
#include "game/entity/mob/LivingEntity.hpp"
#include "game/entity/mob/Mob.hpp"
#include "game/entity/mob/MobGroupData.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/entity/mob/SynchedEntityData.hpp"
#include "game/item/FishFoodItem.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/SpawnEggItem.hpp"
#include "game/item/not_null_ptr.hpp"
#include "game/ui/EntityTooltipDataHolder.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/arrayWithLength.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/block/OcelotSitOnBlockGoal.hpp"
#include "game/world/generation/LookAtPlayerGoal.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/Player.hpp"
#include "game/item/ItemId.hpp"

const EntityDataAccessor<int>* Ocelot::DATA_CAT_TYPE = NULL;
const EntityDataAccessor<bool>* Ocelot::DATA_IS_TEMPTED = NULL;
const EntityDataAccessor<bool>* Ocelot::DATA_TRUSTING = NULL;

Ocelot::Ocelot(Level* level) : Ocelot(level, false)
{
}

Ocelot::Ocelot(Level* level, bool deferInitialization) : TamableAnimal(level)
{
    if (!deferInitialization)
    {
        fjDerivedCtorCalls();
        setSize(0.6f, 0.8f);
    }
}

Ocelot::~Ocelot()
{
    delete mTemptGoal;
    delete mAvoidPlayersGoal;
}

Entity* Ocelot::create(Level* level)
{
    return new Ocelot(level);
}

void Ocelot::staticCtor()
{
    DATA_CAT_TYPE = SynchedEntityData::defineId<int>(EntityType::OCELOT, EntityType::TAMABLE_ANIMAL,
                                                     EntityDataSerializers::INT);
    DATA_IS_TEMPTED = SynchedEntityData::defineId<bool>(EntityType::OCELOT, EntityType::TAMABLE_ANIMAL,
                                                        EntityDataSerializers::BOOLEAN);
    DATA_TRUSTING = SynchedEntityData::defineId(EntityType::OCELOT, EntityType::TAMABLE_ANIMAL,
                                              EntityDataSerializers::BOOLEAN);
}

void Ocelot::addDataWalkers(DataFixerUpper* fixer)
{
    Mob::addDataWalkers(fixer, EntityType::OCELOT);
    Mob::addDataWalkers(fixer, EntityType::CAT);
}

void Ocelot::setSittingOnBlock(bool sitting)
{
    unsigned char flags = mEntityData->get(TamableAnimal::DATA_FLAGS);
    mEntityData->set(TamableAnimal::DATA_FLAGS,
                     sitting ? static_cast<unsigned char>(flags | 2) : static_cast<unsigned char>(flags & ~2), true);
}

bool Ocelot::isSittingOnBlock()
{
    return (mEntityData->get(TamableAnimal::DATA_FLAGS) & 2) > 0;
}

void Ocelot::SetIsTempted(bool tempted)
{
    mEntityData->set(DATA_IS_TEMPTED, tempted, true);
}

void Ocelot::registerGoals()
{
    const bool cat = GetType() == EntityType::CAT;
    if (cat)
        mSitGoal = new SitGoal(this);

    static Item* temptItems[2] = {Items::fish, Items::fish};
    static int temptAuxValues[2] = {static_cast<int>(FishFoodItem::Type::COD->getId()),
                                    static_cast<int>(FishFoodItem::Type::SALMON->getId())};

    arrayWithLength<Item*> items;
    items.data = temptItems;
    items.length = 2;

    arrayWithLength<int> auxValues;
    auxValues.data = temptAuxValues;
    auxValues.length = 2;

    mTemptGoal = new TemptGoal(this, 0.6, items, true, auxValues);

    mGoalSelector.addGoal(1, new FloatGoal(this), true);
    if (cat)
    {
        mGoalSelector.addGoal(2, mSitGoal, false);
        mGoalSelector.addGoal(5, new FollowOwnerGoal(this, 1.0, 10.0f, 5.0f), true);
        mGoalSelector.addGoal(6, new OcelotSitOnBlockGoal(this, 0.8), true);
    }
    mGoalSelector.addGoal(3, mTemptGoal, false);
    mGoalSelector.addGoal(7, new LeapAtTargetGoal(this, 0.3f), true);
    mGoalSelector.addGoal(8, new OcelotAttackGoal(this), true);
    mGoalSelector.addGoal(9, new BreedGoal(this, 0.8), true);
    mGoalSelector.addGoal(
        10, new WaterAvoidingRandomStrollGoal(this, 0.8, WaterAvoidingRandomStrollGoal::PROBABILITY / 100.0f), true);
    mGoalSelector.addGoal(11, new LookAtPlayerGoal(this, typeid(Player), 10.0f), true);

    mTargetSelector.addGoal(1, new NonTameRandomTargetGoal(this, typeid(Chicken), false, NULL, true), true);
    mTargetSelector.addGoal(
        1, new NonTameRandomTargetGoal(this, typeid(Turtle), false, Turtle::BABY_ON_LAND_SELECTOR, false), true);
}

void Ocelot::defineSynchedData()
{
    TamableAnimal::defineSynchedData();
    int catType = GetType() == EntityType::CAT ? Cat::Tabby : 0;
    mEntityData->createDateItem(DATA_CAT_TYPE, catType);
    bool tempted = false;
    mEntityData->createDateItem(DATA_IS_TEMPTED, tempted);
    mEntityData->createDateItem(DATA_TRUSTING, false);
}

void Ocelot::customServerAiStep()
{
    if (getMoveControl()->hasWanted())
    {
        double speedModifier = getMoveControl()->getSpeedModifier();
        if (speedModifier == 0.6)
        {
            setSneaking(true);
            setSprinting(false);
        }
        else if (speedModifier == 1.33)
        {
            setSneaking(false);
            setSprinting(true);
        }
        else
        {
            setSneaking(false);
            setSprinting(false);
        }
    }
    else
    {
        setSneaking(false);
        setSprinting(false);
    }

    SetIsTempted(mTemptGoal == NULL || mTemptGoal->isRunning());
}

bool Ocelot::removeWhenFarAway()
{
    return Animal::removeWhenFarAway() && !isTame() && !isTrusting() && mTickCount > 2400;
}

void Ocelot::registerAttributes()
{
    Mob::registerAttributes();
    getAttribute(SharedMonsterAttributes::MAX_HEALTH)->setBaseValue(10.0);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.30000001192092896);
}

void Ocelot::causeFallDamage(float, float)
{
}

void Ocelot::addAdditonalSaveData(CompoundTag* tag)
{
    TamableAnimal::addAdditonalSaveData(tag);
    if (GetType() == EntityType::CAT)
        tag->putInt(L"CatType", getCatType());
    else
        tag->putBoolean(L"Trusting", isTrusting());
}

void Ocelot::readAdditionalSaveData(CompoundTag* tag)
{
    TamableAnimal::readAdditionalSaveData(tag);
    if (GetType() == EntityType::CAT)
    {
        const int type = tag->getInt(L"CatType");
        setCatType(type >= Cat::Tuxedo && type <= Cat::AllBlack ? type : Cat::Tabby);
    }
    else
        setTrusting(tag->getBoolean(L"Trusting"));
}

const SoundEvent* Ocelot::getAmbientSound()
{
    if (isTame())
    {
        if (isInLove())
            return SoundEvents::CAT_PURR;
        if (mRandom->nextInt(4) == 0)
            return SoundEvents::CAT_PURREOW;
        return SoundEvents::CAT_AMBIENT;
    }
    if (GetType() == EntityType::CAT)
        return mEntityData->get(DATA_IS_TEMPTED) ? SoundEvents::CAT_BEG : SoundEvents::CAT_STRAY_MEOW;
    return SoundEvents::OCELOT_IDLE;
}

const SoundEvent* Ocelot::getHurtSound(const DamageSource*)
{
    return SoundEvents::CAT_HURT;
}

const SoundEvent* Ocelot::getDeathSound()
{
    return GetType() == EntityType::CAT ? SoundEvents::CAT_DEATH : SoundEvents::OCELOT_DEATH;
}

float Ocelot::getSoundVolume()
{
    return 0.4f;
}

bool Ocelot::doHurtTarget(boost::shared_ptr<Entity> target)
{
    EntityDamageSource* source = DamageSource::CreateMobAttack(boost::dynamic_pointer_cast<Mob>(shared_from_this()));
    bool hurtTarget = target->hurt(source, 3.0f);
    delete source;
    return hurtTarget;
}

bool Ocelot::hurt(const DamageSource* source, float damage)
{
    if (isInvulnerableTo(source))
        return false;

    if (mSitGoal)
        mSitGoal->wantToSit(false);
    return Animal::hurt(source, damage);
}

ResourceLocation* Ocelot::getDefaultLootTable()
{
    return &BuiltInLootTables::OCELOT;
}

bool Ocelot::mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand)
{
    not_null_ptr<ItemInstance> item = player->getItemInHand(hand);
    const bool cat = GetType() == EntityType::CAT;

    if (!cat && isTrusting())
        return Animal::mobInteract(player, hand);

    if (!isTame())
    {
        if (isFood(item) && canAcceptFish(player))
        {
            if (!mLevel->mIsClientSide)
            {
                usePlayerItem(player, item);
                const bool success = mRandom->nextInt(3) == 0;
                if (success && cat)
                {
                    tame(player);
                    mSitGoal->wantToSit(true);
                    player->awardStat(GenericStats::tamedEntity(EntityType::CAT),
                                      GenericStats::param_tamedEntity(EntityType::CAT));
                }
                else if (success)
                    setTrusting(true);
                mLevel->broadcastEntityEvent(shared_from_this(),
                    success ? EntityEvent::TAMING_SUCCEEDED : EntityEvent::TAMING_FAILED, 0);
            }

            return true;
        }

        if (!AgeUpBaby(player, item))
            return AgableMob::mobInteract(player, hand);
        return true;
    }

    if (isOwnedBy(player))
    {
        bool renaming = false;
        if (item->getItem() == Items::nameTag && item->hasCustomHoverName())
        {
            if (!hasCustomName() || item->getHoverName() != getCustomName())
                renaming = true;
        }

        bool toggleSitting = true;
        if (!item->isEmpty()
            && ((item->getItem() == Items::spawnEgg && SpawnEggItem::fjGetType(item) == GetType()) || renaming
                || item->getItem() == Items::lead))
        {
            toggleSitting = false;
        }

        if (!isLeashedToPlayer(player) && !isFood(item) && toggleSitting)
        {
            if (!mLevel->mIsClientSide)
                mSitGoal->wantToSit(!isSitting());
            return true;
        }
    }

    return Animal::mobInteract(player, hand);
}

boost::shared_ptr<AgableMob> Ocelot::getBreedOffspring(boost::shared_ptr<AgableMob>)
{
    auto offspring = boost::shared_ptr<Ocelot>(new Ocelot(mLevel));
    offspring->setTrusting(true);
    return offspring;
}

bool Ocelot::isFood(const not_null_ptr<ItemInstance>& item)
{
    bool food = false;
    if (item->getItem() == Items::fish)
    {
        int auxValue = item->getAuxValue();
        FishFoodItem::Type* type = FishFoodItem::Type::getById(auxValue);
        if (type == FishFoodItem::Type::COD || type == FishFoodItem::Type::SALMON)
            food = true;
    }
    return food;
}

bool Ocelot::canMate(const boost::shared_ptr<Animal>& other)
{
    if (!Animal::canMate(other))
        return false;

    const auto ocelot = boost::static_pointer_cast<Ocelot>(other);
    return GetType() == EntityType::CAT ? isTame() && ocelot->isTame() : isTrusting() && ocelot->isTrusting();
}

bool Ocelot::canAcceptFish(const boost::shared_ptr<Player>& player)
{
    return (mEntityData->get(DATA_IS_TEMPTED) || player->mAbilities.mInstabuild)
        && player->distanceToSqr(shared_from_this()) < 9.0;
}

bool Ocelot::isTrusting() const
{
    return mEntityData->get(DATA_TRUSTING);
}

void Ocelot::setTrusting(bool trusting)
{
    mEntityData->set(DATA_TRUSTING, trusting, true);
    reassessTameGoals();
}

int Ocelot::getCatType()
{
    return mEntityData->get(DATA_CAT_TYPE);
}

void Ocelot::setCatType(int catType)
{
    mEntityData->set(DATA_CAT_TYPE, catType, true);
}

bool Ocelot::checkSpawnRules()
{
    return mLevel->mRandom->nextInt(3) != 0;
}

bool Ocelot::checkSpawnObstruction()
{
    if (mLevel->isUnobstructed(mBoundingBox)
        && mLevel->getCollisionAABBs(shared_from_this(), mBoundingBox, false, false, false)->empty()
        && !mLevel->containsAnyLiquid(mBoundingBox))
    {
        BlockPos pos(mX, mBoundingBox->mMinY, mZ);
        if (pos.y >= mLevel->getSeaLevel())
        {
            const BlockState* below = mLevel->getBlockState(pos.below());
            Block* block = below->getBlock();
            if (block == Blocks::grass || below->getMaterial() == Material::LEAVES)
                return true;
        }
    }
    return false;
}

std::wstring Ocelot::getName()
{
    if (hasCustomName())
        return getCustomName();
    return std::wstring(L"");
}

MobGroupData* Ocelot::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data, const std::wstring&)
{
    data = Mob::finalizeMobSpawn(difficulty, data, std::wstring(L""));

    if (GetType() == EntityType::OCELOT && mLevel->mRandom->nextInt(7) == 0)
    {
        for (int i = 0; i < 2; ++i)
        {
            boost::shared_ptr<Ocelot> kitten(new Ocelot(mLevel));
            kitten->moveTo(mX, mY, mZ, mYRot, 0.0f);
            kitten->setAge(-24000);
            mLevel->addEntity(kitten);
        }
    }

    return data;
}

void Ocelot::positionRider(const boost::shared_ptr<Entity>& passenger)
{
    if (!RiderPositionOverrides::OverridePosition(shared_from_this(), passenger))
        passenger->setPos(mX, mY + 0.35 + passenger->getRidingHeight(), mZ);

    if (eTYPE_DERIVED_FROM(EntityType::LIVING_ENTITY, passenger->GetType()))
    {
        boost::shared_ptr<LivingEntity> living = boost::static_pointer_cast<LivingEntity>(passenger);
        living->mYBodyRot = mYBodyRot;
    }
}

unsigned int Ocelot::GetInteractTooltip(const EntityTooltipDataHolder& tooltipData)
{
    int itemId = Item::getId(tooltipData.m_pItemInstance->getItem());

    if (itemId == ItemId::SPAWN_EGG && SpawnEggItem::fjGetType(tooltipData.m_pItemInstance) == GetType())
        return IDS_TOOLTIPS_PLACE;

    if (isLeashedToPlayer(tooltipData.m_pPlayer))
        return IDS_TOOLTIPS_UNLEASH;

    if (itemId == ItemId::LEAD)
    {
        if (!isLeashed() && isOwnedBy(tooltipData.m_pPlayer))
            return IDS_TOOLTIPS_LEASH;
        return IDS_NONE;
    }

    if (!isPassenger() && itemId == ItemId::NAME_TAG && tooltipData.m_pItemInstance->hasCustomHoverName())
    {
        if (!hasCustomName() || !(tooltipData.m_pItemInstance->getHoverName() == getCustomName()))
            return IDS_TOOLTIPS_NAME;
    }

    if (isTame())
    {
        if (isFood(tooltipData.m_pItemInstance))
        {
            if (isBaby())
                return IDS_TOOLTIPS_FEED;
            if (!isInLove() && !isBaby() && !isBreedCoolingDown())
                return IDS_TOOLTIPS_LOVEMODE;
            return IDS_NONE;
        }

        if (isOwnedBy(tooltipData.m_pPlayer) && !isSittingOnBlock())
        {
            if (isSitting())
                return IDS_TOOLTIPS_FOLLOWME;
            return IDS_TOOLTIPS_SIT;
        }
        return IDS_NONE;
    }

    if (isFood(tooltipData.m_pItemInstance))
    {
        if (isTrusting())
            return Animal::GetInteractTooltip(tooltipData);
        if (canAcceptFish(tooltipData.m_pPlayer))
        {
            if (getPassengers()->empty())
                return GetType() == EntityType::CAT ? IDS_TOOLTIPS_TAME : IDS_TOOLTIPS_FEED;
        }
        else if (isBaby())
        {
            return IDS_TOOLTIPS_FEED;
        }
    }

    return IDS_NONE;
}

void Ocelot::reassessTameGoals()
{
    if (mAvoidPlayersGoal == NULL)
        mAvoidPlayersGoal = new AvoidEntityGoal(this, typeid(Player), 16.0f, 0.8, 1.33);

    mGoalSelector.removeGoal(mAvoidPlayersGoal);

    if (!isTame() && !isTrusting())
        mGoalSelector.addGoal(4, mAvoidPlayersGoal, false);
}

unsigned long long Ocelot::GetType()
{
    return EntityType::OCELOT;
}
