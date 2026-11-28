#include "game/entity/ai/MakeLoveGoal.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/mob/EntityEvent.hpp"
#include "game/core/EntityType.hpp"

#include <typeinfo>
#include <memory>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "game/entity/ai/LookControl.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/block/BlockPos.hpp"

MakeLoveGoal::MakeLoveGoal(Villager* villager)
{
    mVillage = boost::weak_ptr<Village>();
    mPartner = boost::weak_ptr<Villager>();
    mRunningTicks = 0;
    mVillager = villager;
    mLevel = villager->mLevel;
    setRequiredControlFlags(3);
}

bool MakeLoveGoal::villageNeedsMoreVillagers()
{
    boost::shared_ptr<Village> village = mVillage.lock();
    if (village == NULL)
        return false;

    if (village->usesPointsOfInterest())
    {
        BlockPos bed;
        return mLevel->getVillages()->findFreeBed(BlockPos(mVillager), bed, village.get());
    }

    if (!village->isBreedTimerOk())
        return false;

    int maxPopulation = (int)((float)village->getDoorCount() * 0.35);
    return village->getPopulationSize() < maxPopulation;
}

bool MakeLoveGoal::canUse()
{
    if (mVillager->getAge() != 0 || mVillager->isSleeping() || mVillager->isTakingShelter())
        return false;

    if (mVillager->getRandom()->nextInt(500) != 0)
        return false;

    mVillage = mLevel->getVillages()->getClosestVillage(BlockPos(mVillager->shared_from_this()), 0);
    const auto village = mVillage.lock();
    if (!village)
        return false;

    if (village->usesPointsOfInterest())
    {
        if (!mVillager->hasBreedingFood() || !villageNeedsMoreVillagers())
            return false;
        const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> villagers(
            mLevel->getEntitiesOfClass(typeid(Villager), mVillager->mBoundingBox->grow(8.0, 3.0, 8.0)));
        double best = 65.0;
        for (const auto& entity : *villagers)
        {
            auto candidate = boost::static_pointer_cast<Villager>(entity);
            if (candidate.get() == mVillager || !candidate->isAlive() || candidate->isSleeping()
                || candidate->isTakingShelter() || candidate->getAge() != 0 || !candidate->hasBreedingFood()
                || mLevel->getVillages()->getClosestVillage(BlockPos(candidate), 0) != village)
                continue;
            const auto distance = mVillager->distanceToSqr(candidate);
            if (distance < best)
            {
                best = distance;
                mPartner = candidate;
            }
        }
        return !mPartner.expired();
    }

    if (!villageNeedsMoreVillagers() || !mVillager->isWillingToBreed(true))
        return false;

    Villager* villager = mVillager;
    boost::shared_ptr<Entity> entity = mLevel->getClosestEntityOfClass(
        typeid(Villager), villager->mBoundingBox->grow(8.0, 3.0, 8.0), villager->shared_from_this());
    if (!entity)
        return false;

    mPartner = boost::weak_ptr<Villager>(boost::dynamic_pointer_cast<Villager>(entity));
    if (mPartner.lock()->getAge() != 0 || !mPartner.lock()->isWillingToBreed(true))
        return false;

    return true;
}

void MakeLoveGoal::start()
{
    mRunningTicks = 300;
    mVillager->setInLove(true);
}

void MakeLoveGoal::stop()
{
    mVillage = boost::weak_ptr<Village>();
    mPartner = boost::weak_ptr<Villager>();
    mVillager->setInLove(false);
}

bool MakeLoveGoal::canContinueToUse()
{
    const auto partner = mPartner.lock();
    const auto village = mVillage.lock();
    if (!partner || !village || mRunningTicks < 0 || mVillager->getAge() != 0
        || mVillager->isSleeping() || mVillager->isTakingShelter())
        return false;
    if (village->usesPointsOfInterest())
        return partner->isAlive() && partner->getAge() == 0 && !partner->isSleeping() && !partner->isTakingShelter()
            && partner->hasBreedingFood() && mVillager->hasBreedingFood()
            && village->isInside(BlockPos(partner)) && village->isInside(BlockPos(mVillager));
    return villageNeedsMoreVillagers() && mVillager->isWillingToBreed(false);
}

void MakeLoveGoal::breed()
{
    const auto partner = mPartner.lock();
    const auto village = mVillage.lock();
    if (!partner || !village || !mLevel->canCreateMore(EntityType::VILLAGER, Level::eSpawnType_Breed))
        return;
    const auto villages = mLevel->getVillages();
    const bool modern = village->usesPointsOfInterest();
    BlockPos bed;
    if (modern && (!partner->hasBreedingFood() || !mVillager->hasBreedingFood()
        || !villages->findFreeBed(BlockPos(mVillager), bed, village.get())))
        return;
    auto child = boost::static_pointer_cast<Villager>(mVillager->getBreedOffspring(partner));
    child->setAge(-24000);
    child->moveTo(mVillager->mX, mVillager->mY, mVillager->mZ, 0.0f, 0.0f);
    if (!mLevel->addEntity(child))
        return;
    if (modern)
    {
        villages->claimHome(child->getUUID(), bed);
        partner->consumeBreedingFood();
        mVillager->consumeBreedingFood();
    }
    partner->setAge(6000);
    mVillager->setAge(6000);
    partner->setWillingToBreed(false);
    mVillager->setWillingToBreed(false);
    mLevel->broadcastEntityEvent(child, EntityEvent::LOVE_HEARTS, 0);
}

void MakeLoveGoal::tick()
{
    --mRunningTicks;

    const auto partner = mPartner.lock();
    if (!partner)
        return;
    const auto village = mVillage.lock();
    mVillager->getLookControl()->setLookAt(partner, 10.0F, 30.0F);
    if (mVillager->distanceToSqr(partner) > 2.25)
    {
        mVillager->getNavigation()->moveTo(partner, 0.25);
    }
    else if (mRunningTicks == 0 && (partner->isInLove() || (village && village->usesPointsOfInterest())))
    {
        breed();
    }

    if (mVillager->getRandom()->nextInt(35) == 0)
        mLevel->broadcastEntityEvent(mVillager->shared_from_this(), EntityEvent::LOVE_HEARTS, 0);
}

void MakeLoveGoal::setLevel(Level* level)
{
    mLevel = level;
}
