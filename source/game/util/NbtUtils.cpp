#include "game/util/NbtUtils.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include "game/core/DyeColor.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/data/Tag.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/BooleanProperty.hpp"
#include "game/world/block/HalfSlabBlock.hpp"
#include "game/world/block/Property.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DefaultedMappedRegistry.hpp"
#include "game/world/block/PlanksBlock.hpp"
#include "game/world/block/SandStoneBlock.hpp"
#include "game/world/block/StoneBlock.hpp"
#include "game/world/block/StoneBrickBlock.hpp"

bool NbtUtils::compareNbt(Tag* first, Tag* second, bool compareLists)
{
    if (first == second)
        return true;
    if (first == NULL)
        return true;
    if (second == NULL)
        return false;
    if (first->getId() != second->getId())
        return false;
    if (dynamic_cast<CompoundTag*>(first) != NULL)
    {
        CompoundTag* firstCompound = dynamic_cast<CompoundTag*>(first);
        CompoundTag* secondCompound = dynamic_cast<CompoundTag*>(second);
        const std::unique_ptr<std::vector<std::wstring>> keys(firstCompound->getAllKeys());
        for (const auto& key : *keys)
        {
            Tag* firstChild = firstCompound->get(key);
            Tag* secondChild = secondCompound->get(key);
            if (!compareNbt(firstChild, secondChild, compareLists))
                return false;
        }
        return true;
    }
    if (dynamic_cast<ListTag*>(first) != NULL && compareLists)
    {
        ListTag* firstList = dynamic_cast<ListTag*>(first);
        ListTag* secondList = dynamic_cast<ListTag*>(second);
        if (firstList->size() == 0)
            return secondList->size() == 0;
        for (int i = 0; i < firstList->size(); ++i)
        {
            Tag* element = firstList->get(i);
            bool found = false;
            for (int j = 0; j < secondList->size(); ++j)
            {
                if (compareNbt(element, secondList->get(j), compareLists))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }
    return first->equals(second);
}

BlockPos NbtUtils::readBlockPos(CompoundTag* tag)
{
    return BlockPos(tag->getInt(L"X"), tag->getInt(L"Y"), tag->getInt(L"Z"));
}

CompoundTag* NbtUtils::writeBlockPos(const BlockPos& pos)
{
    CompoundTag* tag = new CompoundTag();
    tag->putInt(L"X", pos.x);
    tag->putInt(L"Y", pos.y);
    tag->putInt(L"Z", pos.z);
    return tag;
}

void NbtUtils::mapToBlock(std::wstring& name, int& data, bool flattened)
{
    struct Alias { const wchar_t* from; const wchar_t* to; int data; };
    static const Alias aliases[] = {
        {L"cave_air", L"air", 0}, {L"void_air", L"air", 0},
        {L"grass_block", L"grass", 0}, {L"grass", L"tallgrass", 1}, {L"fern", L"tallgrass", 2},
        {L"tall_grass", L"double_plant", 2}, {L"large_fern", L"double_plant", 3},
        {L"snow_block", L"snow", 0}, {L"snow", L"snow_layer", 0},
        {L"oak_planks", L"planks", 0}, {L"acacia_planks", L"planks", 4},
        {L"oak_log", L"log", 0}, {L"spruce_log", L"log", 1}, {L"acacia_log", L"log2", 0},
        {L"spruce_wood", L"bark_spruce", 0}, {L"acacia_wood", L"bark_acacia", 0},
        {L"oak_leaves", L"leaves", 4}, {L"oak_fence", L"fence", 0},
        {L"oak_fence_gate", L"fence_gate", 0}, {L"oak_door", L"wooden_door", 0},
        {L"oak_trapdoor", L"trapdoor", 0}, {L"oak_pressure_plate", L"wooden_pressure_plate", 0},
        {L"wall_torch", L"torch", 0},
        {L"dead_bush", L"deadbush", 0}, {L"terracotta", L"hardened_clay", 0},
        {L"dark_oak_log", L"log2", 1}, {L"dark_oak_slab", L"wooden_slab", 5},
        {L"birch_planks", L"planks", 2}, {L"mossy_cobblestone_slab", L"decorative_slab_1", 3},
        {L"mossy_cobblestone_wall", L"cobblestone_wall", 1},
        {L"illager_captain_wall_banner", L"wall_banner", 0},
        {L"granite", L"stone", 1}, {L"cobblestone_stairs", L"stone_stairs", 0},
        {L"oak_slab", L"wooden_slab", 0}, {L"spruce_slab", L"wooden_slab", 1},
        {L"acacia_slab", L"wooden_slab", 4}, {L"smooth_stone_slab", L"stone_slab", 0},
        {L"sandstone_slab", L"stone_slab", 1}, {L"cobblestone_slab", L"stone_slab", 3},
        {L"diorite_slab", L"decorative_slab_1", 3}, {L"smooth_sandstone_slab", L"decorative_slab_1", 7},
        {L"melon", L"melon_block", 0},
        {L"attached_melon_stem", L"melon_stem", 7}, {L"dandelion", L"yellow_flower", 0},
        {L"poppy", L"red_flower", 0}, {L"oxeye_daisy", L"red_flower", 8},
        {L"potted_poppy", L"flower_pot", 1}, {L"potted_dandelion", L"flower_pot", 2},
        {L"potted_spruce_sapling", L"flower_pot", 4}, {L"potted_dead_bush", L"flower_pot", 10},
        {L"potted_cactus", L"flower_pot", 9}, {L"acacia_sapling", L"sapling", 4},
    };
    for (const auto& alias : aliases)
    {
        if (flattened && name == std::wstring(L"minecraft:") + alias.from)
        {
            name = std::wstring(L"minecraft:") + alias.to;
            data = alias.data;
            return;
        }
    }
    for (const DyeColor* color : DyeColor::COLORS)
    {
        const auto prefix = L"minecraft:" + (flattened && color == DyeColor::SILVER
            ? std::wstring(L"light_gray") : color->getSerializedName());
        if (name == prefix + L"_bed" || name == prefix + L"_wall_banner")
        {
            name = name == prefix + L"_bed" ? L"minecraft:bed" : L"minecraft:wall_banner";
            return;
        }
        if (name == prefix + L"_terracotta" || name == prefix + L"_carpet"
            || name == prefix + L"_wool" || name == prefix + L"_stained_glass_pane")
        {
            auto suffix = name.substr(prefix.size() + 1);
            if (suffix == L"terracotta")
                suffix = L"stained_hardened_clay";
            name = L"minecraft:" + suffix;
            data = color->getBlockData();
            return;
        }
    }
    if (name == L"minecraft:stone_bricks")
    {
        name = L"minecraft:stonebrick";
        data = 0;
        return;
    }
    if (name == L"minecraft:bricks")
    {
        name = L"minecraft:brick_block";
        data = 0;
        return;
    }
    if (name == L"minecraft:chiseled_stone_bricks")
    {
        name = L"minecraft:stonebrick";
        data |= StoneBrickBlock::Variant::CHISELED->getData();
        return;
    }
    if (name == L"minecraft:cracked_stone_bricks")
    {
        name = L"minecraft:stonebrick";
        data |= StoneBrickBlock::Variant::CRACKED->getData();
        return;
    }
    if (name == L"minecraft:mossy_stone_bricks")
    {
        name = L"minecraft:stonebrick";
        data |= StoneBrickBlock::Variant::MOSSY->getData();
        return;
    }
    if (name == L"minecraft:polished_granite")
    {
        name = L"minecraft:stone";
        data |= StoneBlock::Variant::SMOOTH_GRANITE->getData();
        return;
    }
    if (name == L"minecraft:diorite")
    {
        name = L"minecraft:stone";
        data |= StoneBlock::Variant::DIORITE->getData();
        return;
    }
    if (name == L"minecraft:polished_diorite")
    {
        name = L"minecraft:stone";
        data |= StoneBlock::Variant::SMOOTH_DIORITE->getData();
        return;
    }
    if (name == L"minecraft:dark_oak_planks")
    {
        name = L"minecraft:planks";
        data |= PlanksBlock::Variant::DARK_OAK->getData();
        return;
    }
    if (name == L"minecraft:spruce_planks")
    {
        name = L"minecraft:planks";
        data |= PlanksBlock::Variant::SPRUCE->getData();
        return;
    }
    if (name == L"minecraft:cut_sandstone")
    {
        name = L"minecraft:sandstone";
        data |= SandStoneBlock::SMOOTH->getData();
        return;
    }
    if (name == L"minecraft:light_blue_terracotta")
    {
        name = L"minecraft:stained_hardened_clay";
        data |= 3;
        return;
    }
    if (name == L"minecraft:magma_block")
    {
        name = L"minecraft:magma";
        return;
    }
    if (name == L"minecraft:stripped_oak_log")
    {
        name = L"minecraft:stripped_log_oak";
        return;
    }
    if (name == L"minecraft:stripped_spruce_log")
    {
        name = L"minecraft:stripped_log_spruce";
        return;
    }
    if (name == L"minecraft:stripped_birch_log")
    {
        name = L"minecraft:stripped_log_birch";
        return;
    }
    if (name == L"minecraft:stripped_jungle_log")
    {
        name = L"minecraft:stripped_log_jungle";
        return;
    }
    if (name == L"minecraft:stripped_acacia_log")
    {
        name = L"minecraft:stripped_log_acacia";
        return;
    }
    if (name == L"minecraft:stripped_dark_oak_log")
    {
        name = L"minecraft:stripped_log_dark_oak";
        return;
    }
    if (name == L"minecraft:chiseled_sandstone")
    {
        name = L"minecraft:sandstone";
        data |= SandStoneBlock::CHISELED->getData();
    }
}

const BlockState* NbtUtils::readBlockState(CompoundTag* tag, bool flattened, bool requireKnownBlock, std::wstring* problem)
{
    if (!tag->contains(L"Name", Tag::TAG_STRING))
    {
        if (problem)
            *problem = L"missing block Name";
        return requireKnownBlock ? nullptr : Blocks::air->defaultBlockState();
    }
    const auto originalName = tag->getString(L"Name");
    auto name = originalName;
    int data = 0;
    mapToBlock(name, data, flattened);
    const ResourceLocation key(name);
    if (requireKnownBlock && !Block::blockRegistry->containsKey(key))
    {
        if (problem)
            *problem = L"unsupported block " + originalName;
        return nullptr;
    }
    Block* block = Block::blockRegistry->get(key);
    if (flattened && tag->contains(L"Properties", Tag::TAG_COMPOUND))
    {
        auto* properties = tag->getCompound(L"Properties");
        if (HalfSlabBlock::isHalfSlab(block))
        {
            const auto type = properties->getString(L"type");
            if (type == L"top") data |= 8;
            else if (type == L"double")
            {
                auto* full = HalfSlabBlock::GetDoubleSlabBlockFromSlab(block);
                if (!full)
                {
                    if (problem) *problem = L"missing native double slab for " + originalName;
                    return requireKnownBlock ? nullptr : block->getBlockState(data);
                }
                block = full;
            }
            else if (requireKnownBlock && !type.empty() && type != L"bottom")
            {
                if (problem) *problem = L"unsupported slab type " + type;
                return nullptr;
            }
        }
        if (originalName == L"minecraft:oak_leaves" && properties->getString(L"persistent") == L"false")
            data &= ~4;
        if (properties->getString(L"lit") == L"true")
        {
            if (block == Blocks::furnace) block = Blocks::lit_furnace;
            else if (block == Blocks::blast_furnace) block = Blocks::lit_blast_furnace;
            else if (block == Blocks::smoker) block = Blocks::lit_smoker;
        }
    }
    const BlockState* state = block->defaultBlockState();
    if (tag->contains(L"Properties", Tag::TAG_COMPOUND))
    {
        CompoundTag* properties = tag->getCompound(L"Properties");
        BlockStateDefinition* definition = block->getBlockStateDefinition();
        std::unique_ptr<std::vector<std::wstring>> keys(properties->getAllKeys());
        std::vector<const Property*> blockProperties = definition->getProperties();
        for (std::vector<std::wstring>::iterator it = keys->begin(); it != keys->end(); ++it)
        {
            std::wstring key = *it;
            for (std::vector<const Property*>::iterator property = blockProperties.begin();
                 property != blockProperties.end(); ++property)
            {
                const Property* blockProperty = *property;
                if (key != blockProperty->getName())
                    continue;

                auto value = properties->getString(key);
                if (block == Blocks::bell && key == L"attachment")
                {
                    if (value == L"ceiling") value = L"1";
                    else if (value == L"single_wall") value = L"2";
                    else if (value == L"double_wall") value = L"3";
                    else if (value == L"floor" || (!requireKnownBlock
                             && (value.size() != 1 || value[0] < L'0' || value[0] > L'3')))
                        value = L"0";
                }
                if (requireKnownBlock)
                {
                    const auto& values = blockProperty->getPossibleValues();
                    const bool boolean = dynamic_cast<const BooleanProperty*>(blockProperty) != nullptr;
                    if (boolean ? (value != L"true" && value != L"false")
                                : std::none_of(values.begin(), values.end(), [&](const auto* candidate) {
                            return blockProperty->getName(candidate) == value;
                        }))
                    {
                        if (problem)
                            *problem = L"unsupported property " + originalName + L"[" + key + L"="
                                + properties->getString(key) + L"]";
                        return nullptr;
                    }
                }
                state = block->setPropertyValueFromString(state, blockProperty, value);
                break;
            }
        }
    }
    return block->getBlockState(block->convertBlockStateToLegacyData(state) | data);
}
