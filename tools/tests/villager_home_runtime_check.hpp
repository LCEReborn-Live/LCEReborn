#pragma once

#include <cstdio>

#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/Cat.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Node.hpp"
#include "game/core/Path.hpp"
#include "game/core/Settings.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/ai/MakeLoveGoal.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/item/SimpleContainer.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/world/Level.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/CropBlock.hpp"
#include "game/world/block/HarvestFarmBlockGoal.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/VillagerGolem.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Villages.hpp"

inline int checkVillagerHomes(Level& level)
{
    const auto villages = level.getVillages();
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            level.setBlock(BlockPos(x, 69, z), Blocks::stone->defaultBlockState(), 0, false);

    const BlockPos origin(5, 70, 5);
    const BlockPos foot(10, 70, 9);
    const BlockPos head = foot.south();
    const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
    level.setBlock(foot, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 0, false);
    level.setBlock(head, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 0, false);

    const auto first = boost::shared_ptr<Villager>(new Villager(&level));
    const auto second = boost::shared_ptr<Villager>(new Villager(&level));
    first->setPos(origin.x + 0.5, origin.y, origin.z + 0.5);
    first->mOnGround = true;
    first->customServerAiStep();
    villages->findOrClaimHome(second->getUUID(), origin);
    const auto* assigned = villages->getHome(first->getUUID());
    if (!assigned || *assigned != head || villages->getHome(second->getUUID()))
        return 232;

    CompoundTag saved;
    villages->save(&saved);
    Villages reloaded(&level);
    reloaded.load(&saved);
    reloaded.findOrClaimHome(second->getUUID(), origin);
    const auto* restored = reloaded.getHome(first->getUUID());
    if (!restored || *restored != head || reloaded.getHome(second->getUUID()))
        return 233;
    std::puts("PASS: one villager per bed, with reservations preserved by save and reload");

    level.getLevelData()->setDayTime(6000);
    if (first->getHomeForShelter())
        return 234;
    const auto hasPathHome = [&] {
        auto* path = first->getNavigation()->getPath();
        const auto* end = path ? path->last() : nullptr;
        return end && BlockPos(end->x, end->y, end->z).distSqr(head) <= 2;
    };
    first->hearBell();
    for (int tick = 0; tick < 3; ++tick)
        first->serverAiStep();
    if (!hasPathHome())
        return 235;

    level.setGameTime(level.getGameTime() + 300);
    for (int tick = 0; tick < 3; ++tick)
        first->serverAiStep();
    if (first->getHomeForShelter())
        return 236;
    level.getLevelData()->setDayTime(13500);
    for (int tick = 0; tick < 3; ++tick)
        first->serverAiStep();
    if (!hasPathHome())
        return 237;

    for (int tick = 0; tick < 100 && BlockPos(first.get()).distSqr(head) > 2; ++tick)
    {
        level.setGameTime(level.getGameTime() + 1);
        first->tick();
    }
    if (BlockPos(first.get()).distSqr(head) > 2)
        return 239;

    level.setBlock(foot, Blocks::air->defaultBlockState(), 0, false);
    if (villages->getHome(first->getUUID()) || first->getHomeForShelter())
        return 238;
    std::puts("PASS: villagers navigate home at night and after a bell, and release removed beds");
    return 0;
}

