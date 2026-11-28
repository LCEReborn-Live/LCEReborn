#include "game/world/generation/Villages.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_set>

#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/core/GameRules.hpp"
#include "game/util/NbtUtils.hpp"
#include "game/util/Random.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/DimensionType.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/DoorBlock.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/WanderingTrader.hpp"

static const std::wstring VILLAGES_FILE_ID = L"villages";

namespace
{
bool isBedHead(const BlockState* state)
{
    return state->getBlock() == Blocks::bed && state->getValue(BedBlock::PART) == &gBedHead;
}

bool isClaimedBedValid(Level* level, const BlockPos& pos)
{
    if (!level->hasChunkAt(pos, false))
        return true;
    const auto* head = level->getBlockState(pos);
    if (!isBedHead(head))
        return false;
    const auto* facing = head->getValue(BedBlock::FACING);
    const auto footPos = pos.relative(facing->getOpposite());
    if (!level->hasChunkAt(footPos, false))
        return true;
    const auto* foot = level->getBlockState(footPos);
    return foot->getBlock() == Blocks::bed && foot->getValue(BedBlock::PART) == &gBedFoot
        && foot->getValue(BedBlock::FACING) == facing;
}
}

Villages::Villages(const std::wstring& id) : SavedData(id)
{
    mTick = 0;
    mLevel = NULL;
}

Villages::Villages(Level* level) : SavedData(VILLAGES_FILE_ID)
{
    mTick = 0;
    mLevel = level;
}

void Villages::setLevel(Level* level)
{
    mLevel = level;
    mBoundariesDirty = true;
    for (std::vector<boost::shared_ptr<Village> >::iterator it = mVillages.begin(); it != mVillages.end(); ++it)
    {
        boost::shared_ptr<Village> village = *it;
        village->setLevel(level);
    }
}

bool Villages::hasQuery(const BlockPos& pos)
{
    std::deque<BlockPos>::const_iterator it;
    for (it = mToUpdate.begin(); it != mToUpdate.end(); ++it)
        if (it->equals(pos))
            return true;
    return false;
}

void Villages::queryUpdateAround(const BlockPos& pos)
{
    if (mToUpdate.size() <= 64 && !hasQuery(pos))
        mToUpdate.push_back(pos);
}

const BlockPos* Villages::getHome(const std::wstring& resident) const
{
    const auto existing = mHomes.find(resident);
    return existing == mHomes.end() ? nullptr : &existing->second;
}

void Villages::findOrClaimHome(const std::wstring& resident, const BlockPos& origin)
{
    if (resident.empty())
        return;

    if (const auto home = getHome(resident))
    {
        if (origin.distSqr(*home) <= 32 * 32 && isClaimedBedValid(mLevel, *home))
            return;
        releaseHome(resident);
    }

    BlockPos nearest;
    if (findFreeBed(origin, nearest))
        claimHome(resident, nearest);
}

bool Villages::findFreeBed(const BlockPos& origin, BlockPos& nearest, Village* village)
{
    std::unordered_set<long long> claimed;
    for (const auto& entry : mHomes)
        claimed.insert(entry.second.compressBlockPos());

    const int radius = village ? Village::POINT_RANGE : 12;
    double nearestDistance = radius * radius + 1;
    for (int x = -radius; x <= radius; ++x)
    {
        for (int z = -radius; z <= radius; ++z)
        {
            if (!mLevel->hasChunkAt(origin.offset(x, 0, z)))
                continue;
            for (int y = -3; y <= 3; ++y)
            {
                const BlockPos pos = origin.offset(x, y, z);
                const double distance = origin.distSqr(pos);
                if (distance >= nearestDistance)
                    continue;
                if (village && !village->isInside(pos))
                    continue;

                const auto* head = mLevel->getBlockState(pos);
                if (!isBedHead(head) || claimed.count(pos.compressBlockPos()) != 0)
                    continue;

                const auto* facing = head->getValue(BedBlock::FACING);
                const BlockPos footPos = pos.relative(facing->getOpposite());
                if (!mLevel->hasChunkAt(footPos))
                    continue;
                const auto* foot = mLevel->getBlockState(footPos);
                if (foot->getBlock() == Blocks::bed && foot->getValue(BedBlock::PART) == &gBedFoot
                    && foot->getValue(BedBlock::FACING) == facing)
                {
                    if (village && (head->getValue(BedBlock::OCCUPIED)
                        || !mLevel->isEmptyBlock(pos.above()) || !mLevel->isEmptyBlock(pos.above(2))
                        || !mLevel->isEmptyBlock(footPos.above()) || !mLevel->isEmptyBlock(footPos.above(2))))
                        continue;
                    nearest = pos;
                    nearestDistance = distance;
                }
            }
        }
    }

    return nearestDistance <= radius * radius;
}

