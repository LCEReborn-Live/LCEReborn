#pragma once
#include "raid_runtime_check.hpp"
#include "game/item/MilkBucketItem.hpp"
#include "game/item/BannerItem.hpp"
#include "game/core/VindicationIllager.hpp"
#include "game/core/HitResult.hpp"
#include "game/core/BossHealthOverlay.hpp"
#include "game/network/ClientboundBossEventPacket.hpp"
#include "game/entity/mob/FireworksRocketEntity.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/core/Wolf.hpp"
#include "game/data/ConsoleSaveFileOriginal.hpp"
#include "game/entity/ai/NodeEvaluator.hpp"
#include "game/entity/ai/PathNavigation.hpp"

class RaidTestStorage : public MockedLevelStorage
{
public:
    ConsoleSavePath getDataFile(const std::wstring& name) override { return ConsoleSavePath(L"data/" + name + L".dat"); }
    ConsoleSaveFile* getSaveFile() override { return &save; }
    ConsoleSaveFileOriginal save{L"", nullptr, 0, false, SAVE_FILE_PLATFORM_LOCAL};
};

class RaidTestWitch : public Witch
{
public:
    explicit RaidTestWitch(Level* level) : Witch(level) {}
    Goal* healGoal() { for(auto* entry:mTargetSelector.mAvailableGoals) if(entry->mGoal->getName()==L"HealRaidAlly")return entry->mGoal;return nullptr; }
};