inline int checkVillagerJobs(Level& level)
{
    const auto villages = level.getVillages();
    const Villages::Job farmer{0, 1}, fisherman{0, 2}, shepherd{0, 3}, fletcher{0, 4};
    const Villages::Job librarian{1, 1}, cartographer{1, 2}, cleric{2, 1};
    const Villages::Job armorer{3, 1}, weaponsmith{3, 2}, toolsmith{3, 3};
    const Villages::Job butcher{4, 1}, leatherworker{4, 2}, mason{Villager::MASON, 1};
    const std::pair<Block*, Villages::Job> roles[] = {
        {Blocks::composter, farmer}, {Blocks::barrel, fisherman}, {Blocks::loom, shepherd},
        {Blocks::fletching_table, fletcher}, {Blocks::lectern, librarian}, {Blocks::cartography_table, cartographer},
        {Blocks::brewing_stand, cleric}, {Blocks::blast_furnace, armorer}, {Blocks::lit_blast_furnace, armorer},
        {Blocks::grindstone, weaponsmith}, {Blocks::smithing_table, toolsmith},
        {Blocks::smoker, butcher}, {Blocks::lit_smoker, butcher}, {Blocks::cauldron, leatherworker},
        {Blocks::stonecutter, mason},
    };
    for (const auto& role : roles)
        if (!(Villages::jobForBlock(role.first) == role.second))
            return 240;

    const auto villager = boost::shared_ptr<Villager>(new Villager(&level));
    const auto competitor = boost::shared_ptr<Villager>(new Villager(&level));
    const auto nitwit = boost::shared_ptr<Villager>(new Villager(&level));
    nitwit->setProfession(Villager::NITWIT);
    const auto child = boost::shared_ptr<Villager>(new Villager(&level));
    child->setAge(-1000);
    for (const auto& resident : {villager, competitor, nitwit, child})
    {
        resident->setPos(3.5, 70, 3.5);
        resident->mOnGround = true;
    }
    const BlockPos site(9, 70, 8);
    level.getLevelData()->setDayTime(6000);
    level.setBlock(site, Blocks::stonecutter->defaultBlockState(), 0, false);
    child->customServerAiStep();
    nitwit->customServerAiStep();
    villager->customServerAiStep();
    competitor->customServerAiStep();
    if (villager->getProfession() != Villager::MASON || villager->getCareer() != 1
        || villages->getJobSite(competitor->getUUID()) || villages->getJobSite(nitwit->getUUID())
        || villages->getJobSite(child->getUUID()))
        return 241;
    auto* offers = villager->getOffers({});
    if (!offers || offers->empty() || offers->at(0)->getBuyAItem()->getItem() != Items::clayBall)
        return 242;

    CompoundTag saved;
    villages->save(&saved);
    Villages reloaded(&level);
    reloaded.load(&saved);
    const auto uuid = villager->getUUID();
    const auto* restored = reloaded.getJobSite(uuid);
    if (!restored || restored->pos != site || !(restored->job == mason)
        || reloaded.findOrClaimJobSite(competitor->getUUID(), BlockPos(3, 70, 3), nullptr))
        return 243;

    for (int tick = 0; tick < 160 && BlockPos(villager.get()).distSqr(site) > 2; ++tick)
    {
        level.setGameTime(level.getGameTime() + 1);
        villager->tick();
    }
    if (BlockPos(villager.get()).distSqr(site) > 2)
        return 244;
    level.getLevelData()->setDayTime(13500);
    if (villager->getWorkDestination())
        return 245;
    level.getLevelData()->setDayTime(6000);

    level.setBlock(site, Blocks::air->defaultBlockState(), 0, false);
    for (int tick = 0; tick < 125; ++tick)
        villager->customServerAiStep();
    if (villager->getProfession() != Villager::UNEMPLOYED || villages->getJobSite(uuid))
        return 246;
    level.setBlock(site, Blocks::fletching_table->defaultBlockState(), 0, false);
    for (int tick = 0; tick < 125; ++tick)
        villager->customServerAiStep();
    if (villager->getProfession() != 0 || villager->getCareer() != 4)
        return 247;
    villager->notifyTrade(villager->getOffers({})->at(0));
    CompoundTag residentSave;
    villager->saveWithoutId(&residentSave);
    const auto loadedVillager = boost::shared_ptr<Villager>(new Villager(&level));
    loadedVillager->load(&residentSave);
    level.setBlock(site, Blocks::smithing_table->defaultBlockState(), 0, false);
    loadedVillager->customServerAiStep();
    if (loadedVillager->getProfession() != 0 || loadedVillager->getCareer() != 4
        || villages->getJobSite(loadedVillager->getUUID()))
        return 248;
    competitor->customServerAiStep();
    for (int tick = 0; tick < 125; ++tick)
        competitor->customServerAiStep();
    if (competitor->getProfession() != 3 || competitor->getCareer() != 3)
        return 249;

    std::puts("PASS: villager job sites assign careers, persist reservations, navigate by day and preserve traded careers");
    return 0;
}

