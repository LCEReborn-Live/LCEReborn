#include "game/world/block/WorkstationTableBlock.hpp"

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/world/block/Material.hpp"

WorkstationTableBlock::WorkstationTableBlock(const wchar_t* texturePrefix, const wchar_t* bottomTexture)
    : Block(Material::WOOD), mTexturePrefix(texturePrefix), mBottomTexture(bottomTexture)
{
    DerivedInit();
}

TextureAtlasSprite* WorkstationTableBlock::getTexture(const Direction* direction, const BlockState*)
{
    if (direction == Direction::UP)
        return mTop;
    if (direction == Direction::DOWN)
        return mBottom;
    if (direction == Direction::NORTH || direction == Direction::SOUTH)
        return mFront;
    return mIcon;
}

void WorkstationTableBlock::registerIcons(IconRegister* icons)
{
    mTop = icons->registerIcon(mTexturePrefix + L"_top");
    mBottom = icons->registerIcon(mBottomTexture);
    mFront = icons->registerIcon(mTexturePrefix + L"_front");
    mIcon = icons->registerIcon(mTexturePrefix + L"_side");
}
