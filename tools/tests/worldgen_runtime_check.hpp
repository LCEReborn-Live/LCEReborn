#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>

#include "native_village_runtime_check.hpp"
#include "game/core/MineShaftStart.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/RegionFileCache.hpp"
#include "game/data/ConsoleSaveFileOriginal.hpp"
#include "game/world/NetherLevelSource.hpp"
#include "game/world/TheEndLevelSource.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/chunk/ChunkPrimer.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/chunk/McRegionChunkStorage.hpp"
#include "game/world/chunk/OldChunkStorage.hpp"
#include "game/world/generation/BuriedTreasureFeature.hpp"
#include "game/world/generation/EndCityFeature.hpp"
#include "game/world/generation/OceanMonumentFeature.hpp"
#include "game/world/generation/OceanRuinFeature.hpp"
#include "game/world/generation/RandomScatteredLargeFeature.hpp"
#include "game/world/generation/ShipwreckFeature.hpp"
#include "game/world/generation/StrongholdFeature.hpp"
#include "game/world/generation/WoodlandMansionFeature.hpp"

namespace WorldgenCheck
{
class Storage : public CustomVillageCheck::Storage
{
public:
    ConsoleSaveFile* getSaveFile() override { return &mSave; }

private:
    ConsoleSaveFileOriginal mSave{L"", nullptr, 0, false, SAVE_FILE_PLATFORM_LOCAL};
};

class World : public NativeVillageCheck::World
{
public:
    using NativeVillageCheck::World::World;
    Biome* getBiome(const BlockPos& pos) override
    {
        return testBiomes ? testBiomes->getBiome(pos) : Level::getBiome(pos);
    }
};

class UnsuitableVillageTerrain : public GenericOverworldLevelSource
{
public:
    explicit UnsuitableVillageTerrain(bool flooded) : mFlooded(flooded) {}

    void prepareHeights(int chunkX, int, ChunkPrimer* primer) override
    {
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
            {
                const int ground = mFlooded ? 50 : (((chunkX * 16 + x) & 1) ? 96 : 68);
                primer->setState(x, ground, z, Blocks::stone->defaultBlockState());
                if (mFlooded)
                    primer->setState(x, 62, z, Blocks::water->defaultBlockState());
            }
    }

private:
    bool mFlooded;
};

inline World* createWorld(MinecraftServer* server, long long seed, int dimension, bool structures = false)
{
    LevelSettings settings(seed, GameType::SURVIVAL, structures, false, true, LevelType::DEFAULT,
                           54, 3, 128, true, 0, 0, 0);
    auto* data = new LevelData(&settings, L"World generation regression");
    data->setDifficulty(Difficulty::NORMAL);
    data->setSpawn(BlockPos(0, 75, 0));
    boost::shared_ptr<Storage> storage(new Storage());
    auto* world = new World(server, storage, data, dimension);
    world->postConstruct();
    world->mListeners.clear();
    return world;
}

inline bool roundTrip(World* world, StructureStart* input, const char* label, int x, int z)
{
    std::unique_ptr<StructureStart> start(input);
    const auto pieces = start->getPieces()->size();
    if (!start->isValid() || !pieces)
    {
        std::printf("FAIL: %s has no valid plan\n", label);
        return false;
    }
    ByteArrayTag saved(start->createTag(x, z), true);
    std::unique_ptr<StructureStart> loaded(StructureFeatureIO::loadStaticStart(&saved, world));
    if (!loaded || !loaded->isValid() || loaded->getPieces()->size() != pieces || loaded->GetType() != start->GetType())
    {
        std::printf("FAIL: %s loses its plan on reload\n", label);
        return false;
    }
    const auto a = saved.getAsByteArray();
    ByteArrayTag again(loaded->createTag(x, z), true);
    const auto b = again.getAsByteArray();
    if (a.length != b.length || std::memcmp(a.data, b.data, a.length))
    {
        std::printf("FAIL: %s changes its serialized plan on reload\n", label);
        return false;
    }
    std::printf("PASS: %s round-trip (%zu pieces)\n", label, pieces);
    return true;
}

inline std::uint64_t chunkHash(LevelChunk& chunk, int height)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (int localX = 0; localX < 16; ++localX)
        for (int localZ = 0; localZ < 16; ++localZ)
        {
            hash = (hash ^ chunk.getHeightmap(localX, localZ)) * 1099511628211ULL;
            for (int y = 0; y < height; ++y)
                hash = (hash ^ Block::blockStateRegistry->getId(chunk.getBlockState(localX, y, localZ)))
                    * 1099511628211ULL;
        }
    return hash;
}

