#include "game/core/Pillager.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "game/client/audio/SoundEvents.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Sensing.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/HurtByTargetGoal.hpp"
#include "game/entity/ai/LookControl.hpp"
#include "game/entity/ai/NearestAttackableTargetGoal.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/ai/WaterAvoidingRandomStrollGoal.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/entity/mob/PatrolSpawner.hpp"
#include "game/entity/projectile/Arrow.hpp"
#include "game/entity/projectile/ArrowItem.hpp"
#include "game/item/BannerItem.hpp"
#include "game/item/CrossbowItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/util/Random.hpp"
#include "game/util/AABB.hpp"
#include "game/util/NbtUtils.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/Level.hpp"
#include "game/world/generation/LargeFeature.hpp"
#include "game/world/generation/LookAtPlayerGoal.hpp"
#include "game/world/generation/PillagerOutpostFeature.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/VillagerGolem.hpp"
#include "game/world/generation/WanderingTrader.hpp"

SoundEvent* Pillager::sAmbientSound = nullptr;
SoundEvent* Pillager::sHurtSound = nullptr;
SoundEvent* Pillager::sDeathSound = nullptr;

namespace
{
class PillagerPatrolGoal : public Goal
{
public:
    explicit PillagerPatrolGoal(Pillager& mob) : mMob(mob) { setRequiredControlFlags(1); }
    std::wstring getName() override { return L"PillagerPatrolGoal"; }
    bool canUse() override
    {
        return !mMob.getCurrentRaid() && mMob.isPatrolling() && !mMob.getTarget() && !mMob.isVehicle() && mMob.hasPatrolTarget();
    }

    void tick() override
    {
        auto* navigation = mMob.getNavigation();
        if (!navigation->isDone())
            return;

        const auto& target = mMob.getPatrolTarget();
        auto* position = Vec3::newTemp(mMob.mX, mMob.mY, mMob.mZ);
        auto* destination = Vec3::newTemp(target.x, target.y, target.z);
        if (mMob.isCaptain() && position->distanceToSqr(destination) < 100.0)
        {
            mMob.findPatrolTarget();
            return;
        }

        // Java 1.14.4's curved ten-block steps, shared by the captain with
        // nearby companions. Followers move faster so they can keep up.
        destination = position->subtract(destination)->yRot(90.0f)->scale(0.4)->add(destination);
        auto* step = destination->subtract(position)->normalize()->scale(10.0)->add(position);
        BlockPos surface;
        if (PatrolSpawner::getSurface(*mMob.mLevel, BlockPos(step->x, step->y, step->z), surface)
            && navigation->moveTo(surface.x + 0.5, surface.y, surface.z + 0.5, mMob.isCaptain() ? 0.595 : 0.7))
        {
            if (mMob.isCaptain())
            {
                const auto companions = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
                    mMob.mLevel->getEntitiesOfClass(typeid(Pillager), mMob.getBoundingBox()->grow(16.0, 16.0, 16.0)));
                for (const auto& entity : *companions)
                {
                    auto member = boost::static_pointer_cast<Pillager>(entity);
                    if (member.get() != &mMob && member->isAlive() && !member->isCaptain())
                        member->setPatrolTarget(surface);
                }
            }
        }
        else
        {
            const int dx = mMob.getRandom()->nextInt(16) - 8;
            const int dz = mMob.getRandom()->nextInt(16) - 8;
            if (PatrolSpawner::getSurface(*mMob.mLevel, BlockPos(&mMob).offset(dx, 0, dz), surface))
                navigation->moveTo(surface.x + 0.5, surface.y, surface.z + 0.5, 0.7);
        }
    }

private:
    Pillager& mMob;
};

class PillagerCrossbowGoal : public Goal
{
public:
    explicit PillagerCrossbowGoal(Pillager& pillager) : mPillager(pillager)
    {
        setRequiredControlFlags(3);
    }

    std::wstring getName() override { return L"PillagerCrossbowGoal"; }

    bool canUse() override
    {
        const auto target = mPillager.getTarget();
        return target && target->isAlive() && mPillager.getMainHandItem()->getItem() == Items::crossbow;
    }

    void stop() override
    {
        mPillager.stopUsingItem();
        mPillager.getNavigation()->stop();
        mVisibleTicks = 0;
        mReleaseDelay = 0;
        mPathDelay = 0;
    }

