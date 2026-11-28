#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <vector>

#include <zlib.h>

#include "game/core/ArmorStand.hpp"
#include "game/core/BoundingBox.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/GameType.hpp"
#include "game/core/NbtIo.hpp"
#include "game/core/Settings.hpp"
#include "game/data/ByteArrayTag.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/DataFixers.hpp"
#include "game/data/DoubleTag.hpp"
#include "game/data/IntTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/StructureFeatureIO.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructurePiece.hpp"
#include "game/world/generation/VillageFeature.hpp"
#include "platform/pc/PcPaths.hpp"

namespace CustomVillageCheck
{
class Storage : public MockedLevelStorage
{
public:
    std::unique_ptr<StructureManager> manager = std::make_unique<StructureManager>(DataFixers::createFixerUpper());
    StructureManager* getStructureManager() override { return manager.get(); }
};

inline ListTag* integers(std::initializer_list<int> values)
{
    auto* list = new ListTag();
    for (int value : values)
        list->add(new IntTag(value));
    return list;
}

inline CompoundTag* paletteEntry(const wchar_t* name, const wchar_t* facing = nullptr)
{
    auto* entry = new CompoundTag();
    entry->putString(L"Name", name);
    if (facing)
    {
        auto* properties = new CompoundTag();
        properties->putString(L"facing", facing);
        entry->put(L"Properties", properties);
    }
    return entry;
}

inline CompoundTag* block(int x, int y, int state)
{
    auto* entry = new CompoundTag();
    entry->put(L"pos", integers({x, y, 0}));
    entry->putInt(L"state", state);
    return entry;
}

inline CompoundTag* connector(int x, int state, const wchar_t* name, const wchar_t* target,
                              const wchar_t* pool, const wchar_t* finalState)
{
    auto* entry = block(x, 0, state);
    auto* nbt = new CompoundTag();
    nbt->putString(L"name", name);
    nbt->putString(L"target", target);
    nbt->putString(L"pool", pool);
    nbt->putString(L"final_state", finalState);
    entry->put(L"nbt", nbt);
    return entry;
}

inline bool writePiece(const std::filesystem::path& path, const wchar_t* material, const wchar_t* name,
                       const wchar_t* target, const wchar_t* pool, bool stair, bool gzip)
{
    CompoundTag tag;
    tag.put(L"size", integers({3, 2, 1}));
    auto* palette = new ListTag();
    palette->add(paletteEntry(material));
    palette->add(paletteEntry(L"minecraft:jigsaw", L"west"));
    palette->add(paletteEntry(L"minecraft:jigsaw", L"east"));
    if (stair)
        palette->add(paletteEntry(L"minecraft:oak_stairs", L"east"));
    tag.put(L"palette", palette);

    auto* blocks = new ListTag();
    blocks->add(name ? connector(0, 1, name, L"", L"lce:empty", material) : block(0, 0, 0));
    blocks->add(block(1, 0, 0));
    blocks->add(pool ? connector(2, 2, L"lce:exit", target, pool, material) : block(2, 0, 0));
    if (stair)
        blocks->add(block(1, 1, 3));
    tag.put(L"blocks", blocks);

    if (gzip)
    {
        // The road fixture carries a decoration at its upper end.
        auto* entity = new CompoundTag();
        auto* position = new ListTag();
        for (double coordinate : {2.5, 1.0, 0.5})
            position->add(new DoubleTag(coordinate));
        entity->put(L"pos", position);
        entity->put(L"blockPos", integers({2, 1, 0}));
        auto* nbt = new CompoundTag();
        nbt->putString(L"id", L"minecraft:armor_stand");
        entity->put(L"nbt", nbt);
        auto* entities = new ListTag();
        entities->add(entity);
        tag.put(L"entities", entities);
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    auto bytes = NbtIo::compress(&tag);
    std::unique_ptr<unsigned char[]> owned(bytes.data);
    std::ofstream file(path, std::ios::binary);
    if (gzip)
    {
        z_stream stream{};
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return false;
        std::vector<unsigned char> encoded(deflateBound(&stream, bytes.length));
        stream.next_in = bytes.data;
        stream.avail_in = bytes.length;
        stream.next_out = encoded.data();
        stream.avail_out = static_cast<uInt>(encoded.size());
        const bool complete = deflate(&stream, Z_FINISH) == Z_STREAM_END;
        const auto count = stream.total_out;
        deflateEnd(&stream);
        return complete && static_cast<bool>(file.write(reinterpret_cast<const char*>(encoded.data()), count));
    }
    return static_cast<bool>(file.write(reinterpret_cast<const char*>(bytes.data), bytes.length));
}
}

inline int RunCustomVillageRegression()
{
    using namespace CustomVillageCheck;
    const auto content = std::filesystem::absolute("village-content");
    PcSetContentRoot(content.generic_string());
    const auto root = content / "custom-villages" / "plains";
    if (!writePiece(root / "center" / "hub.nbt", L"minecraft:stone", nullptr,
                    L"lce:road", L"lce:roads", false, false)
        || !writePiece(root / "roads" / "track.nbt", L"minecraft:gravel", L"lce:road",
                       L"lce:house", L"lce:homes", false, true)
        || !writePiece(root / "homes" / "shelter.nbt", L"minecraft:oak_planks", L"lce:house",
                       L"lce:decor", L"lce:decor", true, false)
        || !writePiece(root / "decor" / "marker.nbt", L"minecraft:gold_block", L"lce:decor",
                       L"lce:gate", L"lce:ends", false, false)
        || !writePiece(root / "ends" / "gate.nbt", L"minecraft:cobblestone", L"lce:gate",
                       L"lce:yard", L"lce:yards", false, false)
        || !writePiece(root / "yards" / "terrace.nbt", L"minecraft:bricks", L"lce:yard",
                       nullptr, nullptr, false, false))
        return 10;
    std::ofstream(root / "center" / "truncated.nbt", std::ios::binary).put(10);
    if (!writePiece(root / "center" / "unknown.nbt", L"lce:no_such_block", nullptr,
                    nullptr, nullptr, false, false))
        return 10;

    auto storage = boost::shared_ptr<Storage>(new Storage());
    auto* manager = storage->getStructureManager();
    if (manager->getNamed(L"LocalVillage/plains/center/truncated")
        || manager->getNamed(L"LocalVillage/plains/center/unknown")
        || manager->getNamed(L"LocalVillage/plains/center/../hub")
        || !manager->getNamed(L"LocalVillage/plains/roads/track"))
        return 11;
    std::puts("PASS: raw and gzip NBT load and invalid custom files are skipped");

    CGameNetworkManager::sInstance.Initialise();
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    LevelSettings settings(51, GameType::SURVIVAL, false, false, false, LevelType::FLAT,
                           54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Custom village check");
    data->setDifficulty(Difficulty::NORMAL);
    data->setSpawn(BlockPos(0, 4, 0));
    auto* world = new ServerLevel(server, storage, data, 0);
    world->postConstruct();
    world->mListeners.clear();
    auto* chunks = static_cast<ServerChunkCache*>(world->getChunkSource());
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            chunks->create(x, z);
    for (int i = -9; i <= 25; ++i)
    {
        const int y = std::abs(i - 8) >= 4 ? 71 : 70;
        world->setBlock(BlockPos(i, y, 8), Blocks::grass->defaultBlockState(), 0, false);
        world->setBlock(BlockPos(8, y, i), Blocks::grass->defaultBlockState(), 0, false);
    }

    Random random(15);
    std::unique_ptr<StructureStart> village(new VillageFeature::VillageStart(world, random, 0, 0, 0, 864));
    if (!village->isValid() || village->getPieces()->size() != 6)
        return 12;
    for (auto* piece : *village->getPieces())
        if (piece->GetType() != eStructurePiece_VillageTemplate)
            return 12;

    const BlockPos center(8, 70, 8);
    BoundingBox firstChunk(0, 0, 15, 15);
    village->postProcess(world, random, &firstChunk);
    if (world->getBlock(center) != Blocks::stone)
        return 13;
    world->setBlock(center, Blocks::lapis_block->defaultBlockState(), 0, false);

    ByteArrayTag saved(village->createTag(0, 0), true);
    village.reset();
    storage->manager = std::make_unique<StructureManager>(DataFixers::createFixerUpper());
    village.reset(StructureFeatureIO::loadStaticStart(&saved, world));
    if (!village || !village->isValid() || village->getPieces()->size() != 6)
        return 14;
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
        {
            BoundingBox clip(x * 16, z * 16, x * 16 + 15, z * 16 + 15);
            village->postProcess(world, random, &clip);
        }
    if (world->getBlock(center) != Blocks::lapis_block)
        return 15;

    for (const auto* direction : Direction::VALUES)
    {
        if (direction->getAxis() == Direction::Axis::Y || world->getBlock(center.relative(direction, 3)) != Blocks::gravel)
            continue;
        const auto house = center.relative(direction, 6).above();
        const auto* stair = world->getBlockState(house.above());
        const auto rampTop = center.relative(direction, 4).above();
        if (world->getBlock(rampTop) != Blocks::gravel
            || world->getBlock(house) != Blocks::planks || stair->getBlock() != Blocks::oak_stairs
            || stair->getValue(HorizontalDirectionalBlock::FACING) != direction
            || world->getBlock(center.relative(direction, 9).above()) != Blocks::gold_block
            || world->getBlock(center.relative(direction, 15).above()) != Blocks::brick_block)
            return 16;
        const auto* box = AABB::newTemp(rampTop.x, rampTop.y + 1, rampTop.z,
                                       rampTop.x + 1, rampTop.y + 3, rampTop.z + 1);
        const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> decorations(
            world->getEntitiesOfClass(typeid(ArmorStand), box));
        if (decorations->size() != 1 || BlockPos(decorations->front()) != rampTop.above())
            return 18;
        std::puts("PASS: local village connects the road, house and decorations, rotates block states and resumes after reload");
        return 0;
    }
    return 17;
}
