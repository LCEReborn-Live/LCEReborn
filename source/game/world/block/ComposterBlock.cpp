#include "game/world/block/ComposterBlock.hpp"

#include <algorithm>
#include <utility>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/DyeColor.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/SimpleContainer.hpp"
#include "game/item/WorldlyContainer.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/LevelEvent.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DoublePlantBlock.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

AbstractProperty<int>* ComposterBlock::LEVEL =
    static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"level", 0, Ready));
constexpr int ComposterBlock::Full;
constexpr int ComposterBlock::Ready;

namespace
{
enum TextureRole { Bottom, Side, Rim, Compost, Ripe };

not_null_ptr<ItemInstance> boneMeal()
{
    return not_null_ptr<ItemInstance>(new ItemInstance(Items::dye, 1, DyeColor::WHITE->getItemData()));
}

class ComposterInventory : public SimpleContainer, public WorldlyContainer
{
public:
    ComposterInventory(ComposterBlock& block, Level* level, const BlockPos& pos, bool output)
        : SimpleContainer(L"", 1), mBlock(block), mLevel(level), mPos(pos), mOutput(output)
    {
        if (mOutput)
            setItem(0, boneMeal());
    }

    arrayWithLength<int> getSlotsForFace(const Direction* face) override
    {
        const auto* state = mLevel->getBlockState(mPos);
        static int slot = 0;
        if (state->getBlock() == &mBlock
            && (mOutput ? face == Direction::DOWN && state->getValue(ComposterBlock::LEVEL) == ComposterBlock::Ready
                        : face == Direction::UP && state->getValue(ComposterBlock::LEVEL) < ComposterBlock::Full))
            return arrayWithLength<int>(&slot, 1);
        return {};
    }

    bool canPlaceItem(int, not_null_ptr<ItemInstance> item) override
    {
        return getSlotsForFace(Direction::UP).length != 0 && item->getCount() == 1 && mBlock.compostChance(item) > 0;
    }

    bool canPlaceItemThroughFace(int, not_null_ptr<ItemInstance>, const Direction* face) override
    {
        return face == Direction::UP;
    }

    bool canTakeItemThroughFace(int, not_null_ptr<ItemInstance>, const Direction* face) override
    {
        return getSlotsForFace(face).length != 0 && mOutput;
    }

    not_null_ptr<ItemInstance> removeItem(unsigned int slot, int count) override
    {
        // A hopper first removes the item and restores it if the destination has no space.
        return mOutput && slot == 0 && count > 0 ? removeItemNoUpdate(0) : ItemInstance::EMPTY;
    }

    void setChanged() override
    {
        if (mOutput)
        {
            if (getItem(0)->isEmpty())
                mBlock.emptyCompost(mLevel, mPos);
        }
        else
        {
            auto item = removeItemNoUpdate(0);
            if (!item->isEmpty())
                mBlock.addCompost(mLevel, mPos, item);
        }
    }

private:
    ComposterBlock& mBlock;
    Level* mLevel;
    BlockPos mPos;
    bool mOutput;
};

std::vector<CuboidBlockModel::Part> fallbackModel()
{
    using Part = CuboidBlockModel::Part;
    const CuboidBlockModel::Box boxes[] = {
        {{ {0, 0, 0}, {16, 2, 16} }}, {{ {0, 2, 0}, {2, 16, 16} }},
        {{ {14, 2, 0}, {16, 16, 16} }}, {{ {2, 2, 0}, {14, 16, 2} }},
        {{ {2, 2, 14}, {14, 16, 16} }},
    };
    std::vector<Part> parts;
    for (const auto& box : boxes)
    {
        Part part{box, {}};
        for (auto& face : part.faces)
            face = {{0, 0, 16, 16}, Side, -1};
        part.faces[0].texture = Bottom;
        part.faces[1].texture = parts.empty() ? Bottom : Rim;
        if (!parts.empty())
            part.faces[1].uv = {box[0][0], box[0][2], box[1][0], box[1][2]};
        parts.push_back(part);
    }
    return parts;
}
}

