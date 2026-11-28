#include "game/core/Panda.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/render/ParticleType.hpp"
#include "game/core/AttributeInstance.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Sensing.hpp"
#include "game/core/StringIds.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/BreedGoal.hpp"
#include "game/entity/ai/AvoidEntityGoal.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/FollowParentGoal.hpp"
#include "game/entity/ai/LookControl.hpp"
#include "game/entity/ai/HurtByTargetGoal.hpp"
#include "game/entity/ai/MeleeAttackGoal.hpp"
#include "game/entity/ai/PanicGoal.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/ai/RandomLookAroundGoal.hpp"
#include "game/entity/ai/RandomStrollGoal.hpp"
#include "game/entity/ai/TemptGoal.hpp"
#include "game/entity/mob/EntityDataSerializers.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/entity/mob/Monster.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/entity/mob/SynchedEntityData.hpp"
#include "game/item/Item.hpp"
#include "game/item/Items.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/ui/EntityTooltipDataHolder.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/Blocks.hpp"

namespace
{
constexpr int EatingTicks = 110;

void emitPandaParticle(Level* level, const ParticleType* type, double x, double y, double z,
                      double xd, double yd, double zd, arrayWithLength<int> data = arrayWithLength<int>())
{
    if (auto* server = dynamic_cast<ServerLevel*>(level))
        server->sendParticles(type, x, y, z, 0, xd, yd, zd, 1.0, data);
    else
        level->addParticle(type, x, y, z, xd, yd, zd, data);
}

Panda::Gene randomGene(Random* random)
{
    const int choice = random->nextInt(16);
    if (choice < 4) return Panda::Normal;
    if (choice < 7) return Panda::Weak;
    if (choice < 9) return Panda::Brown;
    if (choice < 11) return Panda::Lazy;
    if (choice < 13) return Panda::Worried;
    if (choice < 15) return Panda::Playful;
    return Panda::Aggressive;
}

Panda::Gene savedGene(int value)
{
    return value >= Panda::Normal && value <= Panda::Aggressive ? static_cast<Panda::Gene>(value) : Panda::Normal;
}

class PandaRetaliationGoal : public HurtByTargetGoal
{
public:
    explicit PandaRetaliationGoal(Panda* panda)
        : HurtByTargetGoal(panda, true, arrayWithLength<unsigned long long>()), mPanda(panda)
    {
    }

    void start() override
    {
        if (mPanda->isBaby() || mPanda->getGene() == Panda::Worried)
        {
            mTimestamp = mPanda->getLastHurtByMobTimestamp();
            alertOthers();
            return;
        }
        HurtByTargetGoal::start();
    }

    bool canContinueToUse() override
    {
        return mPanda->getTarget() && HurtByTargetGoal::canContinueToUse();
    }

    void alertOther(boost::shared_ptr<PathfinderMob> mob, boost::shared_ptr<LivingEntity> target) override
    {
        if (mob->GetType() == EntityType::PANDA && !mob->isBaby()
            && boost::static_pointer_cast<Panda>(mob)->getGene() == Panda::Aggressive)
            HurtByTargetGoal::alertOther(mob, target);
    }

private:
    Panda* mPanda;
};

class PandaPanicGoal : public PanicGoal
{
public:
    explicit PandaPanicGoal(Panda* panda) : PanicGoal(panda, 1.35), mPanda(panda) {}
    bool canUse() override
    {
        return (mPanda->isOnFire() || mPanda->isBaby() || mPanda->getGene() == Panda::Worried)
            && PanicGoal::canUse();
    }
private:
    Panda* mPanda;
};

class PandaBreedGoal : public BreedGoal
{
public:
    explicit PandaBreedGoal(Panda* panda) : BreedGoal(panda, 1.0, typeid(Panda)), mPanda(panda) {}
    bool canUse() override { return mPanda->getPose() == Panda::Walking && BreedGoal::canUse(); }

