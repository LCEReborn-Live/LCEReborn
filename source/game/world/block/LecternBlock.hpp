#ifndef GAME_WORLD_BLOCK_LECTERNBLOCK_HPP
#define GAME_WORLD_BLOCK_LECTERNBLOCK_HPP

#include "game/item/ItemInstance.hpp"
#include "game/world/block/BaseEntityBlock.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class LecternBlockEntity : public BlockEntity
{
public:
    static constexpr int UpdatePacketType = 15;
    static BlockEntity* create();
    unsigned long long GetType() override;
    boost::shared_ptr<BlockEntity> clone() override;
    void load(CompoundTag*) override;
    CompoundTag* save(CompoundTag*) override;
    CompoundTag* getUpdateTag() override;
    boost::shared_ptr<Packet> getUpdatePacket() override;
    const not_null_ptr<ItemInstance>& book() const { return mBook; }
    int page() const { return mPage; }
    int pageCount() const;
    void setBook(not_null_ptr<ItemInstance>);
    bool setPage(int page);

private:
    not_null_ptr<ItemInstance> mBook = ItemInstance::EMPTY;
    int mPage = 0;
};

class LecternBlock : public BaseEntityBlock
{
public:
    LecternBlock();
    static const wchar_t* const PageMessage;
    static const Direction* facing(const BlockState*);
    static CuboidBlockModel::Point orientPoint(const BlockState*, CuboidBlockModel::Point);
    boost::shared_ptr<BlockEntity> newBlockEntity(Level*, int) override;
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_LECTERN; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    void addCollisionAABBs(const BlockState*, Level*, const BlockPos&, const AABB*, std::vector<AABB*>*,
                           boost::shared_ptr<Entity>, bool) override;
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>, InteractionHand::EInteractionHand,
             const Direction*, float, float, float, bool) override;
    void onRemove(Level*, const BlockPos&, const BlockState*) override;
    bool turnPage(Level*, const BlockPos&, int);
    void tick(Level*, const BlockPos&, const BlockState*, Random*) override;
    bool isSignalSource(const BlockState*) override { return true; }
    int getSignal(const BlockState*, LevelSource*, const BlockPos&, const Direction*) override;
    int getDirectSignal(const BlockState*, LevelSource*, const BlockPos&, const Direction*) override;
    bool hasAnalogOutputSignal(const BlockState*) override { return true; }
    int getAnalogOutputSignal(const BlockState*, Level*, const BlockPos&) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                           int, boost::shared_ptr<LivingEntity>) override;
    const BlockState* getBlockState(int) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    const BlockState* rotate(const BlockState*, Rotation*) override;
    const BlockState* mirror(const BlockState*, Mirror*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    void registerIcons(IconRegister*) override;
    const std::vector<CuboidBlockModel::Part>& getModel() const { return mModel; }
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }

private:
    std::vector<CuboidBlockModel::Part> mModel;
    std::array<TextureAtlasSprite*, 5> mTextures{};
};

#endif