inline std::uint64_t terrainHash(ChunkGenerator& generator, int x, int z, int height)
{
    std::unique_ptr<LevelChunk> chunk(generator.createChunk(x, z));
    return chunkHash(*chunk, height);
}

inline bool checkTerrain(ChunkGenerator& generator, int height, int z)
{
    const auto west = terrainHash(generator, -1, z, height);
    const auto east = terrainHash(generator, 0, z, height);
    return east != west && east == terrainHash(generator, 0, z, height)
        && west == terrainHash(generator, -1, z, height);
}

inline bool checkPlacement(World* world, StructureStart* start, int x, int z, bool mansion)
{
    std::unique_ptr<StructureStart> layout(start);
    if (!layout->isValid())
        return false;
    BoundingBox bounds(layout->getBoundingBox());
    auto* cache = static_cast<ServerChunkCache*>(world->getChunkSource());
    for (int cx = (bounds.x0 >> 4) - 1; cx <= (bounds.x1 >> 4) + 1; ++cx)
        for (int cz = (bounds.z0 >> 4) - 1; cz <= (bounds.z1 >> 4) + 1; ++cz)
            cache->create(cx, cz);

    Random random(world->getSeed());
    std::set<long long> built;
    std::set<long long> visible;
    for (int cx = bounds.x1 >> 4; cx >= bounds.x0 >> 4; --cx)
        for (int cz = bounds.z1 >> 4; cz >= bounds.z0 >> 4; --cz)
        {
            auto* chunk = world->getChunk(cx, cz);
            const auto before = chunkHash(*chunk, 128);
            BoundingBox clip(cx * 16, cz * 16, cx * 16 + 15, cz * 16 + 15);
            layout->postProcess(world, random, &clip);
            if (before != chunkHash(*chunk, 128))
                visible.insert(ChunkPos::asLong(cx, cz));
            if (built.empty())
            {
                ByteArrayTag save(layout->createTag(x, z), true);
                layout.reset(StructureFeatureIO::loadStaticStart(&save, world));
                if (!layout || !layout->isValid())
                    return false;
            }
            built.insert(ChunkPos::asLong(cx, cz));
        }

    size_t residents = 0;
    boost::shared_ptr<Entity> resident;
    for (const auto& entity : world->mEntities)
    {
        const bool expected = mansion
            ? entity->GetType() == EntityType::EVOCATION_ILLAGER || entity->GetType() == EntityType::VINDICATION_ILLAGER
            : entity->GetType() == EntityType::VILLAGER;
        if (expected && entity->isAlive() && bounds.isInside(BlockPos(entity)))
        {
            ++residents;
            if (!resident)
                resident = entity;
        }
    }
    int chests = 0, paths = 0, beds = 0, lootChests = 0;
    for (int px = bounds.x0; px <= bounds.x1; ++px)
        for (int pz = bounds.z0; pz <= bounds.z1; ++pz)
            for (int y = bounds.y0; y <= bounds.y1; ++y)
            {
                auto* block = world->getBlock(BlockPos(px, y, pz));
                chests += block == Blocks::chest;
                beds += block == Blocks::bed;
                paths += block == Blocks::grass_path;
                if (block == Blocks::chest)
                {
                    CompoundTag tag;
                    auto chest = world->getBlockEntity(BlockPos(px, y, pz));
                    if (chest)
                    {
                        chest->save(&tag);
                        lootChests += tag.contains(L"LootTable", Tag::TAG_STRING);
                    }
                }
            }
    if (!resident)
        return false;
    const ChunkPos sample{BlockPos(resident)};
    auto* chunk = world->getChunk(sample.x, sample.z);
    const auto expectedHash = chunkHash(*chunk, 256);
    ByteArrayOutputStream bytes;
    DataOutputStream output(&bytes);
    OldChunkStorage::save(chunk, world, &output);
    const auto data = bytes.toByteArray();
    ByteArrayInputStream buffer(data);
    DataInputStream input(&buffer);
    std::unique_ptr<DataFixerUpper> fixer(DataFixers::createFixerUpper());
    std::unique_ptr<LevelChunk> reloaded(OldChunkStorage::load(world, &input, fixer.get(), nullptr));
    if (!reloaded || expectedHash != chunkHash(*reloaded, 256))
        return false;
    if (!mansion && world->getSeed() == 1)
    {
        McRegionChunkStorage region(world->getLevelStorage()->getSaveFile(), L"", fixer.get());
        region.save(world, chunk);
        region.WaitForAll();
        gRegionFileCache._clear();
        reloaded.reset(region.load(world, sample.x, sample.z, nullptr));
        gRegionFileCache._clear();
        if (!reloaded || expectedHash != chunkHash(*reloaded, 256))
            return false;
        std::puts("PASS: village chunk reloaded from region storage with a fresh region cache");
    }
    std::vector<boost::shared_ptr<Entity>> loadedEntities;
    if (reloaded)
    {
        const auto area = AABB::newTemp(sample.x * 16, 0, sample.z * 16,
                                        sample.x * 16 + 16, 256, sample.z * 16 + 16);
        reloaded->getEntities(nullptr, area, loadedEntities, nullptr);
    }
    const bool residentRestored = std::any_of(loadedEntities.begin(), loadedEntities.end(), [&](const auto& entity) {
        return entity->getUUID() == resident->getUUID() && entity->isAlive();
    });
    std::printf("%s seed=%lld chunks=%zu chests=%d loot=%d residents=%zu beds=%d paths=%d residentReload=%d\n",
                mansion ? "Mansion" : "Village", world->getSeed(), visible.size(), chests, lootChests, residents,
                beds, paths, residentRestored);
    return visible.size() >= 2 && lootChests && (mansion || (beds && paths)) && residentRestored
        && reloaded && expectedHash == chunkHash(*reloaded, 256);
}
}

