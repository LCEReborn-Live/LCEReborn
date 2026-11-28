#include "game/item/BannerStencilItem.hpp"

#include "game/core/BannerPattern.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/item/ItemInstance.hpp"

BannerStencilItem::BannerStencilItem()
{
    setStackedByData(true);
    setMaxStackSize(1);
    setIconName(L"banner_stencil");
}

int BannerStencilItem::getDescriptionId(not_null_ptr<ItemInstance> item)
{
    const int variant = item->getAuxValue();
    return IDS_LCE_STENCIL_FIRST + (variant >= 0 && variant < Count ? variant : Flower);
}

const BannerPattern* BannerStencilItem::patternFor(int variant)
{
    const BannerPattern* patterns[] = {BannerPattern::FLOWER, BannerPattern::CREEPER, BannerPattern::SKULL,
                                       BannerPattern::MOJANG, BannerPattern::BRICKS, BannerPattern::CURLY_BORDER};
    return variant >= 0 && variant < Count ? patterns[variant] : nullptr;
}
