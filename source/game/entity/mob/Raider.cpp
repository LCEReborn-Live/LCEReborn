#include "game/entity/mob/Raider.hpp"
#include <algorithm>
#include <memory>
#include "game/core/DamageSource.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/Wolf.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/Goal.hpp"
#include "game/entity/ai/BreakDoorGoal.hpp"
#include "game/entity/ai/GroundPathNavigation.hpp"
#include "game/core/JumpControl.hpp"
#include "game/entity/ai/OpenDoorGoal.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/entity/mob/EntityEvent.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/item/BannerItem.hpp"
#include "game/item/Enchantments.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/util/RandomPos.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Raids.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Villages.hpp"

namespace
{
class RaidPathGoal : public Goal
{
    Raider& mMob;
public:
    explicit RaidPathGoal(Raider& mob) : mMob(mob) { setRequiredControlFlags(1); }
    std::wstring getName() override { return L"PathfindToRaid"; }
    bool canUse() override
    {
        auto* raid = mMob.getCurrentRaid();
        return !mMob.getTarget() && !mMob.isPassenger() && raid && raid->isActive() && !raid->isOver()
            && !raid->level().getRaids()->isVillage(BlockPos(&mMob));
    }
    void tick() override
    {
        auto* raid = mMob.getCurrentRaid();
        if (!raid) return;
        if (mMob.mTickCount % 20 == 0)
        {
            std::vector<boost::shared_ptr<Entity>> nearby;
            mMob.mLevel->getEntities(mMob.shared_from_this(), mMob.getBoundingBox()->grow(16, 16, 16), &nearby);
            for (const auto& entity : nearby)
                if (auto other = boost::dynamic_pointer_cast<Raider>(entity))
                    if (other->isAlive() && other->canJoinRaid() && !other->getCurrentRaid() && other->getNoActionTime() <= 2400)
                        raid->join(other, raid->getGroupsSpawned());
        }
        if (!mMob.getNavigation()->isDone()) return;
        auto destination = raid->getCenter();
        auto* step = RandomPos::getPosTowards(boost::static_pointer_cast<PathfinderMob>(mMob.shared_from_this()),
            15, 4, Vec3::newTemp(destination.x, destination.y, destination.z));
        if (step) mMob.getNavigation()->moveTo(step->x, step->y, step->z, 1.0);
    }
};

class RaidVillageGoal : public Goal
{
    Raider& mMob;
    BlockPos mDestination;
    std::vector<BlockPos> mVisited;
    bool mStuck = false;
public:
    explicit RaidVillageGoal(Raider& mob) : mMob(mob) { setRequiredControlFlags(1); }
    std::wstring getName() override { return L"RaidMoveThroughVillage"; }
    bool canUse() override
    {
        auto* raid = mMob.getCurrentRaid();
        if (mMob.getTarget() || mMob.isPassenger() || !raid || !raid->isActive() || raid->isOver()) return false;
        if (mVisited.size() > 2) mVisited.erase(mVisited.begin());
        std::vector<BlockPos> beds;
        for (const auto& village : *mMob.mLevel->getVillages()->getVillages())
            for (const auto& bed : village->getBeds())
                if (bed.distSqr(BlockPos(&mMob)) < 48 * 48
                    && std::find(mVisited.begin(), mVisited.end(), bed) == mVisited.end()) beds.push_back(bed);
        if (beds.empty()) return false;
        mDestination = beds[mMob.getRandom()->nextInt(static_cast<int>(beds.size()))];
        return true;
    }
    void start() override
    {
        mStuck = false;
        mMob.mNoActionTime = 0;
        mMob.getNavigation()->moveTo(mDestination.x, mDestination.y, mDestination.z, 1.05);
    }
    bool canContinueToUse() override
    {
        const double range = mMob.mBbWidth + 1;
        return !mMob.getTarget() && !mStuck && mMob.hasActiveRaid()
            && !mMob.getNavigation()->isDone() && mMob.distanceToSqr(mDestination) > range * range;
    }
    void stop() override
    {
        if (mMob.distanceToSqr(mDestination) <= 1) mVisited.push_back(mDestination);
    }
    void tick() override
    {
        if (!mMob.getNavigation()->isDone()) return;
        const auto mob = boost::static_pointer_cast<PathfinderMob>(mMob.shared_from_this());
        auto* target = Vec3::newTemp(mDestination.x, mDestination.y, mDestination.z);
        auto* step = RandomPos::getPosTowards(mob, 16, 7, target, 0.3141592741);
        if (!step) step = RandomPos::getPosTowards(mob, 8, 7, target);
        if (step) mMob.getNavigation()->moveTo(step->x, step->y, step->z, 1.05);
        else mStuck = true;
    }
};

class RaidBannerGoal : public Goal
{
    Raider& mMob;
    boost::weak_ptr<ItemEntity> mBanner;
public:
    explicit RaidBannerGoal(Raider& mob) : mMob(mob) { setRequiredControlFlags(1); }
    std::wstring getName() override { return L"ObtainRaidLeaderBanner"; }
    bool canUse() override
    {
        auto* raid = mMob.getCurrentRaid();
        if (!raid || !raid->isActive() || !mMob.canLeadRaid() || mMob.isCaptain() || raid->getLeader(mMob.getWave())) return false;
        const auto items = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
            mMob.mLevel->getEntitiesOfClass(typeid(ItemEntity), mMob.getBoundingBox()->grow(16, 8, 16)));
        for (const auto& entity : *items)
        {
            auto item = boost::static_pointer_cast<ItemEntity>(entity);
            if (!item->hasPickUpDelay() && item->isAlive() && Raider::isLeaderBanner(item->getItem()))
            {
                mBanner = item;
                return mMob.getNavigation()->moveTo(item, 1.15);
            }
        }
        return false;
    }
    bool canContinueToUse() override { return !mBanner.expired() && !mMob.getNavigation()->isDone() && !mMob.isCaptain(); }
    void tick() override
    {
        auto item = mBanner.lock();
        if (item && item->isAlive() && mMob.distanceToSqr(item) < 2) mMob.pickUpItem(item);
    }
};

