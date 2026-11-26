#include "game/ui/fuiRenderNodeBitmap.hpp"

#include "game/client/render/C4JRender.hpp"
#include "game/client/render/Renderer.hpp"
#include "game/ui/fui.hpp"
#include "game/ui/fuiBitmap.hpp"
#include "game/ui/fuiFile.hpp"

fuiRenderNodeBitmap::fuiRenderNodeBitmap(fuiRenderNode* parent, fuiRenderNode* root, fuiObject* object,
                                         unsigned int symbolIndex, unsigned char depth, fuiRGBA* color,
                                         unsigned char clipDepth, bool visible, fuiFile* file)
    : fuiRenderNode(parent, root, object, symbolIndex, eFuiObjectType_Bitmap, depth, color, clipDepth, visible, file)
{
    fuiBitmap* bitmap = static_cast<fuiBitmap*>(m_object);
    m_bounds.xMin = 0.0f;
    m_bounds.xMax = (float)bitmap->width * file->getBitmapCoordinateScale(bitmap);
    m_bounds.yMin = 0.0f;
    m_bounds.yMax = (float)bitmap->height * file->getBitmapCoordinateScale(bitmap);
}

fuiRenderNodeBitmap::~fuiRenderNodeBitmap()
{
}

void fuiRenderNodeBitmap::gather(bool)
{
}

void fuiRenderNodeBitmap::preRender()
{
}

void fuiRenderNodeBitmap::render(fuiRect* bounds, fuiMatrix* matrix)
{
    if (!isVisible())
    {
        return;
    }

    pushMatrix();
    pushColor();
    pushFilterFlags();

    {
        fuiBitmap* bitmap = static_cast<fuiBitmap*>(m_object);
        const float width = bitmap->width * m_file->getBitmapCoordinateScale(bitmap);
        const float height = bitmap->height * m_file->getBitmapCoordinateScale(bitmap);
        float vertices[12];
        vertices[0] = 0.0f;
        vertices[1] = 0.0f;
        vertices[2] = width;
        vertices[3] = height;
        vertices[4] = 0.0f;
        vertices[5] = height;
        vertices[6] = 0.0f;
        vertices[7] = 0.0f;
        vertices[8] = width;
        vertices[9] = 0.0f;
        vertices[10] = width;
        vertices[11] = height;

        m_file->initialiseRenderTexture(bitmap, true);
        gRenderer->TextureBind(bitmap->textureId);
        gRenderer->StateSetTextureScaleUI(1.0f / width, 1.0f / height, 0.0f, 0.0f);

        const float redMultiply = gFui->m_colorStack[gFui->m_colorDepth].redMultiply;
        const float greenMultiply = gFui->m_colorStack[gFui->m_colorDepth].greenMultiply;
        const float blueMultiply = gFui->m_colorStack[gFui->m_colorDepth].blueMultiply;
        const float alphaMultiply = gFui->m_colorStack[gFui->m_colorDepth].alphaMultiply;
        const float redAdd = gFui->m_colorStack[gFui->m_colorDepth].redAdd;
        const float greenAdd = gFui->m_colorStack[gFui->m_colorDepth].greenAdd;
        const float blueAdd = gFui->m_colorStack[gFui->m_colorDepth].blueAdd;
        const float alphaAdd = gFui->m_colorStack[gFui->m_colorDepth].alphaAdd;

        if (gFui->m_filterStack[gFui->m_filterDepth] & 1)
        {
            gRenderer->TextureSetParam(1, 1);
            gRenderer->TextureSetParam(2, 1);
        }
        else
        {
            gRenderer->TextureSetParam(1, 0);
            gRenderer->TextureSetParam(2, 0);
        }
        gRenderer->StateSetColour(redMultiply, greenMultiply, blueMultiply, alphaMultiply);
        gRenderer->StateSetColourAdd(redAdd, greenAdd, blueAdd, alphaAdd);
        gRenderer->DrawVertices(C4JRender::ePrimitiveType_Triangles, 6, vertices, C4JRender::eVertexType_UI,
                                C4JRender::ePixelShaderType_UIColourAdd, NULL);
    }

    popFilterFlags();
    popColor();
    popMatrix();
}