    void tick() override
    {
        const auto partner = mPartner.lock();
        if (!partner) return;
        mPanda->getLookControl()->setLookAt(partner, 10.0f, static_cast<float>(mPanda->getMaxHeadXRot()));
        if (mPanda->distanceToSqr(partner) >= 9.0 || !mPanda->getSensing()->canSee(partner))
        {
            mTicks = 0;
            mPanda->getNavigation()->moveTo(partner, mSpeedModifier);
        }
        else if (++mTicks >= 60)
            breed();
    }

private:
    Panda* mPanda;
};

class PandaAvoidMonstersGoal : public AvoidEntityGoal
{
public:
    explicit PandaAvoidMonstersGoal(Panda* panda)
        : AvoidEntityGoal(panda, typeid(Monster), 8.0f, 1.0, 1.35), mPanda(panda)
    {
    }

    bool canUse() override
    {
        return mPanda->getGene() == Panda::Worried && !mPanda->mLevel->isThundering() && AvoidEntityGoal::canUse();
    }

private:
    Panda* mPanda;
};

class PandaFindFoodGoal : public Goal
{
public:
    explicit PandaFindFoodGoal(Panda* panda) : mPanda(panda) { setRequiredControlFlags(1); }

    bool canUse() override
    {
        if (!canSearch() || mPanda->mTickCount % 10 != 0)
            return false;

        std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> candidates(
            mPanda->mLevel->getEntitiesOfClass(typeid(ItemEntity), mPanda->mBoundingBox->grow(8.0, 3.0, 8.0)));
        double bestDistance = 64.0;
        for (const auto& entity : *candidates)
        {
            const auto food = boost::static_pointer_cast<ItemEntity>(entity);
            const double distance = mPanda->distanceToSqr(food);
            if (!food->mRemoved && !food->hasPickUpDelay() && mPanda->isFood(food->getItem()) && distance < bestDistance)
            {
                mFood = food;
                bestDistance = distance;
            }
        }
        return !mFood.expired();
    }

    bool canContinueToUse() override
    {
        const auto food = mFood.lock();
        return canSearch() && food && !food->mRemoved && !mPanda->getNavigation()->isDone();
    }

    void start() override
    {
        if (const auto food = mFood.lock())
            mPanda->getNavigation()->moveTo(food, 1.0);
    }

    void stop() override
    {
        mFood.reset();
        mPanda->getNavigation()->stop();
    }

    std::wstring getName() override { return L"Find bamboo"; }

private:
    bool canSearch() const
    {
        return !mPanda->isBaby() && !mPanda->isInLove() && mPanda->getPose() == Panda::Walking
            && mPanda->mLevel->getGameRules()->getBoolean(GameRules::RULE_MOBGRIEFING);
    }

    Panda* mPanda;
    boost::weak_ptr<ItemEntity> mFood;
};
}

const EntityDataAccessor<int>* Panda::DATA_GENES = nullptr;
const EntityDataAccessor<int>* Panda::DATA_POSE = nullptr;

Panda::Panda(Level* level) : Animal(level)
{
    fjDerivedCtorCalls();
    setSize(1.3f, 1.25f);
}

Entity* Panda::create(Level* level)
{
    return new Panda(level);
}

void Panda::staticCtor()
{
    DATA_GENES = SynchedEntityData::defineId(EntityType::PANDA, EntityType::ANIMAL, EntityDataSerializers::INT);
    DATA_POSE = SynchedEntityData::defineId(EntityType::PANDA, EntityType::ANIMAL, EntityDataSerializers::INT);
}

void Panda::defineSynchedData()
{
    Animal::defineSynchedData();
    mEntityData->createDateItem(DATA_GENES, 0);
    mEntityData->createDateItem(DATA_POSE, 0);
}

void Panda::registerGoals()
{
    Animal::registerGoals();
    mGoalSelector.addGoal(0, new FloatGoal(this), true);
    mGoalSelector.addGoal(1, new PandaPanicGoal(this), true);
    mGoalSelector.addGoal(1, new PandaAvoidMonstersGoal(this), true);
    mGoalSelector.addGoal(2, new MeleeAttackGoal(this, 1.2, true), true);
    mGoalSelector.addGoal(3, new PandaBreedGoal(this), true);
    static Item* food[] = {Items::bamboo};
    mGoalSelector.addGoal(4, new TemptGoal(this, 1.0, arrayWithLength<Item*>(food, 1), false,
                                         arrayWithLength<int>()), true);
    mGoalSelector.addGoal(5, new FollowParentGoal(this, 1.1), true);
    mGoalSelector.addGoal(6, new PandaFindFoodGoal(this), true);
    mGoalSelector.addGoal(7, new RandomStrollGoal(this, 0.85), true);
    mGoalSelector.addGoal(8, new RandomLookAroundGoal(this), true);
    mTargetSelector.addGoal(1, new PandaRetaliationGoal(this), true);
}

