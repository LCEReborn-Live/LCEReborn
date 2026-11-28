#include "game/item/SeedFoodItem.hpp"

#include "game/entity/mob/LivingEntity.hpp"
#include "game/world/generation/Player.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/core/Direction.hpp"

SeedFoodItem::SeedFoodItem(int nutrition, float saturationModifier, Block* cropBlock, Block* soilBlock)
    : FoodItem(nutrition, saturationModifier, false),
      mCropBlock(cropBlock),
      mSoilBlock(soilBlock)
{
}

InteractionResult::EInteractionResult SeedFoodItem::useOn(boost::shared_ptr<Player> player, Level* level,
                                                          const BlockPos& pos, InteractionHand::EInteractionHand hand,
                                                          const Direction* direction, float hitX, float hitY,
                                                          float hitZ, bool simulate)
{
    not_null_ptr<ItemInstance> itemInstance = player->getItemInHand(hand);
    const BlockPos target = pos.above();
    if (direction != Direction::UP || !player->mayUseItemAt(target, direction, itemInstance)
        || !level->isEmptyBlock(target))
        return InteractionResult::eInteractionResult_Fail;
    if (mSoilBlock ? level->getBlockState(pos)->getBlock() != mSoilBlock : !mCropBlock->mayPlace(level, target))
    {
        return InteractionResult::eInteractionResult_Fail;
    }

    if (!simulate)
    {
        if (!level->setBlock(target, mCropBlock->defaultBlockState(), Block::UPDATE_ALL | Block::UPDATE_IMMEDIATE, false))
            return InteractionResult::eInteractionResult_Fail;
        itemInstance->shrink(1);
    }
    return InteractionResult::eInteractionResult_Success;
}