ComposterBlock::ComposterBlock() : Block(Material::WOOD)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(LEVEL, 0));
    setLightBlock(0);

    auto parts = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/composter_model.bin", mTextures.size());
    constexpr size_t baseParts = 5;
    if (parts.size() == baseParts + mContents.size())
    {
        std::copy(parts.begin() + baseParts, parts.end(), mContents.begin());
        parts.resize(baseParts);
        mModel = std::move(parts);
    }
    else
    {
        mModel = fallbackModel();
        for (int level = 1; level <= Ready; ++level)
        {
            auto& part = mContents[level - 1];
            part.bounds = {{ {2, 0, 2}, {14, float(2 * std::min(level, Full) + 1), 14} }};
            part.faces[1] = {{2, 2, 14, 14}, level == Ready ? Ripe : Compost, 1};
        }
    }
}

int ComposterBlock::compostChance(not_null_ptr<ItemInstance> stack)
{
    if (stack->isEmpty())
        return 0;
    Item* item = stack->getItem();
    if (item == Items::cake || item == Items::pumpkinPie)
        return 100;
    Block* block = Block::byItem(item);
    if (block == Blocks::double_plant)
    {
        auto* variant = DoublePlantBlock::Variant::byData(stack->getAuxValue());
        return variant == DoublePlantBlock::Variant::GRASS || variant == DoublePlantBlock::Variant::FERN ? 50 : 65;
    }
    if (item == Items::bakedPotato || item == Items::bread || item == Items::cookie || block == Blocks::hay_block
        || block == Blocks::brown_mushroom_block || block == Blocks::red_mushroom_block)
        return 85;
    if (item == Items::apple || item == Items::beetroot || item == Items::carrot || item == Items::potato
        || item == Items::wheat || item == Items::sweetBerries
        || (item == Items::dye && stack->getAuxValue() == DyeColor::BROWN->getItemData())
        || block == Blocks::yellow_flower || block == Blocks::red_flower
        || block == Blocks::brown_mushroom || block == Blocks::red_mushroom || block == Blocks::pumpkin
        || block == Blocks::carved_pumpkin || block == Blocks::melon_block || block == Blocks::sea_pickle
        || block == Blocks::waterlily || (block == Blocks::tallgrass && stack->getAuxValue() == 2))
        return 65;
    if (item == Items::melon || item == Items::reeds || block == Blocks::cactus || block == Blocks::dried_kelp_block
        || block == Blocks::vine)
        return 50;
    if (item == Items::wheatSeeds || item == Items::beetrootSeeds || item == Items::melonSeeds
        || item == Items::pumpkinSeeds || item == Items::kelp || item == Items::driedKelp
        || block == Blocks::leaves || block == Blocks::leaves2 || block == Blocks::sapling || block == Blocks::sea_grass
        || (block == Blocks::tallgrass && stack->getAuxValue() == 1))
        return 30;
    return 0;
}

bool ComposterBlock::use(Level* level, const BlockPos& pos, const BlockState* state, boost::shared_ptr<Player> player,
                         InteractionHand::EInteractionHand hand, const Direction*, float, float, float, bool clientSide)
{
    const int fill = state->getValue(LEVEL);
    if (fill == Ready)
    {
        if (!clientSide && !level->mIsClientSide && emptyCompost(level, pos))
        {
            auto result = boost::shared_ptr<ItemEntity>(new ItemEntity(
                level, pos.x + 0.5, pos.y + 1.1, pos.z + 0.5, boneMeal()));
            result->setDefaultPickUpDelay();
            level->addEntity(result);
        }
        return true;
    }

    auto stack = player->getItemInHand(hand);
    if (!compostChance(stack))
        return false;
    if (fill < Full && !clientSide && !level->mIsClientSide && addCompost(level, pos, stack)
        && !player->mAbilities.mInstabuild)
        stack->shrink(1);
    return true;
}

unsigned int ComposterBlock::GetInteractTooltip(const BlockTooltipDataHolder& data)
{
    return (data.m_bUseBlock || data.m_bUseItemOnBlock) && data.m_bCanPlace
        ? IDS_TOOLTIPS_USE : Block::GetInteractTooltip(data);
}

void ComposterBlock::onPlace(Level* level, const BlockPos& pos, const BlockState* state)
{
    if (!level->mIsClientSide && state->getValue(LEVEL) == Full)
        level->addToTickNextTick(pos, this, 20);
}

const BlockState* ComposterBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                                       int, boost::shared_ptr<LivingEntity>)
{
    return defaultBlockState();
}