class RaiderOpenDoorGoal : public OpenDoorGoal
{
    Raider& mMob;
public:
    explicit RaiderOpenDoorGoal(Raider& mob) : OpenDoorGoal(&mob, false), mMob(mob) {}
    bool canUse() override { return mMob.hasActiveRaid() && OpenDoorGoal::canUse(); }
};
class RaidBreakDoorGoal : public BreakDoorGoal
{
    Raider& mMob;
public:
    explicit RaidBreakDoorGoal(Raider& mob) : BreakDoorGoal(&mob, 6, 2), mMob(mob) {}
    bool canUse() override
    {
        return mMob.hasActiveRaid() && mMob.mLevel->getDifficulty()->getId() >= 2
            && mMob.getRandom()->nextInt(10) == 0 && BreakDoorGoal::canUse();
    }
    bool canContinueToUse() override { return mMob.hasActiveRaid() && BreakDoorGoal::canContinueToUse(); }
    void start() override { BreakDoorGoal::start(); mMob.mNoActionTime = 0; }
};
class RaidCelebrateGoal : public Goal
{
    Raider& mMob;
public:
    explicit RaidCelebrateGoal(Raider& mob) : mMob(mob) { setRequiredControlFlags(1); }
    std::wstring getName() override { return L"CelebrateRaidLoss"; }
    bool canUse() override { auto* raid = mMob.getCurrentRaid(); return !mMob.getTarget() && raid && raid->isLoss(); }
    void start() override { mMob.setCelebrating(true); mMob.getNavigation()->stop(); }
    void stop() override { mMob.setCelebrating(false); }
};
}

