#include "game/core/Ravager.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include "game/core/AbstractIllager.hpp"
#include "game/core/AttributeInstance.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Mth.hpp"
#include "game/core/PathFinder.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/GroundPathNavigation.hpp"
#include "game/entity/ai/HurtByTargetGoal.hpp"
#include "game/entity/ai/MeleeAttackGoal.hpp"
#include "game/entity/ai/NearestAttackableTargetGoal.hpp"
#include "game/entity/ai/WaterAvoidingRandomStrollGoal.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/item/Items.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/core/GameRules.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockPathTypes.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/LeafBlock.hpp"
#include "game/world/generation/LookAtPlayerGoal.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/VillagerGolem.hpp"
#include "game/world/generation/WanderingTrader.hpp"
#include "game/client/render/ParticleType.hpp"

SoundEvent* Ravager::sAmbient = nullptr;
SoundEvent* Ravager::sBite = nullptr;
SoundEvent* Ravager::sDeath = nullptr;
SoundEvent* Ravager::sHurt = nullptr;
SoundEvent* Ravager::sRoar = nullptr;
SoundEvent* Ravager::sStep = nullptr;
SoundEvent* Ravager::sStun = nullptr;

namespace
{
class RavagerMeleeGoal : public MeleeAttackGoal
{
public:
    explicit RavagerMeleeGoal(Ravager* mob) : MeleeAttackGoal(mob, 1.0, true) {}
    double getAttackReachSqr(boost::shared_ptr<LivingEntity> target) override
    {
        const double reach = (mMob->mBbWidth - 0.1f) * 2.0f;
        return reach * reach + target->mBbWidth;
    }
};

class RavagerNodes : public WalkNodeEvaluator
{
public:
    explicit RavagerNodes(Mob* mob) : WalkNodeEvaluator(mob) {}
    const BlockPathTypes* getBlockPathType(LevelSource* level, int x, int y, int z) override
    {
        if (mEntity->mLevel->getGameRules()->getBoolean(GameRules::RULE_MOBGRIEFING)
            && dynamic_cast<LeafBlock*>(level->getBlockState(BlockPos(x, y, z))->getBlock()))
            return BlockPathTypes::OPEN;
        return WalkNodeEvaluator::getBlockPathType(level, x, y, z);
    }
};

class RavagerNavigation : public GroundPathNavigation
{
public:
    RavagerNavigation(Mob* mob, Level* level) : GroundPathNavigation(mob, level, false), mNodes(mob)
    {
        mNodes.setCanFloat(true);
        setCanFloat(true);
        mPathFinder = createPathFinder();
    }
    NodeEvaluator* getNodeEvaluator() override { return &mNodes; }
    PathFinder* createPathFinder() override { return new PathFinder(&mNodes); }
private:
    RavagerNodes mNodes;
};
}

Ravager::Ravager(Level* level) : Raider(level)
{
    fjDerivedCtorCalls();
    setSize(1.95f, 2.2f);
    mMaxUpStep = 1.0f;
    mExperienceReward = 20;
    getAttribute(SharedMonsterAttributes::MAX_HEALTH)->setBaseValue(100.0);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.3);
    getAttribute(SharedMonsterAttributes::KNOCKBACK_RESISTANCE)->setBaseValue(0.5);
    getAttribute(SharedMonsterAttributes::ATTACK_DAMAGE)->setBaseValue(12.0);
    getAttribute(SharedMonsterAttributes::FOLLOW_RANGE)->setBaseValue(32.0);
    setHealth(100.0f);
}
Entity* Ravager::create(Level* level) { return new Ravager(level); }
unsigned long long Ravager::GetType() { return EntityType::RAVAGER; }
std::wstring Ravager::getName() { return hasCustomName() ? getCustomName() : L10N::GetString(IDS_LCE_RAVAGER); }
PathNavigation* Ravager::createNavigation(Level* level) { return new RavagerNavigation(this, level); }

