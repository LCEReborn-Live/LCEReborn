#ifndef GAME_UI_FUIRENDERNODECODEGENRECT_HPP
#define GAME_UI_FUIRENDERNODECODEGENRECT_HPP

#include "game/ui/fuiRenderNode.hpp"

class fuiRenderNodeCodeGenRect : public fuiRenderNode
{
public:
    fuiRenderNodeCodeGenRect(fuiRenderNode* parent, fuiRect* rect, const char* name, unsigned int textureWidth,
                             unsigned int textureHeight, int textureId, bool customTexture, bool textureAlpha);
    fuiRenderNodeCodeGenRect(fuiRenderNode* parent, fuiRect* rect, const char* name);
    virtual ~fuiRenderNodeCodeGenRect();
    virtual void gather(bool recurse);
    virtual void preRender();
    virtual void render(fuiRect* bounds, fuiMatrix* matrix);
    virtual void setWidth(float width);
    virtual void setHeight(float height);
    void setTexture(unsigned int textureWidth, unsigned int textureHeight, int textureId, bool textureAlpha);
    void setSolidColor(float red, float green, float blue, float alpha)
    { mSolidRed = red; mSolidGreen = green; mSolidBlue = blue; mSolidAlpha = alpha; }

private:
    int mUnusedE4;
    float mSolidRed = 1, mSolidGreen = 0, mSolidBlue = 0, mSolidAlpha = 0;
    int mTextureId;
    unsigned char mCustomTexture;
    unsigned char mTextureAlpha;
    unsigned int mTextureWidth;
    unsigned int mTextureHeight;
};

#endif
