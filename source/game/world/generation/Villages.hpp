#ifndef GAME_WORLD_GENERATION_VILLAGES_HPP
#define GAME_WORLD_GENERATION_VILLAGES_HPP

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "game/core/DoorInfo.hpp"
#include "game/data/SavedData.hpp"
#include "game/world/block/BlockPos.hpp"

class CompoundTag;
class Block;
class Dimension;
class Direction;
class Level;
class Village;

class Villages : public SavedData
{
public:
    struct Job
    {
        int profession;
        int career;
        bool operator==(const Job& other) const { return profession == other.profession && career == other.career; }
    };

    struct JobSite
    {
        BlockPos pos;
        Job job;
    };

    Villages(const std::wstring& id);
    Villages(Level* level);

    void setLevel(Level* level);
    bool hasQuery(const BlockPos& pos);
    void queryUpdateAround(const BlockPos& pos);
    void removeVillages();
    bool isDoor(const BlockPos& pos);
    boost::shared_ptr<DoorInfo> getDoorInfo(const BlockPos& pos);
    int skyVisibleBlockCount(const BlockPos& pos, const Direction* direction, int count);
    void createDoorInfo(const BlockPos& pos);
    void addDoorInfos(const BlockPos& pos);
    void processNextQuery();
    boost::shared_ptr<Village> getClosestVillage(const BlockPos& pos, int distance);
    void cluster();
    void tick();
    std::vector<boost::shared_ptr<Village> >* getVillages();
    std::vector<BlockPos> getOccupiedPointsOfInterest() const;
    const BlockPos* getHome(const std::wstring& resident) const;
    void findOrClaimHome(const std::wstring& resident, const BlockPos& origin);
    void releaseHome(const std::wstring& resident);
    void removeHomeAt(const BlockPos& bed);
    bool findFreeBed(const BlockPos& origin, BlockPos& nearest, Village* village = nullptr);
    void claimHome(const std::wstring& resident, const BlockPos& bed);
    static Job jobForBlock(const Block* block);
    const JobSite* getJobSite(const std::wstring& resident) const;
    const JobSite* findOrClaimJobSite(const std::wstring& resident, const BlockPos& origin, const Job* required);
    void releaseJobSite(const std::wstring& resident);
    void registerMeetingBell(const BlockPos& pos);
    const BlockPos* getMeetingBell(const BlockPos& origin);

    virtual void load(CompoundTag* tag);
    virtual CompoundTag* save(CompoundTag* tag);
    static std::wstring getFileId(Dimension* dimension);

private:
    void tickWanderingTrader();
    void validatePointsOfInterest();
    void refreshBoundaries();
    Level* mLevel;
    std::deque<BlockPos> mToUpdate;
    std::vector<boost::shared_ptr<DoorInfo> > mDoorInfos;
    std::vector<boost::shared_ptr<Village> > mVillages;
    std::unordered_map<std::wstring, BlockPos> mHomes;
    std::unordered_map<std::wstring, JobSite> mJobSites;
    std::unordered_map<long long, std::wstring> mJobSiteOwners;
    std::vector<BlockPos> mMeetingBells;
    bool mBoundariesDirty = true;
    long long mNextTraderAttempt = 0;
    long long mTraderVisitUntil = 0;
    int mTick;
};

#endif
