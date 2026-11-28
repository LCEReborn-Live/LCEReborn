#ifndef GAME_ITEM_BANNERSTENCILITEM_HPP
#define GAME_ITEM_BANNERSTENCILITEM_HPP

#include "game/item/Item.hpp"

class BannerPattern;

class BannerStencilItem : public Item
{
public:
    enum Variant { Flower, Creeper, Skull, Thing, Masoned, Indented, Count };
    BannerStencilItem();
    int getDescriptionId(not_null_ptr<ItemInstance>) override;
    static const BannerPattern* patternFor(int variant);
};

#endif