inline int checkVillageBoundaries(ServerLevel& level)
{
    Villages villages(&level);
    auto* cache = static_cast<ServerChunkCache*>(level.getChunkSource());
    for (int x = 0; x <= 7; ++x)
        cache->create(x, 0);

    const auto legacy = boost::shared_ptr<Village>(new Village(&level));
    legacy->addDoorInfo(boost::shared_ptr<DoorInfo>(
        new DoorInfo(BlockPos(2, 70, 7), Direction::NORTH, 0)));
    legacy->modifyStanding(L"resident", 7);
    CompoundTag legacyData;
    legacy->addAdditonalSaveData(&legacyData);
    auto* oldVillages = new ListTag();
    oldVillages->add(legacyData.copy());
    CompoundTag previousSave;
    previousSave.put(L"Villages", oldVillages);
    villages.load(&previousSave);

    const auto old = villages.getClosestVillage(BlockPos(2, 70, 8), 0);
    if (!old || old->usesPointsOfInterest() || old->getStanding(L"resident") != 7)
        return 250;

    const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
    const auto claimBed = [&](const std::wstring& resident, const BlockPos& head) {
        const auto foot = head.north();
        level.setBlock(foot.below(), Blocks::stone->defaultBlockState(), 0, false);
        level.setBlock(head.below(), Blocks::stone->defaultBlockState(), 0, false);
        level.setBlock(foot, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 0, false);
        level.setBlock(head, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 0, false);
        villages.findOrClaimHome(resident, head);
    };
    const BlockPos first(3, 70, 8), bridge(29, 70, 8), third(55, 70, 8), distant(111, 70, 8);
    const BlockPos workplace(18, 70, 8), extended(47, 70, 8);
    claimBed(L"first", first);
    const auto migrated = villages.getClosestVillage(first, 0);
    if (migrated != old || !migrated->usesPointsOfInterest() || *migrated->getCenter() != first
        || migrated->getStanding(L"resident") != 7 || villages.getClosestVillage(extended, 0))
        return 251;

    level.setBlock(workplace, Blocks::composter->defaultBlockState(), 0, false);
    if (!villages.findOrClaimJobSite(L"first", workplace, nullptr)
        || villages.getClosestVillage(extended, 0) != migrated || !migrated->isInside(extended))
        return 252;
    level.setBlock(workplace, Blocks::air->defaultBlockState(), 0, false);
    for (int i = 0; i < 20; ++i)
        villages.tick();
    if (villages.getClosestVillage(extended, 0) || *migrated->getCenter() != first)
        return 253;

    claimBed(L"bridge", bridge);
    claimBed(L"third", third);
    claimBed(L"distant", distant);
    const auto combined = villages.getClosestVillage(first, 0);
    const auto neighbor = villages.getClosestVillage(distant, 0);
    if (combined != migrated || !neighbor || neighbor == combined || combined->getBeds().size() != 3
        || villages.getClosestVillage(third, 0) != combined)
        return 254;

    CompoundTag saved;
    villages.save(&saved);
    Villages reloaded(&level);
    reloaded.load(&saved);
    const auto restored = reloaded.getClosestVillage(first, 0);
    if (!restored || restored->getBeds().size() != 3 || restored->getStanding(L"resident") != 7
        || reloaded.getClosestVillage(third, 0) != restored)
        return 255;

    level.setBlock(bridge.north(), Blocks::air->defaultBlockState(), 0, false);
    for (int i = 0; i < 20; ++i)
        reloaded.tick();
    if (reloaded.getHome(L"bridge") || reloaded.getClosestVillage(first, 0) == reloaded.getClosestVillage(third, 0)
        || reloaded.getVillages()->size() != 3)
        return 256;

    std::puts("PASS: village boundaries follow beds and job sites, split after removal and migrate legacy saves");
    return 0;
}