void Panda::registerAttributes()
{
    Animal::registerAttributes();
    getAttribute(SharedMonsterAttributes::MAX_HEALTH)->setBaseValue(20.0);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.15);
    getAttributes()->registerAttribute(SharedMonsterAttributes::ATTACK_DAMAGE);
    getAttribute(SharedMonsterAttributes::ATTACK_DAMAGE)->setBaseValue(6.0);
}

Panda::Gene Panda::getGene() const
{
    const int genes = mEntityData->get(DATA_GENES);
    const Gene main = savedGene(genes & 7);
    if ((main == Brown || main == Weak) && main != savedGene(genes >> 3))
        return Normal;
    return main;
}

void Panda::setGenes(Gene main, Gene hidden)
{
    mEntityData->set(DATA_GENES, static_cast<int>(main) | (static_cast<int>(hidden) << 3), true);
    const Gene gene = getGene();
    getAttribute(SharedMonsterAttributes::MAX_HEALTH)->setBaseValue(gene == Weak ? 10.0 : 20.0);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(gene == Lazy ? 0.07 : 0.15);
    getAttribute(SharedMonsterAttributes::ATTACK_DAMAGE)->setBaseValue(gene == Aggressive ? 8.0 : 6.0);
    setHealth(std::min(getHealth(), getMaxHealth()));
}

Panda::Pose Panda::getPose() const
{
    return static_cast<Pose>(mEntityData->get(DATA_POSE));
}

float Panda::getSittingProgress(float partialTick) const
{
    return mOldSittingProgress + (mSittingProgress - mOldSittingProgress) * partialTick;
}

float Panda::getLyingProgress(float partialTick) const
{
    return mOldLyingProgress + (mLyingProgress - mOldLyingProgress) * partialTick;
}

std::wstring Panda::getName()
{
    return hasCustomName() ? getCustomName() : L10N::GetString(IDS_LCE_PANDA);
}

MobGroupData* Panda::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                     const std::wstring& spawnData)
{
    const Gene main = randomGene(mRandom);
    const Gene hidden = randomGene(mRandom);
    setGenes(main, hidden);
    setHealth(getMaxHealth());
    return Animal::finalizeMobSpawn(difficulty, data, spawnData);
}

boost::shared_ptr<AgableMob> Panda::getBreedOffspring(boost::shared_ptr<AgableMob> partner)
{
    auto cub = boost::shared_ptr<Panda>(new Panda(mLevel));
    const auto other = boost::dynamic_pointer_cast<Panda>(partner);
    if (!other) return cub;

    const auto inherit = [this](const Panda& parent) {
        if (mRandom->nextInt(32) == 0) return randomGene(mRandom);
        const int genes = parent.mEntityData->get(DATA_GENES);
        return savedGene(mRandom->nextBoolean() ? genes & 7 : genes >> 3);
    };
    Gene main = inherit(*this);
    Gene hidden = inherit(*other);
    if (mRandom->nextBoolean()) std::swap(main, hidden);
    cub->setGenes(main, hidden);
    cub->setHealth(cub->getMaxHealth());
    return cub;
}

void Panda::addAdditonalSaveData(CompoundTag* tag)
{
    Animal::addAdditonalSaveData(tag);
    const int genes = mEntityData->get(DATA_GENES);
    tag->putInt(L"MainGene", genes & 7);
    tag->putInt(L"HiddenGene", genes >> 3);
}

void Panda::readAdditionalSaveData(CompoundTag* tag)
{
    Animal::readAdditionalSaveData(tag);
    setGenes(savedGene(tag->getInt(L"MainGene")), savedGene(tag->getInt(L"HiddenGene")));
}

bool Panda::isFood(const not_null_ptr<ItemInstance>& item)
{
    return item->getItem() == Items::bamboo;
}

unsigned int Panda::GetInteractTooltip(const EntityTooltipDataHolder& holder)
{
    if (isFood(holder.m_pItemInstance))
        return canEnterLoveMode() && hasBambooNearby() ? IDS_TOOLTIPS_LOVEMODE : IDS_TOOLTIPS_FEED;
    return Animal::GetInteractTooltip(holder);
}