inline int RunWorldgenAudit()
{
    using namespace WorldgenCheck;
    ChunkPrimer primer(true, arrayWithLength<unsigned short>(65536, true));
    primer.setState(0, 63, 0, Blocks::stone->defaultBlockState());
    primer.setState(0, 66, 0, Blocks::water->defaultBlockState());
    primer.setState(0, 73, 1, Blocks::stone->defaultBlockState());
    primer.setState(15, 223, 15, Blocks::stone->defaultBlockState());
    if (primer.getHighestNonAirPos(0, 0) != 66 || primer.getHighestSolidPos(0, 0) != 63
        || primer.getHighestSolidPos(0, 1) != 73 || primer.getHighestSolidPos(15, 15) != 223
        || primer.getHighestNonAirPos(15, 15) != 223)
        return 1;
    std::puts("PASS: solid and fluid heights in adjacent columns and upper chunk storage");

    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    const auto begin = std::chrono::steady_clock::now();
    {
        auto* world = createWorld(server, 1, 0);
        FixedBiomeSource plains(Biome::PLAINS, 0.0f);
        world->testBiomes = &plains;
        for (bool flooded : {false, true})
        {
            UnsuitableVillageTerrain terrain(flooded);
            Random random(1);
            VillageFeature::VillageStart village(world, random, 0, 0, 0, 864, &terrain);
            if (village.isValid())
                return 15;
            if (flooded)
            {
                VillageFeature locator(world->getLevelData()->getXZSize());
                locator.setTerrainSource(&terrain);
                std::unique_ptr<BlockPos> found(locator.getNearestGeneratedFeature(world, BlockPos::ZERO, false));
                if (found)
                    return 16;
            }
        }
        std::puts("PASS: village rejects a cliff field and a flooded site, and locate skips invalid sites");
    }
    for (long long seed : {1LL, -90210LL, 987654321LL})
    {
        World* world = createWorld(server, seed, 0);
        World* netherWorld = createWorld(server, seed, -1);
        World* endWorld = createWorld(server, seed, 1);
        OverworldLevelSource overworld(world, seed, false, nullptr);
        NetherLevelSource nether(netherWorld, false, seed);
        TheEndLevelSource theEnd(endWorld, false, seed, BlockPos::ZERO);
        if (!checkTerrain(overworld, 128, -1) || !checkTerrain(nether, 128, -1) || !checkTerrain(theEnd, 128, 25))
        {
            std::printf("FAIL: terrain determinism for seed %lld\n", seed);
            return 2;
        }
        std::printf("PASS: terrain, caves and chunk heightmaps match in both orders for seed %lld (three dimensions)\n", seed);

        FixedBiomeSource plains(Biome::PLAINS, 0.0f);
        FixedBiomeSource forest(Biome::ROOFED_FOREST, 0.0f);
        FixedBiomeSource ocean(Biome::DEEP_OCEAN, 0.0f);
        world->testBiomes = &plains;
        const int x = -1, z = 0;
        Random random(seed);
        if (!roundTrip(world, new MineShaftStart(world, random, x, z, MineShaftFeature::eMineShaftType_Normal),
                       "mineshaft", x, z)
            || !roundTrip(world, new StrongholdFeature::StrongholdStart(world, random, x, z), "stronghold", x, z)
            || !roundTrip(world, new VillageFeature::VillageStart(world, random, x, z, 0, 864, &overworld),
                          "village", x, z)
            || !roundTrip(world, new BuriedTreasureFeature::BuriedTreasureStart(nullptr, world, random, x, z),
                          "buried treasure", x, z))
            return 3;
        world->testBiomes = &forest;
        if (!roundTrip(world, new WoodlandMansionFeature::WoodlandMansionStart(nullptr, world, &overworld, random, x, z),
                       "woodland mansion", x, z))
            return 4;
        for (auto* biome : {Biome::DESERT, Biome::JUNGLE, Biome::SWAMPLAND, Biome::ICE_PLAINS})
        {
            FixedBiomeSource fixed(biome, 0.0f);
            world->testBiomes = &fixed;
            if (!roundTrip(world, new RandomScatteredLargeFeature::ScatteredFeatureStart(world, random, x, z, &overworld),
                           biome == Biome::DESERT ? "desert pyramid" : biome == Biome::JUNGLE ? "jungle pyramid"
                               : biome == Biome::SWAMPLAND ? "swamp hut" : "igloo", x, z))
                return 5;
        }
        world->testBiomes = &ocean;
        OceanRuinConfiguration config;
        config.enabled = true;
        config.smallProbability = 1.0f;
        config.clusterProbability = 1.0f;
        if (!roundTrip(world, new OceanMonumentFeature::OceanMonumentStart(world, random, x, z), "monument", x, z)
            || !roundTrip(world, new OceanRuinFeature::OceanRuinStart(world, random, x, z, config, &overworld),
                          "ocean ruins", x, z)
            || !roundTrip(world, new ShipwreckFeature::ShipwreckStart(world, &overworld, random, x, z), "shipwreck", x, z)
            || !roundTrip(netherWorld, new NetherBridgeFeature::NetherBridgeStart(netherWorld, random, x, z),
                          "nether fortress", x, z)
            || !roundTrip(endWorld, new EndCityFeature::EndCityStart(endWorld, &theEnd, random, 0, 25), "end city", 0, 25))
            return 6;
        world->testBiomes = nullptr;
    }
    for (long long seed : {1LL, -90210LL, 987654321LL})
    {
        World* village = createWorld(server, seed, 0);
        FixedBiomeSource savanna(Biome::SAVANNA, 0.0f);
        village->testBiomes = &savanna;
        OverworldLevelSource villageTerrain(village, seed, false, nullptr);
        Random random(seed);
        if (!checkPlacement(village, new VillageFeature::VillageStart(village, random, 0, 0, 0, 864, &villageTerrain),
                            0, 0, false))
            return 7;

        World* mansion = createWorld(server, seed, 0);
        FixedBiomeSource forest(Biome::ROOFED_FOREST, 0.0f);
        mansion->testBiomes = &forest;
        OverworldLevelSource mansionTerrain(mansion, seed, false, nullptr);
        if (!checkPlacement(mansion, new WoodlandMansionFeature::WoodlandMansionStart(nullptr, mansion, &mansionTerrain,
                                                                                     random, -1, 0), -1, 0, true))
            return 8;
    }
    {
        const long long seed = 1;
        auto* world = createWorld(server, seed, 0, true);
        FixedBiomeSource savanna(Biome::SAVANNA, 0.0f);
        world->testBiomes = &savanna;
        std::unique_ptr<BlockPos> site(world->findNearestMapFeature(L"Village", BlockPos::ZERO, false));
        if (!site)
            return 9;
        const ChunkPos center(*site);
        auto* cache = static_cast<ServerChunkCache*>(world->getChunkSource());
        for (int x = center.x - 4; x <= center.x + 4; ++x)
            for (int z = center.z - 4; z <= center.z + 4; ++z)
                cache->create(x, z);
        const auto residents = std::count_if(world->mEntities.begin(), world->mEntities.end(), [](const auto& entity) {
            return entity->GetType() == EntityType::VILLAGER && entity->isAlive();
        });
        std::printf("Automatic village seed=%lld center=(%d,%d) residents=%lld\n",
                    seed, center.x, center.z, static_cast<long long>(residents));
        if (!residents)
            return 10;
    }
    std::printf("PASS: worldgen audit (%.2f s)\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    return 0;
}

inline int RunWorldgenStructurePlacement()
{
    using namespace WorldgenCheck;
    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    const long long seed = 1;
    auto* world = createWorld(server, seed, 0);
    Random random(seed);
    const auto check = [](World* level, StructureStart* input, const char* name, Block* expected, int x, int z) {
        std::unique_ptr<StructureStart> start(input);
        ByteArrayTag save(start->createTag(x, z), true);
        start.reset(StructureFeatureIO::loadStaticStart(&save, level));
        if (!start || !start->isValid())
            return false;
        const auto bounds = *start->getBoundingBox();
        auto* cache = static_cast<ServerChunkCache*>(level->getChunkSource());
        for (int cx = (bounds.x0 >> 4) - 1; cx <= (bounds.x1 >> 4) + 1; ++cx)
            for (int cz = (bounds.z0 >> 4) - 1; cz <= (bounds.z1 >> 4) + 1; ++cz)
                cache->create(cx, cz);
        Random population(level->getSeed());
        size_t chunks = 0, blocks = 0;
        for (int cx = bounds.x1 >> 4; cx >= bounds.x0 >> 4; --cx)
            for (int cz = bounds.z1 >> 4; cz >= bounds.z0 >> 4; --cz)
            {
                auto* chunk = level->getChunk(cx, cz);
                const auto before = chunkHash(*chunk, level->getHeight());
                BoundingBox clip(cx * 16, cz * 16, cx * 16 + 15, cz * 16 + 15);
                start->postProcess(level, population, &clip);
                chunks += before != chunkHash(*chunk, level->getHeight());
            }
        for (int px = bounds.x0; px <= bounds.x1; ++px)
            for (int pz = bounds.z0; pz <= bounds.z1; ++pz)
                for (int py = 1; py < level->getHeight(); ++py)
                    blocks += level->getBlock(BlockPos(px, py, pz)) == expected;
        std::printf("%s placement: %zu changed chunks, %zu signature blocks\n", name, chunks, blocks);
        return chunks > 0 && blocks > 0;
    };

    if (!check(world, new MineShaftStart(world, random, -6, 0, MineShaftFeature::eMineShaftType_Normal),
               "Mineshaft", Blocks::rail, -6, 0)
        || !check(world, new StrongholdFeature::StrongholdStart(world, random, 6, 0), "Stronghold",
                  Blocks::end_portal_frame, 6, 0))
        return 11;

    const struct { Biome* biome; const char* name; Block* block; int x; } scattered[] = {
        {Biome::DESERT, "Desert pyramid", Blocks::tnt, -8},
        {Biome::JUNGLE, "Jungle pyramid", Blocks::tripwire_hook, -4},
        {Biome::SWAMPLAND, "Swamp hut", Blocks::crafting_table, 4},
        {Biome::ICE_PLAINS, "Igloo", Blocks::carpet, 8},
    };
    for (const auto& entry : scattered)
    {
        auto* land = createWorld(server, seed, 0);
        FixedBiomeSource fixed(entry.biome, 0.0f);
        land->testBiomes = &fixed;
        OverworldLevelSource terrain(land, seed, false, nullptr);
        if (!check(land, new RandomScatteredLargeFeature::ScatteredFeatureStart(land, random, entry.x, -3, &terrain),
                   entry.name, entry.block, entry.x, -3))
            return 12;
    }

    auto* oceanWorld = createWorld(server, seed, 0);
    FixedBiomeSource ocean(Biome::DEEP_OCEAN, 0.0f);
    oceanWorld->testBiomes = &ocean;
    OverworldLevelSource oceanTerrain(oceanWorld, seed, false, nullptr);
    OceanRuinConfiguration config;
    config.enabled = true;
    config.smallProbability = 1.0f;
    config.clusterProbability = 1.0f;
    if (!check(oceanWorld, new OceanMonumentFeature::OceanMonumentStart(oceanWorld, random, -10, 0),
               "Monument", Blocks::prismarine, -10, 0)
        || !check(oceanWorld, new OceanRuinFeature::OceanRuinStart(oceanWorld, random, 0, 0, config, &oceanTerrain),
                  "Ocean ruins", Blocks::chest, 0, 0)
        || !check(oceanWorld, new ShipwreckFeature::ShipwreckStart(oceanWorld, &oceanTerrain, random, 6, 0),
                  "Shipwreck", Blocks::chest, 6, 0)
        || !check(oceanWorld, new BuriedTreasureFeature::BuriedTreasureStart(nullptr, oceanWorld, random, 10, 0),
                  "Buried treasure", Blocks::chest, 10, 0))
        return 13;

    auto* nether = createWorld(server, seed, -1);
    auto* end = createWorld(server, seed, 1);
    TheEndLevelSource endTerrain(end, false, seed, BlockPos::ZERO);
    if (!check(nether, new NetherBridgeFeature::NetherBridgeStart(nether, random, 0, 0), "Nether fortress",
               Blocks::nether_brick, 0, 0)
        || !check(end, new EndCityFeature::EndCityStart(end, &endTerrain, random, 0, 25), "End city",
                  Blocks::purpur_block, 0, 25))
        return 14;
    std::puts("PASS: worldgen structure placement");
    return 0;
}