SoundEvent* Raider::celebrationSounds[5] = {};
Raider::Raider(Level* level) : Monster(level) {}
void Raider::registerGoals()
{
    mGoalSelector.addGoal(1, new RaidBannerGoal(*this), true);
    mGoalSelector.addGoal(3, new RaidPathGoal(*this), true);
    mGoalSelector.addGoal(4, new RaidVillageGoal(*this), true);
    if (canLeadRaid()) mGoalSelector.addGoal(2, new RaiderOpenDoorGoal(*this), true);
    if (GetType() == EntityType::VINDICATION_ILLAGER) mGoalSelector.addGoal(1, new RaidBreakDoorGoal(*this), true);
    mGoalSelector.addGoal(5, new RaidCelebrateGoal(*this), true);
}
Raid* Raider::getCurrentRaid() const
{
    auto* level = dynamic_cast<ServerLevel*>(mLevel);
    return level && level->getRaids() && mRaidId ? level->getRaids()->get(mRaidId) : nullptr;
}
bool Raider::hasActiveRaid() const { auto* raid = getCurrentRaid(); return raid && raid->isActive(); }
void Raider::setRaid(int id, int wave)
{
    mRaidId = id; mWave = wave; mTicksOutsideRaid = 0;
    if (id) mCanJoinRaid = true;
    if (auto* navigation = dynamic_cast<GroundPathNavigation*>(getNavigation()))
        if (canLeadRaid()) navigation->setCanOpenDoors(id != 0);
}
void Raider::aiStep()
{
    if (!mLevel->mIsClientSide && isAlive())
    {
        auto* level = static_cast<ServerLevel*>(mLevel);
        auto self = boost::static_pointer_cast<Raider>(shared_from_this());
        auto* raid = getCurrentRaid();
        if (!raid && mRaidId) setRaid(0, 0);
        if (raid)
        {
            // Reattach loaded entities without charging their health twice.
            raid->join(self, mWave, false);
            if (getTarget()) mNoActionTime = 0;
        }
        else if (mCanJoinRaid && mTickCount % 20 == 0 && mNoActionTime <= 2400 && level->getRaids())
            if (auto* nearby = level->getRaids()->getNearby(BlockPos(this)))
                if (!nearby->isOver()) nearby->join(self, nearby->getGroupsSpawned());
        raid = getCurrentRaid();
        setCelebrating(raid && raid->isLoss() && !getTarget());
        if (mCelebrating)
        {
            getNavigation()->stop();
            if (mRandom->nextInt(100) == 0)
            {
                const auto type = GetType();
                const int index = type == EntityType::VINDICATION_ILLAGER ? 0 : type == EntityType::EVOCATION_ILLAGER ? 1
                    : type == EntityType::PILLAGER ? 2 : type == EntityType::WITCH ? 3 : 4;
                if (celebrationSounds[index]) playSound(celebrationSounds[index], getSoundVolume(), getVoicePitch());
            }
            if (!isPassenger() && mRandom->nextInt(50) == 0) getJumpControl()->jump();
        }
    }
    Monster::aiStep();
}
bool Raider::isCaptain() { return mCaptain; }
void Raider::setCaptain(bool captain)
{
    mCaptain = captain;
    setItemSlot(EquipmentSlot::HEAD, captain ? BannerItem::makeIllagerBanner() : ItemInstance::EMPTY);
    setDropChance(EquipmentSlot::HEAD, captain ? 2.0f : 0.085f);
}
bool Raider::canLeadRaid()
{
    return eTYPE_DERIVED_FROM(EntityType::ABSTRACT_ILLAGER, GetType());
}
bool Raider::isLeaderBanner(const not_null_ptr<ItemInstance>& item)
{
    return ItemInstance::matches(item, BannerItem::makeIllagerBanner(), true);
}
void Raider::pickUpItem(boost::shared_ptr<ItemEntity> item)
{
    auto* raid = getCurrentRaid();
    if (raid && canLeadRaid() && !raid->getLeader(mWave) && isLeaderBanner(item->getItem()))
    {
        if (!getItemSlot(EquipmentSlot::HEAD)->isEmpty()) spawnAtLocation(getItemSlot(EquipmentSlot::HEAD), 0);
        setCaptain(true);
        take(item, item->getItem()->getCount());
        item->remove();
        raid->join(boost::static_pointer_cast<Raider>(shared_from_this()), mWave, false);
    }
    else Monster::pickUpItem(item);
}
void Raider::die(const DamageSource* source)
{
    if (!mDead && !mLevel->mIsClientSide)
    {
        auto killer = source->getEntity();
        auto player = boost::dynamic_pointer_cast<Player>(killer);
        if (!player)
            if (auto wolf = boost::dynamic_pointer_cast<Wolf>(killer))
                if (wolf->isTame()) player = boost::dynamic_pointer_cast<Player>(wolf->getOwner());
        if (player && GetType() == EntityType::RAVAGER)
            player->awardStat(GenericStats::achievement(eAward_KillTheBeast), GenericStats::param_noArgs());
        if (player && GetType() == EntityType::PILLAGER && isCaptain())
            player->awardStat(GenericStats::achievement(eAward_BadFeeling), GenericStats::param_noArgs());
        auto* raid = getCurrentRaid();
        if (raid)
        {
            if (boost::dynamic_pointer_cast<Player>(killer)) raid->addHero(killer->getUUID());
            raid->remove(*this);
        }
        else if (player && isCaptain() && isLeaderBanner(getItemSlot(EquipmentSlot::HEAD)))
        {
            auto* level = static_cast<ServerLevel*>(mLevel);
            if (level->getRaids() && !level->getRaids()->isDisabled() && !level->getRaids()->getNearby(BlockPos(this)))
            {
                const auto* old = player->getEffect(MobEffects::BAD_OMEN);
                const int amplifier = old ? std::min(old->getAmplifier() + 1, 5) : 0;
                player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN, 120000, amplifier, false, false), shared_from_this());
                level->broadcastEntityEvent(player, EntityEvent::RAID_BAD_OMEN, 0);
            }
        }
    }
    Monster::die(source);
}
bool Raider::removeWhenFarAway() { return !getCurrentRaid() && Monster::removeWhenFarAway(); }
void Raider::finalizeSpawnEggSpawn(int data) { Monster::finalizeSpawnEggSpawn(data); mCanJoinRaid = true; }
void Raider::setCelebrating(bool value)
{
    if (value == mCelebrating) return;
    mCelebrating = value;
    if (!mLevel->mIsClientSide) mLevel->broadcastEntityEvent(shared_from_this(), value ? 60 : 61, 0);
}
void Raider::handleEntityEvent(unsigned char event, int data)
{
    if (event == 60 || event == 61) mCelebrating = event == 60;
    else Monster::handleEntityEvent(event, data);
}
CompoundTag* Raider::getAdditionContructionTag()
{
    auto* tag = new CompoundTag(); tag->putBoolean(L"Celebrating", mCelebrating); return tag;
}
void Raider::readAdditionContructionTag(CompoundTag* tag)
{
    if (tag) mCelebrating = tag->getBoolean(L"Celebrating");
}
void Raider::addAdditonalSaveData(CompoundTag* tag)
{
    Monster::addAdditonalSaveData(tag);
    tag->putInt(L"RaidId", mRaidId); tag->putInt(L"Wave", mWave);
    tag->putBoolean(L"PatrolLeader", isCaptain()); tag->putBoolean(L"CanJoinRaid", mCanJoinRaid);
    tag->putInt(L"TicksOutsideRaid", mTicksOutsideRaid);
}
void Raider::readAdditionalSaveData(CompoundTag* tag)
{
    Monster::readAdditionalSaveData(tag);
    setRaid(tag->getInt(L"RaidId"), tag->getInt(L"Wave"));
    mCaptain = tag->getBoolean(L"PatrolLeader");
    if (tag->contains(L"CanJoinRaid")) mCanJoinRaid = tag->getBoolean(L"CanJoinRaid");
    mTicksOutsideRaid = tag->getInt(L"TicksOutsideRaid");
}
void Raider::applyRaidBuffs(int wave)
{
    auto* raid = getCurrentRaid();
    if (!raid || mRandom->nextFloat() > raid->getEnchantOdds()) return;
    auto item = getMainHandItem()->copy();
    if (item->getItem() == Items::crossbow)
    {
        if (wave > 3) item->enchant(Enchantments::QUICK_CHARGE, wave > 5 ? 2 : 1);
        item->enchant(Enchantments::MULTISHOT, 1);
    }
    else if (item->getItem() == Items::ironAxe) item->enchant(Enchantments::SHARPNESS, wave > 5 ? 2 : 1);
    setItemSlot(EquipmentSlot::MAINHAND, item);
}
