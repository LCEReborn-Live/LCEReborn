#include "game/item/SignItem.hpp"
#include "game/core/EntityType.hpp"

#include "game/core/Direction.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Stats.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/ItemId.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/block/BlockItem.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/AbstractProperty.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/block/SignBlockEntity.hpp"
#include "game/world/block/StandingSignBlock.hpp"
#include "game/world/block/TypedBoxed.hpp"
#include "game/world/block/WallSignBlock.hpp"
#include "game/world/generation/Player.hpp"

SignItem::SignItem(int woodType) : mWoodType(woodType)
{
    mMaxStackSize = 16;
}

Item* SignItem::byWoodType(int woodType)
{
    return Item::byId(woodType == 0 ? ItemId::SIGN : ItemId::WOOD_SIGN_FIRST + woodType - 1);
}

InteractionResult::EInteractionResult SignItem::useOn(boost::shared_ptr<Player> player, Level* level,
                                                      const BlockPos& pos, InteractionHand::EInteractionHand hand,
                                                      const Direction* direction, float hitX, float hitY, float hitZ,
                                                      bool simulate)
{
    BlockPos signPos = pos;
    const BlockState* state = level->getBlockState(signPos);
    bool replaceable = state->getBlock()->mayReplaceWithPlace(level, signPos);

    if (direction == Direction::DOWN)
    {
        return InteractionResult::eInteractionResult_Fail;
    }

    if (!state->getMaterial()->isSolid() && !replaceable)
    {
        return InteractionResult::eInteractionResult_Fail;
    }

    if (replaceable && direction != Direction::UP)
    {
        return InteractionResult::eInteractionResult_Fail;
    }

    signPos = replaceable ? pos : pos.relative(direction);
    const bool standing = direction == Direction::UP;
    auto* signBlock = SignBlock::byWoodType(mWoodType, standing);

    not_null_ptr<ItemInstance> itemInstance = player->getItemInHand(hand);

    if (!player->mayUseItemAt(signPos, direction, itemInstance) || !signBlock->mayPlace(level, signPos))
    {
        return InteractionResult::eInteractionResult_Fail;
    }

    if (level->mIsClientSide)
    {
        return InteractionResult::eInteractionResult_Success;
    }

    if (!simulate)
    {
        const auto* signState = signBlock->defaultBlockState();
        if (standing)
        {
            int rotation = Mth::absFloor((double)((player->mYRot + 180.0f) * 16.0f / 360.0f) + 0.5) & 15;
            signState = signState->setValue(StandingSignBlock::ROTATION, rotation);
        }
        else
        {
            signState = signState->setValue(WallSignBlock::FACING, direction);
        }

        if (!level->setBlock(signPos, signState, Block::UPDATE_ALL | Block::UPDATE_IMMEDIATE, false))
            return InteractionResult::eInteractionResult_Fail;

        boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(signPos);

        if (blockEntity && eTYPE_DERIVED_FROM(BlockEntityType::SIGN, blockEntity->GetType()))
        {
            if (BlockItem::updateCustomBlockEntityTag(level, player, signPos, itemInstance))
            {
                player->openTextEdit(boost::dynamic_pointer_cast<SignBlockEntity>(blockEntity));
            }
        }

        itemInstance->shrink(1);

        player->awardStat(GenericStats::blocksPlaced(signBlock),
                          GenericStats::param_blocksPlaced(signState, 1));
    }

    return InteractionResult::eInteractionResult_Success;
}
