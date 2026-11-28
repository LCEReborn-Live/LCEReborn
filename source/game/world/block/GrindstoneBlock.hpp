#ifndef GAME_WORLD_BLOCK_GRINDSTONEBLOCK_HPP
#define GAME_WORLD_BLOCK_GRINDSTONEBLOCK_HPP

#include <array>
#include "game/world/block/CuboidBlockModel.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"

class GrindstoneBlock : public HorizontalDirectionalBlock
{
public:
    using ModelPoint = CuboidBlockModel::Point;
    using ModelBox = CuboidBlockModel::Box;
    using ModelPart = CuboidBlockModel::Part;

    GrindstoneBlock();
    static ModelPoint orientPoint(const BlockState* state, ModelPoint point);
    const std::vector<ModelPart>& getModel() const { return mModel; }
    TextureAtlasSprite* getModelTexture(int index) const { return mModelTextures[index]; }
    TextureAtlasSprite* const* getModelTextures() const { return mModelTextures.data(); }
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>, InteractionHand::EInteractionHand,
             const Direction*, float, float, float, bool clientSide) override;
    unsigned int GetInteractTooltip(const BlockTooltipDataHolder& data) override;
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_GRINDSTONE; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override
    { return BLOCK_FACE_UNDEFINED; }
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    void addCollisionAABBs(const BlockState*, Level*, const BlockPos&, const AABB*, std::vector<AABB*>*,
                           boost::shared_ptr<Entity>, bool) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState* state) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                           boost::shared_ptr<LivingEntity>) override;
    const BlockState* rotate(const BlockState* state, Rotation* rotation) override;
    const BlockState* mirror(const BlockState* state, Mirror* mirror) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    void registerIcons(IconRegister* icons) override;

private:
    static AbstractProperty<int>* MOUNT;
    std::vector<ModelPart> mModel;
    std::array<TextureAtlasSprite*, 4> mModelTextures{};
    bool mImportedModel = false;
};

#endif
