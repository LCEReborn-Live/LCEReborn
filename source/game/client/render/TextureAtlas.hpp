#ifndef GAME_CLIENT_RENDER_TEXTUREATLAS_HPP
#define GAME_CLIENT_RENDER_TEXTUREATLAS_HPP

#include <string>
#include <vector>
#include <boost/unordered_map.hpp>

#include "game/core/BufferedImage.hpp"
#include "game/core/IconRegister.hpp"
#include "game/client/render/Texture.hpp"
#include "game/client/render/TextureAtlasSprite.hpp"
#include "game/client/render/TextureManager.hpp"
#include "game/client/render/TexturePack.hpp"

class TextureAtlas : public IconRegister
{
public:
    TextureAtlas(int iconType, const std::wstring& name, const std::wstring& path, BufferedImage* image,
                 bool singleImage);

    void addIcon(bool singleImage, int x, int y, const std::wstring& name);
    void addIconWithName(bool singleImage, int x, int y, const std::wstring& name, const std::wstring& spriteName);
    void addIconSize(bool singleImage, int x, int y, const std::wstring& name, int width, int height);
    void loadUVs();
    void makeTextureAnimated(TexturePack* pack, TextureAtlasSprite* sprite);
    void stitch();
    void freeStitch();
    void cycleAnimationFrames();
    Texture* getStitchedTexture();
    virtual TextureAtlasSprite* registerIcon(const std::wstring& name);
    virtual int getIconType();
    TextureAtlasSprite* getMissingIcon();
    TextureAtlasSprite* getTexture(const std::wstring& name);

private:
    struct SpriteCell
    {
        SpriteCell(int column = 0, int row = 0, int width = 1, int height = 1)
            : column(column), row(row), width(width), height(height) {}
        int column, row, width, height;
    };

    int mIconType;
    std::wstring mName;
    std::wstring mPath;
    std::wstring mSpritePrefix;
    bool mSingleImage;
    boost::unordered_map<std::wstring, TextureAtlasSprite*> mSprites;
    boost::unordered_map<std::wstring, SpriteCell> mSpriteCells;
    BufferedImage* mImage;
    TextureAtlasSprite* mMissingIcon;
    Texture* mStitchedTexture;
    std::vector<TextureAtlasSprite*> mAnimatedSprites;
};

#endif
