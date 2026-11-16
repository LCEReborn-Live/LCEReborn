#include "game/item/ShieldItem.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/StringIds.hpp"

#include "game/item/BannerItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/ItemCooldowns.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/block/Blocks.hpp"

ShieldItem::ShieldItem()
{
    setMaxStackSize(1);
    setMaxDamage(336);
    setAllowOffhand(true);
    setDescriptionId(IDS_LCE_ITEM_SHIELD, IDS_LCE_DESC_SHIELD);
}

unsigned int ShieldItem::GetUseTooltip(const ItemTooltipDataHolder& tooltipData)
{
    // Raising the shield is bound to sneak, not to the item-use button.
    return IDS_NONE;
}

bool ShieldItem::isCrouchBlocking(Player* player)
{
    return player != NULL && player->isSneaking() && player->isUsingItem()
           && !player->getUseItem()->isEmpty() && Item::getId(player->getUseItem()->getItem()) == 442;
}

void ShieldItem::updateCrouchBlocking(Player* player)
{
#if LCE_PLATFORM_PC
    if (player == NULL)
        return;

    const bool usingShield = player->isUsingItem() && !player->getUseItem()->isEmpty()
                             && Item::getId(player->getUseItem()->getItem()) == 442;
    if (!player->isSneaking() || !player->isAlive() || player->isSpectator() || player->isSleeping())
    {
        if (usingShield)
            player->stopUsingItem();
        return;
    }

    if (player->isUsingItem())
    {
        // Never replace an active food, drink, bow or other item action.
        if (!usingShield)
            return;
        const not_null_ptr<ItemInstance> held = player->getItemInHand(player->getUsedItemHand());
        if (held == player->getUseItem() && !held->isEmpty()
            && !player->getCooldowns()->isOnCooldown(held->getItem()) && player->isAllowedToUse(held))
            return;
        player->stopUsingItem();
    }

    const InteractionHand::EInteractionHand hands[] = {
        InteractionHand::eInteractionHand_OffHand, InteractionHand::eInteractionHand_MainHand};
    for (InteractionHand::EInteractionHand hand : hands)
    {
        const not_null_ptr<ItemInstance> held = player->getItemInHand(hand);
        if (!held->isEmpty() && Item::getId(held->getItem()) == 442
            && !player->getCooldowns()->isOnCooldown(held->getItem()) && player->isAllowedToUse(held))
        {
            player->startUsingItem(hand);
            return;
        }
    }
#endif
}

void ShieldItem::appendHoverText(not_null_ptr<ItemInstance> itemInstance, boost::shared_ptr<Player> player,
                                 std::vector<HtmlString>* lines, bool advanced)
{
    BannerItem::appendHoverTextFromBannerBlockEntityTag(itemInstance, lines);
}

int ShieldItem::getUseAnimation(not_null_ptr<const ItemInstance> itemInstance) const
{
    return UseAnim_block;
}

int ShieldItem::getUseDuration(not_null_ptr<ItemInstance> itemInstance)
{
    return 72000;
}

InteractionResultHolder ShieldItem::use(Level* level, boost::shared_ptr<Player> player,
                                        InteractionHand::EInteractionHand hand)
{
#if LCE_PLATFORM_PC
    return Item::use(level, player, hand);
#else
    not_null_ptr<ItemInstance> itemInstance = player->getItemInHand(hand);
    player->startUsingItem(hand);
    return InteractionResultHolder(InteractionResult::eInteractionResult_Success, itemInstance);
#endif
}

bool ShieldItem::isValidRepairItem(not_null_ptr<ItemInstance> itemInstance, not_null_ptr<ItemInstance> repairItem)
{
    if (repairItem->getItem() == Item::byBlock(Blocks::planks))
    {
        return true;
    }

    return Item::isValidRepairItem(itemInstance, repairItem);
}
