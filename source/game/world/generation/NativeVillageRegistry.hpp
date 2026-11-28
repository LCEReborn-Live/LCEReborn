#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "game/world/generation/StructureManager.hpp"

class CompoundTag;
class StructureTemplate;
class Level;
class Random;
class BlockPos;

namespace NativeVillageRegistry
{
using DataFactory = CompoundTag* (*)();
struct State { unsigned short block, data, extraBlock, extraData; };
struct Block { short x, y, z; unsigned short palette; DataFactory data; };
struct Connector { short x, y, z; const wchar_t* facing; const wchar_t* name; const wchar_t* target; const wchar_t* pool; };
struct Entity { double x, y, z; short blockX, blockY, blockZ; DataFactory data; };
struct Definition
{
    const wchar_t* id;
    short x, y, z;
    const State* palette;
    const Block* blocks;
    size_t blockCount;
    const Connector* connectors;
    size_t connectorCount;
    const Entity* entities;
    size_t entityCount;
};
struct PoolEntry { const wchar_t* piece; int weight; };
struct Pool
{
    const wchar_t* id;
    const wchar_t* fallback;
    bool terrain;
    const PoolEntry* entries;
    size_t count;
};

// Generated, typed geometry and registration tables. No NBT decoder or files.
const std::vector<const Definition*>& definitions();
const std::vector<const Pool*>& pools();
const Pool* findPool(const std::wstring& id);
const Definition* find(const std::wstring& id);
StructureTemplate* create(const Definition& definition);
std::vector<LocalVillageTemplateEntry> entries(const std::wstring& pool);
std::wstring fallback(const std::wstring& pool);
bool isFeature(const std::wstring& id);
bool placeFeature(const std::wstring& id, Level* level, Random& random, const BlockPos& pos);
}
