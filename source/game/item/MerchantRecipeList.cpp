#include "game/item/MerchantRecipeList.hpp"
#include "game/core/Mth.hpp"

#include <algorithm>

#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/MerchantRecipe.hpp"

MerchantRecipeList::MerchantRecipeList() : std::vector<MerchantRecipe*>()
{
}

MerchantRecipeList::MerchantRecipeList(CompoundTag* tag) : std::vector<MerchantRecipe*>()
{
    load(tag);
}

MerchantRecipeList::~MerchantRecipeList()
{
    for (iterator it = std::vector<MerchantRecipe*>::begin(); it != std::vector<MerchantRecipe*>::end(); ++it)
    {
        delete *it;
    }
}

void MerchantRecipeList::load(CompoundTag* tag)
{
    ListTag* recipes = tag->getList(L"Recipes", Tag::TAG_COMPOUND);
    for (int i = 0; i < recipes->size(); ++i)
    {
        std::vector<MerchantRecipe*>::push_back(new MerchantRecipe(recipes->getCompound(i)));
    }
}

bool MerchantRecipeList::isRequiredItem(not_null_ptr<ItemInstance> supplied, not_null_ptr<ItemInstance> required)
{
    return ItemInstance::isSame(supplied, required);
}

MerchantRecipe* MerchantRecipeList::getRecipeFor(not_null_ptr<ItemInstance> first, not_null_ptr<ItemInstance> second,
                                                 int hint)
{
    if (hint >= 0 && hint < size())
    {
        MerchantRecipe* recipe = std::vector<MerchantRecipe*>::at(hint);
        if (!isLocked(recipe) && !recipe->isDeprecated() && isRequiredItem(first, recipe->getBuyAItem())
            && ((second->isEmpty() && !recipe->hasSecondaryBuyItem())
                || (recipe->hasSecondaryBuyItem() && isRequiredItem(second, recipe->getBuyBItem()))))
        {
            if (first->getCount() >= recipe->getBuyAItem()->getCount()
                && (!recipe->hasSecondaryBuyItem() || second->getCount() >= recipe->getBuyBItem()->getCount()))
            {
                return recipe;
            }
        }
        return NULL;
    }

    for (unsigned int index = 0; index < std::vector<MerchantRecipe*>::size(); ++index)
    {
        MerchantRecipe* recipe = std::vector<MerchantRecipe*>::at(index);
        if (!isLocked(recipe) && !recipe->isDeprecated() && isRequiredItem(first, recipe->getBuyAItem())
            && first->getCount() >= recipe->getBuyAItem()->getCount()
            && ((!recipe->hasSecondaryBuyItem() && second->isEmpty())
                || (recipe->hasSecondaryBuyItem() && isRequiredItem(second, recipe->getBuyBItem())
                    && second->getCount() >= recipe->getBuyBItem()->getCount())))
        {
            return recipe;
        }
    }
    return NULL;
}

bool MerchantRecipeList::addIfNewOrBetter(MerchantRecipe* recipe)
{
    bool result = false;
    for (int i = 0; i < std::vector<MerchantRecipe*>::size(); ++i)
    {
        MerchantRecipe* recipe = std::vector<MerchantRecipe*>::at(i);
        if (recipe->isSame(recipe))
        {
            if (recipe->isSameSameButBetter(recipe))
            {
                delete (*this)[i];
                (*this)[i] = recipe;
                result = true;
            }
            return result;
        }
    }
    std::vector<MerchantRecipe*>::push_back(recipe);
    return true;
}

MerchantRecipe* MerchantRecipeList::getMatchingRecipeFor(not_null_ptr<ItemInstance> first,
                                                         not_null_ptr<ItemInstance> second,
                                                         not_null_ptr<ItemInstance> result)
{
    for (unsigned int index = 0; index < std::vector<MerchantRecipe*>::size(); ++index)
    {
        MerchantRecipe* recipe = std::vector<MerchantRecipe*>::at(index);
        if (!isLocked(recipe) && !recipe->isDeprecated() && first->getItem() == recipe->getBuyAItem()->getItem()
            && first->getCount() >= recipe->getBuyAItem()->getCount()
            && result->getItem() == recipe->getSellItem()->getItem())
        {
            if (!recipe->hasSecondaryBuyItem()
                || (!second->isEmpty() && second->getItem() == recipe->getBuyBItem()->getItem()
                    && second->getCount() >= recipe->getBuyBItem()->getCount()))
            {
                return recipe;
            }
        }
    }
    return NULL;
}

