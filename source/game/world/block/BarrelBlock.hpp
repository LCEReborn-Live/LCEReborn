#ifndef GAME_WORLD_BLOCK_BARRELBLOCK_HPP
#define GAME_WORLD_BLOCK_BARRELBLOCK_HPP

#include "game/world/block/BaseEntityBlock.hpp"

class DirectionProperty;

class BarrelBlock : public BaseEntityBlock
{
public:
    static DirectionProperty* FACING;
    static AbstractProperty<bool>* OPEN;

    BarrelBlock();
    boost::shared_ptr<BlockEntity> newBlockEntity(Level* level, int data) override;
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>,
             InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide) override;
    unsigned int GetInteractTooltip(const BlockTooltipDataHolder& data) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                           boost::shared_ptr<LivingEntity>) override;
    void setPlacedBy(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<LivingEntity>,
                     not_null_ptr<ItemInstance>) override;
    void onRemove(Level*, const BlockPos&, const BlockState*) override;
    bool hasAnalogOutputSignal(const BlockState*) override { return true; }
    int getAnalogOutputSignal(const BlockState*, Level*, const BlockPos&) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState* state) override;
    const BlockState* rotate(const BlockState* state, Rotation* rotation) override;
    const BlockState* mirror(const BlockState* state, Mirror* mirror) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_MODEL; }
    TextureAtlasSprite* getTexture(const Direction* direction, const BlockState* state) override;
    void registerIcons(IconRegister* icons) override;

private:
    TextureAtlasSprite* mLid = nullptr;
    TextureAtlasSprite* mOpenLid = nullptr;
    TextureAtlasSprite* mBottom = nullptr;
};

#endif