void Ravager::registerGoals()
{
    Raider::registerGoals();
    mGoalSelector.addGoal(0, new FloatGoal(this), true);
    mGoalSelector.addGoal(4, new RavagerMeleeGoal(this), true);
    mGoalSelector.addGoal(5, new WaterAvoidingRandomStrollGoal(this, 0.4), true);
    mGoalSelector.addGoal(6, new LookAtPlayerGoal(this, typeid(Player), 6.0f), true);
    mGoalSelector.addGoal(10, new LookAtPlayerGoal(this, typeid(Mob), 8.0f), true);
    mTargetSelector.addGoal(2, new HurtByTargetGoal(this, true, {}), true);
    mTargetSelector.addGoal(3, new NearestAttackableTargetGoal(this, typeid(Player), true, false, false), true);
    mTargetSelector.addGoal(4, new NearestAttackableTargetGoal(this, typeid(Villager), true, false, false), true);
    mTargetSelector.addGoal(4, new NearestAttackableTargetGoal(this, typeid(WanderingTrader), true, false, false), true);
    mTargetSelector.addGoal(4, new NearestAttackableTargetGoal(this, typeid(VillagerGolem), true, false, false), true);
}

bool Ravager::isAlliedTo(boost::shared_ptr<Entity> entity)
{
    if (Monster::isAlliedTo(entity)) return true;
    return entity && !getTeam() && !entity->getTeam()
        && (entity->GetType() == EntityType::RAVAGER || boost::dynamic_pointer_cast<AbstractIllager>(entity));
}
bool Ravager::isImmobile() { return Monster::isImmobile() || mAttackTick > 0 || mStunTick > 0 || mRoarTick > 0; }
bool Ravager::canSee(boost::shared_ptr<Entity> entity)
{
    return mStunTick == 0 && mRoarTick == 0 && Monster::canSee(entity);
}

void Ravager::aiStep()
{
    Raider::aiStep();
    if (!isAlive()) return;
    if (!mLevel->mIsClientSide)
    {
        auto* speed = getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED);
        const double desired = getTarget() ? 0.35 : 0.3;
        speed->setBaseValue(isImmobile() ? 0.0 : speed->getBaseValue() + (desired - speed->getBaseValue()) * 0.1);
        if (mHorizontalCollision && mLevel->getGameRules()->getBoolean(GameRules::RULE_MOBGRIEFING))
        {
            bool broken = false;
            const auto* box = mBoundingBox->grow(0.2, 0.2, 0.2);
            for (int x = Mth::floor(box->mMinX); x <= Mth::floor(box->mMaxX); ++x)
                for (int y = Mth::floor(box->mMinY); y <= Mth::floor(box->mMaxY); ++y)
                    for (int z = Mth::floor(box->mMinZ); z <= Mth::floor(box->mMaxZ); ++z)
                    {
                        const BlockPos pos(x, y, z);
                        if (mLevel->hasChunkAt(pos) && dynamic_cast<LeafBlock*>(mLevel->getBlockState(pos)->getBlock()))
                            broken = mLevel->destroyBlock(pos, true) || broken;
                    }
            if (!broken && mOnGround) jumpFromGround();
        }
    }
    if (mRoarTick > 0 && --mRoarTick == 10) roar();
    if (mAttackTick > 0) --mAttackTick;
    if (mStunTick > 0)
    {
        --mStunTick;
        if (mLevel->mIsClientSide && mRandom->nextInt(6) == 0)
        {
            const double angle = mYBodyRot * Mth::DEG_TO_RAD;
            mLevel->addParticle(PARTICLE_MOB_SPELL, mX - mBbWidth * std::sin(angle) + mRandom->nextDouble() * 0.6 - 0.3,
                mY + mBbHeight - 0.3, mZ + mBbWidth * std::cos(angle) + mRandom->nextDouble() * 0.6 - 0.3,
                0.498039, 0.513725, 0.572549, arrayWithLength<int>());
        }
        if (mStunTick == 0)
        {
            mRoarTick = 20;
            if (!mLevel->mIsClientSide) playSound(sRoar, 1.0f, 1.0f);
        }
    }
}