bool Panda::canEnterLoveMode()
{
    return getAge() == 0 && !isInLove() && !isBreedCoolingDown() && getPose() != Eating && getPose() != Scared
        && !isOnFire();
}

bool Panda::hasBambooNearby()
{
    const BlockPos origin(this);
    int bamboo = 0;
    for (int x = -5; x <= 5; ++x)
        for (int z = -5; z <= 5; ++z)
            for (int y = 0; y < 3; ++y)
                if (mLevel->getBlock(origin.offset(x, y, z)) == Blocks::bamboo && ++bamboo >= 8)
                    return true;
    return false;
}

bool Panda::mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand)
{
    const auto item = player->getItemInHand(hand);
    if (!isFood(item) || isBaby())
        return Animal::mobInteract(player, hand);

    if (mLevel->mIsClientSide) return true;
    if (getPose() == Scared || getPose() == Eating || isOnFire()) return true;
    if (isInLove()) return Animal::mobInteract(player, hand);

    if (canEnterLoveMode())
    {
        if (hasBambooNearby())
        {
            startPose(Walking, 0);
            return Animal::mobInteract(player, hand);
        }
        playSound(SoundEvents::PANDA_CANT_BREED, 0.8f, 1.0f);
    }

    usePlayerItem(player, item);
    startPose(Eating, EatingTicks);
    return true;
}

void Panda::startPose(Pose pose, int duration)
{
    mEntityData->set(DATA_POSE, static_cast<int>(pose), true);
    mLastPose = pose;
    mPoseAge = 0;
    mPoseDuration = duration;
    if (pose == Rolling) mRollYaw = mYBodyRot;
}

void Panda::tick()
{
    Animal::tick();
    const Pose pose = getPose();
    if (pose != mLastPose)
    {
        mLastPose = pose;
        mPoseAge = 0;
    }
    else if (pose != Walking)
    {
        ++mPoseAge;
    }
    mOldSittingProgress = mSittingProgress;
    const bool sitting = pose == Eating || pose == Sitting || pose == Scared;
    mSittingProgress = Mth::clamp(mSittingProgress + (sitting ? 0.17f : -0.17f), 0.0f, 1.0f);
    mOldLyingProgress = mLyingProgress;
    mLyingProgress = Mth::clamp(mLyingProgress + (pose == Lying ? 0.125f : -0.125f), 0.0f, 1.0f);
}

void Panda::tickPose()
{
    const Pose pose = getPose();
    getNavigation()->stop();
    if (pose == Eating && mPoseAge % ChewInterval == 0)
        playSound(SoundEvents::PANDA_EAT, 0.75f, 0.9f + mRandom->nextFloat() * 0.2f);
    if (pose == Eating && mPoseAge >= ChewInterval && mPoseAge % ChewInterval == 0)
    {
        const float yaw = mYBodyRot * Mth::DEG_TO_RAD;
        int particleData[] = {Item::getId(Items::bamboo)};
        for (int i = 0; i < 3; ++i)
        {
            const double spread = (mRandom->nextFloat() - 0.5) * 0.3;
            const double x = mX - Mth::sin(yaw) * 0.82 + Mth::cos(yaw) * spread;
            const double z = mZ + Mth::cos(yaw) * 0.82 + Mth::sin(yaw) * spread;
            emitPandaParticle(mLevel, PARTICLE_ICON_CRACK, x, mY + 1.13, z, spread * 0.3, 0.04, 0,
                              arrayWithLength<int>(particleData, 1));
        }
    }
    if (pose == Rolling && mPoseAge < RollTicks - 2)
    {
        mYRot = mYBodyRot = mRollYaw;
        const float yaw = mRollYaw * Mth::DEG_TO_RAD;
        const float speed = mPoseAge < RollTicks - 6 ? 0.16f : 0.09f;
        mXd = -Mth::sin(yaw) * speed;
        mZd = Mth::cos(yaw) * speed;
        if (mPoseAge == 0) mYd = 0.22;
    }
    if (pose == Sneezing && mPoseAge == SneezeTick)
    {
        playSound(SoundEvents::PANDA_SNEEZE, 0.85f, 1.0f);
        const float yaw = mYHeadRot * Mth::DEG_TO_RAD;
        const float forwardX = -Mth::sin(yaw);
        const float forwardZ = Mth::cos(yaw);
        const float reach = isBaby() ? 0.84f : 1.32f;
        emitPandaParticle(mLevel, PARTICLE_SPIT, mX + forwardX * reach, mY + mBbHeight * 0.46,
                          mZ + forwardZ * reach, forwardX * 0.10, 0.025, forwardZ * 0.10);
        std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> nearby(
            mLevel->getEntitiesOfClass(typeid(Panda), mBoundingBox->grow(10.0)));
        for (const auto& entity : *nearby)
        {
            const auto panda = boost::static_pointer_cast<Panda>(entity);
            if (panda->isAlive() && !panda->isBaby() && panda->mOnGround && panda->getPose() == Walking
                && distanceToSqr(panda) <= 100.0)
                panda->jumpFromGround();
        }
        if (mLevel->getGameRules()->getBoolean(GameRules::RULE_DOMOBLOOT) && mRandom->nextInt(700) == 0)
            spawnAtLocation(Items::slimeBall, 1);
    }
    if (mPoseAge >= mPoseDuration) startPose(Walking, 0);
}