inline int checkVillagerScheduleAndTrades(ServerLevel& level)
{
    auto* cache = static_cast<ServerChunkCache*>(level.getChunkSource());
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = 1; cz <= 3; ++cz)
            cache->create(cx, cz);
    for (int x = -6; x <= 21; ++x)
        for (int z = 26; z <= 53; ++z)
            level.setBlock(BlockPos(x, 69, z), Blocks::stone->defaultBlockState(), 0, false);

    const auto villages = level.getVillages();
    const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
    const auto placeBed = [&](const BlockPos& head) {
        level.setBlock(head.north(), bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 0, false);
        level.setBlock(head, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 0, false);
    };
    const BlockPos heads[] = {{2, 70, 36}, {5, 70, 36}, {8, 70, 36}, {11, 70, 36}};
    std::vector<boost::shared_ptr<Villager>> residents;
    level.getLevelData()->setDayTime(13500);
    for (const auto& head : heads)
    {
        placeBed(head);
        auto resident = boost::shared_ptr<Villager>(new Villager(&level));
        resident->setPos(head.x + 0.5, head.y, head.z + 1.5);
        resident->mOnGround = true;
        level.addEntity(resident);
        residents.push_back(resident);
        resident->customServerAiStep();
        resident->customServerAiStep();
        if (!resident->isSleepingAt(head) || !resident->sleptRecently())
            return 30;
    }
    auto resident = residents.front();
    const auto village = villages->getClosestVillage(heads[0], 0);
    village->countPopulation();
    village->countGolem();
    if (!village->qualifiesForGolem() || village->getCatCapacity() != 1)
        return 31;
    const auto* area = AABB::newTemp(-6, 66, 26, 22, 76, 54);
    for (int i = 0; i < 16; ++i)
        village->tick(1200 * (i + 1));
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> cats(level.getEntitiesOfClass(typeid(Cat), area));
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> golems(
        level.getEntitiesOfClass(typeid(VillagerGolem), area));
    if (cats->size() != 1 || golems->size() != 1)
        return 32;

    level.setBlock(heads[0], level.getBlockState(heads[0])->setValue<bool>(BedBlock::OCCUPIED, true), 0, false);
    resident->customServerAiStep();
    if (resident->isSleeping())
        return 33;
    level.setBlock(heads[0], level.getBlockState(heads[0])->setValue<bool>(BedBlock::OCCUPIED, false), 0, false);
    const BlockPos bell(13, 70, 44);
    level.setBlock(bell, Blocks::bell->defaultBlockState(), 0, false);
    villages->addDoorInfos(bell);
    level.getLevelData()->setDayTime(9500);
    for (const auto& member : residents)
        member->customServerAiStep();
    if (residents[1]->isSleeping() || !resident->getMeetingDestination() || *resident->getMeetingDestination() != bell)
        return 34;
    for (int tick = 0; tick < 170 && BlockPos(resident).distSqr(bell) > 2; ++tick)
    {
        level.setGameTime(level.getGameTime() + 1);
        resident->tick();
    }
    if (BlockPos(resident).distSqr(bell) > 2)
    {
        const auto pos = BlockPos(resident);
        auto* path = resident->getNavigation()->getPath();
        const auto* end = path ? path->last() : nullptr;
        std::printf("Meeting route ended at (%d, %d, %d), path target (%d, %d, %d)\n", pos.x, pos.y, pos.z,
                    end ? end->x : -999, end ? end->y : -999, end ? end->z : -999);
        return 35;
    }
    level.setBlock(bell, Blocks::air->defaultBlockState(), 0, false);
    if (resident->getMeetingDestination())
        return 36;
    std::puts("PASS: villagers sleep, wake for occupied beds and visit bells; cats and golems respect village caps");

    const BlockPos composter(3, 70, 42);
    level.getLevelData()->setDayTime(6000);
    resident->setPos(3.5, 70, 41.5);
    level.setBlock(composter, Blocks::composter->defaultBlockState(), 0, false);
    resident->getInventory()->setItem(0, not_null_ptr<ItemInstance>(new ItemInstance(Items::wheat, 3)));
    resident->getInventory()->setItem(1, not_null_ptr<ItemInstance>(new ItemInstance(Items::wheatSeeds, 20)));
    for (int tick = 0; tick < 125; ++tick)
        resident->customServerAiStep();
    if (resident->getProfession() != 0 || resident->getCareer() != 1 || resident->getTradeLevel() != 1
        || resident->getInventory()->getItem(0)->getItem() != Items::bread
        || resident->getInventory()->getItem(1)->getCount() >= 20)
        return 37;

    const BlockPos cropPos(5, 70, 43);
    auto* wheat = static_cast<CropBlock*>(Blocks::wheat);
    level.setBlock(cropPos.below(), Blocks::farmland->defaultBlockState(), 0, false);
    level.setBlock(cropPos, wheat->getBlockState(wheat->getMaxAge()), 0, false);
    resident->setPos(cropPos.x + 0.5, cropPos.y, cropPos.z + 0.5);
    resident->mOnGround = true;
    HarvestFarmBlockGoal harvest(resident.get(), 0.6);
    if (!harvest.fjGetNextFarmTarget())
        return 46;
    harvest.start();
    harvest.tick();
    const auto* replanted = level.getBlockState(cropPos);
    if (replanted->getBlock() != wheat || wheat->isMaxAge(replanted))
        return 47;

    auto* offer = resident->getOffers({})->at(0);
    resident->setPos(10.5, 70, 41.5);
    const int maxUses = offer->getMaxUses();
    for (int use = 0; use < maxUses; ++use)
        resident->notifyTrade(offer);
    resident->customServerAiStep();
    if (resident->getTradeLevel() < 2 || !offer->isDeprecated())
        return 38;
    resident->setPos(3.5, 70, 41.5);
    resident->customServerAiStep();
    if (offer->getUses() != 0)
        return 39;
    for (int restock = 0; restock < 2; ++restock)
    {
        resident->notifyTrade(offer);
        level.setGameTime(level.getGameTime() + 2400);
        resident->customServerAiStep();
    }
    if (offer->getUses() != 1)
        return 40;
    CompoundTag save;
    resident->saveWithoutId(&save);
    auto loaded = boost::shared_ptr<Villager>(new Villager(&level));
    loaded->load(&save);
    auto* loadedOffer = loaded->getOffers({})->at(0);
    loaded->customServerAiStep();
    if (loadedOffer->getUses() != 1 || loaded->getTradeLevel() != resident->getTradeLevel())
        return 41;
    level.getLevelData()->setDayTime(30000);
    loaded->customServerAiStep();
    if (loadedOffer->getUses() != 0)
        return 42;
    auto fletcher = boost::shared_ptr<Villager>(new Villager(&level));
    fletcher->setProfession(0);
    fletcher->setCareer(4);
    for (int rank = 0; rank < 5; ++rank)
        fletcher->increaseMerchantCareer();
    bool sellsCrossbow = false;
    for (auto* deal : *fletcher->getOffers({}))
        sellsCrossbow |= deal->getSellItem()->getItem() == Items::crossbow;
    if (fletcher->getTradeLevel() != 5 || !sellsCrossbow)
        return 48;
    std::puts("PASS: farmers make bread and compost seeds; trade levels and two daily restocks survive reload");

    resident->setPos(4.5, 70, 40.5);
    residents[1]->setPos(5.5, 70, 40.5);
    resident->getInventory()->clearContent();
    residents[1]->getInventory()->clearContent();
    resident->getInventory()->setItem(0, not_null_ptr<ItemInstance>(new ItemInstance(Items::bread, 3)));
    residents[1]->getInventory()->setItem(0, not_null_ptr<ItemInstance>(new ItemInstance(Items::beetroot, 12)));
    MakeLoveGoal breed(resident.get());
    resident->getRandom()->setSeed(37);
    for (int i = 0; i < 2500; ++i)
        if (breed.canUse())
            return 43;
    const BlockPos nursery(8, 70, 43);
    placeBed(nursery);
    bool ready = false;
    for (int i = 0; i < 10000 && !ready; ++i)
        ready = breed.canUse();
    if (!ready)
        return 44;
    breed.start();
    breed.breed();
    const auto* nurseryArea = AABB::newTemp(0, 69, 34, 15, 74, 46);
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> villagers(
        level.getEntitiesOfClass(typeid(Villager), nurseryArea));
    bool hasChild = false;
    for (const auto& entity : *villagers)
    {
        auto* child = static_cast<Villager*>(entity.get());
        const auto* home = villages->getHome(child->getUUID());
        hasChild |= child->isBaby() && home && *home == nursery;
    }
    if (!hasChild || resident->hasBreedingFood() || residents[1]->hasBreedingFood())
        return 45;
    std::puts("PASS: current village mechanics cover sleep, gathering, breeding, farming, spawning and trades");
    return 0;
}