void Villages::claimHome(const std::wstring& resident, const BlockPos& bed)
{
    if (!resident.empty()
        && std::none_of(mHomes.begin(), mHomes.end(), [&](const auto& entry) { return entry.second == bed; })
        && mHomes.emplace(resident, bed).second)
    {
        mBoundariesDirty = true;
        setDirty();
    }
}

void Villages::releaseHome(const std::wstring& resident)
{
    if (mHomes.erase(resident) != 0)
    {
        mBoundariesDirty = true;
        setDirty();
    }
}

void Villages::removeHomeAt(const BlockPos& bed)
{
    const auto found = std::find_if(mHomes.begin(), mHomes.end(), [&](const auto& entry) { return entry.second == bed; });
    if (found != mHomes.end())
    {
        mHomes.erase(found);
        mBoundariesDirty = true;
        setDirty();
    }
}

Villages::Job Villages::jobForBlock(const Block* block)
{
    if (block == Blocks::composter) return {0, 1};
    if (block == Blocks::barrel) return {0, 2};
    if (block == Blocks::loom) return {0, 3};
    if (block == Blocks::fletching_table) return {0, 4};
    if (block == Blocks::lectern) return {1, 1};
    if (block == Blocks::cartography_table) return {1, 2};
    if (block == Blocks::brewing_stand) return {2, 1};
    if (block == Blocks::blast_furnace || block == Blocks::lit_blast_furnace) return {3, 1};
    if (block == Blocks::grindstone) return {3, 2};
    if (block == Blocks::smithing_table) return {3, 3};
    if (block == Blocks::smoker || block == Blocks::lit_smoker) return {4, 1};
    if (block == Blocks::cauldron) return {4, 2};
    if (block == Blocks::stonecutter) return {Villager::MASON, 1};
    return {Villager::UNEMPLOYED, 0};
}

const Villages::JobSite* Villages::getJobSite(const std::wstring& resident) const
{
    const auto entry = mJobSites.find(resident);
    if (entry == mJobSites.end())
        return nullptr;
    const auto& site = entry->second;
    if (mLevel && mLevel->hasChunkAt(site.pos, false) && !(jobForBlock(mLevel->getBlock(site.pos)) == site.job))
        return nullptr;
    return &site;
}

const Villages::JobSite* Villages::findOrClaimJobSite(const std::wstring& resident, const BlockPos& origin,
                                                    const Job* required)
{
    if (resident.empty())
        return nullptr;
    if (const auto* existing = getJobSite(resident))
        if (!required || existing->job == *required)
            return existing;
    releaseJobSite(resident);

    constexpr int radius = 16;
    double nearestDistance = radius * radius + 1;
    JobSite nearest{};
    for (int x = -radius; x <= radius; ++x)
    {
        for (int z = -radius; z <= radius; ++z)
        {
            if (!mLevel->hasChunkAt(origin.offset(x, 0, z)))
                continue;
            for (int y = -4; y <= 4; ++y)
            {
                const auto pos = origin.offset(x, y, z);
                const auto distance = origin.distSqr(pos);
                if (distance >= nearestDistance || pos.y < 0 || pos.y >= mLevel->getHeight())
                    continue;
                const auto job = jobForBlock(mLevel->getBlock(pos));
                if (!job.career || (required && !(job == *required)))
                    continue;
                const auto owner = mJobSiteOwners.find(pos.compressBlockPos());
                if (owner != mJobSiteOwners.end())
                {
                    if (getJobSite(owner->second))
                        continue;
                    const auto former = owner->second;
                    releaseJobSite(former);
                }
                nearest = {pos, job};
                nearestDistance = distance;
            }
        }
    }
    if (nearestDistance > radius * radius)
        return nullptr;
    mJobSiteOwners.emplace(nearest.pos.compressBlockPos(), resident);
    const auto inserted = mJobSites.emplace(resident, nearest);
    mBoundariesDirty = true;
    setDirty();
    return &inserted.first->second;
}

