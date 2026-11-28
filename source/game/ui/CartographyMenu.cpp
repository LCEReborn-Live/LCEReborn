#include "game/ui/CartographyMenu.hpp"

#include "game/client/audio/SoundEvents.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/MapExtendingRecipe.hpp"
#include "game/item/MapItem.hpp"
#include "game/item/MapItemSavedData.hpp"
#include "game/network/ClientboundSoundPacket.hpp"
#include "game/network/ServerGamePacketListenerImpl.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/world/block/Blocks.hpp"

CartographyMenu::CartographyMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos)
    : TwoInputMenu(inventory, level, pos, Blocks::cartography_table)
{
}

bool CartographyMenu::accepts(int slot, not_null_ptr<ItemInstance> item)
{
    Item* type = item->getItem();
    if (slot == 0)
        return type == Items::filledMap || type == Items::paper;
    return type == Items::paper || type == Items::map || type == Item::byBlock(Blocks::glass_pane);
}

int CartographyMenu::preferredInput(not_null_ptr<ItemInstance> item)
{
    return item->getItem() == Items::filledMap
        || (item->getItem() == Items::paper && input(0)->getItem() != Items::filledMap) ? 0 : 1;
}

not_null_ptr<ItemInstance> CartographyMenu::makeResult()
{
    const auto map = input(0);
    const auto material = input(1);
    not_null_ptr<ItemInstance> result = ItemInstance::EMPTY;
    if (map->getItem() == Items::paper && material->isEmpty())
        result = not_null_ptr<ItemInstance>(new ItemInstance(Items::map));
    else if (map->getItem() == Items::filledMap && material->getItem() == Items::map)
    {
        result = map->copy();
        result->setCount(2);
    }
    else if (map->getItem() == Items::filledMap)
    {
        const auto data = MapItem::getSavedDataIfExists(map->getAuxValue(), mLevel);
        const bool zoom = material->getItem() == Items::paper;
        const bool lock = material->getItem() == Item::byBlock(Blocks::glass_pane);
        if (data && !data->isLocked() && (lock || (zoom && data->getScale() < 4
                                                   && !MapExtendingRecipe::isExplorationMap(data))))
        {
            result = map->copy();
            result->setCount(1);
            if (!result->getTag())
                result->setTag(new CompoundTag());
            if (zoom)
                result->getTag()->putInt(L"map_scale_direction", 1);
            else
                result->getTag()->putBoolean(L"map_lock_pending", true);
        }
    }
    return result;
}

void CartographyMenu::finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> result)
{
    result->onCraftedBy(mLevel, player, result->getCount());
    if (auto serverPlayer = boost::dynamic_pointer_cast<ServerPlayer>(player))
        if (serverPlayer->mConnection)
            serverPlayer->mConnection->send(boost::shared_ptr<Packet>(
                new ClientboundSoundPacket(SoundEvents::CARTOGRAPHY_TAKE_RESULT, 1.0f, 1.0f)));
}
