#include "game/world/block/CartographyTableBlock.hpp"

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/CartographyMenu.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/LocalBlockInteraction.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class CartographyInteraction : public LocalBlockInteraction
{
public:
    CartographyInteraction(Level* level, const BlockPos& pos)
        : LocalBlockInteraction(ClientboundContainerOpenPacket::CARTOGRAPHY, L""), mLevel(level), mPos(pos) {}

    AbstractContainerMenu* createMenu(boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Player>) override
    {
        return new CartographyMenu(inventory, mLevel, mPos);
    }

private:
    Level* mLevel;
    BlockPos mPos;
};
}

bool CartographyTableBlock::use(Level* level, const BlockPos& pos, const BlockState*,
                              boost::shared_ptr<Player> player, InteractionHand::EInteractionHand, const Direction*,
                              float, float, float, bool clientSide)
{
    if (clientSide)
        return false;
    if (!level->mIsClientSide)
    {
        CartographyInteraction interaction(level, pos);
        player->startBlockInteraction(interaction);
    }
    return true;
}

TextureAtlasSprite* CartographyTableBlock::getTexture(const Direction* direction, const BlockState*)
{
    if (direction == Direction::UP)
        return mMapTop;
    if (direction == Direction::NORTH)
        return mMapSide1;
    if (direction == Direction::SOUTH)
        return mMapSide2;
    return mMapSide3;
}

void CartographyTableBlock::registerIcons(IconRegister* icons)
{
    mMapTop = icons->registerIcon(L"cartography_table_top");
    mMapSide1 = icons->registerIcon(L"cartography_table_side1");
    mMapSide2 = icons->registerIcon(L"cartography_table_side2");
    mMapSide3 = icons->registerIcon(L"cartography_table_side3");
    mIcon = mMapSide3;
}
