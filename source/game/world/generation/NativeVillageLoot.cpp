#include "game/world/generation/NativeVillageLoot.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/LootPool.hpp"
#include "game/core/LootTable.hpp"
#include "game/item/Item.hpp"
#include "game/item/LootItem.hpp"
#include "game/item/SetItemCountFunction.hpp"
#include "game/item/SetItemDataFunction.hpp"
#include "game/world/block/MappedRegistry.hpp"

void NativeVillageRegistry::registerLoot()
{
    for (const auto& definition : lootDefinitions())
    {
        auto* entries = new std::vector<LootPoolEntry*>;
        for (size_t i = 0; i < definition.count; ++i)
        {
            const auto& entry = definition.entries[i];
            auto* item = Item::itemRegistry.get(ResourceLocation(entry.item));
            auto* functions = new std::vector<LootItemFunction*>;
            if (entry.data) functions->push_back(new SetItemDataFunction(nullptr, RandomValueBounds(entry.data)));
            functions->push_back(new SetItemCountFunction(nullptr, RandomValueBounds(entry.minimum, entry.maximum)));
            entries->push_back(new LootItem(item, entry.weight, 0, functions, nullptr, nullptr));
        }
        auto* pools = new std::vector<LootPool*>;
        pools->push_back(new LootPool(entries, nullptr, RandomValueBounds(definition.minimum, definition.maximum),
                                      RandomValueBounds(0), nullptr));
        BuiltInLootTables::getLootTables()->emplace(new ResourceLocation(definition.id), new LootTable(pools));
    }
}
