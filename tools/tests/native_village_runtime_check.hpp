#pragma once

#include <cstring>
#include <tuple>
#include "custom_village_runtime_check.hpp"
#include "game/world/biome/FixedBiomeSource.hpp"
#include "game/world/generation/NativeVillageRegistry.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include "game/world/generation/NativeVillageLoot.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/LootTable.hpp"
#include "game/util/NbtUtils.hpp"
#include "game/util/Vec3.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/world/block/MappedRegistry.hpp"
#include "game/item/Item.hpp"
#include "game/core/StringConversion.hpp"
#include "game/world/block/HalfSlabBlock.hpp"
#include "game/world/OverworldLevelSource.hpp"

namespace NativeVillageCheck
{
class World : public ServerLevel
{
public:
    using ServerLevel::ServerLevel;
    using Level::mEntities;
    BiomeSource* testBiomes = nullptr;
    BiomeSource* getBiomeSource() override { return testBiomes ? testBiomes : ServerLevel::getBiomeSource(); }
};
}

inline int RunNativeVillageRegression()
{
    using namespace NativeVillageCheck;
    if (NativeVillageRegistry::definitions().size() != 482) return 1;
    CompoundTag slab;
    slab.putString(L"Name", L"minecraft:oak_slab");
    if (NbtUtils::compareNbt(&slab, slab.get(L"Name"), false)) return 27;
    CompoundTag empty;
    if (NbtUtils::compareNbt(&slab, &empty, false)) return 28;
    auto* slabProperties = new CompoundTag();
    slab.put(L"Properties", slabProperties);
    slabProperties->putString(L"type", L"top");
    const auto* top = NbtUtils::readBlockState(&slab, true, true);
    if (!top || !(top->getBlock()->convertBlockStateToLegacyData(top) & 8)) return 21;
    slabProperties->putString(L"type", L"double");
    const auto* full = NbtUtils::readBlockState(&slab, true, true);
    if (!full || !HalfSlabBlock::isFullSlab(full->getBlock())) return 22;
    auto storage = boost::shared_ptr<CustomVillageCheck::Storage>(new CustomVillageCheck::Storage());
    auto* manager = storage->getStructureManager();
    for (const auto* pool : NativeVillageRegistry::pools())
        for (size_t i = 0; i < pool->count; ++i)
        {
            const std::wstring id = pool->entries[i].piece;
            if (!id.empty() && !NativeVillageRegistry::find(id) && !NativeVillageRegistry::isFeature(id)) return 10;
            if (pool->entries[i].weight <= 0) return 11;
        }
    for (const auto& table : NativeVillageRegistry::lootDefinitions())
    {
        ResourceLocation id(table.id);
        if (BuiltInLootTables::getLootTable(&id) == LootTable::EMPTY_LOOT_TABLE) return 12;
        for (size_t i = 0; i < table.count; ++i)
            if (!Item::itemRegistry.get(ResourceLocation(table.entries[i].item)))
            {
                std::printf("Missing loot item: %s\n", convWstringToString(table.entries[i].item).c_str());
                return 13;
            }
    }
    size_t blocks = 0, connectors = 0, entities = 0, waterlogged = 0;
    for (const auto* definition : NativeVillageRegistry::definitions())
    {
        const auto* structure = manager->getNamed(definition->id);
        if (!structure || structure->getBlocks().size() != definition->blockCount
            || structure->getConnectors().size() != definition->connectorCount
            || structure->getEntities().size() != definition->entityCount) return 2;
        blocks += structure->getBlocks().size();
        connectors += structure->getConnectors().size();
        entities += structure->getEntities().size();
        for (const auto* block : structure->getBlocks())
            waterlogged += block->mExtraState && block->mExtraState->getBlock() == Blocks::water;
        for (const auto* entity : structure->getEntities())
            if (!EntityIO::isValidEntityType(ResourceLocation(entity->mNbt->getString(L"id")))) return 14;
        if (const char* source = std::getenv("LCE_VILLAGE_SOURCE"))
        {
            const std::wstring relative = std::wstring(definition->id).substr(std::wstring(L"NativeVillage/v1/").size()) + L".nbt";
            auto bytes = PcReadVillageTemplate(convStringToWstring(source), relative);
            std::unique_ptr<CompoundTag> tag(NbtIo::readChecked({bytes.data(), static_cast<unsigned int>(bytes.size())}));
            StructureTemplate original;
            std::wstring problem;
            if (!tag || !original.loadLocalVillage(tag.get(), problem)) return 15;
            if (original.getSize() != structure->getSize() || original.getBlocks().size() != structure->getBlocks().size()) return 16;
            auto a = original.getBlocks(), b = structure->getBlocks();
            const auto order = [](const StructureTemplate::StructureBlockInfo* entry) {
                return std::make_tuple(entry->mPos.x, entry->mPos.y, entry->mPos.z,
                    Block::getId(entry->mState->getBlock()), entry->mState->getBlock()->convertBlockStateToLegacyData(entry->mState),
                    entry->mExtraState ? Block::getId(entry->mExtraState->getBlock()) : 0);
            };
            const auto compare = [&](const auto* x, const auto* y) { return order(x) < order(y); };
            std::sort(a.begin(), a.end(), compare);
            std::sort(b.begin(), b.end(), compare);
            for (size_t i = 0; i < a.size(); ++i)
            {
                const bool sameState = order(a[i]) == order(b[i]);
                if (sameState && NbtUtils::compareNbt(a[i]->mNbt, b[i]->mNbt, false))
                    continue;
                const auto& pos = a[i]->mPos;
                std::printf("Template comparison failed for %s at (%d, %d, %d): %s\n",
                            convWstringToString(relative).c_str(), pos.x, pos.y, pos.z,
                            sameState ? "block NBT differs" : "block position or state differs");
                return 17;
            }
            if (original.getEntities().size() != structure->getEntities().size()) return 18;
            for (size_t i = 0; i < original.getEntities().size(); ++i)
            {
                const auto* x = original.getEntities()[i];
                const auto* y = structure->getEntities()[i];
                if (x->mBlockPos != y->mBlockPos || x->mPos->x != y->mPos->x || x->mPos->y != y->mPos->y
                    || x->mPos->z != y->mPos->z || !NbtUtils::compareNbt(x->mNbt, y->mNbt, false)) return 19;
            }
        }
    }
    std::printf("PASS: 482 native definitions, %zu blocks, %zu connectors, %zu entities\n", blocks, connectors, entities);
    if (!waterlogged) return 23;
    std::printf("PASS: upper/double slabs and %zu waterlogged blocks use native representations\n", waterlogged);
    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));
    LevelSettings settings(51, GameType::SURVIVAL, false, false, false, LevelType::FLAT,
                           54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Native village regression");
    data->setDifficulty(Difficulty::NORMAL);
    data->setSpawn(BlockPos(0, 4, 0));
    auto* world = new World(server, storage, data, 0);
    world->postConstruct();
    world->mListeners.clear();
    auto* chunks = static_cast<ServerChunkCache*>(world->getChunkSource());
    for (int x = -4; x <= 4; ++x)
        for (int z = -4; z <= 4; ++z) chunks->create(x, z);
    const std::vector<Biome*> biomes = {Biome::PLAINS, Biome::DESERT, Biome::SAVANNA,
                                      Biome::TAIGA, Biome::COLD_TAIGA, Biome::ICE_PLAINS};
    int generated = 0;
    for (size_t biome = 0; biome < biomes.size(); ++biome)
    {
        FixedBiomeSource fixed(biomes[biome], 0.0f);
        world->testBiomes = &fixed;
        for (long long seed : {1LL, 51LL, 4321LL, -90210LL, 987654321LL})
        {
            data->setSeed(seed);
            Random first(seed), second(seed);
            std::unique_ptr<StructureStart> a(new VillageFeature::VillageStart(world, first, 0, 0, 0, 864));
            std::unique_ptr<StructureStart> b(new VillageFeature::VillageStart(world, second, 0, 0, 0, 864));
            std::printf("biome=%zu seed=%lld pieces=%zu valid=%d\n", biome, seed, a->getPieces()->size(), a->isValid());
            std::fflush(stdout);
            if (!a->isValid() || a->getPieces()->size() < 6) return 3;
            auto bytesA = a->createTag(0, 0), bytesB = b->createTag(0, 0);
            ByteArrayTag saved(bytesA, true), other(bytesB, true);
            if (bytesA.length != bytesB.length || std::memcmp(bytesA.data, bytesB.data, bytesA.length)) return 4;
            a.reset(StructureFeatureIO::loadStaticStart(&saved, world));
            if (!a || !a->isValid() || a->getPieces()->size() != b->getPieces()->size()) return 5;
            ++generated;
            OverworldLevelSource terrain(world, seed, false, nullptr);
            Random terrainRandom(seed), terrainRepeat(seed);
            std::unique_ptr<StructureStart> terrainA(new VillageFeature::VillageStart(world, terrainRandom, 0, 0, 0, 864, &terrain));
            std::unique_ptr<StructureStart> terrainB(new VillageFeature::VillageStart(world, terrainRepeat, 0, 0, 0, 864, &terrain));
            if (!terrainA->isValid()) return 24;
            auto ta = terrainA->createTag(0, 0), tb = terrainB->createTag(0, 0);
            ByteArrayTag terrainSave(ta, true), terrainOther(tb, true);
            if (ta.length != tb.length || std::memcmp(ta.data, tb.data, ta.length)) return 25;
            if (biome == 0 && seed == 1)
            {
                BoundingBox firstClip(0, 0, 15, 15);
                a->postProcess(world, first, &firstClip);
                world->setBlock(BlockPos(8, 3, 8), Blocks::gold_block->defaultBlockState(), 0, false);
                ByteArrayTag partial(a->createTag(0, 0), true);
                a.reset(StructureFeatureIO::loadStaticStart(&partial, world));
                for (int x = -4; x <= 4; ++x)
                    for (int z = -4; z <= 4; ++z)
                    {
                        BoundingBox clip(x * 16, z * 16, x * 16 + 15, z * 16 + 15);
                        a->postProcess(world, first, &clip);
                    }
                std::puts("PASS: native placement resumes after partial chunk save");
                int beds = 0, chests = 0, paths = 0, villagers = 0;
                for (int x = -54; x <= 62; ++x)
                    for (int z = -54; z <= 62; ++z)
                        for (int y = 1; y < 30; ++y)
                        {
                            auto* block = world->getBlock(BlockPos(x, y, z));
                            beds += block == Blocks::bed;
                            chests += block == Blocks::chest;
                            paths += block == Blocks::grass_path;
                        }
                for (const auto& entity : world->mEntities) villagers += entity->GetType() == EntityType::VILLAGER;
                std::printf("Placed beds=%d chests=%d paths=%d villagers=%d\n", beds, chests, paths, villagers);
                if (!beds || !chests || !paths || !villagers) return 20;
                const auto entityCount = world->mEntities.size();
                for (int x = 4; x >= -4; --x)
                    for (int z = 4; z >= -4; --z)
                    {
                        BoundingBox clip(x * 16, z * 16, x * 16 + 15, z * 16 + 15);
                        a->postProcess(world, first, &clip);
                    }
                if (world->mEntities.size() != entityCount || world->getBlock(BlockPos(8, 3, 8)) != Blocks::gold_block) return 26;
            }
        }
    }
    std::printf("PASS: native village regression (%d layouts, six biomes, deterministic save/reload)\n", generated);
    return 0;
}