void Villages::releaseJobSite(const std::wstring& resident)
{
    const auto entry = mJobSites.find(resident);
    if (entry != mJobSites.end())
    {
        mJobSiteOwners.erase(entry->second.pos.compressBlockPos());
        mJobSites.erase(entry);
        mBoundariesDirty = true;
        setDirty();
    }
}

void Villages::validatePointsOfInterest()
{
    mMeetingBells.erase(std::remove_if(mMeetingBells.begin(), mMeetingBells.end(), [&](const BlockPos& pos) {
        return mLevel->hasChunkAt(pos, false) && mLevel->getBlock(pos) != Blocks::bell;
    }), mMeetingBells.end());
    for (auto it = mHomes.begin(); it != mHomes.end();)
    {
        if (isClaimedBedValid(mLevel, it->second))
        {
            ++it;
            continue;
        }
        it = mHomes.erase(it);
        mBoundariesDirty = true;
        setDirty();
    }
    for (auto it = mJobSites.begin(); it != mJobSites.end();)
    {
        const auto& entry = *it++;
        if (!getJobSite(entry.first))
            releaseJobSite(entry.first);
    }
}

void Villages::registerMeetingBell(const BlockPos& pos)
{
    if (std::find(mMeetingBells.begin(), mMeetingBells.end(), pos) == mMeetingBells.end())
        mMeetingBells.push_back(pos);
}

const BlockPos* Villages::getMeetingBell(const BlockPos& origin)
{
    const auto village = getClosestVillage(origin, 0);
    if (!village)
        return nullptr;
    const BlockPos* nearest = nullptr;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& bell : mMeetingBells)
    {
        const auto distance = origin.distSqr(bell);
        if (distance < best && village->isInside(bell)
            && mLevel->hasChunkAt(bell, false) && mLevel->getBlock(bell) == Blocks::bell)
        {
            best = distance;
            nearest = &bell;
        }
    }
    return nearest;
}

void Villages::refreshBoundaries()
{
    if (!mLevel || !mBoundariesDirty)
        return;
    validatePointsOfInterest();

    // Beds form the settlements. Only a bed's resident can add their workplace to its boundary.
    using Resident = std::pair<std::wstring, BlockPos>;
    std::vector<Resident> residents(mHomes.begin(), mHomes.end());
    std::sort(residents.begin(), residents.end(), [](const Resident& a, const Resident& b) {
        return std::tie(a.second.x, a.second.z, a.second.y, a.first)
             < std::tie(b.second.x, b.second.z, b.second.y, b.first);
    });
    constexpr int range = Village::POINT_RANGE;
    std::vector<size_t> parents(residents.size());
    std::iota(parents.begin(), parents.end(), size_t{0});
    const auto root = [&](size_t index) {
        while (parents[index] != index)
        {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }
        return index;
    };
    for (size_t i = 0; i < residents.size(); ++i)
        for (size_t j = i + 1; j < residents.size() && residents[j].second.x - residents[i].second.x <= range; ++j)
            if (residents[i].second.distSqr(residents[j].second) <= range * range)
                parents[root(j)] = root(i);

    std::map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < residents.size(); ++i)
        groups[root(i)].push_back(i);

    // Give each old village to the group retaining the most of its beds.
    std::unordered_map<long long, size_t> groupByBed;
    for (const auto& group : groups)
        for (auto index : group.second)
            groupByBed.emplace(residents[index].second.compressBlockPos(), group.first);
    std::map<size_t, boost::shared_ptr<Village>> previous;
    std::map<size_t, size_t> previousOverlap;
    for (const auto& village : mVillages)
    {
        std::map<size_t, size_t> overlaps;
        for (const auto& bed : village->getBeds())
        {
            const auto entry = groupByBed.find(bed.compressBlockPos());
            if (entry != groupByBed.end())
                ++overlaps[entry->second];
        }
        size_t bestGroup = 0, bestOverlap = 0;
        for (const auto& overlap : overlaps)
            if (overlap.second > bestOverlap)
            {
                bestGroup = overlap.first;
                bestOverlap = overlap.second;
            }
        if (bestOverlap && bestOverlap > previousOverlap[bestGroup])
        {
            previous[bestGroup] = village;
            previousOverlap[bestGroup] = bestOverlap;
        }
    }

    std::vector<boost::shared_ptr<Village>> updated;
    std::unordered_set<Village*> reused;
    for (const auto& group : groups)
    {
        std::vector<BlockPos> beds;
        std::vector<BlockPos> workSites;
        for (auto index : group.second)
        {
            const auto& resident = residents[index];
            beds.push_back(resident.second);
            if (const auto* site = getJobSite(resident.first))
                if (site->pos.distSqr(resident.second) <= range * range)
                    workSites.push_back(site->pos);
        }
        auto village = previous[group.first];
        if (!village)
        {
            for (const auto& legacy : mVillages)
                if (!legacy->usesPointsOfInterest() && !reused.count(legacy.get())
                    && std::any_of(beds.begin(), beds.end(), [&](const BlockPos& bed) { return legacy->isInside(bed); }))
                {
                    village = legacy;
                    break;
                }
        }
        if (!village)
            village.reset(new Village(mLevel));
        reused.insert(village.get());
        village->setPointsOfInterest(std::move(beds), std::move(workSites));
        updated.push_back(village);
    }
    for (const auto& legacy : mVillages)
        if (!legacy->usesPointsOfInterest() && !reused.count(legacy.get()))
            updated.push_back(legacy);
    mVillages.swap(updated);
    mBoundariesDirty = false;
}

