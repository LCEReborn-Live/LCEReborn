#ifndef GAME_ITEM_CROSSBOWITEM_HPP
#define GAME_ITEM_CROSSBOWITEM_HPP

#include "game/item/Item.hpp"

class CrossbowItem : public Item
{
public:
    CrossbowItem();

    InteractionResultHolder use(Level* level, boost::shared_ptr<Player> player,
                                InteractionHand::EInteractionHand hand) override;
    int getUseDuration(not_null_ptr<ItemInstance> item) override;
    int getUseAnimation(not_null_ptr<const ItemInstance> item) const override;
    void releaseUsing(not_null_ptr<ItemInstance> item, Level* level,
                      boost::shared_ptr<LivingEntity> entity, int remaining) override;
    int getEnchantmentValue() override;
    int GetOverrideCount() override;
    unsigned int GetUseTooltip(const ItemTooltipDataHolder& data) override;
    void appendHoverText(not_null_ptr<ItemInstance> item, boost::shared_ptr<Player> player,
                         std::vector<HtmlString>* lines, bool advanced) override;
    void registerIcons(IconRegister* icons) override;
    TextureAtlasSprite* getIcon(not_null_ptr<ItemInstance> item) override;
    TextureAtlasSprite* getLayerIcon(int aux, int layer, not_null_ptr<ItemInstance> item) override;
    TextureAtlasSprite* GetOverrideCountIcon(not_null_ptr<ItemInstance> item) override;
    TextureAtlasSprite* getPullingIcon(int ticks, not_null_ptr<ItemInstance> item);

    static int getChargeDuration(not_null_ptr<ItemInstance> item);
    static bool isCharged(not_null_ptr<ItemInstance> item);
    static void playLoadingSound(Level* level, const boost::shared_ptr<LivingEntity>& entity,
                                 not_null_ptr<ItemInstance> item, int ticks);

private:
    void shoot(Level* level, const boost::shared_ptr<Player>& player, not_null_ptr<ItemInstance> item);

    TextureAtlasSprite* mPullingIcons[3] = {};
    TextureAtlasSprite* mLoadedArrowIcon = nullptr;
    TextureAtlasSprite* mLoadedRocketIcon = nullptr;
};

#endif
