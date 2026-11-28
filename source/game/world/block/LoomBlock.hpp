#ifndef GAME_WORLD_BLOCK_LOOMBLOCK_HPP
#define GAME_WORLD_BLOCK_LOOMBLOCK_HPP

#include <array>

#include "game/world/block/HorizontalDirectionalBlock.hpp"

class LoomBlock : public HorizontalDirectionalBlock
{
public:
    LoomBlock();
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>,
             InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide) override;
    unsigned int GetInteractTooltip(const BlockTooltipDataHolder&) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                           boost::shared_ptr<LivingEntity>) override;
    const BlockState* rotate(const BlockState*, Rotation*) override;
    const BlockState* mirror(const BlockState*, Mirror*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    void registerIcons(IconRegister*) override;
    TextureAtlasSprite* getTexture(const Direction*, const BlockState*) override;

private:
    std::array<TextureAtlasSprite*, 4> mTextures{};
};

#endif
