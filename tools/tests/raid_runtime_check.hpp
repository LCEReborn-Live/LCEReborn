#pragma once
#include <cstdio>
#include <cmath>
#include <memory>
#include "game/core/CMinecraftApp.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/GameType.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/Pillager.hpp"
#include "game/core/Ravager.hpp"
#include "game/core/Witch.hpp"
#include "game/core/Settings.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/core/LootTable.hpp"
#include "game/core/LootTableContext.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/data/SavedDataStorage.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffect.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/entity/mob/PatrolSpawner.hpp"
#include "game/entity/mob/Raider.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/item/Items.hpp"
#include "game/item/ThrownPotion.hpp"
#include "game/item/PotionUtils.hpp"
#include "game/item/Potions.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/BlockChangeListener.hpp"
#include "game/network/ClientboundUpdateMobEffectPacket.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/BellBlock.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/world/generation/Raids.hpp"
#include "game/world/generation/RaidHeroGifts.hpp"

#define RAID_CHECK(c,m) do { if (!(c)) { std::printf("FAIL: %s, line %d\n",m,__LINE__); return 1; } } while(0)
class RaidTestPlayer : public Player
{
public:
    RaidTestPlayer(Level* level) : Player(level, L"RaidTester") { fjDerivedCtorCalls(); }
    bool isSpectator() override { return spectator; }
    const GameType* GetGameType() override { return GameType::SURVIVAL; }
    void awardStat(Stat* stat, arrayWithLength<unsigned char> params) override
    {
        if (stat == GenericStats::achievement(eAward_BadFeeling)) ++captainAwards;
        if (stat == GenericStats::achievement(eAward_WereBeingAttacked)) ++raidAwards;
        if (stat == GenericStats::achievement(eAward_KillTheBeast)) ++ravagerAwards;
        delete[] params.data;
    }
    bool spectator = false;
    int captainAwards = 0;
    int raidAwards = 0;
    int ravagerAwards = 0;
};
class RaidSoundCheck : public BlockChangeListener
{
public:
    RaidSoundCheck() : BlockChangeListener(nullptr) {}
    void playSound(boost::shared_ptr<Player>, const SoundEvent* sound, SoundSource::ESoundSource,
                   double, double, double, float, float, float) override
    {
        if (sound == SoundEvents::VILLAGER_YES) ++cheers;
    }
    int cheers = 0;
};
inline int RunRaidRegression()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer(); gMinecraftServer = server;
    server->mSettings = new Settings(); server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));
    const long long seeds[] = {8531,98837,42017};
    for (int difficulty = 1; difficulty <= 3; ++difficulty)
    {
        auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
        LevelSettings settings(seeds[difficulty-1], GameType::SURVIVAL, false, false, false,
            difficulty == 1 ? LevelType::FLAT : LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0);
        auto* data = new LevelData(&settings, L"Raid regression"); data->setDifficulty(Difficulty::byId(difficulty));
        auto* level = new ServerLevel(server, storage, data, 0); server->mLevels[0] = level;
        level->postConstruct(); level->mListeners.clear();
        auto* chunks = static_cast<ServerChunkCache*>(level->getChunkSource());
        for (int x = -6; x <= 6; ++x) for (int z = -6; z <= 6; ++z) chunks->create(x,z);
        BlockPos center; RAID_CHECK(PatrolSpawner::getSurface(*level, BlockPos(15,0,15),center), "surface query");
        auto resident = boost::shared_ptr<Villager>(new Villager(level));
        resident->moveTo(center.x+.5,center.y,center.z+.5,0,0); resident->setNoAi(true); level->addEntity(resident);
        const BlockPos head=center.south();
        const auto* bed=Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
        level->setBlock(center,bed->setValue<const BedBlock::Part*>(BedBlock::PART,&gBedFoot),0,false);
        level->setBlock(head,bed->setValue<const BedBlock::Part*>(BedBlock::PART,&gBedHead),0,false);
        auto* manager=level->getRaids(); RAID_CHECK(manager, "overworld raid storage");
        RAID_CHECK(!manager->isVillage(center), "unclaimed bed cannot start raid");
        level->getVillages()->claimHome(resident->getUUID(),head);
        RAID_CHECK(manager->isVillage(center), "occupied POI village, including chunk boundary");
        auto player=boost::shared_ptr<RaidTestPlayer>(new RaidTestPlayer(level));
        player->moveTo(center.x+.5,center.y,center.z+.5,0,0); level->mPlayers.push_back(player);
        auto source=std::unique_ptr<EntityDamageSource>(DamageSource::CreatePlayerAttack(player));
        auto captain=boost::dynamic_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER,level));
        captain->moveTo(center.x+3,center.y,center.z,0,0); captain->setCaptain(true); level->addEntity(captain);
        captain->hurt(source.get(),1000);
        RAID_CHECK(player->hasEffect(MobEffects::BAD_OMEN), "patrol captain grants Bad Omen");
        RAID_CHECK(player->captainAwards == 1, "captain kill attempts the PS4 trophy exactly once");
        RAID_CHECK(player->getEffect(MobEffects::BAD_OMEN)->getDuration()==120000, "100 minute Bad Omen duration");
        ClientboundUpdateMobEffectPacket omenPacket(player->getId(), player->getEffect(MobEffects::BAD_OMEN));
        RAID_CHECK(omenPacket.getEffectDurationTicks() == 120000 && !omenPacket.isSuperLongDuration(),
                   "multiplayer client receives the timed Bad Omen duration");
        player->spectator=true; RAID_CHECK(!manager->createOrExtend(player), "spectator cannot trigger raid"); player->spectator=false;
        player->removeEffect(MobEffects::BAD_OMEN);
        for (int omen : {1,2,5})
        {
            player->removeEffect(MobEffects::HERO_OF_THE_VILLAGE);
            player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,omen-1,false,false),{});
            const int raidAwards = player->raidAwards;
            Raid* raid=manager->createOrExtend(player);
            RAID_CHECK(raid && !player->hasEffect(MobEffects::BAD_OMEN), "village consumes Bad Omen");
            RAID_CHECK(player->raidAwards == raidAwards + 1, "starting a raid attempts the PS4 trophy");
            const int id=raid->getId();
            RAID_CHECK(raid->getBadOmenLevel()==omen && raid->getCooldown()==300, "omen level and preparation time");
            RAID_CHECK(raid->bossbar().getColor()==BossEvent::PURPLE && raid->bossbar().getOverlay()==BossEvent::PROGRESS,
                       "purple unsegmented raid bar");
            RAID_CHECK(resident->isTakingShelter(), "villagers shelter during raid");
            for(int tick=0;tick<299;++tick) raid->tick();
            RAID_CHECK(raid->getRemaining()==0 && raid->getCooldown()==1, "no premature wave");
            CompoundTag prepared; manager->save(&prepared); manager->load(&prepared); raid=manager->get(id);
            raid->tick();
            const int waves=Raid::groupsForDifficulty(difficulty)+(omen>1?1:0);
            for(int wave=1;wave<=waves;++wave)
            {
                RAID_CHECK(raid->getStatus()==Raid::Ongoing && raid->getGroupsSpawned()==wave && raid->getRemaining()>0, "wave appears on loaded terrain");
                std::vector<boost::shared_ptr<Raider>> members;
                int types[5]={}; int leaders=0,riders=0; float maxHealth=0;
                for(auto& entry:raid->members())
                {
                    auto mob=entry.second.entity.lock(); RAID_CHECK(mob && mob->getRaidId()==id, "wave member attached");
                    members.push_back(mob); mob->setNoAi(true); maxHealth+=mob->getHealth();
                    const auto type=mob->GetType();
                    ++types[type==EntityType::VINDICATION_ILLAGER?0:type==EntityType::EVOCATION_ILLAGER?1:type==EntityType::PILLAGER?2:type==EntityType::WITCH?3:4];
                    if(mob->isCaptain()) ++leaders;
                    if(mob->isPassenger()) ++riders;
                    RAID_CHECK(!mob->removeWhenFarAway(), "raid participant never randomly despawns");
                }
                RAID_CHECK(leaders==1, "exactly one captain per wave");
                const bool bonus=wave>Raid::groupsForDifficulty(difficulty);
                for(int type=0;type<5;++type)
                {
                    int passengerBonus=type==2&&wave==5?types[4]:type==1&&wave>=7?1:type==0&&wave>=7?types[4]-1:0;
                    int base=Raid::baseCount(type,bonus?Raid::groupsForDifficulty(difficulty):wave)+passengerBonus;
                    int extra=type==1?0:type==4?(difficulty>1&&bonus?1:0):type==3?(difficulty>1&&wave>2&&wave!=4?1:0):difficulty==3?2:1;
                    RAID_CHECK(types[type]>=base && types[type]<=base+extra, "original 1.14 wave composition");
                }
                RAID_CHECK(riders==((wave==5||wave>=7)?types[4]:0), "ravager riders by wave");
                RAID_CHECK(std::abs(raid->bossbar().getPercent()-1)<.001, "counter bar starts full");
                auto first=members.front(); first->hurt(source.get(),3);
                raid->updateBossbar();
                RAID_CHECK(std::abs(raid->bossbar().getPercent()-1)<.001, "nonlethal damage preserves mob count");
                CompoundTag hornTimer;
                raid->save(&hornTimer);
                RAID_CHECK(hornTimer.getLong(L"NextHornTick") - hornTimer.getLong(L"TicksActive") == 400,
                           "wave repeats its horn after twenty active seconds");
                // Simulate loading raid metadata before its entity chunks.
                CompoundTag saved; manager->save(&saved); manager->load(&saved); raid=manager->get(id);
                RAID_CHECK(raid->getRemaining()==int(members.size()), "unattached saved UUIDs prevent duplicate waves");
                for(int tick=0;tick<40;++tick) raid->tick();
                RAID_CHECK(raid->getGroupsSpawned()==wave && raid->getRemaining()==int(members.size()), "saved wave reattaches by UUID");
                if (difficulty == 1 && omen == 1 && wave == 1)
                {
                    for (int tick = 40; tick < 400; ++tick) raid->tick();
                    CompoundTag replay;
                    raid->save(&replay);
                    RAID_CHECK(replay.getLong(L"NextHornTick") - replay.getLong(L"TicksActive") == 400,
                               "horn repeat timer fires and resets after a saved active wave");
                }
                for(auto& mob:members)
                {
                    CompoundTag entity; mob->addAdditonalSaveData(&entity);
                    auto restored=boost::dynamic_pointer_cast<Raider>(EntityIO::newEntity(mob->GetType(),level));
                    restored->readAdditionalSaveData(&entity);
                    RAID_CHECK(restored->getRaidId()==id && restored->getWave()==wave, "all five mob types persist raid fields");
                    const int before = raid->getRemaining();
                    const int previousAwards = player->ravagerAwards;
                    mob->mInvulnerableTime=0; mob->hurt(source.get(),1000);
                    RAID_CHECK(std::abs(raid->bossbar().getPercent()
                                        - float(before - 1) / members.size()) < .001,
                               "each kill removes an equal fraction of the bar after reload");
                    if (mob->GetType() == EntityType::RAVAGER)
                        RAID_CHECK(player->ravagerAwards == previousAwards + 1, "Ravager kill attempts the PS4 trophy");
                }
                RAID_CHECK(raid->getRemaining()==0 && !player->hasEffect(MobEffects::BAD_OMEN), "raid captains never restart Bad Omen");
                if(wave<waves)
                {
                    for(int tick=0;tick<300;++tick)raid->tick();
                    RAID_CHECK(raid->getRemaining()==0, "inter-wave cooldown");
                    raid->tick();
                }
            }
            for(int tick=0;tick<39;++tick)raid->tick();
            RAID_CHECK(raid->getStatus()==Raid::Ongoing,"victory waits 40 ticks");
            RaidSoundCheck outcomeSound;
            level->mListeners.push_back(&outcomeSound);
            raid->tick();
            level->mListeners.pop_back();
            RAID_CHECK(outcomeSound.cheers == 1, "one nearby villager cheers as the player wins");
            RAID_CHECK(raid->getStatus()==Raid::Victory && player->hasEffect(MobEffects::HERO_OF_THE_VILLAGE),"victory rewards combat contributor");
            RAID_CHECK(player->getEffect(MobEffects::HERO_OF_THE_VILLAGE)->getAmplifier()==omen-1,"Hero amplifier follows omen");
            RAID_CHECK(player->getEffect(MobEffects::HERO_OF_THE_VILLAGE)->getDuration()==48000,"Hero lasts 40 minutes");
            ClientboundUpdateMobEffectPacket heroPacket(player->getId(), player->getEffect(MobEffects::HERO_OF_THE_VILLAGE));
            RAID_CHECK(heroPacket.getEffectDurationTicks() == 48000 && !heroPacket.isSuperLongDuration(),
                       "multiplayer client receives the timed Hero duration");
            RAID_CHECK(!raid->bossbar().isVisible(), "victory removes the raid bar immediately");
            RAID_CHECK(!resident->isTakingShelter(),"victory releases villagers");
            for(int tick=0;tick<600;++tick)manager->tick();
            RAID_CHECK(!manager->get(id),"victory celebration ends after 30 seconds");
            std::printf("PASS: seed %lld difficulty %d omen %d: %d waves, riders, save/reload and victory\n", seeds[difficulty-1], difficulty, omen, waves);
        }
        player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,0,false,false),{});
        auto* raid=manager->createOrExtend(player);
        for(int tick=0;tick<300;++tick)raid->tick();
        RAID_CHECK(raid->getRemaining()>0,"defeat scenario started");
        level->getVillages()->releaseHome(resident->getUUID()); raid->tick();
        RAID_CHECK(raid->isLoss(),"destroyed village loses raid");
        for(auto& entry:raid->members()) if(auto mob=entry.second.entity.lock()) { mob->aiStep(); RAID_CHECK(mob->isCelebrating(),"raiders celebrate loss"); }
        raid->stop(); manager->tick();
        level->getVillages()->claimHome(resident->getUUID(),head);
        player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,0,false,false),{});
        raid=manager->createOrExtend(player); const int id=raid->getId();
        data->setDifficulty(Difficulty::PEACEFUL); manager->tick();
        RAID_CHECK(!manager->get(id),"peaceful cancels ongoing raid");data->setDifficulty(Difficulty::byId(difficulty));
        MerchantRecipe trade(not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald,20)),not_null_ptr<ItemInstance>(new ItemInstance(Items::bread)));
        trade.setHeroDiscount(0); RAID_CHECK(trade.getBuyAItem()->getCount()==14,"Hero I discount 30 percent");
        trade.setHeroDiscount(4); RAID_CHECK(trade.getBuyAItem()->getCount()==9,"Hero V discount 55 percent");
        std::unique_ptr<CompoundTag> original(trade.createTag()); MerchantRecipe loaded(original.get());
        RAID_CHECK(loaded.getBuyAItem()->getCount()==20,"customer discount never corrupts saved base price");
        trade.setHeroDiscount(-1); RAID_CHECK(trade.getBuyAItem()->getCount()==20,"normal customer pays base price");
        LootTableContext context(0,level,level->getLootTables(),resident,player,nullptr);
        for(int profession=0;profession<8;++profession) for(unsigned career=1;career<=4;++career)
            if(auto* table=raidHeroGiftTable(profession,career))
                for(int sample=0;sample<40;++sample)
                {
                    std::unique_ptr<std::vector<not_null_ptr<ItemInstance>>> gifts(table->getRandomItems(level->mRandom,context));
                    for(auto& gift:*gifts) RAID_CHECK(!gift->isEmpty() && gift->getItem()!=Items::air,"profession gift resolves to real legacy item");
                }
        std::puts("PASS: defeat, celebration, peaceful cancellation, per-customer discounts and profession gift tables");
    }
    std::puts("PASS: raid regression complete");
    return 0;
}
