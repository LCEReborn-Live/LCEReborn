#pragma once

#include <cmath>
#include <cstdio>
#include <memory>

#include "game/core/CMinecraftApp.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Pillager.hpp"
#include "game/core/Settings.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/data/SavedDataStorage.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/PatrolSpawner.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/server/ServerPlayerGameMode.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/WorldBorder.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Villages.hpp"

namespace
{
class PatrolTestPlayer : public ServerPlayer
{
public:
    PatrolTestPlayer(MinecraftServer* server, ServerLevel* level)
        : ServerPlayer(server, level, L"PatrolTester", new ServerPlayerGameMode(level, nullptr)) {}
    bool isSpectator() override { return spectator; }
    bool spectator = false;
};
}

inline int RunPatrolRegression()
{
    CGameNetworkManager::sInstance.Initialise();
    const auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));
    gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 1);

    const long long seeds[] = {8531, 98837, 42017};
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        LevelSettings settings(seeds[scenario], GameType::CREATIVE, false, false, false,
                               scenario == 0 ? LevelType::FLAT : LevelType::DEFAULT,
                               54, 3, 128, false, 0, 0, 0);
        auto* data = new LevelData(&settings, L"Patrol regression");
        data->setDifficulty(scenario == 0 ? Difficulty::NORMAL : scenario == 1 ? Difficulty::EASY : Difficulty::HARD);
        auto* level = new ServerLevel(server, storage, data, 0);
        server->mLevels[0] = level;
        level->postConstruct();
        level->mListeners.clear();
        auto* chunks = static_cast<ServerChunkCache*>(level->getChunkSource());
        for (int x = 0; x < 8; ++x)
            for (int z = 0; z < 8; ++z)
                chunks->create(x, z);
        auto player = boost::shared_ptr<PatrolTestPlayer>(new PatrolTestPlayer(server, level));
        player->mAbilities.mInstabuild = true;
        player->mAbilities.mInvulnerable = true;
        BlockPos playerPos;
        if (!PatrolSpawner::getSurface(*level, BlockPos(64, 64, 64), playerPos))
            return 140;
        player->moveTo(playerPos.x + 0.5, playerPos.y, playerPos.z + 0.5, 0, 0);
        level->mPlayers.push_back(player);
        level->setDayTime(120000);
        level->setSkyDarken(0);

        level->mRandom->setSeed(seeds[scenario]);
        auto patrols = boost::static_pointer_cast<PatrolSpawner>(level->getSavedDataStorage()->get(typeid(PatrolSpawner), L"patrols"));
        if (!patrols)
            return 141;
        CompoundTag due;
        due.putInt(L"NextAttemptTicks", 0);

        player->spectator = true;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            patrols->load(&due);
            if (patrols->tick(*level))
                return 166;
        }
        player->spectator = false;
        patrols->load(&due);
        if (patrols->tick(*level, false))
            return 167;
        level->mRandom->setSeed(seeds[scenario]);

        level->setDayTime(119999);
        if (patrols->tick(*level))
            return 142;
        CompoundTag pending;
        patrols->save(&pending);
        if (pending.getInt(L"NextAttemptTicks") < 11999 || pending.getInt(L"NextAttemptTicks") > 13198)
            return 143;
        PatrolSpawner loaded;
        loaded.load(&pending);
        gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 0);
        loaded.tick(*level);
        CompoundTag paused;
        loaded.save(&paused);
        if (paused.getInt(L"NextAttemptTicks") != pending.getInt(L"NextAttemptTicks"))
            return 144;
        gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 1);
        level->setDayTime(120000);
        loaded.tick(*level);
        loaded.save(&paused);
        if (paused.getInt(L"NextAttemptTicks") != pending.getInt(L"NextAttemptTicks") - 1)
            return 145;
        level->setSkyDarken(11);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            patrols->load(&due);
            if (patrols->tick(*level))
                return 146;
        }
        level->setSkyDarken(0);

        int count = 0;
        for (int attempt = 0; attempt < 100 && count == 0; ++attempt)
        {
            patrols->load(&due);
            count = patrols->tick(*level);
        }
        if (count == 0)
            return 147;
        auto members = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
            level->getEntitiesOfClass(typeid(Pillager), AABB::newTemp(0, 0, 0, 128, 256, 128)));
        if (members->size() != count)
            return 148;
        boost::shared_ptr<Pillager> captain;
        int leaders = 0;
        for (const auto& entity : *members)
        {
            auto mob = boost::static_pointer_cast<Pillager>(entity);
            leaders += mob->isCaptain();
            if (mob->isCaptain())
                captain = mob;
            if (!mob->isPatrolling() || mob->getMainHandItem()->getItem() != Items::crossbow
                || !mob->checkSpawnObstruction() || !PatrolSpawner::canSpawnAt(*level, BlockPos(mob)))
                return 149;
        }
        if (leaders != 1 || captain->getItemSlot(EquipmentSlot::HEAD)->getItem() != Items::banner
            || captain->isPersistenceRequired())
            return 150;
        const int dx = std::abs(BlockPos(captain).x - playerPos.x);
        const int dz = std::abs(BlockPos(captain).z - playerPos.z);
        if (dx < 24 || dx >= 48 || dz < 24 || dz >= 48)
            return 151;
        const int expectedSize = static_cast<int>(std::ceil(level->getCurrentDifficultyAt(BlockPos(captain)).getEffectiveDifficulty())) + 1;
        if (count > expectedSize || (scenario == 0 && count < 2))
            return 152;
        std::printf("PASS: seed %lld, difficulty %d: %d/%d patrol members, one captain, safe surface and 24..47 axis offsets\n",
                    seeds[scenario], level->getDifficulty()->getId(), count, expectedSize);

        if (scenario == 0)
        {
            const auto start = BlockPos(captain);
            captain->setPatrolTarget(start.offset(start.x < 64 ? 35 : -35, 0, start.z < 64 ? 35 : -35));
            bool following = false;
            for (int tick = 0; tick < 120; ++tick)
            {
                for (const auto& entity : *members)
                {
                    auto mob = boost::static_pointer_cast<Pillager>(entity);
                    ++mob->mTickCount;
                    mob->tick();
                    following |= !mob->isCaptain() && mob->hasPatrolTarget() && !mob->getNavigation()->isDone();
                }
            }
            if (!following || std::abs(captain->mX - start.x) + std::abs(captain->mZ - start.z) < 2.0)
                return 153;
            CompoundTag saved;
            captain->saveWithoutId(&saved);
            auto restored = boost::static_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, level));
            restored->load(&saved);
            if (!restored->isPatrolling() || !restored->isCaptain() || !restored->hasPatrolTarget()
                || restored->getPatrolTarget() != captain->getPatrolTarget())
                return 154;
            restored->moveTo(player->mX + 64, player->mY, player->mZ, 0, 0);
            if (restored->removeWhenFarAway())
                return 155;
            restored->moveTo(player->mX + 129, player->mY, player->mZ, 0, 0);
            if (!restored->removeWhenFarAway())
                return 156;
            restored->checkDespawn();
            if (!restored->mRemoved)
                return 157;
            std::puts("PASS: captain walks and shares waypoints, followers navigate, patrol NBT survives reload, despawn threshold is 128 blocks");

            const BlockPos test(15, 4, 15);
            level->setBrightness(LightLayer::BLOCK, test, 9, false);
            if (PatrolSpawner::canSpawnAt(*level, test))
                return 158;
            level->setBrightness(LightLayer::BLOCK, test, 8, false);
            if (!PatrolSpawner::canSpawnAt(*level, test))
                return 159;
            level->setBrightness(LightLayer::BLOCK, test, 0, false);
            level->setBlock(test, Blocks::water->defaultBlockState(), 0, false);
            BlockPos surface;
            if (!PatrolSpawner::getSurface(*level, test, surface) || surface.y != 5
                || PatrolSpawner::canSpawnAt(*level, surface))
                return 160;
            level->setBlock(test, Blocks::air->defaultBlockState(), 0, false);
            level->setBlock(test.above(3), Blocks::leaves->defaultBlockState(), 0, false);
            if (!PatrolSpawner::getSurface(*level, test, surface) || surface.y != 4)
                return 161;
            level->setBlock(test.above(3), Blocks::air->defaultBlockState(), 0, false);
            level->getChunkAt(test)->getBiomes().data[(test.z & 15) * 16 + (test.x & 15)] = Biome::eBiomeIDs_MushroomIsland;
            if (PatrolSpawner::canSpawnAt(*level, test))
                return 162;
            level->getChunkAt(test)->getBiomes().data[(test.z & 15) * 16 + (test.x & 15)] = Biome::eBiomeIDs_Plains;
            const BlockPos unloaded(160, 4, 160);
            if (level->hasChunkAt(unloaded, false) || PatrolSpawner::getSurface(*level, unloaded, surface)
                || patrols->spawnGroup(*level, unloaded) || level->hasChunkAt(unloaded, false))
                return 163;
            std::puts("PASS: block light 9 rejects and 8 allows, water rejects, leaves are skipped, mushroom biome rejects, unloaded chunks stay unloaded");

            const auto borderSize = level->getWorldBorder()->getSize();
            level->getWorldBorder()->setSize(32);
            if (PatrolSpawner::canSpawnAt(*level, BlockPos(31, 4, 31))
                || patrols->spawnGroup(*level, BlockPos(31, 4, 31)))
                return 168;
            level->getWorldBorder()->setSize(borderSize);
            const int edgeGroup = patrols->spawnGroup(*level, test);
            if (edgeGroup == 0)
                return 169;
            auto edgeMembers = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
                level->getEntitiesOfClass(typeid(Pillager), AABB::newTemp(7, 0, 7, 24, 20, 24)));
            int edgeLeaders = 0;
            bool crossesChunk = false;
            for (const auto& entity : *edgeMembers)
            {
                auto mob = boost::static_pointer_cast<Pillager>(entity);
                edgeLeaders += mob->isCaptain();
                crossesChunk |= (BlockPos(mob).x >> 4) != 0 || (BlockPos(mob).z >> 4) != 0;
                if (!mob->checkSpawnObstruction())
                    return 170;
            }
            if (edgeLeaders != 1 || edgeMembers->size() != edgeGroup || !crossesChunk)
                return 171;
            std::puts("PASS: world border rejects patrols and a group straddles chunk boundaries without collisions");

            const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
            const auto home = playerPos.east();
            level->setBlock(home, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 0, false);
            level->setBlock(home.south(), bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 0, false);
            level->getVillages()->claimHome(L"PatrolTestResident", home.south());
            for (int attempt = 0; attempt < 40; ++attempt)
            {
                patrols->load(&due);
                if (patrols->tick(*level))
                    return 164;
            }
            level->getLevelData()->setDifficulty(Difficulty::PEACEFUL);
            patrols->load(&due);
            if (patrols->tick(*level) || patrols->spawnGroup(*level, test))
                return 165;
            std::puts("PASS: village protection and peaceful difficulty block patrol spawning");
        }
    }
    for (int dimension : {-1, 1})
    {
        LevelSettings settings(8531, GameType::CREATIVE, false, false, false, LevelType::DEFAULT,
                               54, 3, 128, false, 0, 0, 0);
        auto* data = new LevelData(&settings, L"No dimension patrols");
        data->setDifficulty(Difficulty::NORMAL);
        auto* level = new ServerLevel(server, storage, data, dimension);
        level->postConstruct();
        level->setDayTime(240000);
        level->setSkyDarken(0);
        PatrolSpawner patrols;
        if (patrols.tick(*level) || PatrolSpawner::canSpawnAt(*level, BlockPos(0, 64, 0))
            || level->getSavedDataStorage()->get(typeid(PatrolSpawner), L"patrols"))
            return 172;
    }
    std::puts("PASS: Nether and End have no patrol scheduler or spawning");
    std::puts("PASS: patrol regression complete");
    return 0;
}