bool Ravager::doHurtTarget(boost::shared_ptr<Entity> target)
{
    if (mLevel->mIsClientSide || isImmobile()) return false;
    mAttackTick = 10;
    mLevel->broadcastEntityEvent(shared_from_this(), 4, 0);
    playSound(sBite, 1.0f, 1.0f);
    const bool hurt = Monster::doHurtTarget(target);
    if (hurt)
    {
        auto living = boost::dynamic_pointer_cast<LivingEntity>(target);
        if (living)
            living->knockback(shared_from_this(), 0.75f, std::sin(mYRot * Mth::DEG_TO_RAD),
                              -std::cos(mYRot * Mth::DEG_TO_RAD));
        else target->push(-std::sin(mYRot * Mth::DEG_TO_RAD) * 0.75, 0.1, std::cos(mYRot * Mth::DEG_TO_RAD) * 0.75);
        mXd *= 0.6; mZd *= 0.6;
    }
    return hurt;
}
void Ravager::blockedByShield(const boost::shared_ptr<LivingEntity>& defender)
{
    if (mLevel->mIsClientSide || mRoarTick > 0) return;
    if (mRandom->nextDouble() < 0.5)
    {
        mStunTick = 40;
        playSound(sStun, 1.0f, 1.0f);
        mLevel->broadcastEntityEvent(shared_from_this(), 39, 0);
        Monster::blockedByShield(defender);
    }
    else strongKnockback(defender);
    defender->markHurt();
}
void Ravager::strongKnockback(const boost::shared_ptr<Entity>& target)
{
    const double dx = target->mX - mX, dz = target->mZ - mZ;
    const double squared = std::max(dx * dx + dz * dz, 0.001);
    target->push(dx / squared * 4.0, 0.2, dz / squared * 4.0);
    target->markHurt();
}
void Ravager::roar()
{
    if (!isAlive()) return;
    if (!mLevel->mIsClientSide)
    {
        // Supply our own result buffer: the default Level query buffer is borrowed
        // and nested damage/entity queries may overwrite it.
        std::vector<boost::shared_ptr<Entity>> entities;
        mLevel->getEntities(shared_from_this(), mBoundingBox->grow(4.0, 4.0, 4.0), &entities);
        for (const auto& entity : entities)
        {
            const auto living = boost::dynamic_pointer_cast<LivingEntity>(entity);
            if (!living || !living->isAlive() || entity->GetType() == EntityType::RAVAGER) continue;
            if (!boost::dynamic_pointer_cast<AbstractIllager>(entity))
            {
                std::unique_ptr<EntityDamageSource> source(DamageSource::CreateMobAttack(
                    boost::static_pointer_cast<LivingEntity>(shared_from_this())));
                living->hurt(source.get(), 6.0f);
            }
            strongKnockback(entity);
        }
    }
    else
        for (int i = 0; i < 40; ++i)
            mLevel->addParticle(PARTICLE_EXPLODE, mX, mY + mBbHeight * 0.5, mZ,
                mRandom->nextGaussian() * 0.2, mRandom->nextGaussian() * 0.2, mRandom->nextGaussian() * 0.2,
                arrayWithLength<int>());
}
void Ravager::handleEntityEvent(unsigned char event, int data)
{
    if (event == 4) mAttackTick = 10;
    else if (event == 39) mStunTick = 40;
    else Raider::handleEntityEvent(event, data);
}
void Ravager::saveAnimation(CompoundTag* tag)
{
    tag->putInt(L"AttackTick", mAttackTick); tag->putInt(L"StunTick", mStunTick); tag->putInt(L"RoarTick", mRoarTick);
}
void Ravager::loadAnimation(CompoundTag* tag)
{
    if (!tag) return;
    mAttackTick = Mth::clamp(tag->getInt(L"AttackTick"), 0, 10);
    mStunTick = Mth::clamp(tag->getInt(L"StunTick"), 0, 40);
    mRoarTick = Mth::clamp(tag->getInt(L"RoarTick"), 0, 20);
}
void Ravager::addAdditonalSaveData(CompoundTag* tag) { Raider::addAdditonalSaveData(tag); saveAnimation(tag); }
void Ravager::readAdditionalSaveData(CompoundTag* tag) { Raider::readAdditionalSaveData(tag); loadAnimation(tag); }
CompoundTag* Ravager::getAdditionContructionTag() { auto* tag = Raider::getAdditionContructionTag(); saveAnimation(tag); return tag; }
void Ravager::readAdditionContructionTag(CompoundTag* tag) { Raider::readAdditionContructionTag(tag); loadAnimation(tag); }
void Ravager::dropDeathLoot(bool, int) { spawnAtLocation(Items::saddle, 1); }
boost::shared_ptr<Entity> Ravager::getControllingPassenger()
{
    auto* passengers = getPassengers();
    return passengers->empty() ? boost::shared_ptr<Entity>() : passengers->front().lock();
}
bool Ravager::canBeControlledByRider()
{
    return !isNoAi() && !!boost::dynamic_pointer_cast<LivingEntity>(getControllingPassenger());
}
const SoundEvent* Ravager::getAmbientSound() { return sAmbient; }
const SoundEvent* Ravager::getHurtSound(const DamageSource*) { return sHurt; }
const SoundEvent* Ravager::getDeathSound() { return sDeath; }
void Ravager::playStepSound(const BlockPos&, Block*) { playSound(sStep, 0.15f, 1.0f); }