inline int checkVillagerTradeProgress(ServerLevel& level)
{
    class PlayerProbe : public Player
    {
    public:
        explicit PlayerProbe(Level* level) : Player(level, L"TradingTester") { fjDerivedCtorCalls(); }
        bool isSpectator() override { return false; }
        const GameType* GetGameType() override { return GameType::SURVIVAL; }
        void awardStat(Stat* stat, arrayWithLength<unsigned char> params) override
        {
            if (stat == GenericStats::achievement(eAward_BuyLowSellHigh))
                ++awards;
            delete[] params.data;
        }
        int awards = 0;
    };

    const auto fletcher = boost::shared_ptr<Villager>(new Villager(&level));
    fletcher->setPos(3.5, 72.0, 40.5);
    fletcher->setProfession(0);
    fletcher->setCareer(4);
    fletcher->getRandom()->setSeed(4096);
    auto* offers = fletcher->getOffers({});
    if (offers->size() != 4 || offers->getTradeLevel() != 1 || offers->isLocked(offers->at(0)))
        return 51;
    auto* sticks = offers->at(0);
    auto* flint = offers->at(1);
    auto* next = offers->at(2);
    if (sticks->getBuyAItem()->getItem() != Items::stick || sticks->getBuyAItem()->getCount() != 32
        || flint->getBuyAItem()->getItem() != Items::emerald || flint->getBuyBItem()->getCount() != 10
        || flint->getSellItem()->getItem() != Items::flint || !offers->isLocked(next))
        return 52;

    const auto player = boost::shared_ptr<PlayerProbe>(new PlayerProbe(&level));
    player->setPos(3.5, 72.0, 42.5);
    player->mInventory->setItem(0, sticks->getBuyAItem()->copy());
    level.mPlayers.push_back(player);
    fletcher->customServerAiStep();
    if (fletcher->getTradePreview()->getItem() != Items::emerald)
        return 53;
    fletcher->setTradingPlayer(player);
    fletcher->notifyTrade(next);
    if (next->getUses() || offers->getTradeExperience() || !fletcher->getTradePreview()->isEmpty()
        || offers->getRecipeFor(next->getBuyAItem(), next->getBuyBItem(), 2))
        return 54;
    if (offers->getRecipeFor(flint->getBuyAItem(), flint->getBuyBItem(), 1) != flint)
        return 55;

    for (int i = 0; i < 10; ++i)
        fletcher->notifyTrade(sticks);
    if (offers->getTradeLevel() != 2 || offers->getTradeExperience() != 20 || offers->isLocked(next)
        || offers->size() != 6 || player->awards)
        return 56;

    const auto village = level.getVillages()->getClosestVillage(BlockPos(fletcher), 32);
    if (!village)
        return 62;
    village->modifyStanding(player->getName(), 10 - village->getStanding(player->getName()));
    fletcher->getOffers(player);
    if (sticks->getBuyAItem()->getCount() != 29)
        return 63;
    village->modifyStanding(player->getName(), -10);

    player->addEffect(new MobEffectInstance(MobEffects::HERO_OF_THE_VILLAGE, 600, 0), {});
    fletcher->getOffers(player);
    if (sticks->getBuyAItem()->getCount() != 23)
        return 57;
    fletcher->notifyTrade(sticks);
    if (player->awards != 1)
        return 58;

    ByteArrayOutputStream bytes;
    DataOutputStream output(&bytes);
    offers->writeToStream(&output);
    ByteArrayInputStream packet(bytes.toByteArray());
    DataInputStream input(&packet);
    const std::unique_ptr<MerchantRecipeList> received(MerchantRecipeList::createFromStream(&input));
    if (received->getTradeLevel() != 2 || received->getTradeExperience() != 22 || received->size() != 6
        || received->isLocked(received->at(2)) || !received->isLocked(received->at(4))
        || received->at(0)->getBuyAItem()->getCount() != 23 || received->at(0)->getUses() != 11)
        return 59;

    CompoundTag save;
    fletcher->saveWithoutId(&save);
    const auto loaded = boost::shared_ptr<Villager>(new Villager(&level));
    loaded->load(&save);
    auto* restored = loaded->getOffers({});
    if (restored->getTradeLevel() != 2 || restored->getTradeExperience() != 22 || restored->size() != 6
        || restored->at(0)->getBuyAItem()->getCount() != 32 || restored->at(0)->getUses() != 11
        || !restored->isLocked(restored->at(4)))
        return 60;

    const auto farmer = boost::shared_ptr<Villager>(new Villager(&level));
    farmer->setProfession(0);
    farmer->setCareer(1);
    for (int i = 0; i < 5; ++i)
        farmer->increaseMerchantCareer();
    auto* masterOffers = farmer->getOffers({});
    auto* carrots = masterOffers->at(masterOffers->size() - 1);
    if (masterOffers->isLocked(carrots) || carrots->getTradeLevel() != 5
        || carrots->getBuyAItem()->getItem() != Items::emerald || carrots->getBuyAItem()->getCount() != 3
        || carrots->getSellItem()->getItem() != Items::goldenCarrot || carrots->getSellItem()->getCount() != 3)
        return 61;
    std::puts("PASS: trade preview, locked offers, live level-up, Hero trophy, network payload and save round-trip");
    return 0;
}