void Villages::removeVillages()
{
    std::vector<boost::shared_ptr<Village> >::iterator it = mVillages.begin();
    while (it != mVillages.end())
    {
        boost::shared_ptr<Village> village = *it;
        if (village->canRemove())
        {
            it = mVillages.erase(it);
            setDirty();
        }
        else
            ++it;
    }
}

bool Villages::isDoor(const BlockPos& pos)
{
    const BlockState* state = mLevel->getBlockState(pos);
    if (DoorBlock::isDoor(state->getBlock()))
        return state->getMaterial() == Material::WOOD;
    return false;
}

boost::shared_ptr<DoorInfo> Villages::getDoorInfo(const BlockPos& pos)
{
    for (std::vector<boost::shared_ptr<DoorInfo> >::iterator it = mDoorInfos.begin(); it != mDoorInfos.end(); ++it)
    {
        boost::shared_ptr<DoorInfo> doorInfo = *it;
        if (doorInfo->getPos().x == pos.x && doorInfo->getPos().z == pos.z
            && std::abs(doorInfo->getPos().y - pos.y) <= 1)
            return boost::shared_ptr<DoorInfo>(doorInfo);
    }

    for (std::vector<boost::shared_ptr<Village> >::iterator it = mVillages.begin(); it != mVillages.end(); ++it)
    {
        boost::shared_ptr<Village> village = *it;
        boost::shared_ptr<DoorInfo> doorInfo = village->getDoorInfo(pos);
        if (doorInfo)
            return boost::shared_ptr<DoorInfo>(doorInfo);
    }

    return boost::shared_ptr<DoorInfo>();
}

int Villages::skyVisibleBlockCount(const BlockPos& pos, const Direction* direction, int count)
{
    int visible = 0;
    for (int i = 1; i <= 4; ++i)
    {
        if (mLevel->canSeeSky(pos.relative(direction, i)) && ++visible >= count)
            return visible;
    }
    return visible;
}

void Villages::createDoorInfo(const BlockPos& pos)
{
    const Direction* facing = DoorBlock::getFacing(mLevel, pos);
    const Direction* opposite = facing->getOpposite();
    int front = skyVisibleBlockCount(pos, facing, 5);
    int back = skyVisibleBlockCount(pos, opposite, front + 1);
    if (front != back)
        mDoorInfos.push_back(boost::shared_ptr<DoorInfo>(new DoorInfo(pos, front < back ? facing : opposite, mTick)));
}