void MerchantRecipeList::writeToStream(DataOutputStream* stream)
{
    stream->writeByte(static_cast<unsigned char>(std::vector<MerchantRecipe*>::size()));
    for (unsigned int i = 0; i < std::vector<MerchantRecipe*>::size(); ++i)
    {
        MerchantRecipe* recipe = std::vector<MerchantRecipe*>::at(i);
        stream->writeItem(recipe->getBuyAItem());
        stream->writeItem(recipe->getSellItem());
        not_null_ptr<ItemInstance> buyB = recipe->getBuyBItem();
        stream->writeBoolean(!buyB->isEmpty());
        if (!buyB->isEmpty())
        {
            stream->writeItem(buyB);
        }
        stream->writeBoolean(recipe->isDeprecated() || isLocked(recipe));
        stream->writeInt(recipe->getUses());
        stream->writeInt(recipe->getMaxUses());
    }
    // Legacy readers stop after the item list. Keep the progression in an optional trailer.
    if (mTradeLevel > 0)
    {
        stream->writeByte(static_cast<unsigned char>(mTradeLevel));
        stream->writeShort(static_cast<short>(mTradeExperience));
        for (const auto* recipe : *this)
        {
            stream->writeByte(static_cast<unsigned char>(recipe->getTradeLevel()));
            stream->writeByte(static_cast<unsigned char>(recipe->getTradeExperience()));
        }
    }
}

void MerchantRecipeList::push_back(MerchantRecipe* recipe)
{
    std::vector<MerchantRecipe*>::push_back(recipe);
}

MerchantRecipeList* MerchantRecipeList::createFromStream(DataInputStream* stream)
{
    MerchantRecipeList* result = new MerchantRecipeList();
    int count = stream->readByte();
    for (int i = 0; i < count; ++i)
    {
        not_null_ptr<ItemInstance> buyA = stream->readItem();
        not_null_ptr<ItemInstance> sell = stream->readItem();
        not_null_ptr<ItemInstance> buyB = ItemInstance::EMPTY;
        if (stream->readBoolean())
        {
            buyB = stream->readItem();
        }
        stream->readBoolean();
        int uses = stream->readInt();
        int maxUses = stream->readInt();
        MerchantRecipe* recipe = new MerchantRecipe(buyA, buyB, sell, uses, maxUses);
        result->push_back(recipe);
    }
    const int level = stream->read();
    if (level >= 1 && level <= 5)
    {
        result->setTradeProgress(level, stream->readUnsignedShort());
        for (auto* recipe : *result)
        {
            const int requiredLevel = stream->readUnsignedByte();
            const int experience = stream->readUnsignedByte();
            recipe->setTradeProgress(requiredLevel, experience);
        }
    }
    return result;
}

int MerchantRecipeList::experienceForLevel(int level)
{
    constexpr int thresholds[] = {0, 20, 70, 150, 250};
    return thresholds[Mth::clamp(level, 1, 5) - 1];
}

void MerchantRecipeList::setTradeProgress(int level, int experience)
{
    mTradeLevel = Mth::clamp(level, 0, 5);
    mTradeExperience = Mth::clamp(experience, 0, experienceForLevel(5));
}

bool MerchantRecipeList::isLocked(const MerchantRecipe* recipe) const
{
    return recipe->getTradeLevel() > mTradeLevel;
}

CompoundTag* MerchantRecipeList::createTag()
{
    CompoundTag* tag = new CompoundTag();
    ListTag* recipes = new ListTag();
    for (unsigned int i = 0; i < std::vector<MerchantRecipe*>::size(); ++i)
    {
        recipes->add(std::vector<MerchantRecipe*>::at(i)->createTag());
    }
    tag->put(L"Recipes", recipes);
    return tag;
}

MerchantRecipe* MerchantRecipeList::at(unsigned int index)
{
    return std::vector<MerchantRecipe*>::at(index);
}

MerchantRecipeList::iterator MerchantRecipeList::begin()
{
    return std::vector<MerchantRecipe*>::begin();
}

MerchantRecipeList::iterator MerchantRecipeList::end()
{
    return std::vector<MerchantRecipe*>::end();
}

int MerchantRecipeList::size()
{
    return static_cast<int>(std::vector<MerchantRecipe*>::size());
}

bool MerchantRecipeList::empty()
{
    return std::vector<MerchantRecipe*>::empty();
}

MerchantRecipeList::iterator MerchantRecipeList::erase(iterator position)
{
    delete *position;
    return std::vector<MerchantRecipe*>::erase(position);
}