inline int RunVillagerHomeRegression()
{
    auto* textures = Minecraft::GetInstance()->mTextures;
    for (const wchar_t* badge : {L"mob/villager/levels/level_iron", L"mob/villager/levels/level_gold",
                                 L"mob/villager/levels/level_diamond"})
    {
        const std::unique_ptr<BufferedImage> image(textures->readImage(TN_UNINDEXED, badge));
        if (!image || image->getWidth() != 64 || image->getHeight() != 64)
            return 64;
        textures->loadTexture(TN_UNINDEXED, badge);
    }
    std::puts("PASS: iron, gold and diamond villager trade badge textures load");

    CGameNetworkManager::sInstance.Initialise();
    const auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    LevelSettings settings(1, GameType::SURVIVAL, false, false, false, LevelType::FLAT,
                           54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Villager home test");
    data->setDifficulty(Difficulty::NORMAL);
    data->setSpawn(BlockPos(0, 4, 0));
    auto* world = new ServerLevel(server, storage, data, 0);
    world->postConstruct();
    world->mListeners.clear();
    static_cast<ServerChunkCache*>(world->getChunkSource())->create(0, 0);

    const int homeResult = checkVillagerHomes(*world);
    if (homeResult)
        return homeResult;
    const int jobResult = checkVillagerJobs(*world);
    if (jobResult)
        return jobResult;
    const int boundariesResult = checkVillageBoundaries(*world);
    if (boundariesResult)
        return boundariesResult;
    const int scheduleResult = checkVillagerScheduleAndTrades(*world);
    return scheduleResult ? scheduleResult : checkVillagerTradeProgress(*world);
}