void Villages::addDoorInfos(const BlockPos& pos)
{
    int horizontalRange = 16;
    int verticalRange = 4;
    int depthRange = 16;
    for (int x = -horizontalRange; x < horizontalRange; ++x)
        for (int y = -verticalRange; y < verticalRange; ++y)
            for (int z = -depthRange; z < depthRange; ++z)
            {
                BlockPos doorPos = pos.offset(x, y, z);
                if (!mLevel->hasChunkAt(doorPos, false))
                    continue;
                if (mLevel->getBlock(doorPos) == Blocks::bell)
                    registerMeetingBell(doorPos);
                if (isDoor(doorPos))
                {
                    boost::shared_ptr<DoorInfo> info = getDoorInfo(doorPos);
                    if (info == NULL)
                        createDoorInfo(doorPos);
                    else
                        info->setTimeStamp(mTick);
                }
            }
}

void Villages::processNextQuery()
{
    if (!mToUpdate.empty())
    {
        BlockPos pos = mToUpdate.front();
        mToUpdate.pop_front();
        addDoorInfos(pos);
    }
}

boost::shared_ptr<Village> Villages::getClosestVillage(const BlockPos& pos, int distance)
{
    refreshBoundaries();
    boost::shared_ptr<Village> closest;
    double best = std::numeric_limits<double>::infinity();
    for (std::vector<boost::shared_ptr<Village> >::iterator it = mVillages.begin(); it != mVillages.end(); ++it)
    {
        boost::shared_ptr<Village> village = *it;
        const auto d = village->distanceToBoundaryPointsSqr(pos);
        if (d >= best)
            continue;
        const double radius = distance + village->getBoundaryRange();
        if (d > radius * radius)
            continue;
        closest = village;
        best = d;
    }
    return closest;
}

void Villages::cluster()
{
    for (std::vector<boost::shared_ptr<DoorInfo> >::iterator it = mDoorInfos.begin(); it != mDoorInfos.end(); ++it)
    {
        boost::shared_ptr<DoorInfo> doorInfo = *it;
        boost::shared_ptr<Village> village = getClosestVillage(doorInfo->getPos(), 32);
        if (village)
            village->addDoorInfo(doorInfo);
    }
    mDoorInfos.clear();
}

void Villages::tick()
{
    ++mTick;
    if (mTick % 20 == 0)
        validatePointsOfInterest();
    refreshBoundaries();
    for (std::vector<boost::shared_ptr<Village> >::iterator it = mVillages.begin(); it != mVillages.end(); ++it)
    {
        boost::shared_ptr<Village> village = *it;
        village->tick(mTick);
    }
    removeVillages();
    processNextQuery();
    cluster();
    tickWanderingTrader();
    if (mTick % 400 == 0)
        setDirty();
}

void Villages::tickWanderingTrader()
{
    constexpr int ticksPerDay = 24000;
    const auto now = mLevel->getGameTime();
    if (mLevel->mIsClientSide || mLevel->mDimension->getType() != DimensionType::OVERWORLD
        || now < mTraderVisitUntil || now < mNextTraderAttempt
        || !mLevel->getGameRules()->getBoolean(GameRules::RULE_DOMOBSPAWNING))
        return;

    if (mNextTraderAttempt == 0)
    {
        mNextTraderAttempt = now + ticksPerDay;
        setDirty();
        return;
    }

    std::vector<BlockPos> origins;
    for (const auto& player : mLevel->mPlayers)
    {
        if (!player->isAlive() || player->isSpectator())
            continue;
        auto origin = BlockPos(player);
        if (const auto* bell = getMeetingBell(origin))
            origin = *bell;
        origins.push_back(origin);
    }
    if (origins.empty())
        return;

    const auto& origin = origins[mLevel->mRandom->nextInt(static_cast<int>(origins.size()))];
    const auto expiresAt = now + 2 * ticksPerDay + mLevel->mRandom->nextInt(ticksPerDay + 1);
    if (WanderingTrader::spawnVisit(*mLevel, origin, expiresAt))
    {
        mTraderVisitUntil = expiresAt;
        mNextTraderAttempt = expiresAt + ticksPerDay;
    }
    else
    {
        mNextTraderAttempt = now + 1200;
    }
    setDirty();
}

std::vector<boost::shared_ptr<Village> >* Villages::getVillages()
{
    refreshBoundaries();
    return &mVillages;
}