    void tick() override
    {
        const auto target = mPillager.getTarget();
        if (!target)
            return;
        const auto crossbow = mPillager.getMainHandItem();
        const bool visible = mPillager.getSensing()->canSee(target);
        mVisibleTicks = visible ? mVisibleTicks + 1 : 0;
        const bool inRange = mPillager.distanceToSqr(target) <= 64.0;
        if (inRange && mVisibleTicks >= 5)
            mPillager.getNavigation()->stop();
        else if (--mPathDelay <= 0)
        {
            mPillager.getNavigation()->moveTo(target, 1.0);
            mPathDelay = 10;
        }
        mPillager.getLookControl()->setLookAt(target, 30.0f, 30.0f);

        if (mPillager.isUsingItem())
        {
            if (mPillager.getTicksUsingItem() >= CrossbowItem::getChargeDuration(crossbow))
            {
                mPillager.stopUsingItem();
                CrossbowItem::loadProjectile(crossbow, not_null_ptr<ItemInstance>(new ItemInstance(Items::arrow)), 1);
                mPillager.setItemSlot(EquipmentSlot::MAINHAND, crossbow->copy());
                mPillager.playSound(SoundEvents::CROSSBOW_LOADING_END, 1.0f, 1.0f);
                mReleaseDelay = 20 + mPillager.getRandom()->nextInt(20);
            }
        }
        else if (CrossbowItem::isCharged(crossbow))
        {
            if (mReleaseDelay > 0)
                --mReleaseDelay;
            if (mReleaseDelay == 0 && visible && inRange)
                mPillager.shootCrossbow(target);
        }
        else if (inRange && mVisibleTicks >= 5)
        {
            mPillager.startUsingItem(InteractionHand::eInteractionHand_MainHand);
        }
    }

private:
    Pillager& mPillager;
    int mVisibleTicks = 0;
    int mReleaseDelay = 0;
    int mPathDelay = 0;
};
}

Pillager::Pillager(Level* level) : AbstractIllager(level)
{
    fjDerivedCtorCalls();
    setSize(0.6f, 1.95f);
    mCategories |= EntityCategory::eEntityCategory_Humanoid | EntityCategory::eEntityCategory_Monster;
    getAttribute(SharedMonsterAttributes::MAX_HEALTH)->setBaseValue(24.0);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.35);
    getAttribute(SharedMonsterAttributes::FOLLOW_RANGE)->setBaseValue(32.0);
    setHealth(24.0f);
    mExperienceReward = 5;
}

Entity* Pillager::create(Level* level)
{
    return new Pillager(level);
}

unsigned long long Pillager::GetType()
{
    return EntityType::PILLAGER;
}

std::wstring Pillager::getName()
{
    return hasCustomName() ? getCustomName() : L10N::GetString(isCaptain() ? IDS_LCE_PILLAGER_CAPTAIN : IDS_LCE_PILLAGER);
}

void Pillager::registerGoals()
{
    Raider::registerGoals();
    mGoalSelector.addGoal(0, new FloatGoal(this), true);
    mGoalSelector.addGoal(3, new PillagerCrossbowGoal(*this), true);
    mGoalSelector.addGoal(4, new PillagerPatrolGoal(*this), true);
    mGoalSelector.addGoal(8, new WaterAvoidingRandomStrollGoal(this, 0.6), true);
    mGoalSelector.addGoal(9, new LookAtPlayerGoal(this, typeid(Player), 8.0f), true);
    mTargetSelector.addGoal(1, new HurtByTargetGoal(this, true, {}), true);
    mTargetSelector.addGoal(2, new NearestAttackableTargetGoal(this, typeid(Player), true, false, false), true);
    mTargetSelector.addGoal(3, new NearestAttackableTargetGoal(this, typeid(Villager), true, false, false), true);
    mTargetSelector.addGoal(3, new NearestAttackableTargetGoal(this, typeid(VillagerGolem), true, false, false), true);
    mTargetSelector.addGoal(3, new NearestAttackableTargetGoal(this, typeid(WanderingTrader), true, false, false), true);
}

int Pillager::getArmPose()
{
    if (isCelebrating()) return ILLAGER_ARM_POSE_CELEBRATING;
    if (getMainHandItem()->getItem() != Items::crossbow)
        return ILLAGER_ARM_POSE_NEUTRAL;
    return isUsingItem() ? ILLAGER_ARM_POSE_CROSSBOW_CHARGE : ILLAGER_ARM_POSE_CROSSBOW_HOLD;
}

bool Pillager::checkSpawnRules()
{
    const BlockPos pos(this);
    auto* chunks = dynamic_cast<ServerChunkCache*>(mLevel->getChunkSource());
    if (!chunks || !chunks->isPosInFeature(mLevel, LargeFeature::PILLAGER_OUTPOST, pos))
        return Monster::checkSpawnRules();

    if (mLevel->getDifficulty() == Difficulty::PEACEFUL || mLevel->getBrightness(LightLayer::BLOCK, pos) > 8
        || mRandom->nextInt(16) != 0)
        return false;

    // An attempt at one edge must also count guards at the opposite edge.
    constexpr int range = PillagerOutpostFeature::PatrolRadius * 2;
    constexpr int limit = 4;
    const auto* area = AABB::newTemp(pos.x - range, 0, pos.z - range,
                                     pos.x + range + 1, mLevel->getMaxBuildHeight(), pos.z + range + 1);
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> nearby(mLevel->getEntitiesOfClass(typeid(Pillager), area));
    return std::count_if(nearby->begin(), nearby->end(), [](const auto& mob) { return mob->isAlive(); }) < limit;
}