bool ComposterBlock::addCompost(Level* level, const BlockPos& pos, not_null_ptr<ItemInstance> stack)
{
    const auto* state = level->getBlockState(pos);
    const int chance = compostChance(stack);
    if (level->mIsClientSide || state->getBlock() != this || !chance || state->getValue(LEVEL) >= Full)
        return false;

    const int fill = state->getValue(LEVEL);
    const bool success = fill == 0 || level->mRandom->nextInt(100) < chance;
    if (success)
    {
        if (!setFillLevel(level, pos, state, fill + 1))
            return false;
        if (fill + 1 == Full)
            level->addToTickNextTick(pos, this, 20);
        level->levelEvent(LevelEvent::PARTICLES_PLANT_GROWTH, pos, 6);
    }
    const auto* sound = success ? SoundEvents::COMPOSTER_FILL_SUCCESS : SoundEvents::COMPOSTER_FILL;
    level->playSound({}, pos, sound, SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, 16.0f);
    return true;
}

bool ComposterBlock::emptyCompost(Level* level, const BlockPos& pos)
{
    const auto* state = level->getBlockState(pos);
    if (level->mIsClientSide || state->getBlock() != this || state->getValue(LEVEL) != Ready
        || !setFillLevel(level, pos, state, 0))
        return false;
    level->playSound({}, pos, SoundEvents::COMPOSTER_EMPTY, SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, 16.0f);
    return true;
}

void ComposterBlock::tick(Level* level, const BlockPos& pos, const BlockState* state, Random*)
{
    if (!level->mIsClientSide && state->getValue(LEVEL) == Full && setFillLevel(level, pos, state, Ready))
    {
        level->playSound({}, pos, SoundEvents::COMPOSTER_READY, SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, 16.0f);
    }
}

bool ComposterBlock::setFillLevel(Level* level, const BlockPos& pos, const BlockState* state, int fill)
{
    if (!level->setBlock(pos, state->setValue(LEVEL, fill), Block::UPDATE_CLIENTS, false))
        return false;
    level->updateNeighbourForOutputSignal(pos, this);
    return true;
}

boost::shared_ptr<Container> ComposterBlock::hopperContainer(Level* level, const BlockPos& pos, const BlockState* state)
{
    return boost::shared_ptr<Container>(new ComposterInventory(*this, level, pos, state->getValue(LEVEL) == Ready));
}

int ComposterBlock::getAnalogOutputSignal(const BlockState* state, Level*, const BlockPos&)
{
    return state->getValue(LEVEL);
}

const BlockState* ComposterBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(LEVEL, std::min(std::max(data, 0), Ready));
}

int ComposterBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(LEVEL);
}

BlockStateDefinition* ComposterBlock::createBlockStateDefinition()
{
    const Property* properties[] = {LEVEL};
    return new BlockStateDefinition(this, properties, 1);
}

EBlockFaceShape ComposterBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction* face)
{
    return face == Direction::UP ? BLOCK_FACE_UNDEFINED : BLOCK_FACE_SOLID;
}

void ComposterBlock::addCollisionAABBs(const BlockState* state, Level*, const BlockPos& pos, const AABB* query,
                                      std::vector<AABB*>* colliding, boost::shared_ptr<Entity>, bool)
{
    const auto addPart = [&](const auto& part)
    {
        const auto& low = part.bounds[0];
        const auto& high = part.bounds[1];
        addCollisionAABB(pos, query, colliding,
                         AABB::newTemp(low[0] / 16, low[1] / 16, low[2] / 16, high[0] / 16, high[1] / 16, high[2] / 16));
    };
    for (const auto& part : mModel)
        addPart(part);
    if (const auto* contents = getContents(state))
        addPart(*contents);
}

const CuboidBlockModel::Part* ComposterBlock::getContents(const BlockState* state) const
{
    const int fill = state->getValue(LEVEL);
    return fill ? &mContents[fill - 1] : nullptr;
}

TextureAtlasSprite* ComposterBlock::getTexture(const Direction* direction, const BlockState*)
{
    return mTextures[direction == Direction::UP ? Rim : direction == Direction::DOWN ? Bottom : Side];
}

void ComposterBlock::registerIcons(IconRegister* icons)
{
    const wchar_t* names[] = {L"composter_bottom", L"composter_side", L"composter_top", L"composter_compost",
                              L"composter_ready"};
    for (size_t i = 0; i < mTextures.size(); ++i)
        mTextures[i] = icons->registerIcon(names[i]);
    mIcon = mTextures[Side];
}
