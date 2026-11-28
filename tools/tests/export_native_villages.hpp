#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "game/core/NbtIo.hpp"
#include "game/core/StringConversion.hpp"
#include "game/core/Direction.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/data/IntTag.hpp"
#include "game/data/DoubleTag.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include "platform/pc/PcPaths.hpp"

inline int ExportNativeVillages()
{
    const char* source = std::getenv("LCE_VILLAGE_SOURCE");
    const char* output = std::getenv("LCE_VILLAGE_EXPORT");
    if (!source || !output) return 1;
    const auto root = std::filesystem::u8path(source);
    const auto destination = std::filesystem::u8path(output);
    const auto position = [](const BlockPos& pos) {
        auto* list = new ListTag();
        for (int n : {pos.x, pos.y, pos.z}) list->add(new IntTag(n));
        return list;
    };
    int count = 0, failed = 0;
    for (const auto& file : PcListVillageTemplates(root.wstring()))
    {
        const auto bytes = PcReadVillageTemplate(root.wstring(), file);
        std::unique_ptr<CompoundTag> tag(NbtIo::readChecked({const_cast<unsigned char*>(bytes.data()),
                                                          static_cast<unsigned int>(bytes.size())}));
        StructureTemplate structure;
        std::wstring problem;
        if (!tag || !structure.loadLocalVillage(tag.get(), problem))
        {
            std::printf("FAIL %s: %s\n", convWstringToString(file).c_str(), convWstringToString(problem).c_str());
            ++failed;
            continue;
        }
        CompoundTag native;
        native.put(L"size", position(structure.getSize()));
        auto* blocks = new ListTag();
        native.put(L"blocks", blocks);
        for (const auto* block : structure.getBlocks())
        {
            auto* item = new CompoundTag();
            item->put(L"pos", position(block->mPos));
            item->putInt(L"id", Block::getId(block->mState->getBlock()));
            item->putInt(L"data", block->mState->getBlock()->convertBlockStateToLegacyData(block->mState));
            item->putInt(L"extraId", block->mExtraState ? Block::getId(block->mExtraState->getBlock()) : 0);
            item->putInt(L"extraData", block->mExtraState ? block->mExtraState->getBlock()->convertBlockStateToLegacyData(block->mExtraState) : 0);
            if (block->mNbt) item->put(L"nbt", block->mNbt->copy());
            blocks->add(item);
        }
        auto* connectors = new ListTag();
        native.put(L"connectors", connectors);
        for (const auto& connector : structure.getConnectors())
        {
            auto* item = new CompoundTag();
            item->put(L"pos", position(connector.pos));
            item->putString(L"facing", connector.facing->getSerializedName());
            item->putString(L"name", connector.type);
            item->putString(L"target", connector.target);
            item->putString(L"pool", connector.pool);
            connectors->add(item);
        }
        auto* entities = new ListTag();
        native.put(L"entities", entities);
        for (const auto* entity : structure.getEntities())
        {
            auto* item = new CompoundTag();
            item->put(L"blockPos", position(entity->mBlockPos));
            auto* pos = new ListTag();
            for (double n : {entity->mPos->x, entity->mPos->y, entity->mPos->z}) pos->add(new DoubleTag(n));
            item->put(L"pos", pos);
            item->put(L"nbt", entity->mNbt->copy());
            entities->add(item);
        }
        const auto path = destination / file;
        std::filesystem::create_directories(path.parent_path());
        auto data = NbtIo::compress(&native);
        std::unique_ptr<unsigned char[]> owned(data.data);
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(data.data), data.length);
        if (!stream) return 2;
        ++count;
    }
    std::printf("Native export: %d templates, %d failures\n", count, failed);
    if (!failed && count == 482) std::puts("PASS: native village export");
    return failed || count != 482;
}