MobGroupData* Pillager::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                       const std::wstring& name)
{
    auto* group = Monster::finalizeMobSpawn(difficulty, data, name);
    setItemSlot(EquipmentSlot::MAINHAND, not_null_ptr<ItemInstance>(new ItemInstance(Items::crossbow)));
    // A captain is a role of the same mob, including when spawned from an egg.
    // Explicitly assigned leaders keep their role during spawn finalization.
    if (!getCurrentRaid() && !mPatrolling && !isCaptain() && mRandom->nextFloat() < 0.06f)
        setCaptain(true);
    return group;
}

bool Pillager::isAlliedTo(boost::shared_ptr<Entity> entity)
{
    if (Entity::isAlliedTo(entity))
        return true;
    const auto living = boost::dynamic_pointer_cast<LivingEntity>(entity);
    return living && living->getMobType() == MOB_TYPE_ILLAGER && !getTeam() && !living->getTeam();
}

bool Pillager::isCaptain()
{
    return getIllagerFlag(1);
}

void Pillager::setCaptain(bool captain)
{
    if (isCaptain() == captain)
        return;
    setIllagerFlag(1, captain);
    setItemSlot(EquipmentSlot::HEAD, captain ? BannerItem::makeIllagerBanner() : ItemInstance::EMPTY);
    setDropChance(EquipmentSlot::HEAD, captain ? 2.0f : 0.085f);
}

void Pillager::preparePatrol(bool captain)
{
    mPatrolling = true;
    setCaptain(captain);
    if (captain)
        findPatrolTarget();
}

void Pillager::setPatrolTarget(const BlockPos& pos)
{
    mPatrolTarget = pos;
    mHasPatrolTarget = true;
    mPatrolling = true;
}

void Pillager::findPatrolTarget()
{
    const int dx = mRandom->nextInt(1000) - 500;
    const int dz = mRandom->nextInt(1000) - 500;
    setPatrolTarget(BlockPos(this).offset(dx, 0, dz));
}

bool Pillager::removeWhenFarAway()
{
    if (!Raider::removeWhenFarAway())
        return false;
    if (!mPatrolling)
        return true;
    const auto player = mLevel->getNearestPlayer(shared_from_this(), -1.0, -1.0);
    return !player || distanceToSqr(player) > 16384.0;
}

void Pillager::addAdditonalSaveData(CompoundTag* tag)
{
    Raider::addAdditonalSaveData(tag);
    tag->putBoolean(L"PatrolLeader", isCaptain());
    tag->putBoolean(L"Patrolling", mPatrolling);
    if (mHasPatrolTarget)
        tag->put(L"PatrolTarget", NbtUtils::writeBlockPos(mPatrolTarget));
}

void Pillager::readAdditionalSaveData(CompoundTag* tag)
{
    Raider::readAdditionalSaveData(tag);
    setIllagerFlag(1, tag->getBoolean(L"PatrolLeader"));
    mPatrolling = tag->getBoolean(L"Patrolling");
    mHasPatrolTarget = tag->contains(L"PatrolTarget", Tag::TAG_COMPOUND);
    if (mHasPatrolTarget)
        mPatrolTarget = NbtUtils::readBlockPos(tag->getCompound(L"PatrolTarget"));
}

bool Pillager::shootCrossbow(const boost::shared_ptr<LivingEntity>& target)
{
    auto crossbow = getMainHandItem();
    if (mLevel->mIsClientSide || !target || !target->isAlive() || crossbow->getItem() != Items::crossbow
        || !CrossbowItem::isCharged(crossbow))
        return false;

    auto ammo = not_null_ptr<ItemInstance>(new ItemInstance(Items::arrow));
    auto arrow = static_cast<ArrowItem*>(Items::arrow)->createArrow(
        mLevel, ammo, boost::static_pointer_cast<LivingEntity>(shared_from_this()));
    const double x = target->mX - arrow->mX;
    const double z = target->mZ - arrow->mZ;
    const double y = target->mY + target->mBbHeight / 3.0 - arrow->mY;
    arrow->setCrossbowShot(0);
    arrow->setPickup(AbstractRotatedItemProjectile::ePickup_Disallowed);
    arrow->shoot(x, y + std::sqrt(x * x + z * z) * 0.2, z, 1.6f, 14.0f - mLevel->getDifficulty()->getId() * 4.0f);
    if (!mLevel->addEntity(arrow))
        return false;
    auto unloaded = crossbow->copy();
    CrossbowItem::clearProjectiles(unloaded);
    setItemSlot(EquipmentSlot::MAINHAND, unloaded);
    playSound(SoundEvents::CROSSBOW_SHOOT, 1.0f, 1.0f);
    return true;
}

const SoundEvent* Pillager::getAmbientSound()
{
    return sAmbientSound;
}

const SoundEvent* Pillager::getHurtSound(const DamageSource*)
{
    return sHurtSound;
}

const SoundEvent* Pillager::getDeathSound()
{
    return sDeathSound;
}
