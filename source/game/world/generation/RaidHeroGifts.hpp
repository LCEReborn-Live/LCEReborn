#pragma once
#include "game/entity/ai/Goal.hpp"
#include <boost/weak_ptr.hpp>
class Villager;
class Player;
class LootTable;
class BlockPos;
class ServerLevel;
LootTable* raidHeroGiftTable(int profession, unsigned int career);
void announceRaidVictory(ServerLevel& level, const BlockPos& center);
void tickVillagerRaidCelebration(Villager& villager);
class GiveRaidHeroGiftGoal : public Goal
{
public:
    explicit GiveRaidHeroGiftGoal(Villager& villager);
    std::wstring getName() override { return L"GiveGiftToHero"; }
    bool canUse() override;
    bool canContinueToUse() override;
    void start() override;
    void tick() override;
    void stop() override;
private:
    Villager& mVillager;
    boost::weak_ptr<Player> mHero;
    int mCooldown = 600;
    int mLastCheck = 0;
    int mStarted = 0;
    bool mGiftGiven = false;
};
