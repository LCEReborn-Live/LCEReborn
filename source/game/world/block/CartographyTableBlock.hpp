#ifndef GAME_WORLD_BLOCK_CARTOGRAPHYTABLEBLOCK_HPP
#define GAME_WORLD_BLOCK_CARTOGRAPHYTABLEBLOCK_HPP

#include "game/world/block/CraftingTableBlock.hpp"

class CartographyTableBlock : public CraftingTableBlock
{
public:
    bool use(Level* level, const BlockPos& pos, const BlockState* state, boost::shared_ptr<Player> player,
             InteractionHand::EInteractionHand hand, const Direction* direction, float hitX, float hitY,
             float hitZ, bool clientSide) override;
    TextureAtlasSprite* getTexture(const Direction* direction, const BlockState* state) override;
    void registerIcons(IconRegister* icons) override;

private:
    TextureAtlasSprite* mMapTop = nullptr;
    TextureAtlasSprite* mMapSide1 = nullptr;
    TextureAtlasSprite* mMapSide2 = nullptr;
    TextureAtlasSprite* mMapSide3 = nullptr;
};

#endif