void Villages::load(CompoundTag* tag)
{
    mBoundariesDirty = true;
    mVillages.clear();
    mMeetingBells.clear();
    mTick = tag->getInt(L"Tick");
    mNextTraderAttempt = tag->getLong(L"NextTraderAttempt");
    mTraderVisitUntil = tag->getLong(L"TraderVisitUntil");
    ListTag* list = tag->getList(L"Villages", Tag::TAG_COMPOUND);
    int i;
    for (i = 0; i < list->size(); ++i)
    {
        CompoundTag* villageTag = list->getCompound(i);
        boost::shared_ptr<Village> village(new Village());
        village->readAdditionalSaveData(villageTag);
        village->setLevel(mLevel);
        mVillages.push_back(village);
    }

    mHomes.clear();
    if (tag->contains(L"Homes", Tag::TAG_LIST))
    {
        auto* homes = tag->getList(L"Homes", Tag::TAG_COMPOUND);
        for (int index = 0; index < homes->size(); ++index)
        {
            auto* home = homes->getCompound(index);
            auto resident = home->getString(L"Resident");
            if (!resident.empty() && home->contains(L"Bed", Tag::TAG_COMPOUND))
                mHomes.emplace(resident, NbtUtils::readBlockPos(home->getCompound(L"Bed")));
        }
    }
    mJobSites.clear();
    mJobSiteOwners.clear();
    if (tag->contains(L"JobSites", Tag::TAG_LIST))
    {
        auto* sites = tag->getList(L"JobSites", Tag::TAG_COMPOUND);
        for (int i = 0; i < sites->size(); ++i)
        {
            auto* entry = sites->getCompound(i);
            const auto resident = entry->getString(L"Resident");
            if (resident.empty() || !entry->contains(L"Pos", Tag::TAG_COMPOUND))
                continue;
            const auto pos = NbtUtils::readBlockPos(entry->getCompound(L"Pos"));
            const Job job{entry->getInt(L"Profession"), entry->getInt(L"Career")};
            if (job.career <= 0 || mJobSiteOwners.count(pos.compressBlockPos()))
                continue;
            if (mJobSites.emplace(resident, JobSite{pos, job}).second)
                mJobSiteOwners.emplace(pos.compressBlockPos(), resident);
        }
    }
}

CompoundTag* Villages::save(CompoundTag* tag)
{
    refreshBoundaries();
    tag->putInt(L"Tick", mTick);
    tag->putLong(L"NextTraderAttempt", mNextTraderAttempt);
    tag->putLong(L"TraderVisitUntil", mTraderVisitUntil);
    ListTag* list = new ListTag();
    std::vector<boost::shared_ptr<Village> >::iterator it;
    for (it = mVillages.begin(); it != mVillages.end(); ++it)
    {
        boost::shared_ptr<Village> village = *it;
        CompoundTag* villageTag = new CompoundTag();
        village->addAdditonalSaveData(villageTag);
        list->add(villageTag);
    }
    tag->put(L"Villages", list);
    if (!mHomes.empty())
    {
        auto* homes = new ListTag();
        for (const auto& entry : mHomes)
        {
            auto* home = new CompoundTag();
            home->putString(L"Resident", entry.first);
            home->put(L"Bed", NbtUtils::writeBlockPos(entry.second));
            homes->add(home);
        }
        tag->put(L"Homes", homes);
    }
    if (!mJobSites.empty())
    {
        auto* sites = new ListTag();
        for (const auto& entry : mJobSites)
        {
            auto* site = new CompoundTag();
            site->putString(L"Resident", entry.first);
            site->put(L"Pos", NbtUtils::writeBlockPos(entry.second.pos));
            site->putInt(L"Profession", entry.second.job.profession);
            site->putInt(L"Career", entry.second.job.career);
            sites->add(site);
        }
        tag->put(L"JobSites", sites);
    }
    return tag;
}

std::wstring Villages::getFileId(Dimension* dimension)
{
    return VILLAGES_FILE_ID + dimension->getType()->getFileSuffix();
}

std::vector<BlockPos> Villages::getOccupiedPointsOfInterest() const
{
    std::vector<BlockPos> points;
    for (const auto& home : mHomes) points.push_back(home.second);
    for (const auto& site : mJobSites) points.push_back(site.second.pos);
    return points;
}