void Panda::customServerAiStep()
{
    Animal::customServerAiStep();
    if (isOnFire() || isInWater() || getTarget())
    {
        if (getPose() != Walking) startPose(Walking, 0);
        return;
    }
    const Gene gene = getGene();
    if (gene == Worried && mLevel->isThundering())
    {
        if (getPose() != Scared) startPose(Scared, 40);
        getNavigation()->stop();
        return;
    }
    if (getPose() == Scared) startPose(Walking, 0);
    if (getPose() != Walking)
    {
        tickPose();
        return;
    }
    if (!mOnGround || isInLove()) return;

    if (isBaby() && mRandom->nextInt(gene == Weak ? 500 : 6000) == 0)
    {
        startPose(Sneezing, SneezeTick + SneezeRecoveryTicks);
        playSound(SoundEvents::PANDA_PRESNEEZE, 0.8f, 1.0f);
    }
    else if ((gene == Playful || isBaby()) && mRandom->nextInt(gene == Playful ? 180 : 900) == 0)
    {
        startPose(Rolling, RollTicks);
        tickPose();
    }
    else if (mRandom->nextInt(gene == Lazy ? 190 : 1400) == 0)
    {
        startPose(gene == Lazy ? Lying : Sitting, gene == Lazy ? 140 : 70);
    }
}

bool Panda::canPickUpLoot()
{
    return !isBaby() && !isInLove() && getPose() == Walking;
}

void Panda::pickUpItem(boost::shared_ptr<ItemEntity> entity)
{
    auto item = entity->getItem();
    if (getPose() != Walking || !isFood(item)) return;
    item->shrink(1);
    if (item->isEmpty()) entity->remove();
    startPose(Eating, EatingTicks);
}

const SoundEvent* Panda::getAmbientSound()
{
    if (getGene() == Worried) return SoundEvents::PANDA_WORRIED;
    if (getGene() == Aggressive) return SoundEvents::PANDA_AGGRESSIVE;
    return SoundEvents::PANDA_IDLE;
}

const SoundEvent* Panda::getHurtSound(const DamageSource*)
{
    return SoundEvents::PANDA_HURT;
}

const SoundEvent* Panda::getDeathSound()
{
    return SoundEvents::PANDA_DEATH;
}

void Panda::playStepSound(const BlockPos&, Block*)
{
    playSound(SoundEvents::PANDA_STEP, 0.15f, 1.0f);
}

bool Panda::doHurtTarget(boost::shared_ptr<Entity> target)
{
    auto self = boost::static_pointer_cast<LivingEntity>(shared_from_this());
    EntityDamageSource* source = DamageSource::CreateMobAttack(self);
    const bool hurt = target->hurt(source, static_cast<float>(getAttribute(SharedMonsterAttributes::ATTACK_DAMAGE)->getValue()));
    delete source;
    if (!hurt) return false;
    doEnchantDamageEffects(self, target);
    playSound(SoundEvents::PANDA_BITE, 0.8f, 1.0f);
    if (getGene() != Aggressive) setTarget(boost::shared_ptr<LivingEntity>());
    return true;
}

void Panda::dropDeathLoot(bool, int)
{
    if (!isBaby()) spawnAtLocation(Items::bamboo, 1);
}
