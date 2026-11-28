#pragma once
#include <map>
#include <memory>
#include <set>
#include <boost/weak_ptr.hpp>
#include "game/data/SavedData.hpp"
#include "game/server/ServerBossEvent.hpp"
#include "game/util/Random.hpp"
#include "game/world/block/BlockPos.hpp"

class Player;
class Raider;
class ServerLevel;
class Village;
class SoundEvent;
class Raids;

class Raid
{
public:
    enum Status { Ongoing, Victory, Loss, Stopped };
    struct Member
    {
        boost::weak_ptr<Raider> entity;
        BlockPos pos;
        int wave = 0;
        int missingTicks = 0;
        bool captain = false;
    };
    Raid(Raids& owner, int id, const BlockPos& center, int groups, long long seed);
    void tick();
    void stop();
    void join(const boost::shared_ptr<Raider>& mob, int wave, bool countArrival = true);
    void remove(Raider& mob);
    void addHero(const std::wstring& uuid);
    void absorbBadOmen(const boost::shared_ptr<Player>& player);
    void updateBossbar();
    void updatePlayers();
    void save(CompoundTag* tag);
    void load(CompoundTag* tag);
    bool findSpawnPos(int stage, int attempts, BlockPos& result);
    bool spawnWave(const BlockPos& pos);
    static int groupsForDifficulty(int difficulty);
    static int baseCount(int type, int wave);
    static int bonusCount(int type, int wave, int difficulty, bool bonusWave, Random& random);
    Raider* getLeader(int wave);
    float getEnchantOdds() const;
    ServerLevel& level() const;
    int getId() const { return mId; }
    int getGroupsSpawned() const { return mGroupsSpawned; }
    int getBadOmenLevel() const { return mBadOmenLevel; }
    int getRemaining() const { return static_cast<int>(mMembers.size()); }
    int getCooldown() const { return mCooldown; }
    bool isActive() const { return mActive && mStatus != Stopped; }
    bool isOver() const { return mStatus == Victory || mStatus == Loss; }
    bool isLoss() const { return mStatus == Loss; }
    Status getStatus() const { return mStatus; }
    const BlockPos& getCenter() const { return mCenter; }
    const std::map<std::wstring, Member>& members() const { return mMembers; }
    ServerBossEvent& bossbar() { return mBossbar; }

private:
    void updateMembers();
    bool hasMoreWaves() const;
    void playHorn(const BlockPos& pos);
    void tickWaveHorn();
    void expire();
    Raids& mOwner;
    int mId;
    BlockPos mCenter;
    Status mStatus = Ongoing;
    bool mActive = true;
    bool mStarted = false;
    int mNumGroups;
    int mGroupsSpawned = 0;
    int mBadOmenLevel = 0;
    int mCooldown = 300;
    int mPostRaidTicks = 0;
    int mCelebrationTicks = 0;
    long long mTicksActive = 0;
    int mWaveTotal = 0;
    long long mNextHornTick = 0;
    Random mRandom;
    bool mHasSpawnPos = false;
    BlockPos mSpawnPos;
    std::map<std::wstring, Member> mMembers;
    std::set<std::wstring> mHeroes;
    ServerBossEvent mBossbar;
};

class Raids : public SavedData
{
public:
    explicit Raids(const std::wstring& id = L"raids");
    ~Raids() override;
    void setLevel(ServerLevel* level) { mLevel = level; }
    ServerLevel& level() const { return *mLevel; }
    Raid* get(int id) const;
    Raid* getNearby(const BlockPos& pos, double distanceSqr = 9216.0) const;
    Raid* createOrExtend(const boost::shared_ptr<Player>& player);
    bool isVillage(const BlockPos& pos) const;
    bool findVillageCenter(const BlockPos& pos, int range, BlockPos& center) const;
    void tick();
    void load(CompoundTag* tag) override;
    bool isDisabled() const { return mDisabled; }
    void setDisabled(bool disabled) { mDisabled = disabled; setDirty(); }
    CompoundTag* save(CompoundTag* tag) override;
    static SoundEvent* horn;
private:
    ServerLevel* mLevel = nullptr;
    int mNextId = 1;
    int mTick = 0;
    bool mDisabled = false;
    std::map<int, std::unique_ptr<Raid>> mRaids;
};