inline int RunRaidMechanicsRegression()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);CGameNetworkManager::sInstance.Initialise();
    auto* server=new MinecraftServer();gMinecraftServer=server;server->mSettings=new Settings();
    server->mLevels=arrayWithLength<ServerLevel*>(3,true);server->setPlayers(new PlayerList(server,nullptr));
    auto storage=boost::shared_ptr<RaidTestStorage>(new RaidTestStorage());
    LevelSettings settings(341234,GameType::SURVIVAL,false,false,false,LevelType::FLAT,54,3,128,false,0,0,0);
    auto* data=new LevelData(&settings,L"Raid mechanics");data->setDifficulty(Difficulty::NORMAL);
    auto* level=new ServerLevel(server,storage,data,0);server->mLevels[0]=level;level->postConstruct();level->mListeners.clear();
    auto* chunks=static_cast<ServerChunkCache*>(level->getChunkSource());
    for(int x=-6;x<=6;++x)for(int z=-6;z<=6;++z)chunks->create(x,z);
    BlockPos ground;RAID_CHECK(PatrolSpawner::getSurface(*level,BlockPos(15,0,15),ground),"surface");
    auto villager=boost::shared_ptr<Villager>(new Villager(level));villager->moveTo(15.5,ground.y,15.5,0,0);level->addEntity(villager);
    level->getVillages()->claimHome(villager->getUUID(),ground);
    auto player=boost::shared_ptr<RaidTestPlayer>(new RaidTestPlayer(level));player->moveTo(15.5,ground.y,15.5,0,0);level->mPlayers.push_back(player);
    auto* manager=level->getRaids();
    player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,0,false,false),{});
    static_cast<MilkBucketItem*>(Items::milkBucket)->finishUsingItem(not_null_ptr<ItemInstance>(new ItemInstance(Items::milkBucket)),level,player);
    RAID_CHECK(!player->hasEffect(MobEffects::BAD_OMEN) && !manager->createOrExtend(player),"milk prevents raid");
    player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,0,false,false),{});
    Raid* raid=manager->createOrExtend(player);const int id=raid->getId();
    player->addEffect(new MobEffectInstance(MobEffects::BAD_OMEN,120000,2,false,false),{});
    RAID_CHECK(manager->createOrExtend(player)==raid && raid->getBadOmenLevel()==4,"existing raid absorbs omen instead of duplicate");
    CompoundTag checkpoint;raid->save(&checkpoint);BlockPos first,second;
    const bool found=raid->findSpawnPos(0,20,first);raid->load(&checkpoint);
    RAID_CHECK(raid->findSpawnPos(0,20,second)==found && (!found||first==second),"spawn PRNG deterministic across reload");
    Raid sleeping(*manager,999,BlockPos(1000,ground.y,1000),5,0);sleeping.tick();
    RAID_CHECK(!sleeping.isActive() && sleeping.getCooldown()==300,"unloaded center pauses countdown");
    for(int i=0;i<300;++i)raid->tick();
    RAID_CHECK(raid->getGroupsSpawned()==1,"first wave");
    std::vector<boost::shared_ptr<Raider>> members;
    for(const auto& entry:raid->members())members.push_back(entry.second.entity.lock());
    auto mob=members.back();mob->moveTo(50.5,ground.y,15.5,0,0);mob->setNoAi(false);
    const double before=mob->distanceToSqr(ground);
    for(int i=0;i<160;++i){mob->tick();mob->aiStep();}
    RAID_CHECK(mob->distanceToSqr(ground)<before-25,"native raid navigation advances toward village");
    auto witch=boost::shared_ptr<RaidTestWitch>(new RaidTestWitch(level));
    witch->moveTo(10.5,ground.y,15.5,0,0);level->addEntity(witch);raid->join(witch,1);witch->setNoAi(true);
    mob->moveTo(13.5,ground.y,15.5,0,0);mob->setHealth(3);mob->setNoAi(true);level->tick(mob,true);
    const auto nearby=[&](const std::type_info& type) { return std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(level->getEntitiesOfClass(type,AABB::newTemp(-100,0,-100,100,128,100))); };
    const size_t entitiesBefore=nearby(typeid(ThrownPotion))->size();witch->performRangedAttack(mob,1);
    auto potions=nearby(typeid(ThrownPotion));auto potion=boost::dynamic_pointer_cast<ThrownPotion>(potions->back());
    RAID_CHECK(potions->size()==entitiesBefore+1 && potion && PotionUtils::getPotion(potion->getItem())==Potions::HEALING,"witch throws healing at critically injured raider");
    potion->moveTo(mob->mX,mob->mY,mob->mZ,0,0);
    HitResult hit(mob);potion->onHit(&hit);RAID_CHECK(mob->getHealth()>3,"splash actually heals ally");
    witch->performRangedAttack(mob,1);potions=nearby(typeid(ThrownPotion));potion=boost::dynamic_pointer_cast<ThrownPotion>(potions->back());
    RAID_CHECK(potion && PotionUtils::getPotion(potion->getItem())==Potions::REGENERATION,"healthy ally gets regeneration");
    Goal* heal=witch->healGoal();
    RAID_CHECK(heal,"native heal target goal registered");bool selected=false;
    for(int i=0;i<100&&!selected;++i)selected=heal->canUse();RAID_CHECK(selected,"witch finds ally autonomously");
    heal->start();RAID_CHECK(witch->mHealCooldown==200 && boost::dynamic_pointer_cast<Raider>(witch->getTarget()),"witch 200 tick cooldown");heal->stop();
    auto leader=raid->getLeader(1);RAID_CHECK(leader,"captain exists");
    CompoundTag leaderTag;leader->addAdditonalSaveData(&leaderTag);
    auto loadedLeader=boost::dynamic_pointer_cast<Raider>(EntityIO::newEntity(leader->GetType(),level));
    loadedLeader->readAdditionalSaveData(&leaderTag);
    RAID_CHECK(loadedLeader->getNavigation()->getNodeEvaluator()->isCanOpenDoors(),"loaded raider restores door navigation");
    loadedLeader->setRaid(0,0);
    RAID_CHECK(!loadedLeader->getNavigation()->getNodeEvaluator()->isCanOpenDoors(),"leaving raid restores normal door navigation");
    leader->setCaptain(false);
    auto banner=boost::shared_ptr<ItemEntity>(new ItemEntity(level,mob->mX,mob->mY,mob->mZ,BannerItem::makeIllagerBanner()));
    banner->setNoPickUpDelay();level->addEntity(banner);mob->pickUpItem(banner);
    RAID_CHECK(mob->isCaptain() && banner->mRemoved && raid->getLeader(1)==mob.get(),"banner pickup appoints replacement captain");
    BellBlockEntity bell;bell.mLevel=level;bell.mPos=ground;bell.triggerEvent(1,0);
    mob->removeEffect(MobEffects::GLOWING);for(int i=0;i<44;++i)bell.tick();
    RAID_CHECK(!mob->hasEffect(MobEffects::GLOWING),"bell resonance delay");bell.tick();
    RAID_CHECK(mob->hasEffect(MobEffects::GLOWING) && mob->getEffect(MobEffects::GLOWING)->getDuration()==60,"bell reveals nearby raiders");
    manager->setDisabled(true);level->getSavedDataStorage()->save();
    SavedDataStorage restoredStorage(storage.get());
    auto restored=boost::dynamic_pointer_cast<Raids>(restoredStorage.get(typeid(Raids),L"raids"));
    RAID_CHECK(restored && restored->isDisabled() && restored->get(id)->getRemaining()==raid->getRemaining(),"compressed native save container preserves raid members and gamerule");
    manager->tick();RAID_CHECK(!manager->get(id),"disableRaids cancels raid");manager->setDisabled(false);
    player->removeAllEffects();player->addEffect(new MobEffectInstance(MobEffects::HERO_OF_THE_VILLAGE,48000,0,false,false),{});
    villager->setNoAi(true);villager->setTradingPlayer({});
    GiveRaidHeroGiftGoal gift(*villager);bool ready=false;
    for(int i=0;i<600;++i){++villager->mTickCount;RAID_CHECK(!gift.canUse(),"initial gift cooldown");}
    ++villager->mTickCount;RAID_CHECK(gift.canUse(),"hero gift becomes available");gift.start();
    size_t count=nearby(typeid(ItemEntity))->size();for(int i=0;i<21;++i){++villager->mTickCount;gift.tick();}
    RAID_CHECK(nearby(typeid(ItemEntity))->size()>count,"villager throws actual hero gift");gift.stop();
    BossHealthOverlay overlay(nullptr);ServerBossEvent dragon(0,L"Dragon",BossEvent::PURPLE,BossEvent::PROGRESS);dragon.setPlayBossMusic(true);
    overlay.update(boost::shared_ptr<ClientboundBossEventPacket>(new ClientboundBossEventPacket(ClientboundBossEventPacket::ADD,&dragon)));
    ServerBossEvent raidBar(0,L"Raid",BossEvent::PURPLE,BossEvent::PROGRESS);
    for(int i=0;i<50;++i)
    {
        overlay.update(boost::shared_ptr<ClientboundBossEventPacket>(new ClientboundBossEventPacket(ClientboundBossEventPacket::ADD,&raidBar)));
        overlay.update(boost::shared_ptr<ClientboundBossEventPacket>(new ClientboundBossEventPacket(ClientboundBossEventPacket::UPDATE_PROGRESS,&raidBar)));
        overlay.update(boost::shared_ptr<ClientboundBossEventPacket>(new ClientboundBossEventPacket(ClientboundBossEventPacket::REMOVE,&raidBar)));
        RAID_CHECK(overlay.shouldPlayMusic(),"raid packets preserve existing dragon boss");
    }
    overlay.reset();RAID_CHECK(!overlay.shouldPlayMusic(),"boss reset releases events");
    std::puts("PASS: raid mechanics regression complete");return 0;
}
