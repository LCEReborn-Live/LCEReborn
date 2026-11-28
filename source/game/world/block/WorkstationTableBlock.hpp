#ifndef GAME_WORLD_BLOCK_WORKSTATIONTABLEBLOCK_HPP
#define GAME_WORLD_BLOCK_WORKSTATIONTABLEBLOCK_HPP

#include <string>
#include "game/world/block/Block.hpp"

class WorkstationTableBlock : public Block
{
public:
    WorkstationTableBlock(const wchar_t* texturePrefix, const wchar_t* bottomTexture);
    TextureAtlasSprite* getTexture(const Direction* direction, const BlockState* state) override;
    void registerIcons(IconRegister* icons) override;

private:
    const std::wstring mTexturePrefix;
    const std::wstring mBottomTexture;
    TextureAtlasSprite* mTop = nullptr;
    TextureAtlasSprite* mBottom = nullptr;
    TextureAtlasSprite* mFront = nullptr;
};

#endif
