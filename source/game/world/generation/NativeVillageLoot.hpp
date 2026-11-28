#pragma once
#include <cstddef>
#include <vector>
namespace NativeVillageRegistry
{
struct LootEntry { const wchar_t* item; int data, weight, minimum, maximum; };
struct LootDefinition { const wchar_t* id; int minimum, maximum; const LootEntry* entries; size_t count; };
const std::vector<LootDefinition>& lootDefinitions();
void registerLoot();
}
