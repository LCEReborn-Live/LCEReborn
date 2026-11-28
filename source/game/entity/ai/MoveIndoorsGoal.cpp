#include "game/entity/ai/MoveIndoorsGoal.hpp"

#include <boost/shared_ptr.hpp>

#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/entity/mob/LivingEntity.hpp"
#include "game/entity/mob/PathfinderMob.hpp"
#include "game/util/Random.hpp"
#include "game/util/RandomPos.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/world/generation/Villager.hpp"

MoveIndoorsGoal::MoveIndoorsGoal(PathfinderMob* mob) : Goal(), mDoorInfo()
{
    mDoorX = mDoorZ = -1;
    mMob = mob;
    setRequiredControlFlags(1);
}

bool MoveIndoorsGoal::canUse()
{
    BlockPos pos(mMob->shared_from_this());

    const auto* villager = dynamic_cast<Villager*>(mMob);
    const bool alarm = villager && villager->isTakingShelter();
    if (!alarm && mMob->mLevel->isDay())
    {
        if (!mMob->mLevel->isRaining())
            return false;

        if (mMob->mLevel->getBiome(pos)->hasRain())
            return false;
    }

    if (mMob->mLevel->mDimension->isHasSkyLight() && (alarm || mMob->getRandom()->nextInt(50) == 0))
    {
        if (mDoorX != -1 && mMob->distanceToSqr((double)mDoorX, mMob->mY, (double)mDoorZ) < 4.0)
            return false;

        boost::shared_ptr<Village> village = mMob->mLevel->getVillages()->getClosestVillage(pos, 14);
        if (village)
        {
            boost::shared_ptr<DoorInfo> doorInfo = village->getBestDoorInfo(pos);
            mDoorInfo = doorInfo;
            return doorInfo.get() != NULL;
        }
    }

    return false;
}

bool MoveIndoorsGoal::canContinueToUse()
{
    return !mMob->getNavigation()->isDone();
}

void MoveIndoorsGoal::start()
{
    mDoorX = -1;

    boost::shared_ptr<DoorInfo> doorInfo = mDoorInfo.lock();
    if (!doorInfo)
    {
        mDoorInfo = boost::weak_ptr<DoorInfo>();
        return;
    }

    BlockPos pos = doorInfo->getIndoorPos();
    int x = pos.getX();
    int y = pos.getY();
    int z = pos.getZ();
    const auto* villager = dynamic_cast<Villager*>(mMob);
    const double speed = villager && villager->isTakingShelter() ? 1.45 : 1.0;
    if (mMob->distanceToSqr(pos) > 256.0)
    {
        Vec3* destination = RandomPos::getPosTowards(
            boost::dynamic_pointer_cast<PathfinderMob>(mMob->shared_from_this()), 14, 3,
            Vec3::newTemp((double)x + 0.5, (double)y, (double)z + 0.5));
        if (destination != NULL)
        {
            mMob->getNavigation()->moveTo(destination->x, destination->y, destination->z, speed);
        }
    }
    else
    {
        mMob->getNavigation()->moveTo((double)x + 0.5, (double)y, (double)z + 0.5, speed);
    }
}

void MoveIndoorsGoal::stop()
{
    boost::shared_ptr<DoorInfo> doorInfo = mDoorInfo.lock();
    if (!doorInfo)
    {
        mDoorInfo = boost::weak_ptr<DoorInfo>();
        return;
    }

    mDoorX = doorInfo->getIndoorPos().x;
    mDoorZ = doorInfo->getIndoorPos().z;
    mDoorInfo = boost::weak_ptr<DoorInfo>();
}
