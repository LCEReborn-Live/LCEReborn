#include "game/world/generation/NativeVillageRegistry.hpp"

#include <map>
#include "game/core/Direction.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/generation/StructureTemplate.hpp"

namespace NativeVillageRegistry
{
const Definition* find(const std::wstring& id)
{
    static const auto index = [] {
        std::map<std::wstring, const Definition*> result;
        for (const auto* definition : definitions()) result.emplace(definition->id, definition);
        return result;
    }();
    const auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

const Pool* findPool(const std::wstring& id)
{
    static const auto index = [] {
        std::map<std::wstring, const Pool*> result;
        for (const auto* pool : pools()) result.emplace(pool->id, pool);
        return result;
    }();
    const auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

std::vector<LocalVillageTemplateEntry> entries(const std::wstring& id)
{
    std::vector<LocalVillageTemplateEntry> result;
    const auto* pool = findPool(id);
    if (pool)
        for (size_t i = 0; i < pool->count; ++i)
            result.push_back({pool->entries[i].piece, pool->entries[i].weight, pool->terrain});
    return result;
}

std::wstring fallback(const std::wstring& id)
{
    const auto* pool = findPool(id);
    return pool ? pool->fallback : L"minecraft:empty";
}

StructureTemplate* create(const Definition& definition)
{
    std::vector<StructureTemplate::StructureBlockInfo*> blocks;
    std::vector<StructureTemplate::StructureEntityInfo*> entities;
    std::vector<StructureTemplate::Connector> connectors;
    blocks.reserve(definition.blockCount);
    for (size_t i = 0; i < definition.blockCount; ++i)
    {
        const auto& block = definition.blocks[i];
        const auto& state = definition.palette[block.palette];
        blocks.push_back(new StructureTemplate::StructureBlockInfo(BlockPos(block.x, block.y, block.z),
            ::Block::byId(state.block)->getBlockState(state.data), block.data ? block.data() : nullptr,
            ::Block::byId(state.extraBlock)->getBlockState(state.extraData)));
    }
    for (size_t i = 0; i < definition.connectorCount; ++i)
    {
        const auto& connector = definition.connectors[i];
        const Direction* facing = nullptr;
        for (const auto* direction : Direction::VALUES)
            if (direction->getSerializedName() == connector.facing) facing = direction;
        connectors.push_back({BlockPos(connector.x, connector.y, connector.z), facing,
                              connector.name, connector.pool, connector.target});
    }
    for (size_t i = 0; i < definition.entityCount; ++i)
    {
        const auto& entity = definition.entities[i];
        entities.push_back(new StructureTemplate::StructureEntityInfo(new Vec3(entity.x, entity.y, entity.z),
            BlockPos(entity.blockX, entity.blockY, entity.blockZ), entity.data()));
    }
    auto* result = new StructureTemplate();
    result->setNative(BlockPos(definition.x, definition.y, definition.z),
                      std::move(blocks), std::move(entities), std::move(connectors));
    return result;
}
}
