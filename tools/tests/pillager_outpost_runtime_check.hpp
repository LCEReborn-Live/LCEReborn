#pragma once

#include "worldgen_runtime_check.hpp"
#include "game/core/Pillager.hpp"
#include "game/entity/mob/MobCategory.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/world/block/BannerBlockEntity.hpp"
#include "game/world/block/ChestBlockEntity.hpp"
#include "game/world/generation/PillagerOutpostFeature.hpp"
#include "game/world/generation/StructurePlaceSettings.hpp"

namespace
{
class OutpostTestTerrain : public GenericOverworldLevelSource
{
public:
    void prepareHeights(int, int, ChunkPrimer* primer) override
    {
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                primer->setState(x, 72, z, Blocks::stone->defaultBlockState());
    }
};
}

inline int RunPillagerOutpostRegression()
{
    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    OutpostTestTerrain terrain;
    FixedBiomeSource plains(Biome::PLAINS, 0.0f);
    bool checkedCage = false;
    for (int seed = 1; seed <= 12 && !checkedCage; ++seed)
    {
        auto* world = WorldgenCheck::createWorld(server, seed, 0);
        server->mLevels[0] = world;
        world->testBiomes = &plains;
        auto* manager = world->getLevelStorage()->getStructureManager();
        for (const auto* name : {L"base_plate", L"feature_plate", L"watchtower", L"watchtower_overgrown",
                                 L"feature_cage1", L"feature_cage2", L"feature_logs", L"feature_targets",
                                 L"feature_tent1", L"feature_tent2"})
        {
            const auto* original = manager->getNamed(std::wstring(L"LocalVillage/pillager_outpost/") + name);
            if (!original || original->getSize().x <= 0)
            {
                std::printf("FAIL: imported template %ls\n", name);
                return 10;
            }
        }
        const auto* fallback = manager->getNamed(L"Structures\\pillageroutpost\\watchtower");
        StructurePlaceSettings markerSettings;
        if (!fallback || fallback->getMarkers(BlockPos(), &markerSettings).empty())
            return 11;

        PillagerOutpostFeature feature(&terrain, false);
        std::unique_ptr<BlockPos> nearest(feature.getNearestGeneratedFeature(world, BlockPos(0, 72, 0), false));
        if (!nearest)
            return 12;
        const ChunkPos center(*nearest);
        if (feature.isFeatureChunk(center.x, center.z, true) || feature.isPatrolArea(world, *nearest))
            return 13;
        feature.apply(world, center.x, center.z, nullptr);

        auto* cache = static_cast<ServerChunkCache*>(world->getChunkSource());
        for (int x = center.x - 2; x <= center.x + 2; ++x)
            for (int z = center.z - 2; z <= center.z + 2; ++z)
                cache->create(x, z);
        Random random(seed);
        ChunkPos towerChunk(center.x, center.z);
        if (!feature.postProcess(world, random, &towerChunk))
            return 14;

        for (int x = center.x + 2; x >= center.x - 2; --x)
            for (int z = center.z + 2; z >= center.z - 2; --z)
                if (x != center.x || z != center.z)
                {
                    ChunkPos chunk(x, z);
                    feature.postProcess(world, random, &chunk);
                }

        const BlockPos patrol(nearest->x, 74, nearest->z);
        if (!feature.isPatrolArea(world, patrol) || feature.isPatrolArea(world, patrol.offset(48, 0, 0)))
            return 15;
        auto* start = feature.getStructureAt(patrol);
        if (!start || start->getPieces()->size() < 3)
            return 16;

        int captains = 0, pillagers = 0, golems = 0, banners = 0, chests = 0;
        std::vector<boost::shared_ptr<BannerBlockEntity>> wallBanners;
        for (const auto& entity : world->mEntities)
        {
            if (auto* pillager = dynamic_cast<Pillager*>(entity.get()))
            {
                ++pillagers;
                captains += pillager->isCaptain();
            }
            golems += entity->GetType() == EntityType::VILLAGER_GOLEM;
        }
        const auto* tower = start->getPieces()->front()->getBoundingBox();
        for (int x = tower->x0; x <= tower->x1; ++x)
            for (int y = tower->y0; y <= tower->y1; ++y)
                for (int z = tower->z0; z <= tower->z1; ++z)
                {
                    const BlockPos pos(x, y, z);
                    auto* block = world->getBlock(pos);
                    if (block == Blocks::structure_block || block == Blocks::structure_void)
                        return 17;
                    if (block == Blocks::wall_banner)
                    {
                        auto banner = boost::dynamic_pointer_cast<BannerBlockEntity>(world->getBlockEntity(pos));
                        if (!banner || !banner->isIllagerBanner())
                            return 28;
                        wallBanners.push_back(banner);
                        ++banners;
                    }
                    if (auto chest = boost::dynamic_pointer_cast<ChestBlockEntity>(world->getBlockEntity(pos)))
                    {
                        CompoundTag nbt;
                        chest->save(&nbt);
                        if (nbt.getString(L"LootTable") != BuiltInLootTables::PILLAGER_OUTPOST.toString()
                            || nbt.getLong(L"LootTableSeed") == 0)
                            return 18;
                        int filled = 0;
                        for (unsigned int slot = 0; slot < chest->getContainerSize(); ++slot)
                            filled += !chest->getItem(slot)->isEmpty();
                        if (filled < 2)
                            return 25;
                        ++chests;
                    }
                }
        std::printf("Outpost seed %d: %zu pieces, %d pillagers (%d captain), %d golems, %d banners, %d chest\n",
                    seed, start->getPieces()->size(), pillagers, captains, golems, banners, chests);
        if (pillagers != 3 || captains != 1 || chests != 1 || banners < 7)
            return 19;

        PillagerOutpostFeature restored(&terrain, false);
        if (!restored.isPatrolArea(world, patrol) || restored.postProcess(world, random, &towerChunk))
            return 20;
        const auto* list = restored.getEnemies();
        if (list->size() != 1 || list->front()->mEntityBaseClass != EntityType::PILLAGER
            || list->front()->mMaxCount != 1)
            return 21;
        const auto* original = manager->getNamed(L"LocalVillage/pillager_outpost/watchtower");
        const auto originalFlag = std::find_if(original->getBlocks().begin(), original->getBlocks().end(),
            [](const auto* entry) { return entry->mState->getBlock() == Blocks::wall_banner && entry->mNbt; });
        if (originalFlag == original->getBlocks().end())
            return 29;
        for (size_t index = 0; index < 2; ++index)
        {
            const auto& banner = wallBanners[index];
            std::unique_ptr<CompoundTag> legacy((*originalFlag)->mNbt->copy());
            const auto& pos = banner->getBlockPos();
            legacy->putInt(L"x", pos.x);
            legacy->putInt(L"y", pos.y);
            legacy->putInt(L"z", pos.z);
            banner->load(legacy.get());
            if (banner->isIllagerBanner())
                return 30;
        }
        wallBanners[1]->setCustomName(L"Placed by a player", L"", false, true);
        for (size_t index = 0; index < 2; ++index)
        {
            const auto pos = wallBanners[index]->getBlockPos();
            const ChunkPos location(pos);
            ByteArrayOutputStream bytes;
            DataOutputStream output(&bytes);
            OldChunkStorage::save(world->getChunk(location.x, location.z), world, &output);
            const auto data = bytes.toByteArray();
            ByteArrayInputStream buffer(data);
            DataInputStream input(&buffer);
            std::unique_ptr<DataFixerUpper> fixer(DataFixers::createFixerUpper());
            std::unique_ptr<LevelChunk> reloaded(OldChunkStorage::load(world, &input, fixer.get(), nullptr));
            if (!reloaded)
                return 31;
            auto banner = boost::dynamic_pointer_cast<BannerBlockEntity>(
                reloaded->getBlockEntity(pos, LevelChunk::NO_CREATE));
            if (!banner || banner->isIllagerBanner())
                return 31;
            restored.updateLoadedBanners(world, *reloaded);
            if (banner->isIllagerBanner() != (index == 0))
            {
                std::printf("FAIL: reloaded banner %zu at %d, %d, %d\n", index, pos.x, pos.y, pos.z);
                return 31;
            }
        }
        std::puts("PASS: tower banners use the captain pattern and old template banners are updated on load");
        checkedCage = golems > 0;
    }
    if (!checkedCage)
        return 22;
    bool generated = false;
    for (int seed = 1; seed <= 12 && !generated; ++seed)
    {
        auto* world = WorldgenCheck::createWorld(server, seed, 0, true);
        server->mLevels[0] = world;
        std::unique_ptr<BlockPos> location(world->findNearestMapFeature(LargeFeature::PILLAGER_OUTPOST,
                                                                       BlockPos(0, 72, 0), true));
        if (!location)
            continue;
        const ChunkPos origin(*location);
        auto* cache = static_cast<ServerChunkCache*>(world->getChunkSource());
        for (int x = origin.x - 2; x <= origin.x + 2; ++x)
            for (int z = origin.z - 2; z <= origin.z + 2; ++z)
                cache->create(x, z);
        for (const auto& entity : world->mEntities)
        {
            auto* captain = dynamic_cast<Pillager*>(entity.get());
            if (captain && captain->isCaptain())
            {
                const BlockPos pos(captain);
                const auto* spawns = cache->getMobsAt(MobCategory::MONSTER, pos);
                if (!cache->isPosInFeature(world, LargeFeature::PILLAGER_OUTPOST, pos)
                    || spawns->size() != 1 || spawns->front()->mEntityBaseClass != EntityType::PILLAGER)
                    return 23;
                const auto spawn = pos.offset(10, 0, 0);
                auto probe = boost::static_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, world));
                probe->moveTo(spawn.x + 0.5, spawn.y, spawn.z + 0.5, 0.0f, 0.0f);
                probe->getRandom()->setSeed(31);
                bool allowed = false;
                for (int attempt = 0; attempt < 256 && !allowed; ++attempt)
                    allowed = probe->checkSpawnRules();
                if (!allowed || !world->addEntity(probe) || probe->getMaxSpawnClusterSize() != 1)
                    return 26;
                auto extra = boost::static_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, world));
                extra->moveTo(spawn.x + 0.5, spawn.y, spawn.z + 0.5, 0.0f, 0.0f);
                extra->getRandom()->setSeed(31);
                for (int attempt = 0; attempt < 256; ++attempt)
                    if (extra->checkSpawnRules())
                        return 27;
                std::puts("PASS: outpost refills one guard at a time and stops at four nearby pillagers");
                generated = true;
                break;
            }
        }
        if (generated)
            std::printf("PASS: ordinary overworld seed %d generated an outpost near %d, %d\n",
                        seed, location->x, location->z);
    }
    if (!generated)
        return 24;
    std::puts("PASS: imported outpost templates, tower, captain, cage golem, loot and save round-trip");
    return 0;
}
