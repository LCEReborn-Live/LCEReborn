#include "game/client/render/Renderer.hpp"

#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <thread>
#include <vector>

#include "game/core/VideoSettings.hpp"
#include "platform/FileApi.hpp"
#include "platform/pc/GlApi.hpp"
#include "platform/pc/Log.hpp"
#include "platform/pc/PcCrashReport.hpp"
#include "platform/pc/PcKeyboardOverlay.hpp"
#include "platform/pc/PcFrameCapture.hpp"
#include "platform/pc/PcWindow.hpp"
#include "platform/pc/RenderShaders.hpp"
#include "platform/pc/shaderpack/ShaderPipeline.hpp"

struct D3DXIMAGE_INFO
{
    unsigned int Width;
    unsigned int Height;
    unsigned int Depth;
    unsigned int MipLevels;
    unsigned int Format;
    unsigned int ResourceType;
    unsigned int ImageFileFormat;
};

struct RenderSamplerState
{
    unsigned char mClampU;
    unsigned char mClampV;
    unsigned char mMagLinear;
    unsigned char mMinLinear;
    unsigned char mMipFilter;
};

namespace
{
const Logger sLog("Renderer");
std::string sGpuName;
const int TEXTURE_COUNT = 512;
const int TEXTURE_STAGE_COUNT = 4;
const int MAX_CHUNK_GRID_WIDTH = 2 * VideoSettings::RENDER_DISTANCE_MAX;
const int COMMAND_BUFFER_COUNT = MAX_CHUNK_GRID_WIDTH * MAX_CHUNK_GRID_WIDTH * 16 * 3 + 16000;
const int COMMAND_BUFFER_ID_COUNT = 0x800000;
const unsigned int COMMAND_BUFFER_INITIAL_CAPACITY = 0x1000;
const unsigned int DYNAMIC_VERTEX_BUDGET = 0x200000;
const int BONE_COUNT = 48;
const int VERTEX_CONSTANT_COUNT = 256;
const unsigned int STREAM_VERTEX_BUFFER_SIZE = 32 * 1024 * 1024;
const unsigned int STREAM_CONSTANT_BUFFER_SIZE = 8 * 1024 * 1024;
const int UI_WIDTH = 1280;
const int UI_HEIGHT = 720;
const int DRC_WIDTH = 854;
const int DRC_HEIGHT = 480;
const int THUMBNAIL_LEVELS = 4;
const int THUMBNAIL_SIZE = 0x200;
const int THUMBNAIL_OUTPUT_SIZE = 0x40;
const unsigned short DEFAULT_GAMMA = 0x2AAA;

const unsigned int VERTEX_REG_WORLD_VIEW = 0x00;
const unsigned int VERTEX_REG_WORLD_VIEW2 = 0x10;
const unsigned int VERTEX_REG_PROJECTION = 0x20;
const unsigned int VERTEX_REG_TEXTURE = 0x30;
const unsigned int VERTEX_REG_VERTEX_TEXTURE_UV = 0x40;
const unsigned int VERTEX_REG_FOG = 0x44;
const unsigned int VERTEX_REG_LIGHTING = 0x48;
const unsigned int VERTEX_REG_TEXGEN_EYE = 0x5C;
const unsigned int VERTEX_REG_TEXGEN_OBJECT = 0x6C;
const unsigned int VERTEX_REG_UI_TEXTURE_SCALE = 0x84;
const unsigned int VERTEX_REG_SCALING_GRID_X = 0x88;
const unsigned int VERTEX_REG_SCALING_GRID_Y = 0x8C;
const unsigned int VERTEX_REG_SCALING_GRID_SCALE = 0x90;
const unsigned int VERTEX_REG_VERTEX_ALPHA = 0xA0;
const unsigned int VERTEX_REG_BONES = 0xA4;

const unsigned int PIXEL_REG_COLOURS = 0x00;
const unsigned int PIXEL_REG_FORCE_LOD = 0x20;
const unsigned int PIXEL_REG_LAYERED = 0x24;

const int COMPARE_LEQUAL = 3;
const int COMPARE_ALWAYS = 7;
const int BLEND_ONE = 1;

const unsigned int TEXTURE_FLAG_CLAMP_U = 1;
const unsigned int TEXTURE_FLAG_CLAMP_V = 2;
const unsigned int TEXTURE_FLAG_MIN_LINEAR = 4;
const unsigned int TEXTURE_FLAG_MAG_LINEAR = 8;

const unsigned char MIP_FILTER_NONE = 0;
const unsigned char MIP_FILTER_POINT = 1;
const unsigned char MIP_FILTER_LINEAR = 2;

const unsigned char CHANNEL_MASK_RGBA = 0xF;

const unsigned int VERTEX_STRIDES[C4JRender::eVertexType_Count] = {0x20, 0x10, 0x20, 0x20, 8, 8, 8, 0x14, 0x20, 0x20};

enum VertexLayout
{
    VertexLayout_Standard,
    VertexLayout_Terrain,
    VertexLayout_Position,
    VertexLayout_ColouredUI,
    VertexLayout_Count
};

enum RenderTarget
{
    RenderTarget_Main,
    RenderTarget_OrderIndependentAlpha
};

int LayoutForVertexType(int vertexType)
{
    switch (vertexType)
    {
    case C4JRender::eVertexType_Terrain:
        return VertexLayout_Terrain;
    case C4JRender::eVertexType_UI:
    case C4JRender::eVertexType_ScalingGridUI:
    case C4JRender::eVertexType_TiledUI:
        return VertexLayout_Position;
    case C4JRender::eVertexType_ColouredUI:
        return VertexLayout_ColouredUI;
    default:
        return VertexLayout_Standard;
    }
}

GLenum CompareFunction(int function)
{
    return Gl::NEVER + (GLenum)(function & 7);
}

GLenum BlendFunction(int function)
{
    switch (function)
    {
    case 0:
        return Gl::ZERO;
    case 1:
        return Gl::ONE;
    case 2:
        return Gl::SRC_COLOR;
    case 3:
        return Gl::ONE_MINUS_SRC_COLOR;
    case 4:
        return Gl::SRC_ALPHA;
    case 5:
        return Gl::ONE_MINUS_SRC_ALPHA;
    case 6:
        return Gl::DST_ALPHA;
    case 7:
        return Gl::ONE_MINUS_DST_ALPHA;
    case 8:
        return Gl::DST_COLOR;
    case 9:
        return Gl::ONE_MINUS_DST_COLOR;
    case 10:
        return Gl::SRC_ALPHA_SATURATE;
    case 11:
        return Gl::SRC_ALPHA;
    case 12:
        return Gl::ONE_MINUS_SRC_ALPHA;
    case 13:
        return Gl::CONSTANT_COLOR;
    case 14:
        return Gl::ONE_MINUS_CONSTANT_COLOR;
    case 15:
        return Gl::SRC1_COLOR;
    case 16:
        return Gl::ONE_MINUS_SRC1_COLOR;
    case 17:
        return Gl::SRC1_ALPHA;
    case 18:
        return Gl::ONE_MINUS_SRC1_ALPHA;
    case 19:
        return Gl::CONSTANT_ALPHA;
    case 20:
        return Gl::ONE_MINUS_CONSTANT_ALPHA;
    default:
        return Gl::ONE;
    }
}

GLenum PrimitiveMode(int primitiveType)
{
    switch (primitiveType)
    {
    case C4JRender::ePrimitiveType_TriangleStrip:
        return Gl::TRIANGLE_STRIP;
    case C4JRender::ePrimitiveType_TriangleFan:
        return Gl::TRIANGLE_FAN;
    case C4JRender::ePrimitiveType_Lines:
        return Gl::LINES;
    case C4JRender::ePrimitiveType_LineStrip:
        return Gl::LINE_STRIP;
    default:
        return Gl::TRIANGLES;
    }
}

void FatalRendererError(const char* message)
{
    LOG_FATAL(sLog, "%s", message);
    PcCrashReport::Write(message);
    Log::Stop();
    abort();
}

void MatrixIdentity(float* matrix)
{
    memset(matrix, 0, sizeof(float) * 16);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

void MatrixIdentity34(float* matrix)
{
    memset(matrix, 0, sizeof(float) * 12);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
}

void MatrixConcat(const float* a, const float* b, float* ab)
{
    float result[16];

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            result[row * 4 + column] = a[row * 4 + 0] * b[0 * 4 + column] + a[row * 4 + 1] * b[1 * 4 + column]
                                       + a[row * 4 + 2] * b[2 * 4 + column] + a[row * 4 + 3] * b[3 * 4 + column];
        }
    }

    memcpy(ab, result, sizeof(result));
}

void MatrixTranspose(const float* source, float* destination)
{
    float result[16];

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            result[column * 4 + row] = source[row * 4 + column];
        }
    }

    memcpy(destination, result, sizeof(result));
}

bool MatrixInverse(const float* source, float* inverse)
{
    float work[16];
    memcpy(work, source, sizeof(work));
    MatrixIdentity(inverse);

    for (int pivot = 0; pivot < 4; ++pivot)
    {
        float largest = 0.0f;
        int swap = pivot;

        for (int row = pivot; row < 4; ++row)
        {
            float magnitude = fabsf(work[row * 4 + pivot]);
            if (magnitude > largest)
            {
                largest = magnitude;
                swap = row;
            }
        }

        if (largest == 0.0f)
        {
            return false;
        }

        if (swap != pivot)
        {
            for (int column = 0; column < 4; ++column)
            {
                float swapped = work[pivot * 4 + column];
                work[pivot * 4 + column] = work[swap * 4 + column];
                work[swap * 4 + column] = swapped;
                swapped = inverse[pivot * 4 + column];
                inverse[pivot * 4 + column] = inverse[swap * 4 + column];
                inverse[swap * 4 + column] = swapped;
            }
        }

        float scale = 1.0f / work[pivot * 4 + pivot];
        for (int column = 0; column < 4; ++column)
        {
            work[pivot * 4 + column] *= scale;
            inverse[pivot * 4 + column] *= scale;
        }

        for (int row = 0; row < 4; ++row)
        {
            if (row != pivot)
            {
                float factor = work[row * 4 + pivot];
                for (int column = 0; column < 4; ++column)
                {
                    work[row * 4 + column] -= work[pivot * 4 + column] * factor;
                    inverse[row * 4 + column] -= inverse[pivot * 4 + column] * factor;
                }
            }
        }
    }

    return true;
}

void NormalizeVector(float* vector)
{
    float magnitude = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    float scale = 1.0f / sqrtf(magnitude);
    vector[0] *= scale;
    vector[1] *= scale;
    vector[2] *= scale;
}

int ClampTextureLevels(int levels, int width, int height)
{
    int largest = width > height ? width : height;
    int maximum = 1;

    while (largest > 1)
    {
        largest >>= 1;
        ++maximum;
    }

    if (levels < 1)
    {
        levels = 1;
    }

    return levels > maximum ? maximum : levels;
}

void* AlignedAllocate(unsigned int size, unsigned int alignment)
{
    if (alignment < sizeof(void*))
    {
        alignment = sizeof(void*);
    }

    unsigned char* block = static_cast<unsigned char*>(malloc(size + alignment + sizeof(void*)));
    if (block == NULL)
    {
        return NULL;
    }

    uintptr_t start = reinterpret_cast<uintptr_t>(block + sizeof(void*));
    uintptr_t aligned = (start + alignment - 1) & ~static_cast<uintptr_t>(alignment - 1);
    reinterpret_cast<void**>(aligned)[-1] = block;
    return reinterpret_cast<void*>(aligned);
}

void AlignedFree(void* memory)
{
    if (memory != NULL)
    {
        free(reinterpret_cast<void**>(memory)[-1]);
    }
}

RenderSamplerState MakeSampler(bool bClamp, bool bLinear, unsigned char mipFilter)
{
    RenderSamplerState sampler;
    sampler.mClampU = bClamp ? 1 : 0;
    sampler.mClampV = bClamp ? 1 : 0;
    sampler.mMagLinear = bLinear ? 1 : 0;
    sampler.mMinLinear = bLinear ? 1 : 0;
    sampler.mMipFilter = mipFilter;
    return sampler;
}

struct PngMemoryWriter
{
    unsigned char* mStart;
    unsigned char* mCurrent;
    unsigned char* mEnd;
};

void PngWriteToMemory(png_structp png, png_bytep data, png_size_t size)
{
    PngMemoryWriter* writer = static_cast<PngMemoryWriter*>(png_get_io_ptr(png));
    size_t available = static_cast<size_t>(writer->mEnd - writer->mCurrent);
    size_t count = size < available ? size : available;
    memcpy(writer->mCurrent, data, count);
    writer->mCurrent += count;
}

void PngFlushMemory(png_structp)
{
}

thread_local Renderer* sContextOwner = NULL;
thread_local void* sContext = NULL;
}

Renderer* gRendererInstance = NULL;

static C4JCriticalSection sTotalAllocMutex;
static int sTotalCommandBufferBytes = 0;

Renderer* gRenderer = new Renderer;

struct Renderer::Texture
{
    bool mUsed;
    bool mHasStorage;
    int mWidth;
    int mHeight;
    int mLevels;
    int mStorageLevels;
    C4JRender::eTextureFormat mFormat;
    unsigned int mFlags;
    RenderSamplerState mSampler;
    GLuint mName;
    unsigned int mGeneration;
    unsigned int mContentVersion;
};

struct Renderer::GpuState
{
    bool mDepthTest;
    bool mDepthWrite;
    int mDepthFunc;
    int mStencilFunc;
    unsigned char mStencilReference;
    unsigned char mStencilCompareMask;
    unsigned char mStencilWriteMask;
    bool mCullBack;
    bool mFrontFaceCCW;
    float mPolygonOffsetScale;
    float mPolygonOffsetBias;
    bool mBlendEnable;
    int mBlendSource;
    int mBlendDestination;
    float mBlendColour[4];
    bool mAlphaTestOn;
    float mAlphaReference;
    unsigned char mColourMask;
    float mViewport[4];
    int mScissor[4];
    int mPixelTexture[TEXTURE_STAGE_COUNT];
    RenderSamplerState mPixelSampler[TEXTURE_STAGE_COUNT];
    int mVertexTexture;
    RenderSamplerState mVertexSampler;
    int mVertexShader;
    int mPixelShader;
};

struct Renderer::DeferredCBuff
{
    DeferredCBuff(int id, CommandBuffer* buffer, int vertexType, C4JRender::ePixelShaderType pixelShaderType,
                  int primitiveType, const float* matrix);

    CommandBuffer* mBuffer;
    int mId;
    int mVertexType;
    C4JRender::ePixelShaderType mPixelShaderType;
    int mPrimitiveType;
    float mMatrix[16];
};

struct Renderer::Context
{
    Context(Renderer* renderer);

    Renderer* mRenderer;
    float mMatrixStack[4][16][16];
    bool mMatrixDirty[4];
    int mMatrixStackDepth[4];
    int mMatrixMode;
    int mBoundTexture[TEXTURE_STAGE_COUNT];
    bool mTextureIsMask[TEXTURE_STAGE_COUNT];
    bool mTextureIsVertex[TEXTURE_STAGE_COUNT];
    bool mFaceCull;
    bool mAlphaTestEnable;
    float mAlphaTestRef;
    bool mDepthMask;
    bool mDepthTestEnable;
    int mStencilFunc;
    int mDepthFunc;
    int mBlendSource;
    int mBlendDestination;
    bool mBlendEnable;
    bool mFogEnable;
    float mFogNearDistance;
    float mFogFarDistance;
    float mFogDensity;
    int mFogMode;
    bool mLightingEnable;
    bool mLightEnable[2];
    bool mLightingDirty;
    int mForceLOD;
    bool mOrderIndependentAlpha;
    float mVertexAlphaDistance;
    float mVertexAlpha;
    float mLightState[5][4];
    float mBoneMatrix[BONE_COUNT][16];
    int mNumBoneMatrices;
    unsigned int mDynamicVertexOffset;
    float mTexGenMatrix[2][4][4];
    CommandBuffer* mRecordingBuffer;
    int mRecordingId;
    C4JRender::eVertexType mRecordingVertexType;
    C4JRender::ePixelShaderType mRecordingPixelShaderType;
    C4JRender::ePrimitiveType mRecordingPrimitiveType;
    bool mDeferredMode;
    std::vector<DeferredCBuff> mDeferredBuffers;
    float mLastQuadUniform[4];
    const unsigned short* mPendingQuadData;
    float mViewportScale[4];
    float mLayeredColour[16][4];
    unsigned int mScissorRect[4];
    GpuState mGpu;
    float mVertexConstants[VERTEX_CONSTANT_COUNT][4];
    bool mVertexConstantsDirty;
    float mPixelConstants[RENDER_PIXEL_CONSTANT_COUNT][4];
    bool mPixelConstantsDirty;
};

class Renderer::CommandBuffer
{
public:
    enum eCommandType
    {
        eCommandType_Matrix,
        eCommandType_Vertices,
        eCommandType_BindTexture,
        eCommandType_Colour,
        eCommandType_DepthFunc,
        eCommandType_DepthMask,
        eCommandType_DepthTestEnable,
        eCommandType_LightingEnable,
        eCommandType_LightEnable,
        eCommandType_LightDirection,
        eCommandType_LightColour,
        eCommandType_LightAmbientColour,
        eCommandType_BlendEnable,
        eCommandType_BlendFunc,
        eCommandType_BlendFactor,
        eCommandType_FaceCull
    };

    struct Command
    {
        eCommandType mType;

        union
        {
            float mMatrix[16];
            float mColour[4];
            int mTexture;
            int mDepthFunc;
            bool mEnable;
            unsigned int mBlendFactor;

            struct
            {
                unsigned int mOffset;
                unsigned int mCount;
                unsigned char mLayer;
            } mVertices;

            struct
            {
                int mIndex;
                bool mEnable;
            } mLightEnable;

            struct
            {
                int mIndex;
                float mValue[4];
            } mLight;

            struct
            {
                int mSource;
                int mDestination;
            } mBlendFunc;
        };
    };

    CommandBuffer(bool bRecordState);
    ~CommandBuffer();

    void StartRecording();
    void EndRecording(unsigned int vertexStride);
    void Render(C4JRender::eVertexType vertexType, Context& context, int primitiveType, bool bApplyMatrix,
                unsigned char layerMask);
    bool IsBusy();

    void AddMatrix(const float* matrix);
    void AddVertices(unsigned int vertexStride, unsigned int numVertices, void* vertices, Context& context,
                     unsigned char* layers);
    void BindTexture(int texture);
    void SetColor(float red, float green, float blue, float alpha);
    void SetDepthFunc(int depthFunc);
    void SetDepthMask(bool bDepthMask);
    void SetDepthTestEnable(bool bDepthTest);
    void SetLightingEnable(bool bLighting);
    void SetLightEnable(int light, bool bEnable);
    void SetLightDirection(int light, float x, float y, float z);
    void SetLightColour(int light, float red, float green, float blue);
    void SetLightAmbientColour(float red, float green, float blue);
    void SetBlendEnable(bool bBlend);
    void SetBlendFunc(int source, int destination);
    void SetBlendFactor(unsigned int factor);
    void SetFaceCull(bool bFaceCull);

    int getNumVertices();
    bool EnsureUploaded(Device* device, int layout);
    bool EnsureTerrainTextures();

    unsigned char* mVertices;
    unsigned int mVerticesUsed;
    std::vector<Command> mCommands;
    unsigned int mVerticesCapacity;
    int mLastRenderedFrame;
    bool mRecordState;
    bool mHasVertices;
    GLuint mBuffer;
    GLuint mVertexArray;
    int mVertexArrayLayout;
    std::vector<unsigned short> mQuadData;
    GLuint mQuadBuffer;
    GLuint mQuadTexture;
    GLuint mVertexTexture;
};

class Renderer::Device
{
public:
    struct StreamBuffer
    {
        GLenum mTarget;
        GLuint mBuffer;
        size_t mSize;
        size_t mOffset;

        void Create(GLenum target, size_t size);
        void Destroy();
        bool Reserve(size_t size);
        size_t Write(const void* data, size_t size, size_t alignment);
    };

    struct TextureOperation
    {
        enum eType
        {
            eType_Storage,
            eType_Level,
            eType_Delete
        };

        eType mType;
        int mSlot;
        unsigned int mGeneration;
        int mLevel;
        int mWidth;
        int mHeight;
        GLuint mName;
        bool mHasPixels;
        std::vector<unsigned char> mPixels;
    };

    struct Program
    {
        GLuint mName;
        GLint mQuadLocation;
        GLint mFlipLocation;
        GLint mGammaLocation;
    };

    Device();

    void Create(int width, int height);
    void Destroy();
    void CreateTargets(int width, int height);
    void DestroyTargets();
    bool IsGlThread() const;
    const Program& GetProgram(int vertexShader, int pixelShader);
    GLuint GetSampler(const RenderSamplerState& state);
    void SetupVertexArray(GLuint vertexArray, GLuint buffer, int layout);
    void EnsureQuadIndices(unsigned int quads);
    void BindFramebuffer(GLuint framebuffer);
    void BindTextureUnit(int unit, GLuint texture, GLuint sampler);
    void UseProgram(GLuint program);
    void BindVertexArray(GLuint vertexArray);
    void SetCapability(int index, GLenum capability, bool bEnable);
    void SetColourMaskMode(int mode, unsigned char mask);
    void Invalidate();
    void FlushPending(Renderer& renderer);
    void QueueBufferDelete(GLuint buffer, GLuint vertexArray);
    void QueueTextureDelete(GLuint texture);
    const ShaderLinkedProgram* PreparePackDraw(Renderer& renderer, Context& context, ShaderDrawTarget& target);
    GLuint ResolvePackAlbedo(Renderer& renderer, Context& context, float* colourScale, bool& bDiscardZeroAlpha);
    GLuint StreamVertexTexture();
    void ExecuteTextureStorage(Texture& texture, const void* pixels);
    void ExecuteTextureLevel(Texture& texture, int level, int width, int height, const void* pixels);
    bool PrepareDraw(Renderer& renderer, Context& context, bool reuseTerrainRaster = false);
    void DrawRange(int primitiveType, unsigned int firstVertex, unsigned int count);
    void DrawStream(Renderer& renderer, Context& context, const void* vertices, unsigned int size,
                    unsigned int stride, int layout, int primitiveType, unsigned int count);
    void DrawScreenQuad(int vertexShader, int pixelShader, const float* quad, float flipY, float gamma);

    std::thread::id mThread;
    bool mReady;
    int mWidth;
    int mHeight;
    int mTarget;
    GLint mUniformAlignment;
    StreamBuffer mVertexStream;
    StreamBuffer mConstantStream;
    GLuint mStreamVertexArrays[VertexLayout_Count];
    GLuint mEmptyVertexArray;
    GLuint mQuadIndexBuffer;
    unsigned int mQuadIndexCapacity;
    Program mPrograms[RenderVertexShader_Count][RenderPixelShader_Count];
    GLuint mSamplers[2][2][2][2][3];
    GLuint mMainFramebuffer;
    GLuint mMainColourTexture;
    GLuint mMainDepthStencil;
    GLuint mAlphaFramebuffer;
    GLuint mAlphaAccumulateTexture;
    GLuint mAlphaWeightTexture;
    GLuint mThumbnailFramebuffer[THUMBNAIL_LEVELS];
    GLuint mThumbnailTexture[THUMBNAIL_LEVELS];
    int mBoundVertexConstantCount;
    bool mPixelConstantsBound;
    Context* mBoundConstantContext;
    bool mCacheValid;
#if LCE_PLATFORM_SWITCH
    bool mTerrainRasterReady;
#endif
    GLuint mCurrentFramebuffer;
    GLuint mCurrentProgram;
    GLuint mCurrentVertexArray;
    GLuint mCurrentTexture[RENDER_LIGHT_TEXTURE_UNIT + 1];
    GLuint mCurrentSampler[RENDER_LIGHT_TEXTURE_UNIT + 1];
    int mCapabilities[6];
    int mColourMaskMode;
    unsigned char mColourMask;
    C4JCriticalSection mPendingMutex;
    std::vector<TextureOperation> mTextureOperations;
    std::vector<GLuint> mDeletedBuffers;
    std::vector<GLuint> mDeletedVertexArrays;
    std::vector<GLuint> mDeletedTextures;
    GLuint mStreamVertexTexture;
    bool mPackDraw;
    bool mPendingTerrainQuadData;
    ShaderAdapter mPackAdapter;
    unsigned int mTextureContentCounter;
    int mPendingPrimitive;
};

namespace
{
enum Capability
{
    Capability_CullFace,
    Capability_DepthTest,
    Capability_StencilTest,
    Capability_Blend,
    Capability_Scissor,
    Capability_PolygonOffset
};

enum ColourMaskMode
{
    ColourMaskMode_Main,
    ColourMaskMode_OrderIndependentAlpha
};
}

void Renderer::Device::StreamBuffer::Create(GLenum target, size_t size)
{
    mTarget = target;
    mSize = size;
    mOffset = 0;
    Gl::GenBuffers(1, &mBuffer);
    Gl::BindBuffer(target, mBuffer);
    Gl::BufferData(target, (GLsizeiptr)size, NULL, Gl::STREAM_DRAW);
}

void Renderer::Device::StreamBuffer::Destroy()
{
    if (mBuffer != 0)
    {
        Gl::DeleteBuffers(1, &mBuffer);
        mBuffer = 0;
    }
}

bool Renderer::Device::StreamBuffer::Reserve(size_t size)
{
    if (mOffset + size <= mSize)
    {
        return false;
    }

    Gl::BindBuffer(mTarget, mBuffer);
    Gl::BufferData(mTarget, (GLsizeiptr)mSize, NULL, Gl::STREAM_DRAW);
    mOffset = 0;
    return true;
}

size_t Renderer::Device::StreamBuffer::Write(const void* data, size_t size, size_t alignment)
{
    size_t offset = (mOffset + alignment - 1) / alignment * alignment;

    if (offset + size > mSize)
    {
        Gl::BindBuffer(mTarget, mBuffer);
        Gl::BufferData(mTarget, (GLsizeiptr)mSize, NULL, Gl::STREAM_DRAW);
        offset = 0;
    }

    Gl::BindBuffer(mTarget, mBuffer);
    void* destination = Gl::MapBufferRange(mTarget, (GLintptr)offset, (GLsizeiptr)size,
                                           Gl::MAP_WRITE_BIT | Gl::MAP_INVALIDATE_RANGE_BIT
                                               | Gl::MAP_UNSYNCHRONIZED_BIT);
    if (destination != NULL)
    {
        memcpy(destination, data, size);
        Gl::UnmapBuffer(mTarget);
    }
    else
    {
        Gl::BufferSubData(mTarget, (GLintptr)offset, (GLsizeiptr)size, data);
    }

    mOffset = offset + size;
    return offset;
}

Renderer::Device::Device()
{
    mReady = false;
    mWidth = 0;
    mHeight = 0;
    mTarget = RenderTarget_Main;
    mUniformAlignment = 256;
    memset(&mVertexStream, 0, sizeof(mVertexStream));
    memset(&mConstantStream, 0, sizeof(mConstantStream));
    memset(mStreamVertexArrays, 0, sizeof(mStreamVertexArrays));
    mEmptyVertexArray = 0;
    mQuadIndexBuffer = 0;
    mQuadIndexCapacity = 0;
    memset(mPrograms, 0, sizeof(mPrograms));
    memset(mSamplers, 0, sizeof(mSamplers));
    mMainFramebuffer = 0;
    mMainColourTexture = 0;
    mMainDepthStencil = 0;
    mAlphaFramebuffer = 0;
    mAlphaAccumulateTexture = 0;
    mAlphaWeightTexture = 0;
    memset(mThumbnailFramebuffer, 0, sizeof(mThumbnailFramebuffer));
    memset(mThumbnailTexture, 0, sizeof(mThumbnailTexture));
    mBoundVertexConstantCount = 0;
    mPixelConstantsBound = false;
    mBoundConstantContext = NULL;
    mStreamVertexTexture = 0;
    mPackDraw = false;
    mPendingTerrainQuadData = false;
    mTextureContentCounter = 0;
    mPendingPrimitive = -1;
    mPackAdapter = ShaderAdapter_Count;
    InitializeCriticalSection(&mPendingMutex);
    Invalidate();
}

bool Renderer::Device::IsGlThread() const
{
    return mReady && std::this_thread::get_id() == mThread;
}

void Renderer::Device::Invalidate()
{
    mCacheValid = false;
#if LCE_PLATFORM_SWITCH
    mTerrainRasterReady = false;
#endif
    mCurrentFramebuffer = 0xFFFFFFFFu;
    mCurrentProgram = 0xFFFFFFFFu;
    mCurrentVertexArray = 0xFFFFFFFFu;

    for (int unit = 0; unit <= RENDER_LIGHT_TEXTURE_UNIT; ++unit)
    {
        mCurrentTexture[unit] = 0xFFFFFFFFu;
        mCurrentSampler[unit] = 0xFFFFFFFFu;
    }

    for (int index = 0; index < 6; ++index)
    {
        mCapabilities[index] = -1;
    }

    mColourMaskMode = -1;
    mColourMask = 0xFF;
    mBoundVertexConstantCount = 0;
    mPixelConstantsBound = false;
    mBoundConstantContext = NULL;
}

void Renderer::Device::Create(int width, int height)
{
    mThread = std::this_thread::get_id();
    mReady = true;

    Gl::GetIntegerv(Gl::UNIFORM_BUFFER_OFFSET_ALIGNMENT, &mUniformAlignment);
    if (mUniformAlignment < 16)
    {
        mUniformAlignment = 16;
    }

    Gl::PixelStorei(Gl::UNPACK_ALIGNMENT, 4);
    Gl::PixelStorei(Gl::PACK_ALIGNMENT, 4);

    mVertexStream.Create(Gl::ARRAY_BUFFER, STREAM_VERTEX_BUFFER_SIZE);
    mConstantStream.Create(Gl::UNIFORM_BUFFER, STREAM_CONSTANT_BUFFER_SIZE);

    Gl::GenBuffers(1, &mQuadIndexBuffer);
    EnsureQuadIndices(0x4000);

    Gl::GenVertexArrays(VertexLayout_Count, mStreamVertexArrays);
    for (int layout = 0; layout < VertexLayout_Count; ++layout)
    {
        SetupVertexArray(mStreamVertexArrays[layout], mVertexStream.mBuffer, layout);
    }

    Gl::GenVertexArrays(1, &mEmptyVertexArray);

    for (int level = 0, size = THUMBNAIL_SIZE; level < THUMBNAIL_LEVELS; ++level, size >>= 1)
    {
        Gl::GenTextures(1, &mThumbnailTexture[level]);
        Gl::BindTexture(Gl::TEXTURE_2D, mThumbnailTexture[level]);
        Gl::TexImage2D(Gl::TEXTURE_2D, 0, (GLint)Gl::RGBA8, size, size, 0, Gl::RGBA, Gl::UNSIGNED_BYTE, NULL);
        Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_MAX_LEVEL, 0);
        Gl::GenFramebuffers(1, &mThumbnailFramebuffer[level]);
        Gl::BindFramebuffer(Gl::FRAMEBUFFER, mThumbnailFramebuffer[level]);
        Gl::FramebufferTexture2D(Gl::FRAMEBUFFER, Gl::COLOR_ATTACHMENT0, Gl::TEXTURE_2D, mThumbnailTexture[level], 0);
        if (Gl::CheckFramebufferStatus(Gl::FRAMEBUFFER) != Gl::FRAMEBUFFER_COMPLETE)
        {
            FatalRendererError("thumbnail framebuffer is incomplete");
        }
    }

    CreateTargets(width, height);
    Invalidate();
}

void Renderer::Device::Destroy()
{
    if (!IsGlThread())
    {
        return;
    }

    DestroyTargets();

    for (int level = 0; level < THUMBNAIL_LEVELS; ++level)
    {
        Gl::DeleteFramebuffers(1, &mThumbnailFramebuffer[level]);
        Gl::DeleteTextures(1, &mThumbnailTexture[level]);
    }

    for (int vertexShader = 0; vertexShader < RenderVertexShader_Count; ++vertexShader)
    {
        for (int pixelShader = 0; pixelShader < RenderPixelShader_Count; ++pixelShader)
        {
            if (mPrograms[vertexShader][pixelShader].mName != 0)
            {
                Gl::DeleteProgram(mPrograms[vertexShader][pixelShader].mName);
            }
        }
    }

    GLuint* samplers = &mSamplers[0][0][0][0][0];
    for (size_t index = 0; index < sizeof(mSamplers) / sizeof(GLuint); ++index)
    {
        if (samplers[index] != 0)
        {
            Gl::DeleteSamplers(1, &samplers[index]);
        }
    }

    if (mStreamVertexTexture != 0)
    {
        Gl::DeleteTextures(1, &mStreamVertexTexture);
        mStreamVertexTexture = 0;
    }
    Gl::DeleteVertexArrays(VertexLayout_Count, mStreamVertexArrays);
    Gl::DeleteVertexArrays(1, &mEmptyVertexArray);
    Gl::DeleteBuffers(1, &mQuadIndexBuffer);
    mVertexStream.Destroy();
    mConstantStream.Destroy();
    mReady = false;
}

void Renderer::Device::CreateTargets(int width, int height)
{
    mWidth = width;
    mHeight = height;

    Gl::GenTextures(1, &mMainColourTexture);
    Gl::BindTexture(Gl::TEXTURE_2D, mMainColourTexture);
    Gl::TexImage2D(Gl::TEXTURE_2D, 0, (GLint)Gl::RGBA8, width, height, 0, Gl::RGBA, Gl::UNSIGNED_BYTE, NULL);
    Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_MAX_LEVEL, 0);

    Gl::GenTextures(1, &mAlphaAccumulateTexture);
    Gl::BindTexture(Gl::TEXTURE_2D, mAlphaAccumulateTexture);
    Gl::TexImage2D(Gl::TEXTURE_2D, 0, (GLint)Gl::RGBA16F, width, height, 0, Gl::RGBA, Gl::HALF_FLOAT, NULL);
    Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_MAX_LEVEL, 0);

    Gl::GenTextures(1, &mAlphaWeightTexture);
    Gl::BindTexture(Gl::TEXTURE_2D, mAlphaWeightTexture);
    Gl::TexImage2D(Gl::TEXTURE_2D, 0, (GLint)Gl::R16F, width, height, 0, Gl::RED, Gl::HALF_FLOAT, NULL);
    Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_MAX_LEVEL, 0);

    Gl::GenRenderbuffers(1, &mMainDepthStencil);
    Gl::BindRenderbuffer(Gl::RENDERBUFFER, mMainDepthStencil);
    Gl::RenderbufferStorage(Gl::RENDERBUFFER, Gl::DEPTH24_STENCIL8, width, height);

    Gl::GenFramebuffers(1, &mMainFramebuffer);
    Gl::BindFramebuffer(Gl::FRAMEBUFFER, mMainFramebuffer);
    Gl::FramebufferTexture2D(Gl::FRAMEBUFFER, Gl::COLOR_ATTACHMENT0, Gl::TEXTURE_2D, mMainColourTexture, 0);
    Gl::FramebufferRenderbuffer(Gl::FRAMEBUFFER, Gl::DEPTH_STENCIL_ATTACHMENT, Gl::RENDERBUFFER, mMainDepthStencil);
    GLenum mainBuffers[1] = {Gl::COLOR_ATTACHMENT0};
    Gl::DrawBuffers(1, mainBuffers);
    if (Gl::CheckFramebufferStatus(Gl::FRAMEBUFFER) != Gl::FRAMEBUFFER_COMPLETE)
    {
        FatalRendererError("main framebuffer is incomplete");
    }

    Gl::Disable(Gl::SCISSOR_TEST);
    Gl::ColorMask(1, 1, 1, 1);
    Gl::DepthMask(1);
    Gl::StencilMask(0xFF);
    Gl::ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    Gl::ClearDepth(1.0);
    Gl::ClearStencil(0);
    Gl::Clear(Gl::COLOR_BUFFER_BIT | Gl::DEPTH_BUFFER_BIT | Gl::STENCIL_BUFFER_BIT);

    Gl::GenFramebuffers(1, &mAlphaFramebuffer);
    Gl::BindFramebuffer(Gl::FRAMEBUFFER, mAlphaFramebuffer);
    Gl::FramebufferTexture2D(Gl::FRAMEBUFFER, Gl::COLOR_ATTACHMENT0, Gl::TEXTURE_2D, mAlphaAccumulateTexture, 0);
    Gl::FramebufferTexture2D(Gl::FRAMEBUFFER, Gl::COLOR_ATTACHMENT0 + 1, Gl::TEXTURE_2D, mMainColourTexture, 0);
    Gl::FramebufferTexture2D(Gl::FRAMEBUFFER, Gl::COLOR_ATTACHMENT0 + 2, Gl::TEXTURE_2D, mAlphaWeightTexture, 0);
    Gl::FramebufferRenderbuffer(Gl::FRAMEBUFFER, Gl::DEPTH_STENCIL_ATTACHMENT, Gl::RENDERBUFFER, mMainDepthStencil);
    GLenum alphaBuffers[3] = {Gl::COLOR_ATTACHMENT0, Gl::COLOR_ATTACHMENT0 + 1, Gl::COLOR_ATTACHMENT0 + 2};
    Gl::DrawBuffers(3, alphaBuffers);
    if (Gl::CheckFramebufferStatus(Gl::FRAMEBUFFER) != Gl::FRAMEBUFFER_COMPLETE)
    {
        FatalRendererError("order independent alpha framebuffer is incomplete");
    }

    Invalidate();
}

void Renderer::Device::DestroyTargets()
{
    Gl::BindFramebuffer(Gl::FRAMEBUFFER, 0);
    Gl::DeleteFramebuffers(1, &mMainFramebuffer);
    Gl::DeleteFramebuffers(1, &mAlphaFramebuffer);
    Gl::DeleteRenderbuffers(1, &mMainDepthStencil);
    Gl::DeleteTextures(1, &mMainColourTexture);
    Gl::DeleteTextures(1, &mAlphaAccumulateTexture);
    Gl::DeleteTextures(1, &mAlphaWeightTexture);
    mMainFramebuffer = 0;
    mAlphaFramebuffer = 0;
    mMainDepthStencil = 0;
    mMainColourTexture = 0;
    mAlphaAccumulateTexture = 0;
    mAlphaWeightTexture = 0;
    Invalidate();
}

namespace
{
GLuint CompileShader(GLenum type, const std::string& source)
{
    GLuint shader = Gl::CreateShader(type);
    const GLchar* text = source.c_str();
    Gl::ShaderSource(shader, 1, &text, NULL);
    Gl::CompileShader(shader);

    GLint status = 0;
    Gl::GetShaderiv(shader, Gl::COMPILE_STATUS, &status);
    if (status == 0)
    {
        GLint length = 0;
        Gl::GetShaderiv(shader, Gl::INFO_LOG_LENGTH, &length);
        std::vector<GLchar> log((size_t)(length > 1 ? length : 1));
        Gl::GetShaderInfoLog(shader, (GLsizei)log.size(), NULL, &log[0]);
        LOG_ERROR(sLog, "%s\n%s", &log[0], source.c_str());
        FatalRendererError("shader compilation failed");
    }

    return shader;
}
}

const Renderer::Device::Program& Renderer::Device::GetProgram(int vertexShader, int pixelShader)
{
    Program& program = mPrograms[vertexShader][pixelShader];

    if (program.mName != 0)
    {
        return program;
    }

    GLuint vertex = CompileShader(Gl::VERTEX_SHADER, RenderVertexShaderSource(vertexShader));
    GLuint pixel = CompileShader(Gl::FRAGMENT_SHADER, RenderPixelShaderSource(pixelShader));
    GLuint name = Gl::CreateProgram();
    Gl::AttachShader(name, vertex);
    Gl::AttachShader(name, pixel);
    Gl::LinkProgram(name);
    Gl::DeleteShader(vertex);
    Gl::DeleteShader(pixel);

    GLint status = 0;
    Gl::GetProgramiv(name, Gl::LINK_STATUS, &status);
    if (status == 0)
    {
        GLint length = 0;
        Gl::GetProgramiv(name, Gl::INFO_LOG_LENGTH, &length);
        std::vector<GLchar> log((size_t)(length > 1 ? length : 1));
        Gl::GetProgramInfoLog(name, (GLsizei)log.size(), NULL, &log[0]);
        LOG_ERROR(sLog, "%s", &log[0]);
        FatalRendererError("shader program link failed");
    }

    UseProgram(name);

    static const char* const samplerNames[4] = {"uTexture0", "uTexture1", "uTexture2", "uTexture3"};
    for (int unit = 0; unit < 4; ++unit)
    {
        GLint location = Gl::GetUniformLocation(name, samplerNames[unit]);
        if (location >= 0)
        {
            Gl::Uniform1i(location, unit);
        }
    }

    GLint lightLocation = Gl::GetUniformLocation(name, "uLightTexture");
    if (lightLocation >= 0)
    {
        Gl::Uniform1i(lightLocation, RENDER_LIGHT_TEXTURE_UNIT);
    }

    GLuint vertexBlock = Gl::GetUniformBlockIndex(name, "VertexConstants");
    if (vertexBlock != Gl::INVALID_INDEX)
    {
        Gl::UniformBlockBinding(name, vertexBlock, RENDER_VERTEX_CONSTANT_BINDING);
    }

    GLuint pixelBlock = Gl::GetUniformBlockIndex(name, "PixelConstants");
    if (pixelBlock != Gl::INVALID_INDEX)
    {
        Gl::UniformBlockBinding(name, pixelBlock, RENDER_PIXEL_CONSTANT_BINDING);
    }

    program.mQuadLocation = Gl::GetUniformLocation(name, "uQuad");
    program.mFlipLocation = Gl::GetUniformLocation(name, "uFlipY");
    program.mGammaLocation = Gl::GetUniformLocation(name, "uGamma");
    program.mName = name;
    return program;
}

GLuint Renderer::Device::GetSampler(const RenderSamplerState& state)
{
    int mipFilter = state.mMipFilter > MIP_FILTER_LINEAR ? MIP_FILTER_LINEAR : state.mMipFilter;
    GLuint& sampler = mSamplers[state.mClampU ? 1 : 0][state.mClampV ? 1 : 0][state.mMagLinear ? 1 : 0]
                               [state.mMinLinear ? 1 : 0][mipFilter];

    if (sampler != 0)
    {
        return sampler;
    }

    GLenum minFilter;
    if (mipFilter == MIP_FILTER_NONE)
    {
        minFilter = state.mMinLinear ? Gl::LINEAR : Gl::NEAREST;
    }
    else if (mipFilter == MIP_FILTER_POINT)
    {
        minFilter = state.mMinLinear ? Gl::LINEAR_MIPMAP_NEAREST : Gl::NEAREST_MIPMAP_NEAREST;
    }
    else
    {
        minFilter = state.mMinLinear ? Gl::LINEAR_MIPMAP_LINEAR : Gl::NEAREST_MIPMAP_LINEAR;
    }

    Gl::GenSamplers(1, &sampler);
    Gl::SamplerParameteri(sampler, Gl::TEXTURE_MIN_FILTER, (GLint)minFilter);
    Gl::SamplerParameteri(sampler, Gl::TEXTURE_MAG_FILTER, (GLint)(state.mMagLinear ? Gl::LINEAR : Gl::NEAREST));
    Gl::SamplerParameteri(sampler, Gl::TEXTURE_WRAP_S, (GLint)(state.mClampU ? Gl::CLAMP_TO_EDGE : Gl::REPEAT));
    Gl::SamplerParameteri(sampler, Gl::TEXTURE_WRAP_T, (GLint)(state.mClampV ? Gl::CLAMP_TO_EDGE : Gl::REPEAT));
    return sampler;
}

void Renderer::Device::SetupVertexArray(GLuint vertexArray, GLuint buffer, int layout)
{
    BindVertexArray(vertexArray);
    Gl::BindBuffer(Gl::ARRAY_BUFFER, buffer);

    switch (layout)
    {
    case VertexLayout_Terrain:
        Gl::EnableVertexAttribArray(0);
        Gl::VertexAttribIPointer(0, 4, Gl::SHORT, 0x10, reinterpret_cast<const void*>(0));
        Gl::EnableVertexAttribArray(1);
        Gl::VertexAttribIPointer(1, 4, Gl::SHORT, 0x10, reinterpret_cast<const void*>(8));
        break;
    case VertexLayout_Position:
        Gl::EnableVertexAttribArray(0);
        Gl::VertexAttribPointer(0, 2, Gl::FLOAT, 0, 8, reinterpret_cast<const void*>(0));
        break;
    case VertexLayout_ColouredUI:
        Gl::EnableVertexAttribArray(0);
        Gl::VertexAttribPointer(0, 2, Gl::FLOAT, 0, 0x14, reinterpret_cast<const void*>(0));
        Gl::EnableVertexAttribArray(1);
        Gl::VertexAttribPointer(1, 2, Gl::FLOAT, 0, 0x14, reinterpret_cast<const void*>(8));
        Gl::EnableVertexAttribArray(2);
        Gl::VertexAttribPointer(2, 4, Gl::UNSIGNED_BYTE, 1, 0x14, reinterpret_cast<const void*>(16));
        break;
    default:
        Gl::EnableVertexAttribArray(0);
        Gl::VertexAttribPointer(0, 3, Gl::FLOAT, 0, 0x20, reinterpret_cast<const void*>(0));
        Gl::EnableVertexAttribArray(1);
        Gl::VertexAttribPointer(1, 4, Gl::UNSIGNED_BYTE, 1, 0x20, reinterpret_cast<const void*>(20));
        Gl::EnableVertexAttribArray(2);
        Gl::VertexAttribIPointer(2, 4, Gl::BYTE, 0x20, reinterpret_cast<const void*>(24));
        Gl::EnableVertexAttribArray(3);
        Gl::VertexAttribPointer(3, 2, Gl::FLOAT, 0, 0x20, reinterpret_cast<const void*>(12));
        Gl::EnableVertexAttribArray(4);
        Gl::VertexAttribIPointer(4, 2, Gl::SHORT, 0x20, reinterpret_cast<const void*>(28));
        break;
    }

    Gl::BindBuffer(Gl::ELEMENT_ARRAY_BUFFER, mQuadIndexBuffer);
}

void Renderer::Device::EnsureQuadIndices(unsigned int quads)
{
    if (quads <= mQuadIndexCapacity)
    {
        return;
    }

    unsigned int capacity = mQuadIndexCapacity * 2;
    if (capacity < quads)
    {
        capacity = quads;
    }

    std::vector<unsigned int> indices((size_t)capacity * 6);
    for (unsigned int quad = 0; quad < capacity; ++quad)
    {
        unsigned int base = quad * 4;
        indices[quad * 6 + 0] = base;
        indices[quad * 6 + 1] = base + 1;
        indices[quad * 6 + 2] = base + 2;
        indices[quad * 6 + 3] = base;
        indices[quad * 6 + 4] = base + 2;
        indices[quad * 6 + 5] = base + 3;
    }

    Gl::BindBuffer(Gl::ARRAY_BUFFER, mQuadIndexBuffer);
    Gl::BufferData(Gl::ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(unsigned int)), &indices[0],
                   Gl::STATIC_DRAW);
    mQuadIndexCapacity = capacity;
}

void Renderer::Device::BindFramebuffer(GLuint framebuffer)
{
    if (mCurrentFramebuffer != framebuffer)
    {
        Gl::BindFramebuffer(Gl::FRAMEBUFFER, framebuffer);
        mCurrentFramebuffer = framebuffer;
    }
}

void Renderer::Device::BindTextureUnit(int unit, GLuint texture, GLuint sampler)
{
    if (mCurrentTexture[unit] != texture)
    {
        Gl::ActiveTexture(Gl::TEXTURE0 + (GLenum)unit);
        Gl::BindTexture(Gl::TEXTURE_2D, texture);
        mCurrentTexture[unit] = texture;
    }

    if (mCurrentSampler[unit] != sampler)
    {
        Gl::BindSampler((GLuint)unit, sampler);
        mCurrentSampler[unit] = sampler;
    }
}

void Renderer::Device::UseProgram(GLuint program)
{
    if (mCurrentProgram != program)
    {
        Gl::UseProgram(program);
        mCurrentProgram = program;
    }
}

void Renderer::Device::BindVertexArray(GLuint vertexArray)
{
    if (mCurrentVertexArray != vertexArray)
    {
        Gl::BindVertexArray(vertexArray);
        mCurrentVertexArray = vertexArray;
    }
}

void Renderer::Device::SetCapability(int index, GLenum capability, bool bEnable)
{
    int value = bEnable ? 1 : 0;

    if (mCapabilities[index] != value)
    {
        if (bEnable)
        {
            Gl::Enable(capability);
        }
        else
        {
            Gl::Disable(capability);
        }

        mCapabilities[index] = value;
    }
}

void Renderer::Device::SetColourMaskMode(int mode, unsigned char mask)
{
    if (mColourMaskMode == mode && mColourMask == mask)
    {
        return;
    }

    if (mode == ColourMaskMode_OrderIndependentAlpha)
    {
        Gl::ColorMaski(0, 1, 1, 1, 0);
        Gl::ColorMaski(1, 0, 0, 0, 1);
        Gl::ColorMaski(2, 1, 0, 0, 0);
    }
    else
    {
        Gl::ColorMask((mask & 1) ? 1 : 0, (mask & 2) ? 1 : 0, (mask & 4) ? 1 : 0, (mask & 8) ? 1 : 0);
    }

    mColourMaskMode = mode;
    mColourMask = mask;
}

void Renderer::Device::QueueBufferDelete(GLuint buffer, GLuint vertexArray)
{
    EnterCriticalSection(&mPendingMutex);

    if (buffer != 0)
    {
        mDeletedBuffers.push_back(buffer);
    }

    if (vertexArray != 0)
    {
        mDeletedVertexArrays.push_back(vertexArray);
    }

    LeaveCriticalSection(&mPendingMutex);
}

void Renderer::Device::QueueTextureDelete(GLuint texture)
{
    if (texture == 0)
    {
        return;
    }

    EnterCriticalSection(&mPendingMutex);
    mDeletedTextures.push_back(texture);
    LeaveCriticalSection(&mPendingMutex);
}

GLuint Renderer::Device::StreamVertexTexture()
{
    if (mStreamVertexTexture == 0)
    {
        Gl::GenTextures(1, &mStreamVertexTexture);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, mStreamVertexTexture);
        Gl::TexBuffer(Gl::TEXTURE_BUFFER, Gl::RGBA16I, mVertexStream.mBuffer);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, 0);
    }

    return mStreamVertexTexture;
}

void Renderer::Device::ExecuteTextureStorage(Texture& texture, const void* pixels)
{
    if (texture.mName == 0)
    {
        Gl::GenTextures(1, &texture.mName);
    }

    Gl::ActiveTexture(Gl::TEXTURE0);
    Gl::BindTexture(Gl::TEXTURE_2D, texture.mName);
    mCurrentTexture[0] = texture.mName;
    Gl::TexImage2D(Gl::TEXTURE_2D, 0, (GLint)Gl::RGBA8, texture.mWidth, texture.mHeight, 0, Gl::RGBA,
                   Gl::UNSIGNED_BYTE, pixels);

    for (int level = 1; level < texture.mStorageLevels; ++level)
    {
        int width = texture.mWidth >> level;
        int height = texture.mHeight >> level;
        Gl::TexImage2D(Gl::TEXTURE_2D, level, (GLint)Gl::RGBA8, width > 0 ? width : 1, height > 0 ? height : 1, 0,
                       Gl::RGBA, Gl::UNSIGNED_BYTE, NULL);
    }

    Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_BASE_LEVEL, 0);
    Gl::TexParameteri(Gl::TEXTURE_2D, Gl::TEXTURE_MAX_LEVEL, texture.mStorageLevels - 1);
    texture.mContentVersion = ++mTextureContentCounter;
}

void Renderer::Device::ExecuteTextureLevel(Texture& texture, int level, int width, int height, const void* pixels)
{
    if (texture.mName == 0)
    {
        return;
    }

    Gl::ActiveTexture(Gl::TEXTURE0);
    Gl::BindTexture(Gl::TEXTURE_2D, texture.mName);
    mCurrentTexture[0] = texture.mName;
    Gl::TexSubImage2D(Gl::TEXTURE_2D, level, 0, 0, width, height, Gl::RGBA, Gl::UNSIGNED_BYTE, pixels);
    texture.mContentVersion = ++mTextureContentCounter;
}

void Renderer::Device::FlushPending(Renderer& renderer)
{
    EnterCriticalSection(&mPendingMutex);

    if (!mDeletedBuffers.empty())
    {
        Gl::DeleteBuffers((GLsizei)mDeletedBuffers.size(), &mDeletedBuffers[0]);
        mDeletedBuffers.clear();
    }

    if (!mDeletedVertexArrays.empty())
    {
        for (size_t index = 0; index < mDeletedVertexArrays.size(); ++index)
        {
            if (mDeletedVertexArrays[index] == mCurrentVertexArray)
            {
                mCurrentVertexArray = 0xFFFFFFFFu;
            }
        }

        Gl::DeleteVertexArrays((GLsizei)mDeletedVertexArrays.size(), &mDeletedVertexArrays[0]);
        mDeletedVertexArrays.clear();
    }

    if (!mDeletedTextures.empty())
    {
        Gl::DeleteTextures((GLsizei)mDeletedTextures.size(), &mDeletedTextures[0]);
        mDeletedTextures.clear();
    }

    LeaveCriticalSection(&mPendingMutex);

    EnterCriticalSection(&renderer.mTextureMutex);

    for (size_t index = 0; index < mTextureOperations.size(); ++index)
    {
        TextureOperation& operation = mTextureOperations[index];

        if (operation.mType == TextureOperation::eType_Delete)
        {
            for (int unit = 0; unit <= RENDER_LIGHT_TEXTURE_UNIT; ++unit)
            {
                if (mCurrentTexture[unit] == operation.mName)
                {
                    mCurrentTexture[unit] = 0xFFFFFFFFu;
                }
            }

            Gl::DeleteTextures(1, &operation.mName);
            continue;
        }

        Texture& texture = renderer.mTextures[operation.mSlot];
        if (texture.mGeneration != operation.mGeneration)
        {
            continue;
        }

        const void* pixels = operation.mHasPixels ? &operation.mPixels[0] : NULL;

        if (operation.mType == TextureOperation::eType_Storage)
        {
            texture.mWidth = operation.mWidth;
            texture.mHeight = operation.mHeight;
            ExecuteTextureStorage(texture, pixels);
        }
        else
        {
            ExecuteTextureLevel(texture, operation.mLevel, operation.mWidth, operation.mHeight, pixels);
        }
    }

    mTextureOperations.clear();
    LeaveCriticalSection(&renderer.mTextureMutex);
}

GLuint Renderer::Device::ResolvePackAlbedo(Renderer& renderer, Context& context, float* colourScale,
                                           bool& bDiscardZeroAlpha)
{
    const GpuState& gpu = context.mGpu;
    const float* diffuse = context.mPixelConstants[0];
    const float* layerColour = context.mPixelConstants[4];
    colourScale[0] = diffuse[0] * layerColour[0];
    colourScale[1] = diffuse[1] * layerColour[1];
    colourScale[2] = diffuse[2] * layerColour[2];
    colourScale[3] = diffuse[3];
    bDiscardZeroAlpha = false;

    switch (gpu.mVertexShader)
    {
    case RenderVertexShader_Standard:
    case RenderVertexShader_StandardLit:
    case RenderVertexShader_TexGen:
    case RenderVertexShader_Entity:
    case RenderVertexShader_EntityLit:
        break;
    default:
        return 0;
    }

    ShaderAlbedoRequest request;
    int textures = 1;
    switch (gpu.mPixelShader)
    {
    case RenderPixelShader_DefaultTwoLayers:
    case RenderPixelShader_DefaultThreeLayers:
    case RenderPixelShader_DefaultFourLayers:
        request.mode = ShaderAlbedoMode_Layered;
        request.layers = 2 + (gpu.mPixelShader - RenderPixelShader_DefaultTwoLayers);
        textures = request.layers;
        break;
    case RenderPixelShader_LayeredColourMask:
        request.mode = ShaderAlbedoMode_ColourMask;
        textures = 2;
        break;
    case RenderPixelShader_EntityChangeColour:
        request.mode = ShaderAlbedoMode_ChangeColour;
        break;
    case RenderPixelShader_EntityEmissiveAlpha:
        request.mode = ShaderAlbedoMode_EmissiveAlpha;
        break;
    case RenderPixelShader_EntityEmissiveAlphaOnly:
        request.mode = ShaderAlbedoMode_EmissiveAlphaOnly;
        break;
    default:
        return 0;
    }

    const Texture& defaultTexture = renderer.mTextures[renderer.mDefaultTexture];
    for (int layer = 0; layer < textures; ++layer)
    {
        const Texture& texture = renderer.mTextures[gpu.mPixelTexture[layer]];
        const Texture& source = texture.mName != 0 ? texture : defaultTexture;
        request.textures[layer] = source.mName;
        request.versions[layer] = source.mContentVersion;
        request.sizes[layer][0] = source.mWidth;
        request.sizes[layer][1] = source.mHeight;
    }

    switch (request.mode)
    {
    case ShaderAlbedoMode_Layered:
        for (int layer = 0; layer < request.layers; ++layer)
        {
            memcpy(request.colours[layer], context.mPixelConstants[4 + layer], sizeof(request.colours[layer]));
        }
        break;
    case ShaderAlbedoMode_ColourMask:
        memcpy(request.mask, context.mPixelConstants[9], sizeof(request.mask));
        break;
    case ShaderAlbedoMode_ChangeColour:
        memcpy(request.colours[0], layerColour, sizeof(request.colours[0]));
        memcpy(request.diffuse, diffuse, sizeof(request.diffuse));
        break;
    default:
        memcpy(request.colours[0], layerColour, sizeof(request.colours[0]));
        break;
    }

    bool bStateChanged = false;
    GLuint albedo = ShaderPipeline::Get().ResolveAlbedo(request, bStateChanged);
    if (bStateChanged)
    {
        Invalidate();
    }

    if (albedo == 0)
    {
        return 0;
    }

    switch (request.mode)
    {
    case ShaderAlbedoMode_Layered:
    case ShaderAlbedoMode_ColourMask:
        memcpy(colourScale, diffuse, sizeof(float) * 4);
        break;
    case ShaderAlbedoMode_ChangeColour:
        colourScale[0] = colourScale[1] = colourScale[2] = 1.0f;
        colourScale[3] = diffuse[3];
        bDiscardZeroAlpha = true;
        break;
    default:
        colourScale[0] = colourScale[1] = colourScale[2] = colourScale[3] = 1.0f;
        bDiscardZeroAlpha = true;
        break;
    }

    return albedo;
}

const ShaderLinkedProgram* Renderer::Device::PreparePackDraw(Renderer& renderer, Context& context,
                                                             ShaderDrawTarget& target)
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InWorld() || renderer.mScreen != 0)
    {
        return NULL;
    }

    switch (context.mGpu.mVertexShader)
    {
    case RenderVertexShader_Terrain:
        mPackAdapter = ShaderAdapter_Terrain;
        break;
    case RenderVertexShader_Standard:
    case RenderVertexShader_StandardLit:
        mPackAdapter = ShaderAdapter_Standard;
        break;
    case RenderVertexShader_TexGen:
        mPackAdapter = ShaderAdapter_TexGen;
        break;
    case RenderVertexShader_Entity:
    case RenderVertexShader_EntityLit:
        mPackAdapter = ShaderAdapter_Entity;
        break;
    default:
        return NULL;
    }

    const ShaderLinkedProgram* program = pipeline.SelectProgram(mPackAdapter, context.mGpu.mPixelShader);
    if (program == NULL || !pipeline.PrepareTargets(program, target))
    {
        return NULL;
    }

    return program;
}

bool Renderer::Device::PrepareDraw(Renderer& renderer, Context& context, bool reuseTerrainRaster)
{
    if (!IsGlThread() || renderer.mScreen != 0)
    {
        return false;
    }

    GpuState& gpu = context.mGpu;
    if (gpu.mVertexShader < 0 || gpu.mPixelShader < 0)
    {
        return false;
    }

    FlushPending(renderer);

    float colourScale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool bDiscardZeroAlpha = false;
    GLuint packAlbedo = 0;
    if (ShaderPipeline::Get().InWorld() && renderer.mScreen == 0)
    {
        packAlbedo = ResolvePackAlbedo(renderer, context, colourScale, bDiscardZeroAlpha);
    }

    ShaderDrawTarget packTarget;
    const ShaderLinkedProgram* packProgram = PreparePackDraw(renderer, context, packTarget);
    mPackDraw = packProgram != NULL;
    bool bShadowDraw = mPackDraw && ShaderPipeline::Get().InShadow();

    bool bAlphaTarget = !mPackDraw && mTarget == RenderTarget_OrderIndependentAlpha;
#if LCE_PLATFORM_SWITCH
    // A terrain layer uses the same raster state for every chunk. Uniforms,
    // uploads and vertex buffers still go through the usual draw path.
    const bool terrainRaster = !mPackDraw && gpu.mVertexShader == RenderVertexShader_Terrain;
    const bool rasterAlreadySet = reuseTerrainRaster && terrainRaster && mTerrainRasterReady;
    mTerrainRasterReady = terrainRaster;
#else
    (void)reuseTerrainRaster;
    constexpr bool rasterAlreadySet = false;
#endif
    if (!rasterAlreadySet)
    {
        if (mPackDraw)
        {
            mCurrentFramebuffer = packTarget.framebuffer;

            if (bShadowDraw)
            {
                Gl::Viewport(0, 0, packTarget.width, packTarget.height);
                SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, false);
            }
            else
            {
                float scaleX = (float)packTarget.width / (float)(mWidth > 0 ? mWidth : 1);
                float scaleY = (float)packTarget.height / (float)(mHeight > 0 ? mHeight : 1);
                float flippedY = (float)mHeight - (gpu.mViewport[1] + gpu.mViewport[3]);
                Gl::Viewport((GLint)floorf(gpu.mViewport[0] * scaleX + 0.5f), (GLint)floorf(flippedY * scaleY + 0.5f),
                             (GLsizei)floorf(gpu.mViewport[2] * scaleX + 0.5f),
                             (GLsizei)floorf(gpu.mViewport[3] * scaleY + 0.5f));
                int scissorY = mHeight - (gpu.mScissor[1] + gpu.mScissor[3]);
                SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, true);
                Gl::Scissor((GLint)floorf((float)gpu.mScissor[0] * scaleX), (GLint)floorf((float)scissorY * scaleY),
                            (GLsizei)ceilf((float)gpu.mScissor[2] * scaleX), (GLsizei)ceilf((float)gpu.mScissor[3] * scaleY));
            }
        }
        else
        {
            BindFramebuffer(bAlphaTarget ? mAlphaFramebuffer : mMainFramebuffer);

            Gl::Viewport((GLint)floorf(gpu.mViewport[0] + 0.5f), (GLint)floorf(gpu.mViewport[1] + 0.5f),
                         (GLsizei)floorf(gpu.mViewport[2] + 0.5f), (GLsizei)floorf(gpu.mViewport[3] + 0.5f));
            SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, true);
            Gl::Scissor(gpu.mScissor[0], gpu.mScissor[1], gpu.mScissor[2], gpu.mScissor[3]);
        }

        SetCapability(Capability_DepthTest, Gl::DEPTH_TEST, gpu.mDepthTest);
        Gl::DepthFunc(CompareFunction(gpu.mDepthFunc));
        Gl::DepthMask(gpu.mDepthWrite ? 1 : 0);

        SetCapability(Capability_StencilTest, Gl::STENCIL_TEST, true);
        Gl::StencilFunc(CompareFunction(gpu.mStencilFunc), gpu.mStencilReference, gpu.mStencilCompareMask);
        Gl::StencilOp(Gl::KEEP, Gl::KEEP, Gl::REPLACE);
        Gl::StencilMask(gpu.mStencilWriteMask);

        SetCapability(Capability_CullFace, Gl::CULL_FACE, gpu.mCullBack && !bShadowDraw);
        Gl::CullFace(Gl::BACK);
        if (mPackDraw)
        {
            Gl::FrontFace(gpu.mFrontFaceCCW ? Gl::CCW : Gl::CW);
        }
        else
        {
            Gl::FrontFace(gpu.mFrontFaceCCW ? Gl::CW : Gl::CCW);
        }

        SetCapability(Capability_PolygonOffset, Gl::POLYGON_OFFSET_FILL, true);
        Gl::PolygonOffset(gpu.mPolygonOffsetScale, gpu.mPolygonOffsetBias);

        if (bAlphaTarget)
        {
            SetCapability(Capability_Blend, Gl::BLEND, true);
            Gl::BlendFuncSeparate(Gl::ONE, Gl::ONE, Gl::ZERO, Gl::ONE_MINUS_SRC_ALPHA);
            SetColourMaskMode(ColourMaskMode_OrderIndependentAlpha, CHANNEL_MASK_RGBA);
        }
        else if (mPackDraw)
        {
            if (packTarget.blendOverride)
            {
                SetCapability(Capability_Blend, Gl::BLEND, packTarget.blendEnable);
                Gl::BlendFuncSeparate(packTarget.blend[0], packTarget.blend[1], packTarget.blend[2], packTarget.blend[3]);
            }
            else
            {
                SetCapability(Capability_Blend, Gl::BLEND, gpu.mBlendEnable);
                Gl::BlendFunc(BlendFunction(gpu.mBlendSource), BlendFunction(gpu.mBlendDestination));
            }

            if (packTarget.bufferBlendOff != 0)
            {
                for (GLuint buffer = 0; buffer < 8; ++buffer)
                {
                    if ((packTarget.bufferBlendOff & (1u << buffer)) != 0)
                    {
                        Gl::Disablei(Gl::BLEND, buffer);
                    }
                }

                mCapabilities[Capability_Blend] = -1;
            }

            unsigned char mask = (gpu.mColourMask & 7) != 0 ? CHANNEL_MASK_RGBA : gpu.mColourMask;
            SetColourMaskMode(ColourMaskMode_Main, mask);
        }
        else
        {
            SetCapability(Capability_Blend, Gl::BLEND, gpu.mBlendEnable);
            Gl::BlendFunc(BlendFunction(gpu.mBlendSource), BlendFunction(gpu.mBlendDestination));
            SetColourMaskMode(ColourMaskMode_Main, gpu.mColourMask);
        }

        Gl::BlendColor(gpu.mBlendColour[0], gpu.mBlendColour[1], gpu.mBlendColour[2], gpu.mBlendColour[3]);
    }

    if (mPackDraw)
    {
        UseProgram(packProgram->name);

        ShaderDrawValues values;
        const float* fog = context.mVertexConstants[17];
        int fogMode = (int)fog[2];
        values.fogMode = fogMode == 1 ? 9729 : fogMode == 2 ? 2048 : fogMode == 3 ? 2049 : 0;
        values.fogStart = fabsf(fog[1]);
        values.fogEnd = fabsf(fog[0]);
        values.fogDensity = fabsf(fog[0]);
        values.fogColor[0] = context.mPixelConstants[1][0];
        values.fogColor[1] = context.mPixelConstants[1][1];
        values.fogColor[2] = context.mPixelConstants[1][2];
        values.alphaRef = gpu.mAlphaTestOn ? gpu.mAlphaReference : 0.0f;
        memcpy(values.entityColor, context.mPixelConstants[2], sizeof(values.entityColor));
        values.blendFunc[0] = (int)(gpu.mBlendEnable ? BlendFunction(gpu.mBlendSource) : Gl::ONE);
        values.blendFunc[1] = (int)(gpu.mBlendEnable ? BlendFunction(gpu.mBlendDestination) : Gl::ZERO);
        values.blendFunc[2] = values.blendFunc[0];
        values.blendFunc[3] = values.blendFunc[1];
        memcpy(values.lights, context.mLightState, sizeof(values.lights));
        values.quadData = mPendingTerrainQuadData && mPackAdapter == ShaderAdapter_Terrain;
        memcpy(values.colourScale, colourScale, sizeof(values.colourScale));
        values.quadVertices = mPendingPrimitive == C4JRender::ePrimitiveType_Quads;
        ShaderPipeline::Get().ApplyProgram(packProgram, values);
    }
    else
    {
        const Program& program = GetProgram(gpu.mVertexShader, gpu.mPixelShader);
        UseProgram(program.mName);
    }

    float alphaReference = gpu.mAlphaTestOn ? gpu.mAlphaReference : -3.4028235e38f;
    if (mPackDraw && bDiscardZeroAlpha && alphaReference < 1.0f / 512.0f)
    {
        alphaReference = 1.0f / 512.0f;
    }
    if (context.mPixelConstants[RENDER_PIXEL_CONSTANT_ALPHA_REFERENCE][0] != alphaReference)
    {
        context.mPixelConstants[RENDER_PIXEL_CONSTANT_ALPHA_REFERENCE][0] = alphaReference;
        context.mPixelConstantsDirty = true;
    }

    RenderSamplerState baseSampler = gpu.mPixelSampler[0];
    bool bSharpUI = (gpu.mPixelShader == RenderPixelShader_UI || gpu.mPixelShader == RenderPixelShader_UIColourAdd)
                    && baseSampler.mMagLinear == 0;
    float sharpUI = bSharpUI ? 1.0f : 0.0f;
    if (context.mPixelConstants[RENDER_PIXEL_CONSTANT_ALPHA_REFERENCE][1] != sharpUI)
    {
        context.mPixelConstants[RENDER_PIXEL_CONSTANT_ALPHA_REFERENCE][1] = sharpUI;
        context.mPixelConstantsDirty = true;
    }
    if (bSharpUI)
    {
        baseSampler.mMagLinear = 1;
        baseSampler.mMinLinear = 1;
    }

    int vertexCount = RenderVertexConstantCount(gpu.mVertexShader);
    bool bVertexUpload = vertexCount > 0
                         && (context.mVertexConstantsDirty || vertexCount > mBoundVertexConstantCount
                             || mBoundConstantContext != &context);
    bool bPixelUpload = context.mPixelConstantsDirty || !mPixelConstantsBound || mBoundConstantContext != &context;
    size_t vertexBytes = (size_t)vertexCount * 16;
    size_t pixelBytes = (size_t)RENDER_PIXEL_CONSTANT_COUNT * 16;

    if (bVertexUpload || bPixelUpload)
    {
        if (mConstantStream.Reserve(vertexBytes + pixelBytes + (size_t)mUniformAlignment * 2))
        {
            bVertexUpload = vertexCount > 0;
            bPixelUpload = true;
        }

        if (bVertexUpload)
        {
            size_t offset = mConstantStream.Write(context.mVertexConstants, vertexBytes, (size_t)mUniformAlignment);
            Gl::BindBufferRange(Gl::UNIFORM_BUFFER, RENDER_VERTEX_CONSTANT_BINDING, mConstantStream.mBuffer,
                                (GLintptr)offset, (GLsizeiptr)vertexBytes);
            mBoundVertexConstantCount = vertexCount;
            context.mVertexConstantsDirty = false;
        }

        if (bPixelUpload)
        {
            size_t offset = mConstantStream.Write(context.mPixelConstants, pixelBytes, (size_t)mUniformAlignment);
            Gl::BindBufferRange(Gl::UNIFORM_BUFFER, RENDER_PIXEL_CONSTANT_BINDING, mConstantStream.mBuffer,
                                (GLintptr)offset, (GLsizeiptr)pixelBytes);
            mPixelConstantsBound = true;
            context.mPixelConstantsDirty = false;
        }

        mBoundConstantContext = &context;
    }

    GLuint defaultName = renderer.mTextures[renderer.mDefaultTexture].mName;
    for (int stage = 0; stage < TEXTURE_STAGE_COUNT; ++stage)
    {
        GLuint name = renderer.mTextures[gpu.mPixelTexture[stage]].mName;
        if (stage == 0 && mPackDraw && packAlbedo != 0)
        {
            name = packAlbedo;
        }
        BindTextureUnit(stage, name != 0 ? name : defaultName,
                        GetSampler(stage == 0 ? baseSampler : gpu.mPixelSampler[stage]));
    }

    GLuint vertexName = renderer.mTextures[gpu.mVertexTexture].mName;
    BindTextureUnit(RENDER_LIGHT_TEXTURE_UNIT, vertexName != 0 ? vertexName : defaultName,
                    GetSampler(gpu.mVertexSampler));
    mCacheValid = true;
    return true;
}

void Renderer::Device::DrawRange(int primitiveType, unsigned int firstVertex, unsigned int count)
{
    if (mPackDraw)
    {
        ShaderPipeline::Get().SetFirstVertex((int)firstVertex);
    }

    if (primitiveType == C4JRender::ePrimitiveType_Quads)
    {
        unsigned int quads = count / 4;
        if (quads == 0)
        {
            return;
        }

        if (quads > mQuadIndexCapacity)
        {
            GLuint vertexArray = mCurrentVertexArray;
            EnsureQuadIndices(quads);
            mCurrentVertexArray = 0xFFFFFFFFu;
            BindVertexArray(vertexArray);
        }

        Gl::DrawElementsBaseVertex(Gl::TRIANGLES, (GLsizei)(quads * 6), Gl::UNSIGNED_INT, NULL, (GLint)firstVertex);
        return;
    }

    if (count != 0)
    {
        Gl::DrawArrays(PrimitiveMode(primitiveType), (GLint)firstVertex, (GLsizei)count);
    }
}

void Renderer::Device::DrawStream(Renderer& renderer, Context& context, const void* vertices, unsigned int size,
                                  unsigned int stride, int layout, int primitiveType, unsigned int count)
{
    if (size == 0)
    {
        return;
    }

    mPendingPrimitive = layout == VertexLayout_Standard ? primitiveType : -1;
    bool bPrepared = PrepareDraw(renderer, context);
    mPendingPrimitive = -1;
    if (!bPrepared)
    {
        return;
    }

    size_t offset = mVertexStream.Write(vertices, size, stride);
    BindVertexArray(mStreamVertexArrays[layout]);
    if (mPackDraw && (layout == VertexLayout_Terrain || layout == VertexLayout_Standard))
    {
        ShaderPipeline::Get().BindTerrainData(StreamVertexTexture(), 0);
    }
    DrawRange(primitiveType, (unsigned int)(offset / stride), count);
}

void Renderer::Device::DrawScreenQuad(int vertexShader, int pixelShader, const float* quad, float flipY,
                                      float gamma)
{
    const Program& program = GetProgram(vertexShader, pixelShader);
    UseProgram(program.mName);

    if (program.mQuadLocation >= 0)
    {
        Gl::Uniform4f(program.mQuadLocation, quad[0], quad[1], quad[2], quad[3]);
    }

    if (program.mFlipLocation >= 0)
    {
        Gl::Uniform1f(program.mFlipLocation, flipY);
    }

    if (program.mGammaLocation >= 0)
    {
        Gl::Uniform1f(program.mGammaLocation, gamma);
    }

    BindVertexArray(mEmptyVertexArray);
    Gl::DrawArrays(Gl::TRIANGLE_STRIP, 0, 4);
}

Renderer::Context::Context(Renderer* renderer)
{
    memset(mMatrixStack, 0, sizeof(mMatrixStack));
    MatrixIdentity(mMatrixStack[0][0]);
    mMatrixStackDepth[0] = 0;
    MatrixIdentity(mMatrixStack[1][0]);
    mMatrixStackDepth[1] = 0;
    MatrixIdentity(mMatrixStack[2][0]);
    mMatrixStackDepth[2] = 0;
    MatrixIdentity(mMatrixStack[3][0]);
    mMatrixStackDepth[3] = 0;
    memset(mMatrixDirty, 0, sizeof(mMatrixDirty));

    mRenderer = renderer;
    mTextureIsVertex[0] = false;
    mBoundTexture[3] = 0;
    mAlphaTestEnable = false;
    mAlphaTestRef = 1.0f;
    mTextureIsMask[2] = false;
    mTextureIsMask[3] = false;
    mTextureIsMask[1] = false;
    mTextureIsMask[0] = false;
    mTextureIsVertex[2] = false;
    mTextureIsVertex[3] = false;
    mTextureIsVertex[1] = false;
    mMatrixMode = 0;
    mBoundTexture[0] = 0;
    mFaceCull = true;
    mDeferredMode = false;
    mDepthTestEnable = true;
    mBoundTexture[1] = 0;
    mBoundTexture[2] = 0;
    mDepthFunc = COMPARE_ALWAYS;
    mStencilFunc = COMPARE_ALWAYS;
    mBlendSource = BLEND_ONE;
    mBlendDestination = 0;
    mBlendEnable = false;
    mFogEnable = false;
    mFogNearDistance = 0.0f;
    mFogFarDistance = 0.0f;
    mFogDensity = 0.0f;
    mFogMode = 0;
    mRecordingBuffer = NULL;
    mRecordingId = 0;
    mRecordingVertexType = C4JRender::eVertexType_Default;
    mRecordingPixelShaderType = C4JRender::ePixelShaderType_Default;
    mRecordingPrimitiveType = C4JRender::ePrimitiveType_Triangles;
    mLightingEnable = false;
    mLightEnable[0] = false;
    mLightEnable[1] = false;
    mDepthMask = true;
    mForceLOD = -1;

    memset(mLightState, 0, sizeof(mLightState));
    memset(mBoneMatrix, 0, sizeof(mBoneMatrix));
    mNumBoneMatrices = 0;
    memset(mTexGenMatrix, 0, sizeof(mTexGenMatrix));
    memset(mLastQuadUniform, 0, sizeof(mLastQuadUniform));
    memset(mLayeredColour, 0, sizeof(mLayeredColour));
    memset(mScissorRect, 0, sizeof(mScissorRect));

    mLightingDirty = false;
    mViewportScale[1] = 1.0f;
    mVertexAlpha = 0.0f;
    mViewportScale[2] = 0.0f;
    mVertexAlphaDistance = 0.0f;
    mViewportScale[3] = 0.0f;
    mViewportScale[0] = 1.0f;
    mOrderIndependentAlpha = false;
    mDynamicVertexOffset = 0;
    mPendingQuadData = NULL;

    memset(&mGpu, 0, sizeof(mGpu));
    mGpu.mDepthTest = true;
    mGpu.mDepthWrite = true;
    mGpu.mDepthFunc = COMPARE_ALWAYS;
    mGpu.mStencilFunc = COMPARE_ALWAYS;
    mGpu.mStencilWriteMask = 0xFF;
    mGpu.mCullBack = true;
    mGpu.mFrontFaceCCW = true;
    mGpu.mBlendSource = BLEND_ONE;
    mGpu.mBlendDestination = 0;
    mGpu.mAlphaTestOn = true;
    mGpu.mColourMask = CHANNEL_MASK_RGBA;
    mGpu.mVertexShader = -1;
    mGpu.mPixelShader = -1;
    for (int stage = 0; stage < TEXTURE_STAGE_COUNT; ++stage)
    {
        mGpu.mPixelSampler[stage] = MakeSampler(false, false, MIP_FILTER_POINT);
    }
    mGpu.mVertexSampler = MakeSampler(false, false, MIP_FILTER_POINT);

    memset(mVertexConstants, 0, sizeof(mVertexConstants));
    mVertexConstantsDirty = true;
    memset(mPixelConstants, 0, sizeof(mPixelConstants));
    mPixelConstantsDirty = true;
}

Renderer::DeferredCBuff::DeferredCBuff(int id, CommandBuffer* buffer, int vertexType,
                                       C4JRender::ePixelShaderType pixelShaderType, int primitiveType,
                                       const float* matrix)
{
    mId = id;
    mBuffer = buffer;
    mVertexType = vertexType;
    mPixelShaderType = pixelShaderType;
    mPrimitiveType = primitiveType;
    memcpy(mMatrix, matrix, sizeof(mMatrix));
}

Renderer::Renderer()
{
    gRendererInstance = this;

    memset(mClearColour, 0, sizeof(mClearColour));
    mLayout = ELayout_Screen;
    mDRCFrameStarted = false;
    mDisplayEnabled = false;
    mForegroundAcquired = false;
    mInitialised = false;
    mDevice = NULL;
    mDefaultTexture = 0;
    mGamma = DEFAULT_GAMMA;
    mFrameCount = 0;
    mCapturingThumbnail = false;
    mCurrentVertexShader = -1;
    mCurrentPixelShader = -1;
    mCurrentViewport = eViewportType_FullScreen;
    mTextureStateDirty = false;
    mTextures = NULL;
    mScreenWidth[0] = UI_WIDTH;
    mScreenHeight[0] = UI_HEIGHT;
    mScreenWidth[1] = DRC_WIDTH;
    mScreenHeight[1] = DRC_HEIGHT;
    mScreen = 0;
    mHiDef = true;
    mAllowStaticCommandBufferIds = true;
    mNextCommandBufferId = 0;
    mCommandBufferSlots = NULL;
    mCommandBuffers = NULL;
    mCommandBufferMatrices = NULL;
    mCommandBufferIds = NULL;
    mCommandBufferVertexTypes = NULL;
    mCommandBufferPixelShaderTypes = NULL;
    mCommandBufferPrimitiveTypes = NULL;
    mNumCommandBuffers = 0;
    mNumRetiredCommandBuffers = 0;
    mPeakCommandBuffers = 0;
    mPeakCommandBufferFrame = 0;
    mCommandBufferWarned = false;
    mCommandBufferVertsDrawn = 0;
    mCommandBufferVertsDrawnLastFrame = 0;
    mReservedMEM1 = NULL;
    mScreenGrabRequested = false;
    memset(mPixelColours, 0, sizeof(mPixelColours));
    mPixelUniformsDirty = true;
    memset(mLayeredParams, 0, sizeof(mLayeredParams));
    mLayeredParamsDirty = true;
}

Renderer::~Renderer()
{
    if (mDevice != NULL && mDevice->IsGlThread())
    {
        PcKeyboardOverlayShutdown();
        mDevice->Destroy();
        PcWindowDestroy();
    }
}

void Renderer::Tick()
{
    CBuffTick();
}

void Renderer::ShaderSetQuadData(const unsigned short* data)
{
    getContext().mPendingQuadData = data;
}

bool Renderer::ShaderBeginWorld(ShaderWorldState& state)
{
    if (!IsGlThread() || mScreen != 0 || mCurrentViewport != C4JRender::eViewportType_FullScreen
        || mLayout != ELayout_Screen)
    {
        return false;
    }

    memcpy(state.view, MatrixGet(0), sizeof(state.view));
    memcpy(state.projection, MatrixGet(1), sizeof(state.projection));
    mDevice->FlushPending(*this);
    bool bActive = ShaderPipeline::Get().BeginWorld(state, mScreenWidth[0], mScreenHeight[0]);
    mDevice->Invalidate();
    return bActive;
}

void Renderer::ShaderEndWorld()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InWorld())
    {
        return;
    }

    if (IsGlThread())
    {
        pipeline.EndWorld(mDevice->mMainFramebuffer, mScreenWidth[0], mScreenHeight[0]);
        mDevice->Invalidate();
    }
    else
    {
        pipeline.AbortWorld();
    }
}

void Renderer::ShaderSetStage(int stage)
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InWorld() || !IsGlThread())
    {
        return;
    }

    pipeline.SetStage(stage);
    mDevice->Invalidate();
}

void Renderer::ShaderBeginTranslucent()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InWorld() || !IsGlThread())
    {
        return;
    }

    pipeline.BeginTranslucent();
    mDevice->Invalidate();
}

void Renderer::ShaderBeginHand()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InWorld() || !IsGlThread())
    {
        return;
    }

    pipeline.BeginHand();
    mDevice->Invalidate();
}

void Renderer::ShaderEndHand()
{
    ShaderPipeline::Get().EndHand();
}

bool Renderer::ShaderBeginShadow()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!IsGlThread() || !pipeline.BeginShadow())
    {
        return false;
    }

    mDevice->Invalidate();
    float view[16];
    float projection[16];
    pipeline.ShadowMatrices(view, projection);
    MatrixMode(1);
    MatrixPush();
    MatrixSetIdentity();
    MatrixMult(projection);
    MatrixMode(0);
    MatrixPush();
    MatrixSetIdentity();
    MatrixMult(view);
    return true;
}

void Renderer::ShaderEndShadowOpaque()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InShadow() || !IsGlThread())
    {
        return;
    }

    pipeline.EndShadowOpaque();
    mDevice->Invalidate();
}

void Renderer::ShaderEndShadow()
{
    ShaderPipeline& pipeline = ShaderPipeline::Get();
    if (!pipeline.InShadow())
    {
        return;
    }

    MatrixMode(1);
    MatrixPop();
    MatrixMode(0);
    MatrixPop();
    pipeline.EndShadow();
    mDevice->Invalidate();
}

void Renderer::UpdateGamma(unsigned short gamma)
{
    mGamma = gamma;
}

void Renderer::InitialiseContext(bool bReset)
{
    sContext = new Context(this);
    sContextOwner = this;
}

void Renderer::InitialiseContext()
{
    InitialiseContext(false);
}

Renderer::Context& Renderer::getContext()
{
    if (sContext == NULL || sContextOwner != this)
    {
        InitialiseContext(false);
    }

    return *static_cast<Context*>(sContext);
}

bool Renderer::IsGlThread()
{
    return mDevice != NULL && mDevice->IsGlThread();
}

void Renderer::SetVertexUniformReg(unsigned int offset, unsigned int count, const float* values)
{
    Context& context = getContext();

    if (offset >= VERTEX_CONSTANT_COUNT * 4)
    {
        return;
    }

    if (offset + count > VERTEX_CONSTANT_COUNT * 4)
    {
        count = VERTEX_CONSTANT_COUNT * 4 - offset;
    }

    memcpy(&context.mVertexConstants[0][0] + offset, values, count * sizeof(float));
    context.mVertexConstantsDirty = true;
}

void Renderer::SetPixelUniformReg(unsigned int offset, unsigned int count, const float* values)
{
    Context& context = getContext();
    unsigned int limit = RENDER_PIXEL_CONSTANT_ALPHA_REFERENCE * 4;

    if (offset >= limit)
    {
        return;
    }

    if (offset + count > limit)
    {
        count = limit - offset;
    }

    memcpy(&context.mPixelConstants[0][0] + offset, values, count * sizeof(float));
    context.mPixelConstantsDirty = true;
}

void Renderer::SetGpuShaders(int vertexShader, int pixelShader)
{
    Context& context = getContext();
    context.mGpu.mVertexShader = vertexShader;
    context.mGpu.mPixelShader = pixelShader;
}

void Renderer::ApplyGpuDepthStencil()
{
    Context& context = getContext();
    context.mGpu.mDepthTest = context.mDepthTestEnable;
    context.mGpu.mDepthWrite = context.mDepthMask;
    context.mGpu.mDepthFunc = context.mDepthFunc;
    context.mGpu.mStencilFunc = context.mStencilFunc;
}

void Renderer::ApplyGpuPolygonControl(bool bFrontFaceCCW)
{
    Context& context = getContext();
    context.mGpu.mFrontFaceCCW = bFrontFaceCCW;
    context.mGpu.mCullBack = context.mFaceCull;
}

void Renderer::ApplyGpuAlphaTest()
{
    Context& context = getContext();
    context.mGpu.mAlphaTestOn = true;
    context.mGpu.mAlphaReference = context.mAlphaTestEnable ? context.mAlphaTestRef : 0.0f;
}

void Renderer::RestoreGpuStateFromContext()
{
    Context& context = getContext();
    ApplyGpuDepthStencil();
    ApplyGpuPolygonControl(true);
    context.mGpu.mBlendEnable = context.mBlendEnable;
    context.mGpu.mBlendSource = context.mBlendSource;
    context.mGpu.mBlendDestination = context.mBlendDestination;
    ApplyGpuAlphaTest();
}

void Renderer::ApplyGpuViewportAndScissor(float x, float y, float width, float height)
{
    Context& context = getContext();
    context.mGpu.mViewport[0] = x;
    context.mGpu.mViewport[1] = y;
    context.mGpu.mViewport[2] = width;
    context.mGpu.mViewport[3] = height;
    context.mGpu.mScissor[0] = (int)x;
    context.mGpu.mScissor[1] = (int)y;
    context.mGpu.mScissor[2] = (int)width;
    context.mGpu.mScissor[3] = (int)height;
}

void Renderer::SetScreenScissor(unsigned int left, unsigned int top, unsigned int width, unsigned int height)
{
    Context& context = getContext();
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (mScreen == 0)
    {
        float layoutWidth;
        float layoutHeight;
        GetLayoutRect(mLayout, &offsetX, &offsetY, &layoutWidth, &layoutHeight);
        scaleX = layoutWidth / (float)UI_WIDTH;
        scaleY = layoutHeight / (float)UI_HEIGHT;
    }

    float screenWidth = (float)mScreenWidth[mScreen];
    float screenHeight = (float)mScreenHeight[mScreen];
    float x0 = offsetX + (float)left * scaleX;
    float y0 = offsetY + (float)top * scaleY;
    float x1 = offsetX + ((float)left + (float)width) * scaleX;
    float y1 = offsetY + ((float)top + (float)height) * scaleY;

    x0 = x0 < 0.0f ? 0.0f : (x0 > screenWidth ? screenWidth : x0);
    y0 = y0 < 0.0f ? 0.0f : (y0 > screenHeight ? screenHeight : y0);
    x1 = x1 < x0 ? x0 : (x1 > screenWidth ? screenWidth : x1);
    y1 = y1 < y0 ? y0 : (y1 > screenHeight ? screenHeight : y1);

    context.mGpu.mScissor[0] = (int)floorf(x0 + 0.5f);
    context.mGpu.mScissor[1] = (int)floorf(y0 + 0.5f);
    context.mGpu.mScissor[2] = (int)floorf(x1 + 0.5f) - context.mGpu.mScissor[0];
    context.mGpu.mScissor[3] = (int)floorf(y1 + 0.5f) - context.mGpu.mScissor[1];
}

void Renderer::ResizeTargets()
{
    int drawableWidth = 0;
    int drawableHeight = 0;
    PcWindowGetDrawableSize(&drawableWidth, &drawableHeight);

    if (drawableWidth <= 0 || drawableHeight <= 0)
    {
        return;
    }

    int width = drawableWidth;
    int height = drawableHeight;

    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;

    if (width == mDevice->mWidth && height == mDevice->mHeight)
    {
        return;
    }

    mDevice->DestroyTargets();
    mDevice->CreateTargets(width, height);
    mScreenWidth[0] = width;
    mScreenHeight[0] = height;
}

void Renderer::Initialise(bool bFirstTime)
{
    if (bFirstTime)
    {
        InitialiseContext(true);

        mCommandBufferSlots = new int[COMMAND_BUFFER_ID_COUNT];
        mCommandBuffers = new CommandBuffer*[COMMAND_BUFFER_COUNT];
        mCommandBufferMatrices = new float[COMMAND_BUFFER_COUNT * 16];
        mCommandBufferIds = new int[COMMAND_BUFFER_COUNT];
        mCommandBufferVertexTypes = new unsigned char[COMMAND_BUFFER_COUNT];
        mCommandBufferPixelShaderTypes = new int[COMMAND_BUFFER_COUNT];
        mCommandBufferPrimitiveTypes = new unsigned char[COMMAND_BUFFER_COUNT];

        memset(mCommandBufferSlots, 0xFF, sizeof(int) * COMMAND_BUFFER_ID_COUNT);
        memset(mCommandBuffers, 0, sizeof(CommandBuffer*) * COMMAND_BUFFER_COUNT);
        memset(mCommandBufferMatrices, 0, sizeof(float) * COMMAND_BUFFER_COUNT * 16);
        memset(mCommandBufferIds, 0, sizeof(int) * COMMAND_BUFFER_COUNT);
        memset(mCommandBufferVertexTypes, 0, COMMAND_BUFFER_COUNT);
        memset(mCommandBufferPixelShaderTypes, 0, sizeof(int) * COMMAND_BUFFER_COUNT);
        memset(mCommandBufferPrimitiveTypes, 0, COMMAND_BUFFER_COUNT);

        mNumCommandBuffers = 0;
        mScreenGrabRequested = false;
        mDRCFrameStarted = false;
        mNumRetiredCommandBuffers = 0;
        PcCrashReport::AddSection("Command buffers", &Renderer::WriteCommandBufferReport);

        if (!PcWindowCreate(UI_WIDTH, UI_HEIGHT))
        {
            FatalRendererError("the window and OpenGL 3.3 context could not be created");
        }

        if (!Gl::LoadFunctions(PcWindowGetProcAddress))
        {
            FatalRendererError("the OpenGL 3.3 functions could not be loaded");
        }

        LOG_INFO(sLog, "%s / %s / %s", reinterpret_cast<const char*>(Gl::GetString(Gl::VENDOR)),
                 reinterpret_cast<const char*>(Gl::GetString(Gl::RENDERER)),
                 reinterpret_cast<const char*>(Gl::GetString(Gl::VERSION)));
        sGpuName = reinterpret_cast<const char*>(Gl::GetString(Gl::RENDERER));

        mDevice = new Device;
        mDevice->Create(UI_WIDTH, UI_HEIGHT);
        mScreenWidth[0] = UI_WIDTH;
        mScreenHeight[0] = UI_HEIGHT;

        float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        SetClearColour(black);

        mForegroundAcquired = true;
    }

    mDisplayEnabled = false;
    mHiDef = true;
    mScreenWidth[1] = DRC_WIDTH;
    mScreenHeight[1] = DRC_HEIGHT;
    ResizeTargets();

    if (bFirstTime)
    {
        mTextures = new Texture[TEXTURE_COUNT];
        memset(mTextures, 0, sizeof(Texture) * TEXTURE_COUNT);
        InitializeCriticalSection(&mCommandBufferMutex);
        InitializeCriticalSection(&mTextureMutex);
        InitializeCriticalSection(&sTotalAllocMutex);
        mDefaultTexture = TextureCreate();
        TextureBind(mDefaultTexture);

        unsigned char* white = new unsigned char[0x400];
        memset(white, 0xFF, 0x400);
        TextureData(0x10, 0x10, white, 0, C4JRender::eTextureFormat_RGBA8);
        delete[] white;

        mFrameCount = 0;
        mCapturingThumbnail = false;
        mGamma = DEFAULT_GAMMA;
        mNextCommandBufferId = 0;
        mAllowStaticCommandBufferIds = true;
    }

    ApplyGpuViewportAndScissor(0.0f, 0.0f, (float)mScreenWidth[0], (float)mScreenHeight[0]);

    for (int colour = 0; colour < 8; ++colour)
    {
        if (colour == 0 || colour >= 4)
        {
            mPixelColours[colour][0] = 1.0f;
            mPixelColours[colour][1] = 1.0f;
            mPixelColours[colour][2] = 1.0f;
            mPixelColours[colour][3] = 1.0f;
        }
    }

    memset(mPixelColours[3], 0, sizeof(mPixelColours[3]));
    mPixelUniformsDirty = true;
    memset(mLayeredParams, 0, sizeof(mLayeredParams));
    mLayeredParamsDirty = true;

    StateSetVertexTextureUV(0.0f, 0.0f);
    Context& context = getContext();
    context.mGpu.mVertexTexture = mDefaultTexture;
    context.mGpu.mVertexSampler = mTextures[mDefaultTexture].mSampler;
    for (int stage = 0; stage < TEXTURE_STAGE_COUNT; ++stage)
    {
        context.mGpu.mPixelTexture[stage] = mDefaultTexture;
        context.mGpu.mPixelSampler[stage] = mTextures[mDefaultTexture].mSampler;
    }

    mTextureStateDirty = false;
    mCurrentVertexShader = -1;
    mCurrentPixelShader = -1;
    mCommandBufferVertsDrawn = 0;
    mCommandBufferVertsDrawnLastFrame = 0;
    mInitialised = true;
}

void Renderer::Initialise()
{
    Initialise(true);
}

void Renderer::StartFrameInternal(int screen, bool bAlphaTest)
{
    mCurrentVertexShader = -1;
    mCurrentPixelShader = -1;
    TextureBindVertex(-1);
    TextureBind(-1);
    StateSetColour(1.0f, 1.0f, 1.0f, 1.0f);
    StateSetDepthFunc(3);
    StateSetDepthMask(true);
    StateSetStencil(7, 0, 0, 0);
    StateSetBlendEnable(true);
    StateSetBlendFunc(4, 5);
    StateSetBlendFactor(0xFFFFFFFF);
    StateSetAlphaTestEnable(false);
    StateSetAlphaFunc(4, 0.1f);
    StateSetFaceCull(true);
    StateSetLineWidth(1.0f);
    StateSetWriteEnable(true, true, true, true);
    StateSetDepthTestEnable(false);
    StateSetAlphaTestEnable(bAlphaTest);

    if (screen == 0)
    {
        mDevice->mTarget = RenderTarget_Main;
        mScreen = 0;
    }
    else
    {
        mScreen = screen;
    }

    ApplyGpuViewportAndScissor(0.0f, 0.0f, (float)mScreenWidth[mScreen], (float)mScreenHeight[mScreen]);
}

void Renderer::StartFrame()
{
    if (IsGlThread())
    {
        mDevice->FlushPending(*this);
        ResizeTargets();
    }

    mLayout = ELayout_Screen;
    StartFrameInternal(0, true);
}

void Renderer::GetLayoutRect(ELayout layout, float* x, float* y, float* width, float* height)
{
    float screenWidth = (float)mScreenWidth[mScreen];
    float screenHeight = (float)mScreenHeight[mScreen];
    *x = 0.0f;
    *y = 0.0f;
    *width = screenWidth;
    *height = screenHeight;
    if (mScreen != 0 || layout == ELayout_Screen)
        return;

    float fittedWidth = screenHeight * (float)UI_WIDTH / (float)UI_HEIGHT;
    bool heightLimited = fittedWidth <= screenWidth;
    if (layout == ELayout_InterfaceBackground)
        heightLimited = !heightLimited;
    if (heightLimited)
    {
        *width = floorf(fittedWidth + 0.5f);
    }
    else
    {
        *height = floorf(screenWidth * (float)UI_HEIGHT / (float)UI_WIDTH + 0.5f);
    }
    *x = floorf((screenWidth - *width) * 0.5f);
    *y = floorf((screenHeight - *height) * 0.5f);
}

void Renderer::SetLayout(ELayout layout)
{
    if (layout == mLayout)
        return;
    mLayout = layout;
    StateSetViewport(mCurrentViewport);
}

float Renderer::GetScreenAspect()
{
    if (mScreenHeight[0] <= 0)
        return (float)UI_WIDTH / (float)UI_HEIGHT;
    return (float)mScreenWidth[0] / (float)mScreenHeight[0];
}

float Renderer::GetPixelScale()
{
    if (mScreen != 0)
        return 1.0f;
    float x;
    float y;
    float width;
    float height;
    GetLayoutRect(mLayout, &x, &y, &width, &height);
    return height > 0.0f ? height / (float)UI_HEIGHT : 1.0f;
}

void Renderer::GetInterfaceArea(float* left, float* top, float* width, float* height)
{
    int screen = mScreen;
    mScreen = 0;
    float x;
    float y;
    float areaWidth;
    float areaHeight;
    GetLayoutRect(ELayout_Interface, &x, &y, &areaWidth, &areaHeight);
    mScreen = screen;
    float screenWidth = mScreenWidth[0] > 0 ? (float)mScreenWidth[0] : 1.0f;
    float screenHeight = mScreenHeight[0] > 0 ? (float)mScreenHeight[0] : 1.0f;
    *left = x / screenWidth;
    *top = y / screenHeight;
    *width = areaWidth / screenWidth;
    *height = areaHeight / screenHeight;
}

void Renderer::ResetRenderState()
{
    StartFrameInternal(0, false);
}

void Renderer::GammaCorrect()
{
    if (IsGlThread())
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        PcWindowGetDrawableSize(&drawableWidth, &drawableHeight);

        if (drawableWidth > 0 && drawableHeight > 0)
        {
            mDevice->BindFramebuffer(0);
            mDevice->SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, false);
            mDevice->SetCapability(Capability_DepthTest, Gl::DEPTH_TEST, false);
            mDevice->SetCapability(Capability_StencilTest, Gl::STENCIL_TEST, false);
            mDevice->SetCapability(Capability_CullFace, Gl::CULL_FACE, false);
            mDevice->SetCapability(Capability_Blend, Gl::BLEND, false);
            mDevice->SetColourMaskMode(ColourMaskMode_Main, CHANNEL_MASK_RGBA);
            Gl::Viewport(0, 0, drawableWidth, drawableHeight);

            mDevice->BindTextureUnit(0, mDevice->mMainColourTexture,
                                     mDevice->GetSampler(MakeSampler(true, true, MIP_FILTER_NONE)));
            float quad[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            float gamma = 1.0f / ((float)mGamma * 3.0517578125e-05f * 1.5f + 0.5f);
            mDevice->DrawScreenQuad(RenderVertexShader_ScreenQuad, RenderPixelShader_Gamma, quad, 1.0f, gamma);
        }
    }

    SetGpuShaders(RenderVertexShader_ScreenQuad, RenderPixelShader_Gamma);
    RestoreGpuStateFromContext();
    StateSetViewport(mCurrentViewport);
    mCurrentPixelShader = -1;
    mCurrentVertexShader = -1;
}

void Renderer::StartDRCFrame(bool bCopyFrame)
{
    GammaCorrect();
    StartFrameInternal(1, true);
    mDRCFrameStarted = true;
}

void Renderer::DoScreenGrabOnNextPresent()
{
    mScreenGrabRequested = true;
}

void Renderer::TEMP_PatchStateFromIggy()
{
    StateSetViewport(mCurrentViewport);
    mCurrentPixelShader = -1;
    mCurrentVertexShader = -1;
}

void Renderer::Present()
{
    Context& context = getContext();

    if (!mDRCFrameStarted)
    {
        GammaCorrect();
    }

    mDRCFrameStarted = false;

    if (IsGlThread())
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        PcWindowGetDrawableSize(&drawableWidth, &drawableHeight);
#if LCE_DEBUG_BUILD
        PcFrameCaptureFrame(drawableWidth, drawableHeight);
#endif
        PcKeyboardOverlayDraw(drawableWidth, drawableHeight);
        PcWindowSwap();
        mDevice->Invalidate();

        if (!mDisplayEnabled)
        {
            PcWindowShow();
            mDisplayEnabled = true;
        }
    }

    mCurrentPixelShader = -1;
    mCurrentVertexShader = -1;
    ++mFrameCount;

    context.mDynamicVertexOffset = 0;

    mCommandBufferVertsDrawnLastFrame = mCommandBufferVertsDrawn;
    mCommandBufferVertsDrawn = 0;
}

void Renderer::Clear(int flags)
{
    Context& context = getContext();
    GpuState& gpu = context.mGpu;

    if (IsGlThread() && mScreen == 0 && (flags & 3) != 0 && ShaderPipeline::Get().InWorld())
    {
        mDevice->FlushPending(*this);
        ShaderPipeline::Get().ClearTargets((flags & 2) != 0, (flags & 1) != 0);
        mDevice->Invalidate();
    }
    else if (IsGlThread() && mScreen == 0 && (flags & 3) != 0)
    {
        mDevice->FlushPending(*this);
        bool bAlphaTarget = mDevice->mTarget == RenderTarget_OrderIndependentAlpha;
        mDevice->BindFramebuffer(bAlphaTarget ? mDevice->mAlphaFramebuffer : mDevice->mMainFramebuffer);

        int viewportX0 = (int)floorf(gpu.mViewport[0] + 0.5f);
        int viewportY0 = (int)floorf(gpu.mViewport[1] + 0.5f);
        int viewportX1 = viewportX0 + (int)floorf(gpu.mViewport[2] + 0.5f);
        int viewportY1 = viewportY0 + (int)floorf(gpu.mViewport[3] + 0.5f);
        int x0 = gpu.mScissor[0] > viewportX0 ? gpu.mScissor[0] : viewportX0;
        int y0 = gpu.mScissor[1] > viewportY0 ? gpu.mScissor[1] : viewportY0;
        int x1 = gpu.mScissor[0] + gpu.mScissor[2] < viewportX1 ? gpu.mScissor[0] + gpu.mScissor[2] : viewportX1;
        int y1 = gpu.mScissor[1] + gpu.mScissor[3] < viewportY1 ? gpu.mScissor[1] + gpu.mScissor[3] : viewportY1;

        if (x1 > x0 && y1 > y0)
        {
            mDevice->SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, true);
            Gl::Scissor(x0, y0, x1 - x0, y1 - y0);

            GLbitfield mask = 0;
            if ((flags & 2) != 0)
            {
                if (bAlphaTarget)
                {
                    Gl::ColorMaski(0, 1, 1, 1, 1);
                    Gl::ColorMaski(1, 0, 0, 0, 0);
                    Gl::ColorMaski(2, 0, 0, 0, 0);
                }
                else
                {
                    Gl::ColorMask(1, 1, 1, 1);
                }

                mDevice->mColourMaskMode = -1;
                Gl::ClearColor(mClearColour[0], mClearColour[1], mClearColour[2], mClearColour[3]);
                mask |= Gl::COLOR_BUFFER_BIT;
            }

            if ((flags & 1) != 0)
            {
                Gl::DepthMask(1);
                Gl::ClearDepth(1.0);
                mask |= Gl::DEPTH_BUFFER_BIT;
            }

            Gl::Clear(mask);
        }
    }

    SetGpuShaders(RenderVertexShader_Clear, RenderPixelShader_Copy);
    RestoreGpuStateFromContext();
    gpu.mColourMask = CHANNEL_MASK_RGBA;
    gpu.mStencilCompareMask = 0;
    gpu.mStencilWriteMask = 0xFF;
    gpu.mStencilReference = 0;
    mCurrentVertexShader = -1;
    mCurrentPixelShader = -1;
}

void Renderer::SetClearColour(const float* colour)
{
    mClearColour[0] = colour[0];
    mClearColour[1] = colour[1];
    mClearColour[2] = colour[2];
    mClearColour[3] = colour[3];
}

bool Renderer::IsWidescreen()
{
    return true;
}

bool Renderer::IsHiDef()
{
    return mHiDef;
}

void Renderer::ConvertLinearToPng(ImageFileBuffer* buffer, unsigned char* pixels, unsigned int width,
                                  unsigned int height)
{
    int capacity = (int)((height * width * 24) / 5) + 0x100;
    void* png = malloc(capacity);
    int written = 0;
    SaveTextureDataToMemory(png, capacity, &written, width, height, (int*)pixels);
    buffer->mFormat = 0;
    buffer->mData = png;
    buffer->mSize = written;
}

void Renderer::CaptureThumbnail(ImageFileBuffer* buffer)
{
    float left = 0.0f;
    float right = 1.0f;
    float top = 0.0f;
    float bottom = 1.0f;

    switch (mCurrentViewport)
    {
    case C4JRender::eViewportType_FullScreen:
        break;
    case C4JRender::eViewportType_TopHalf:
        bottom = 0.5f;
        break;
    case C4JRender::eViewportType_BottomHalf:
        top = 0.5f;
        break;
    case C4JRender::eViewportType_LeftHalf:
    case C4JRender::eViewportType_RightHalf:
        right = 0.5f;
        break;
    case C4JRender::eViewportType_TopLeftQuarter:
        right = 0.5f;
        bottom = 0.5f;
        break;
    case C4JRender::eViewportType_TopRightQuarter:
        left = 0.5f;
        bottom = 0.5f;
        break;
    case C4JRender::eViewportType_BottomLeftQuarter:
        right = 0.5f;
        top = 0.5f;
        break;
    case C4JRender::eViewportType_BottomRightQuarter:
        left = 0.5f;
        top = 0.5f;
        break;
    default:
        break;
    }

    float aspect = IsWidescreen() ? 1.7777777910232544f : 1.3333333730697632f;
    float height = bottom - top;
    float scaledLeft = left * aspect;
    float scaledRight = right * aspect;
    float width = scaledRight - scaledLeft;

    if (width < height)
    {
        float inset = (height - width) * 0.5f;
        top = top + inset;
        height = (bottom - inset) - top;
    }
    else
    {
        float inset = (width - height) * 0.5f;
        scaledLeft = scaledLeft + inset;
        scaledRight = scaledRight - inset;
    }

    if (!IsGlThread())
    {
        buffer->mFormat = 0;
        buffer->mData = NULL;
        buffer->mSize = 0;
        return;
    }

    mDevice->FlushPending(*this);
    mDevice->SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, false);
    mDevice->SetCapability(Capability_DepthTest, Gl::DEPTH_TEST, false);
    mDevice->SetCapability(Capability_StencilTest, Gl::STENCIL_TEST, false);
    mDevice->SetCapability(Capability_CullFace, Gl::CULL_FACE, false);
    mDevice->SetCapability(Capability_Blend, Gl::BLEND, false);
    mDevice->SetColourMaskMode(ColourMaskMode_Main, CHANNEL_MASK_RGBA);
    GLuint pointSampler = mDevice->GetSampler(MakeSampler(true, false, MIP_FILTER_NONE));

    int size = THUMBNAIL_SIZE;
    float quad[4];
    quad[0] = scaledLeft / aspect;
    quad[1] = top;
    quad[2] = scaledRight / aspect - scaledLeft / aspect;
    quad[3] = height;
    mDevice->BindFramebuffer(mDevice->mThumbnailFramebuffer[0]);
    Gl::Viewport(0, 0, size, size);
    mDevice->BindTextureUnit(0, mDevice->mMainColourTexture, pointSampler);
    mDevice->DrawScreenQuad(RenderVertexShader_ScreenQuad, RenderPixelShader_Copy, quad, -1.0f, 1.0f);

    for (int level = 1; level < THUMBNAIL_LEVELS; ++level)
    {
        size >>= 1;
        quad[0] = 0.0f;
        quad[1] = 0.0f;
        quad[2] = 1.0f;
        quad[3] = 1.0f;
        mDevice->BindFramebuffer(mDevice->mThumbnailFramebuffer[level]);
        Gl::Viewport(0, 0, size, size);
        mDevice->BindTextureUnit(0, mDevice->mThumbnailTexture[level - 1], pointSampler);
        mDevice->DrawScreenQuad(RenderVertexShader_ScreenQuad, RenderPixelShader_Copy, quad, -1.0f, 1.0f);
    }

    std::vector<unsigned char> pixels(THUMBNAIL_OUTPUT_SIZE * THUMBNAIL_OUTPUT_SIZE * 4);
    Gl::ReadPixels(0, 0, THUMBNAIL_OUTPUT_SIZE, THUMBNAIL_OUTPUT_SIZE, Gl::RGBA, Gl::UNSIGNED_BYTE, &pixels[0]);
    ConvertLinearToPng(buffer, &pixels[0], THUMBNAIL_OUTPUT_SIZE, THUMBNAIL_OUTPUT_SIZE);

    SetGpuShaders(RenderVertexShader_ScreenQuad, RenderPixelShader_Copy);
    RestoreGpuStateFromContext();
    StateSetViewport(mCurrentViewport);
    mCurrentPixelShader = -1;
    mCurrentVertexShader = -1;
}

void Renderer::CaptureScreen(ImageFileBuffer* buffer, _XSOCIAL_PREVIEWIMAGE* previewImage)
{
}

void Renderer::BeginConditionalSurvey(int survey)
{
}

void Renderer::EndConditionalSurvey()
{
}

void Renderer::BeginConditionalRendering(int survey)
{
}

void Renderer::EndConditionalRendering()
{
}

void Renderer::BeginOrderIndependentAlpha()
{
    if (ShaderPipeline::Get().InWorld())
    {
        return;
    }

    Context& context = getContext();
    GpuState& gpu = context.mGpu;

    mDevice->mTarget = RenderTarget_OrderIndependentAlpha;

    if (IsGlThread() && mScreen == 0)
    {
        mDevice->FlushPending(*this);
        mDevice->BindFramebuffer(mDevice->mAlphaFramebuffer);
        Gl::Viewport((GLint)floorf(gpu.mViewport[0] + 0.5f), (GLint)floorf(gpu.mViewport[1] + 0.5f),
                     (GLsizei)floorf(gpu.mViewport[2] + 0.5f), (GLsizei)floorf(gpu.mViewport[3] + 0.5f));
        mDevice->SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, true);
        Gl::Scissor(gpu.mScissor[0], gpu.mScissor[1], gpu.mScissor[2], gpu.mScissor[3]);
        mDevice->SetCapability(Capability_DepthTest, Gl::DEPTH_TEST, false);
        mDevice->SetCapability(Capability_StencilTest, Gl::STENCIL_TEST, false);
        mDevice->SetCapability(Capability_CullFace, Gl::CULL_FACE, false);
        mDevice->SetCapability(Capability_Blend, Gl::BLEND, false);
        Gl::ColorMaski(0, 1, 1, 1, 1);
        Gl::ColorMaski(1, 0, 0, 0, 1);
        Gl::ColorMaski(2, 1, 1, 1, 1);
        mDevice->mColourMaskMode = -1;
        float quad[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        mDevice->DrawScreenQuad(RenderVertexShader_Clear, RenderPixelShader_AlphaAccumulate, quad, -1.0f, 1.0f);
    }

    SetGpuShaders(RenderVertexShader_Clear, RenderPixelShader_AlphaAccumulate);
    mCurrentVertexShader = -1;
    mCurrentPixelShader = -1;
    gpu.mColourMask = CHANNEL_MASK_RGBA;
    gpu.mBlendEnable = true;
    gpu.mBlendSource = BLEND_ONE;
    gpu.mBlendDestination = BLEND_ONE;
    gpu.mDepthTest = true;
    gpu.mDepthWrite = false;
    gpu.mDepthFunc = COMPARE_LEQUAL;
    gpu.mStencilFunc = COMPARE_ALWAYS;
    gpu.mFrontFaceCCW = true;
    gpu.mCullBack = true;
    gpu.mAlphaTestOn = false;
    gpu.mAlphaReference = 0.0f;
    context.mOrderIndependentAlpha = true;
}

void Renderer::EndOrderIndependentAlpha()
{
    if (ShaderPipeline::Get().InWorld())
    {
        return;
    }

    Context& context = getContext();
    GpuState& gpu = context.mGpu;

    context.mOrderIndependentAlpha = false;
    mDevice->mTarget = RenderTarget_Main;

    if (IsGlThread() && mScreen == 0)
    {
        mDevice->FlushPending(*this);
        mDevice->BindFramebuffer(mDevice->mMainFramebuffer);
        Gl::Viewport((GLint)floorf(gpu.mViewport[0] + 0.5f), (GLint)floorf(gpu.mViewport[1] + 0.5f),
                     (GLsizei)floorf(gpu.mViewport[2] + 0.5f), (GLsizei)floorf(gpu.mViewport[3] + 0.5f));
        mDevice->SetCapability(Capability_Scissor, Gl::SCISSOR_TEST, true);
        Gl::Scissor(gpu.mScissor[0], gpu.mScissor[1], gpu.mScissor[2], gpu.mScissor[3]);
        mDevice->SetCapability(Capability_DepthTest, Gl::DEPTH_TEST, false);
        mDevice->SetCapability(Capability_StencilTest, Gl::STENCIL_TEST, false);
        mDevice->SetCapability(Capability_CullFace, Gl::CULL_FACE, false);
        mDevice->SetCapability(Capability_Blend, Gl::BLEND, true);
        Gl::BlendFunc(Gl::ONE_MINUS_DST_ALPHA, Gl::DST_ALPHA);
        mDevice->SetColourMaskMode(ColourMaskMode_Main, gpu.mColourMask);

        GLuint pointSampler = mDevice->GetSampler(MakeSampler(true, false, MIP_FILTER_NONE));
        mDevice->BindTextureUnit(0, mDevice->mAlphaAccumulateTexture, pointSampler);
        mDevice->BindTextureUnit(1, mDevice->mAlphaWeightTexture, pointSampler);

        float quad[4];
        quad[0] = context.mViewportScale[2];
        quad[1] = context.mViewportScale[3];
        quad[2] = context.mViewportScale[0];
        quad[3] = context.mViewportScale[1];
        mDevice->DrawScreenQuad(RenderVertexShader_ScreenQuad, RenderPixelShader_AlphaResolve, quad, -1.0f, 1.0f);
    }

    SetGpuShaders(RenderVertexShader_ScreenQuad, RenderPixelShader_AlphaResolve);
    RestoreGpuStateFromContext();
    mCurrentPixelShader = -1;
    mCurrentVertexShader = -1;
}

void Renderer::ReleaseForeground()
{
    if (IsGlThread())
    {
        Gl::Flush();
        Gl::Finish();
    }

    if (mReservedMEM1 != NULL)
    {
        AlignedFree(mReservedMEM1);
        mReservedMEM1 = NULL;
    }

    mDisplayEnabled = false;
    mForegroundAcquired = false;
}

void Renderer::AcquiredForeground()
{
    mForegroundAcquired = true;
    Initialise(false);
}

void Renderer::WiiUWaitDrawDone()
{
    if (!mForegroundAcquired)
    {
        return;
    }

    if (IsGlThread())
    {
        Gl::Finish();
    }
}

int Renderer::GetFramesSinceBoot()
{
    return mFrameCount;
}

void Renderer::BeginEvent(const wchar_t* name)
{
}

void Renderer::EndEvent()
{
}

int Renderer::GetNumCommandBufferVertsDrawnLastFrame()
{
    return mCommandBufferVertsDrawnLastFrame;
}

int Renderer::GetNumVertsInCommandBuffer(int id)
{
    EnterCriticalSection(&mCommandBufferMutex);

    int slot = mCommandBufferSlots[id];
    int numVertices = 0;
    if (slot >= 0)
    {
        numVertices = mCommandBuffers[slot]->getNumVertices();
    }

    LeaveCriticalSection(&mCommandBufferMutex);
    return numVertices;
}

void Renderer::MatrixMode(int mode)
{
    Context& context = getContext();
    context.mMatrixMode = mode;
}

void Renderer::MatrixSetIdentity()
{
    Context& context = getContext();
    MatrixIdentity(context.mMatrixStack[context.mMatrixMode][context.mMatrixStackDepth[context.mMatrixMode]]);
    context.mMatrixDirty[context.mMatrixMode] = true;
}

void Renderer::MultWithStack(const float* matrix)
{
    Context& context = getContext();
    float* top = context.mMatrixStack[context.mMatrixMode][context.mMatrixStackDepth[context.mMatrixMode]];
    MatrixConcat(matrix, top, top);
    context.mMatrixDirty[context.mMatrixMode] = true;
}

void Renderer::MatrixTranslate(float x, float y, float z)
{
    float matrix[16];
    MatrixIdentity(matrix);
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    MultWithStack(matrix);
}

void Renderer::MatrixRotate(float angle, float x, float y, float z)
{
    float axis[3] = {x, y, z};
    NormalizeVector(axis);

    float s = sinf(angle);
    float c = cosf(angle);
    float t = 1.0f - c;
    float ax = axis[0];
    float ay = axis[1];
    float az = axis[2];

    float rotation[16];
    rotation[0] = c + t * ax * ax;
    rotation[1] = ay * (t * ax) - s * az;
    rotation[2] = az * (t * ax) + s * ay;
    rotation[3] = 0.0f;
    rotation[4] = ay * (t * ax) + s * az;
    rotation[5] = c + t * ay * ay;
    rotation[6] = az * (t * ay) - s * ax;
    rotation[7] = 0.0f;
    rotation[8] = az * (t * ax) - s * ay;
    rotation[9] = az * (t * ay) + s * ax;
    rotation[10] = c + t * az * az;
    rotation[11] = 0.0f;
    rotation[12] = 0.0f;
    rotation[13] = 0.0f;
    rotation[14] = 0.0f;
    rotation[15] = 1.0f;

    MatrixTranspose(rotation, rotation);
    MultWithStack(rotation);
}

void Renderer::MatrixScale(float x, float y, float z)
{
    float matrix[16];
    MatrixIdentity(matrix);
    matrix[0] = x;
    matrix[5] = y;
    matrix[10] = z;
    MultWithStack(matrix);
}

void Renderer::MatrixPerspective(float fieldOfView, float aspect, float nearDistance, float farDistance)
{
    float angle = fieldOfView * 0.5f * 0.01745329252f;
    float cotangent = 1.0f / tanf(angle);
    float depth = 1.0f / (farDistance - nearDistance);

    float matrix[16];
    memset(matrix, 0, sizeof(matrix));
    matrix[0] = cotangent / aspect;
    matrix[5] = cotangent;
    matrix[10] = -(farDistance + nearDistance) * depth;
    matrix[11] = -(2.0f * farDistance * nearDistance) * depth;
    matrix[14] = -1.0f;

    MatrixTranspose(matrix, matrix);
    MultWithStack(matrix);
}

void Renderer::MatrixOrthogonal(float left, float right, float bottom, float top, float nearDistance,
                                float farDistance)
{
    float matrix[16];
    memset(matrix, 0, sizeof(matrix));

    float horizontal = 1.0f / (right - left);
    matrix[0] = 2.0f * horizontal;
    matrix[3] = -(right + left) * horizontal;
    float vertical = 1.0f / (top - bottom);
    matrix[5] = 2.0f * vertical;
    matrix[7] = -(top + bottom) * vertical;
    float depth = 1.0f / (farDistance - nearDistance);
    matrix[10] = -2.0f * depth;
    matrix[11] = -(farDistance + nearDistance) * depth;
    matrix[15] = 1.0f;

    MatrixTranspose(matrix, matrix);
    MultWithStack(matrix);
}

void Renderer::MatrixPop()
{
    Context& context = getContext();
    --context.mMatrixStackDepth[context.mMatrixMode];
    context.mMatrixDirty[context.mMatrixMode] = true;
}

void Renderer::MatrixPush()
{
    Context& context = getContext();
    int mode = context.mMatrixMode;
    memcpy(context.mMatrixStack[mode][context.mMatrixStackDepth[mode] + 1],
           context.mMatrixStack[mode][context.mMatrixStackDepth[mode]], sizeof(float) * 16);
    ++context.mMatrixStackDepth[mode];
}

void Renderer::MatrixMult(float* matrix)
{
    float local[16];
    memcpy(local, matrix, sizeof(local));
    MultWithStack(local);
}

float* Renderer::MatrixGet(int mode)
{
    Context& context = getContext();
    return context.mMatrixStack[mode][context.mMatrixStackDepth[mode]];
}

void Renderer::Set_matrixDirty()
{
    Context& context = getContext();

    MatrixIdentity34(context.mMatrixStack[0][0]);
    MatrixIdentity34(context.mMatrixStack[1][0]);
    MatrixIdentity34(context.mMatrixStack[3][0]);
    MatrixIdentity34(context.mMatrixStack[2][0]);

    context.mMatrixDirty[2] = true;
    mCurrentVertexShader = -1;
    context.mMatrixDirty[1] = true;
    mCurrentPixelShader = -1;
    context.mMatrixDirty[3] = true;
    context.mMatrixDirty[0] = true;
}

void* Renderer::MemoryAllocMEM1(unsigned int size, unsigned int alignment, bool bReserved)
{
    if (alignment < 4)
    {
        alignment = 4;
    }

    if (!bReserved)
    {
        return AlignedAllocate(size, alignment);
    }

    if (mReservedMEM1 == NULL)
    {
        mReservedMEM1 = AlignedAllocate(size, alignment);
    }

    return mReservedMEM1;
}

void Renderer::MemoryFreeMEM1(void* memory)
{
    if (memory != NULL && memory == mReservedMEM1)
    {
        mReservedMEM1 = NULL;
    }

    AlignedFree(memory);
}

void* Renderer::MemoryAllocMEM2(unsigned int size, unsigned int alignment)
{
    if (alignment < 4)
    {
        alignment = 4;
    }

    return AlignedAllocate(size, alignment);
}

void Renderer::MemoryFreeMEM2(void* memory)
{
    AlignedFree(memory);
}

void* Renderer::MemoryAllocBucket(unsigned int size, unsigned int alignment)
{
    return AlignedAllocate(size, alignment);
}

void Renderer::MemoryFreeBucket(void* memory)
{
    AlignedFree(memory);
}

void Renderer::StateSetColour(float red, float green, float blue, float alpha)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetColor(red, green, blue, alpha);
        }
    }
    else
    {
        mPixelUniformsDirty = true;
        mPixelColours[0][2] = blue;
        mPixelColours[0][3] = alpha;
        mPixelColours[0][1] = green;
        mPixelColours[0][0] = red;
    }
}

void Renderer::StateSetColourAdd(float red, float green, float blue, float alpha)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        mPixelColours[3][0] = red;
        mPixelColours[3][1] = green;
        mPixelColours[3][2] = blue;
        mPixelColours[3][3] = alpha;
        mPixelUniformsDirty = true;
    }
}

void Renderer::StateSetTexColour(int stage, float red, float green, float blue, float alpha)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL && (unsigned int)stage < 4)
    {
        mPixelColours[4 + stage][0] = red;
        mPixelColours[4 + stage][1] = green;
        mPixelColours[4 + stage][2] = blue;
        mPixelColours[4 + stage][3] = alpha;
        mPixelUniformsDirty = true;
    }
}

void Renderer::StateSetDepthMask(bool bDepthMask)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetDepthMask(bDepthMask);
        }
    }
    else
    {
        context.mDepthMask = bDepthMask;
        ApplyGpuDepthStencil();
    }
}

void Renderer::StateSetBlendEnable(bool bBlend)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetBlendEnable(bBlend);
        }
    }
    else
    {
        context.mBlendEnable = bBlend;
        context.mGpu.mBlendEnable = bBlend;
    }
}

void Renderer::StateSetBlendFunc(int source, int destination)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetBlendFunc(source, destination);
        }
    }
    else
    {
        context.mBlendSource = source;
        context.mBlendDestination = destination;
        context.mGpu.mBlendSource = source;
        context.mGpu.mBlendDestination = destination;
    }
}

void Renderer::StateSetBlendFactor(unsigned int factor)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetBlendFactor(factor);
        }
    }
    else
    {
        context.mGpu.mBlendColour[0] = (float)(factor & 0xFF) / 255.0f;
        context.mGpu.mBlendColour[1] = (float)((factor >> 8) & 0xFF) / 255.0f;
        context.mGpu.mBlendColour[2] = (float)((factor >> 16) & 0xFF) / 255.0f;
        context.mGpu.mBlendColour[3] = (float)(factor >> 24) / 255.0f;
    }
}

void Renderer::StateSetAlphaFunc(int alphaFunc, float reference)
{
    Context& context = getContext();
    context.mAlphaTestRef = reference;
    ApplyGpuAlphaTest();
}

void Renderer::StateSetDepthFunc(int depthFunc)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetDepthFunc(depthFunc);
        }
    }
    else
    {
        context.mDepthFunc = depthFunc;
        ApplyGpuDepthStencil();
    }
}

void Renderer::StateSetFaceCull(bool bFaceCull)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetFaceCull(bFaceCull);
        }
    }
    else
    {
        context.mFaceCull = bFaceCull;
        ApplyGpuPolygonControl(true);
    }
}

void Renderer::StateSetFaceCullCW(bool bClockwise)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        ApplyGpuPolygonControl(bClockwise);
    }
}

void Renderer::StateSetLineWidth(float width)
{
}

void Renderer::StateSetWriteEnable(bool bRed, bool bGreen, bool bBlue, bool bAlpha)
{
    Context& context = getContext();
    context.mGpu.mColourMask = (unsigned char)((bRed ? 1 : 0) | (bGreen ? 2 : 0) | (bBlue ? 4 : 0) | (bAlpha ? 8 : 0));
}

void Renderer::StateSetDepthTestEnable(bool bDepthTest)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetDepthTestEnable(bDepthTest);
        }
    }
    else
    {
        context.mDepthTestEnable = bDepthTest;
        ApplyGpuDepthStencil();
    }
}

void Renderer::StateSetAlphaTestEnable(bool bAlphaTest)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        context.mAlphaTestEnable = bAlphaTest;
        ApplyGpuAlphaTest();
    }
}

void Renderer::StateSetDepthSlopeAndBias(float slope, float bias)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        ApplyGpuPolygonControl(true);
        context.mGpu.mPolygonOffsetScale = slope;
        context.mGpu.mPolygonOffsetBias = bias;
    }
}

void Renderer::StateSetFogEnable(bool bFog)
{
    Context& context = getContext();
    context.mFogEnable = bFog;
}

void Renderer::StateSetFogMode(int mode)
{
    Context& context = getContext();
    context.mFogMode = mode;
}

void Renderer::StateSetFogNearDistance(float nearDistance)
{
    Context& context = getContext();
    context.mFogNearDistance = nearDistance;
}

void Renderer::StateSetFogFarDistance(float farDistance)
{
    Context& context = getContext();
    context.mFogFarDistance = farDistance;
}

void Renderer::StateSetFogDensity(float density)
{
    Context& context = getContext();
    context.mFogDensity = density;
}

void Renderer::StateSetFogColour(float red, float green, float blue)
{
    mPixelColours[1][0] = red;
    mPixelColours[1][1] = green;
    mPixelColours[1][2] = blue;
    mPixelColours[1][3] = 1.0f;
    mPixelUniformsDirty = true;
}

void Renderer::StateSetLightingEnable(bool bLighting)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL && context.mRecordingBuffer->mRecordState)
    {
        context.mRecordingBuffer->SetLightingEnable(bLighting);
        return;
    }

    context.mLightingEnable = bLighting;
}

void Renderer::StateSetVertexTextureUV(float u, float v)
{
    float uniform[4];
    uniform[0] = u - 1.0f;
    uniform[1] = v - 1.0f;
    uniform[2] = 0.0f;
    uniform[3] = 0.0f;
    SetVertexUniformReg(VERTEX_REG_VERTEX_TEXTURE_UV, 4, uniform);
}

void Renderer::StateSetTextureScaleUI(float scaleU, float scaleV, float offsetU, float offsetV)
{
    float uniform[4];
    uniform[0] = scaleU;
    uniform[1] = scaleV;
    uniform[2] = offsetU;
    uniform[3] = offsetV;
    SetVertexUniformReg(VERTEX_REG_UI_TEXTURE_SCALE, 4, uniform);
}

void Renderer::StateSetScalingGridUI(float left, float right, float top, float bottom, float innerLeft,
                                     float innerRight, float innerTop, float innerBottom, float scaleU, float scaleV)
{
    float edges[4];
    float inner[4];
    float scale[4];

    edges[0] = left;
    edges[1] = right;
    edges[2] = top;
    edges[3] = bottom;
    inner[0] = innerLeft;
    inner[1] = innerRight;
    inner[2] = innerTop;
    inner[3] = innerBottom;
    scale[0] = scaleU;
    scale[1] = scaleV;
    scale[2] = 0.0f;
    scale[3] = 0.0f;

    SetVertexUniformReg(VERTEX_REG_SCALING_GRID_X, 4, edges);
    SetVertexUniformReg(VERTEX_REG_SCALING_GRID_Y, 4, inner);
    SetVertexUniformReg(VERTEX_REG_SCALING_GRID_SCALE, 4, scale);
}

void Renderer::StateSetLightColour(int light, float red, float green, float blue)
{
    if (light >= 2)
    {
        return;
    }

    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetLightColour(light, red, green, blue);
        }
    }
    else
    {
        context.mLightState[2 + light][0] = red;
        context.mLightState[2 + light][1] = green;
        context.mLightingDirty = true;
        context.mLightState[2 + light][3] = 1.0f;
        context.mLightState[2 + light][2] = blue;
    }
}

void Renderer::StateSetLightAmbientColour(float red, float green, float blue)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetLightAmbientColour(red, green, blue);
        }
    }
    else
    {
        context.mLightState[4][0] = red;
        context.mLightState[4][2] = blue;
        context.mLightState[4][3] = 1.0f;
        context.mLightingDirty = true;
        context.mLightState[4][1] = green;
    }
}

void Renderer::StateSetLightDirection(int light, float x, float y, float z)
{
    if (light >= 2)
    {
        return;
    }

    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetLightDirection(light, x, y, z);
        }
    }
    else
    {
        const float* modelView = context.mMatrixStack[0][context.mMatrixStackDepth[0]];
        float direction[3];
        for (int axis = 0; axis < 3; ++axis)
        {
            direction[axis] = modelView[0 * 4 + axis] * x + modelView[1 * 4 + axis] * y + modelView[2 * 4 + axis] * z;
        }
        NormalizeVector(direction);

        context.mLightState[light][0] = direction[0];
        context.mLightState[light][1] = direction[1];
        context.mLightState[light][2] = direction[2];
        context.mLightState[light][3] = 0.0f;
        context.mLightingDirty = true;
    }
}

void Renderer::StateSetLightEnable(int light, bool bEnable)
{
    if (light >= 2)
    {
        return;
    }

    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        if (context.mRecordingBuffer->mRecordState)
        {
            context.mRecordingBuffer->SetLightEnable(light, bEnable);
        }
    }
    else
    {
        context.mLightEnable[light] = bEnable;
        context.mLightingDirty = true;
    }
}

void Renderer::StateSetViewport(C4JRender::eViewportType viewport)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        return;
    }

    mCurrentViewport = viewport;

    float screenWidth = (float)mScreenWidth[mScreen];
    float screenHeight = (float)mScreenHeight[mScreen];
    float x = 0.0f;
    float y = 0.0f;
    float width = screenWidth;
    float height = screenHeight;

    switch (viewport)
    {
    case C4JRender::eViewportType_TopHalf:
        height = screenHeight * 0.5f;
        break;
    case C4JRender::eViewportType_BottomHalf:
        height = screenHeight * 0.5f;
        y = height;
        break;
    case C4JRender::eViewportType_LeftHalf:
        width = screenWidth * 0.5f;
        break;
    case C4JRender::eViewportType_RightHalf:
        width = screenWidth * 0.5f;
        x = width;
        break;
    case C4JRender::eViewportType_TopLeftQuarter:
        width = screenWidth * 0.5f;
        height = screenHeight * 0.5f;
        break;
    case C4JRender::eViewportType_TopRightQuarter:
        width = screenWidth * 0.5f;
        height = screenHeight * 0.5f;
        x = width;
        break;
    case C4JRender::eViewportType_BottomLeftQuarter:
        width = screenWidth * 0.5f;
        height = screenHeight * 0.5f;
        y = height;
        break;
    case C4JRender::eViewportType_BottomRightQuarter:
        width = screenWidth * 0.5f;
        height = screenHeight * 0.5f;
        x = width;
        y = height;
        break;
    default:
        break;
    }

    float layoutX;
    float layoutY;
    float layoutWidth;
    float layoutHeight;
    GetLayoutRect(mLayout, &layoutX, &layoutY, &layoutWidth, &layoutHeight);
    x = layoutX + x * layoutWidth / screenWidth;
    y = layoutY + y * layoutHeight / screenHeight;
    width = width * layoutWidth / screenWidth;
    height = height * layoutHeight / screenHeight;

    context.mViewportScale[0] = width / screenWidth;
    context.mViewportScale[1] = height / screenHeight;
    context.mViewportScale[2] = x / screenWidth;
    context.mViewportScale[3] = y / screenHeight;

    ApplyGpuViewportAndScissor(x, y, width, height);
    float scissorX0 = x < 0.0f ? 0.0f : x;
    float scissorY0 = y < 0.0f ? 0.0f : y;
    float scissorX1 = x + width > screenWidth ? screenWidth : x + width;
    float scissorY1 = y + height > screenHeight ? screenHeight : y + height;
    context.mGpu.mScissor[0] = (int)scissorX0;
    context.mGpu.mScissor[1] = (int)scissorY0;
    context.mGpu.mScissor[2] = (int)scissorX1 - (int)scissorX0;
    context.mGpu.mScissor[3] = (int)scissorY1 - (int)scissorY0;
}

void Renderer::StateGetScissorRect(unsigned int& left, unsigned int& top, unsigned int& right, unsigned int& bottom)
{
    Context& context = getContext();
    left = context.mScissorRect[0];
    top = context.mScissorRect[1];
    right = context.mScissorRect[2];
    bottom = context.mScissorRect[3];
}

void Renderer::StateSetScissorRect(unsigned int left, unsigned int top, unsigned int right, unsigned int bottom)
{
    Context& context = getContext();
    unsigned int width = right - left;
    unsigned int height = bottom - top;
    context.mScissorRect[0] = left;
    context.mScissorRect[1] = top;
    context.mScissorRect[2] = right;
    context.mScissorRect[3] = bottom;
    SetScreenScissor(left, top, width, height);
}

void Renderer::StateSetEnableViewportClipPlanes(bool bEnable)
{
}

void Renderer::StateSetTexGenCol(int coordinate, float x, float y, float z, float w, bool bEyeLinear)
{
    Context& context = getContext();

    if (context.mRecordingBuffer != NULL)
    {
        return;
    }

    float plane[4] = {x, y, z, w};

    if (bEyeLinear)
    {
        float modelView[16];
        memcpy(modelView, MatrixGet(0), sizeof(modelView));
        MatrixTranspose(modelView, modelView);

        float inverse[16];
        MatrixInverse(modelView, inverse);

        float transposed[16];
        MatrixTranspose(inverse, transposed);

        float transformed[4];
        for (int row = 0; row < 4; ++row)
        {
            transformed[row] = transposed[row * 4 + 0] * plane[0] + transposed[row * 4 + 1] * plane[1]
                               + transposed[row * 4 + 2] * plane[2] + transposed[row * 4 + 3] * plane[3];
        }

        memcpy(plane, transformed, sizeof(plane));
    }

    int matrix = bEyeLinear ? 0 : 1;
    context.mTexGenMatrix[matrix][0][coordinate] = plane[0];
    context.mTexGenMatrix[matrix][1][coordinate] = plane[1];
    context.mTexGenMatrix[matrix][2][coordinate] = plane[2];
    context.mTexGenMatrix[matrix][3][coordinate] = plane[3];

    int other = bEyeLinear ? 1 : 0;
    context.mTexGenMatrix[other][0][coordinate] = 0.0f;
    context.mTexGenMatrix[other][1][coordinate] = 0.0f;
    context.mTexGenMatrix[other][2][coordinate] = 0.0f;
    context.mTexGenMatrix[other][3][coordinate] = 0.0f;
}

void Renderer::StateSetStencil(int stencilFunc, unsigned char reference, unsigned char compareMask,
                               unsigned char writeMask)
{
    Context& context = getContext();
    context.mStencilFunc = stencilFunc;

    if (context.mRecordingBuffer == NULL)
    {
        ApplyGpuDepthStencil();
        context.mGpu.mStencilCompareMask = compareMask;
        context.mGpu.mStencilWriteMask = writeMask;
        context.mGpu.mStencilReference = reference;
    }
}

void Renderer::StateSetForceLOD(int lod)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        context.mForceLOD = lod;
    }
}

void Renderer::StateSetLerpColour(float red, float green, float blue, float alpha)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        mPixelColours[2][0] = red;
        mPixelColours[2][1] = green;
        mPixelColours[2][2] = blue;
        mPixelColours[2][3] = alpha;
        mPixelUniformsDirty = true;
    }
}

void Renderer::SetLayeredColour(int layer, float red, float green, float blue)
{
    Context& context = getContext();
    context.mLayeredColour[layer][0] = red;
    context.mLayeredColour[layer][1] = green;
    context.mLayeredColour[layer][2] = blue;
    context.mLayeredColour[layer][3] = 1.0f;
}

void Renderer::SetLayeredParam(int layer, int slot, float offset, float scale)
{
    Context& context = getContext();
    mLayeredParams[1 + layer][0] = context.mLayeredColour[slot][0];
    mLayeredParams[1 + layer][1] = context.mLayeredColour[slot][1];
    mLayeredParams[1 + layer][2] = context.mLayeredColour[slot][2];
    mLayeredParams[1 + layer][3] = offset + scale;
    mLayeredParamsDirty = true;
}

void Renderer::SetLayeredTextureSize(int width, int height)
{
    mLayeredParams[0][0] = (float)width;
    mLayeredParams[0][1] = (float)height;
    mLayeredParamsDirty = true;
}

void Renderer::StateSetBoneMatrixData(int bone)
{
    Context& context = getContext();
    memcpy(context.mBoneMatrix[bone], MatrixGet(0), sizeof(float) * 16);

    int count = bone + 1;
    if (count > context.mNumBoneMatrices)
    {
        context.mNumBoneMatrices = count;
    }
}

void Renderer::StateSetBoneMatrixNull(int bone)
{
    Context& context = getContext();
    memset(context.mBoneMatrix[bone], 0, sizeof(float) * 16);

    int count = bone + 1;
    if (count > context.mNumBoneMatrices)
    {
        context.mNumBoneMatrices = count;
    }
}

void Renderer::StateSetVertexAlphaDistance(float distance)
{
    Context& context = getContext();
    context.mVertexAlphaDistance = distance;
}

void Renderer::StateSetVertexAlpha(float alpha)
{
    Context& context = getContext();

    if (context.mVertexAlpha != alpha)
    {
        context.mVertexAlpha = alpha;
        SetVertexUniformReg(VERTEX_REG_VERTEX_ALPHA, 4, &context.mVertexAlpha);
    }
}

void Renderer::UpdateFogState()
{
    Context& context = getContext();

    float uniform[4];
    uniform[0] = 0.0f;
    uniform[1] = 0.0f;
    uniform[2] = 0.0f;
    uniform[3] = 0.0f;

    if (!context.mFogEnable)
    {
        SetVertexUniformReg(VERTEX_REG_FOG, 4, uniform);
        return;
    }

    if (context.mFogMode == 1)
    {
        uniform[0] = context.mFogFarDistance;
        uniform[1] = context.mFogNearDistance;
        uniform[2] = 1.0f;
        uniform[3] = context.mVertexAlphaDistance;
    }
    else
    {
        uniform[0] = context.mFogDensity;
        uniform[2] = context.mFogMode == 2 ? 2.0f : 3.0f;
        uniform[3] = context.mVertexAlphaDistance;
    }

    SetVertexUniformReg(VERTEX_REG_FOG, 4, uniform);
}

void Renderer::UpdateLightingState()
{
    Context& context = getContext();

    if (!context.mLightingDirty || !context.mLightingEnable)
    {
        return;
    }

    if (!context.mLightEnable[0])
    {
        memset(context.mLightState[0], 0, sizeof(context.mLightState[0]));
        memset(context.mLightState[2], 0, sizeof(context.mLightState[2]));
    }

    if (!context.mLightEnable[1])
    {
        memset(context.mLightState[1], 0, sizeof(context.mLightState[1]));
        memset(context.mLightState[3], 0, sizeof(context.mLightState[3]));
    }

    SetVertexUniformReg(VERTEX_REG_LIGHTING, 0x14, &context.mLightState[0][0]);
    context.mLightingDirty = false;
}

void Renderer::UpdateViewportState()
{
}

void Renderer::UpdateTexGenState()
{
    Context& context = getContext();
    SetVertexUniformReg(VERTEX_REG_TEXGEN_EYE, 0x10, &context.mTexGenMatrix[0][0][0]);
    SetVertexUniformReg(VERTEX_REG_TEXGEN_OBJECT, 0x10, &context.mTexGenMatrix[1][0][0]);
}

void Renderer::StateUpdate()
{
    Context& context = getContext();
    StateSetFaceCull(context.mFaceCull);
    StateSetDepthMask(context.mDepthMask);
    StateSetDepthTestEnable(context.mDepthTestEnable);
    StateSetAlphaTestEnable(context.mAlphaTestEnable);
}

int Renderer::GetMaxTextures()
{
    return TEXTURE_COUNT;
}

int Renderer::TextureCreate()
{
    EnterCriticalSection(&mTextureMutex);

    for (int texture = 0; texture < TEXTURE_COUNT; ++texture)
    {
        Texture& slot = mTextures[texture];

        if (!slot.mUsed)
        {
            slot.mHasStorage = false;
            slot.mWidth = 0;
            slot.mHeight = 0;
            slot.mStorageLevels = 1;
            slot.mFlags = 0;
            slot.mUsed = true;
            slot.mLevels = 1;
            slot.mFormat = C4JRender::eTextureFormat_RGBA8;
            slot.mSampler = MakeSampler(false, false, MIP_FILTER_POINT);
            slot.mContentVersion = 0;
            ++slot.mGeneration;
            LeaveCriticalSection(&mTextureMutex);
            return texture;
        }
    }

    LeaveCriticalSection(&mTextureMutex);
    return -1;
}

void Renderer::TextureFree(int texture)
{
    bool bGlThread = IsGlThread();

    if (bGlThread)
    {
        mDevice->FlushPending(*this);
    }

    EnterCriticalSection(&mTextureMutex);
    Texture& slot = mTextures[texture];

    if (slot.mName != 0)
    {
        if (bGlThread)
        {
            for (int unit = 0; unit <= RENDER_LIGHT_TEXTURE_UNIT; ++unit)
            {
                if (mDevice->mCurrentTexture[unit] == slot.mName)
                {
                    mDevice->mCurrentTexture[unit] = 0xFFFFFFFFu;
                }
            }

            Gl::DeleteTextures(1, &slot.mName);
        }
        else
        {
            Device::TextureOperation operation;
            operation.mType = Device::TextureOperation::eType_Delete;
            operation.mSlot = texture;
            operation.mGeneration = slot.mGeneration;
            operation.mLevel = 0;
            operation.mWidth = 0;
            operation.mHeight = 0;
            operation.mName = slot.mName;
            operation.mHasPixels = false;
            mDevice->mTextureOperations.push_back(operation);
        }
    }

    slot.mName = 0;
    slot.mHasStorage = false;
    slot.mUsed = false;
    ++slot.mGeneration;
    LeaveCriticalSection(&mTextureMutex);
}

void Renderer::TextureBind(int texture)
{
    TextureBind(0, texture);
}

void Renderer::UpdateTextureState(int stage)
{
    Context& context = getContext();
    int texture = context.mBoundTexture[stage];

    if (!mTextures[texture].mHasStorage)
    {
        return;
    }

    if (context.mTextureIsVertex[stage])
    {
        context.mGpu.mVertexTexture = texture;
        context.mGpu.mVertexSampler = mTextures[texture].mSampler;
    }
    else
    {
        context.mGpu.mPixelTexture[stage] = texture;
        context.mGpu.mPixelSampler[stage] = mTextures[texture].mSampler;
    }
}

void Renderer::TextureBind(int stage, int texture)
{
    if (texture == -1)
    {
        texture = mDefaultTexture;
    }

    Context& context = getContext();
    CommandBuffer* buffer = context.mRecordingBuffer;

    if (buffer != NULL && buffer->mRecordState)
    {
        buffer->BindTexture(texture);
    }

    context.mBoundTexture[stage] = texture;
    context.mTextureIsMask[stage] = false;
    context.mTextureIsVertex[stage] = false;
    UpdateTextureState(stage);
}

void Renderer::TextureBindMask(int stage)
{
    Context& context = getContext();
    context.mTextureIsMask[stage] = true;
}

void Renderer::TextureBindVertex(int texture)
{
    if (texture == -1)
    {
        texture = mDefaultTexture;
    }

    Context& context = getContext();
    context.mBoundTexture[0] = texture;
    context.mTextureIsMask[0] = false;
    context.mTextureIsVertex[0] = true;
    UpdateTextureState(0);
}

void Renderer::TextureSetTextureLevels(int levels)
{
    Context& context = getContext();
    mTextures[context.mBoundTexture[0]].mLevels = levels;
}

int Renderer::TextureGetTextureLevels()
{
    Context& context = getContext();
    return mTextures[context.mBoundTexture[0]].mLevels;
}

void Renderer::SubmitTextureStorage(int slot, const void* data)
{
    bool bGlThread = IsGlThread();

    if (bGlThread)
    {
        mDevice->FlushPending(*this);
    }

    EnterCriticalSection(&mTextureMutex);
    Texture& texture = mTextures[slot];

    if (bGlThread)
    {
        mDevice->ExecuteTextureStorage(texture, data);
    }
    else
    {
        Device::TextureOperation operation;
        operation.mType = Device::TextureOperation::eType_Storage;
        operation.mSlot = slot;
        operation.mGeneration = texture.mGeneration;
        operation.mLevel = 0;
        operation.mWidth = texture.mWidth;
        operation.mHeight = texture.mHeight;
        operation.mName = 0;
        operation.mHasPixels = data != NULL;
        if (data != NULL)
        {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            operation.mPixels.assign(bytes, bytes + (size_t)texture.mWidth * texture.mHeight * 4);
        }
        mDevice->mTextureOperations.push_back(operation);
    }

    LeaveCriticalSection(&mTextureMutex);
}

void Renderer::SubmitTextureLevel(int slot, int level, int width, int height, const void* data)
{
    bool bGlThread = IsGlThread();

    if (bGlThread)
    {
        mDevice->FlushPending(*this);
    }

    EnterCriticalSection(&mTextureMutex);
    Texture& texture = mTextures[slot];

    if (bGlThread)
    {
        mDevice->ExecuteTextureLevel(texture, level, width, height, data);
    }
    else
    {
        Device::TextureOperation operation;
        operation.mType = Device::TextureOperation::eType_Level;
        operation.mSlot = slot;
        operation.mGeneration = texture.mGeneration;
        operation.mLevel = level;
        operation.mWidth = width;
        operation.mHeight = height;
        operation.mName = 0;
        operation.mHasPixels = true;
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        operation.mPixels.assign(bytes, bytes + (size_t)width * height * 4);
        mDevice->mTextureOperations.push_back(operation);
    }

    LeaveCriticalSection(&mTextureMutex);
}

void Renderer::TextureData(int width, int height, void* data, int level, C4JRender::eTextureFormat format)
{
    Context& context = getContext();
    int slot = context.mBoundTexture[0];
    Texture& texture = mTextures[slot];
    texture.mFormat = format;

    if (level == 0)
    {
        texture.mWidth = width;
        texture.mHeight = height;
        texture.mStorageLevels = ClampTextureLevels(texture.mLevels, width, height);
        texture.mHasStorage = width > 0 && height > 0;

        if (!texture.mHasStorage)
        {
            return;
        }

        SubmitTextureStorage(slot, data);
        return;
    }

    if (!texture.mHasStorage || texture.mStorageLevels <= 1 || data == NULL || level >= texture.mStorageLevels)
    {
        return;
    }

    int levelWidth = texture.mWidth >> level;
    int levelHeight = texture.mHeight >> level;

    if (levelWidth <= 0 || levelHeight <= 0)
    {
        return;
    }

    SubmitTextureLevel(slot, level, levelWidth, levelHeight, data);
}

void Renderer::TextureDataUpdate(int x, int y, int width, int height, void* data, int level)
{
    Context& context = getContext();
    int slot = context.mBoundTexture[0];
    Texture& texture = mTextures[slot];

    if (x != 0 || y != 0 || data == NULL)
    {
        return;
    }

    if (width != (texture.mWidth >> level) || height != (texture.mHeight >> level))
    {
        return;
    }

    if (!texture.mHasStorage)
    {
        return;
    }

    if (level != 0 && (texture.mStorageLevels <= 1 || level >= texture.mStorageLevels))
    {
        return;
    }

    if (width <= 0 || height <= 0)
    {
        return;
    }

    SubmitTextureLevel(slot, level, width, height, data);
}

void Renderer::TextureSetParam(int parameter, int value)
{
    Context& context = getContext();

    if (parameter == 0)
    {
        return;
    }

    Texture& texture = mTextures[context.mBoundTexture[0]];
    unsigned int flags = texture.mFlags;

    if (parameter == 1)
    {
        flags &= ~TEXTURE_FLAG_MIN_LINEAR;
        if (value == 1)
        {
            flags |= TEXTURE_FLAG_MIN_LINEAR;
        }
    }
    else if (parameter == 2)
    {
        flags &= ~TEXTURE_FLAG_MAG_LINEAR;
        if (value == 1)
        {
            flags |= TEXTURE_FLAG_MAG_LINEAR;
        }
    }
    else if (parameter == 3)
    {
        flags &= ~TEXTURE_FLAG_CLAMP_U;
        if (value == 0)
        {
            flags |= TEXTURE_FLAG_CLAMP_U;
        }
    }
    else if (parameter == 4)
    {
        flags &= ~TEXTURE_FLAG_CLAMP_V;
        if (value == 0)
        {
            flags |= TEXTURE_FLAG_CLAMP_V;
        }
    }

    texture.mFlags = flags;
    texture.mSampler.mClampU = (flags & TEXTURE_FLAG_CLAMP_U) != 0 ? 1 : 0;
    texture.mSampler.mClampV = (flags & TEXTURE_FLAG_CLAMP_V) != 0 ? 1 : 0;
    texture.mSampler.mMagLinear = (flags & TEXTURE_FLAG_MAG_LINEAR) != 0 ? 1 : 0;
    texture.mSampler.mMinLinear = (flags & TEXTURE_FLAG_MIN_LINEAR) != 0 ? 1 : 0;
    texture.mSampler.mMipFilter = MIP_FILTER_LINEAR;
    UpdateTextureState(0);
}

void Renderer::TextureDynamicUpdateStart()
{
}

void Renderer::TextureDynamicUpdateEnd()
{
}

int Renderer::LoadTextureData(const char* fileName, D3DXIMAGE_INFO* info, int** data)
{
    int file = CreateFileA(fileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
    {
        return -1;
    }

    unsigned int fileSize = GetFileSize(file, NULL);
    unsigned char* fileData = static_cast<unsigned char*>(malloc(fileSize));
    unsigned int bytesRead = 0;
    ReadFile(file, fileData, fileSize, &bytesRead, NULL);
    CloseHandle(file);

    int result = LoadTextureData(fileData, bytesRead, info, data);
    free(fileData);
    return result;
}

int Renderer::LoadTextureData(unsigned char* fileData, unsigned int fileSize, D3DXIMAGE_INFO* info, int** data)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    png_image_begin_read_from_memory(&image, fileData, fileSize);

    if ((image.warning_or_error & 3) > 1)
    {
        png_image_free(&image);
        return -1;
    }

    image.format = PNG_FORMAT_ARGB;
    *data = (int*)new unsigned char[image.height * image.width * 4];

    if (*data != NULL && png_image_finish_read(&image, NULL, *data, 0, NULL))
    {
        info->Width = image.width;
        info->Height = image.height;
        return 0;
    }

    return -1;
}

int Renderer::SaveTextureData(const char* fileName, D3DXIMAGE_INFO* info, int* data)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = info->Width;
    image.height = info->Height;
    image.format = PNG_FORMAT_RGBA;
    png_image_write_to_file(&image, fileName, 0, data, 0, NULL);
    return 0;
}

int Renderer::SaveTextureDataToMemory(void* destination, int destinationSize, int* bytesWritten, int width, int height,
                                      int* data)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_RGBA;

    PngMemoryWriter writer;
    writer.mStart = static_cast<unsigned char*>(destination);
    writer.mCurrent = writer.mStart;
    writer.mEnd = writer.mStart + destinationSize;

    png_rw_ptr writeFunction = PngWriteToMemory;
    png_flush_ptr flushFunction = PngFlushMemory;
    png_image_write_to_stdio(&image, &writer, 0, data, 0, NULL, reinterpret_cast<void*>(writeFunction),
                             reinterpret_cast<void*>(flushFunction));
    *bytesWritten = (int)(writer.mCurrent - writer.mStart);
    return 0;
}

unsigned int Renderer::TextureGlName(int texture) const
{
    if (mTextures == nullptr || texture <= 0 || texture >= TEXTURE_COUNT)
        return 0;
    return mTextures[texture].mName;
}

void Renderer::TextureGetStats()
{
}

C4JRender::ePixelShaderType Renderer::FinalisePixelShaderType(C4JRender::ePixelShaderType pixelShaderType)
{
    Context& context = getContext();

    if (pixelShaderType != C4JRender::ePixelShaderType_Default)
    {
        return pixelShaderType;
    }

    if (context.mOrderIndependentAlpha)
    {
        return C4JRender::ePixelShaderType_OrderIndependentAlpha;
    }

    if (context.mBoundTexture[1] == mDefaultTexture)
    {
        return C4JRender::ePixelShaderType_Default;
    }

    if (context.mBoundTexture[2] == mDefaultTexture)
    {
        if (context.mTextureIsMask[1])
        {
            return C4JRender::ePixelShaderType_LayeredColourMask;
        }

        return C4JRender::ePixelShaderType_DefaultTwoLayers;
    }

    if (context.mBoundTexture[3] == mDefaultTexture)
    {
        return C4JRender::ePixelShaderType_DefaultThreeLayers;
    }

    return C4JRender::ePixelShaderType_DefaultFourLayers;
}

bool Renderer::GetDynamicVertexMemory(C4JRender::eVertexType vertexType, int numVertices)
{
    Context& context = getContext();

    unsigned int used = context.mDynamicVertexOffset + ((VERTEX_STRIDES[vertexType] * numVertices + 0x1F) & ~0x1Fu);
    context.mDynamicVertexOffset = used;
    return used < DYNAMIC_VERTEX_BUDGET;
}

void Renderer::DrawVertexSetup(C4JRender::eVertexType vertexType, C4JRender::ePixelShaderType pixelShaderType,
                               C4JRender::ePrimitiveType primitiveType)
{
    Context& context = getContext();

    int vertexShader = vertexType;
    if (vertexType == C4JRender::eVertexType_Default && context.mLightingEnable)
    {
        vertexShader = C4JRender::eVertexType_DefaultLighting;
    }

    if (vertexShader != mCurrentVertexShader)
    {
        context.mGpu.mVertexShader = vertexShader;
        mCurrentVertexShader = vertexShader;
    }

    C4JRender::ePixelShaderType finalPixelShader = FinalisePixelShaderType(pixelShaderType);

    if (finalPixelShader == C4JRender::ePixelShaderType_LayeredColourMask
        && (mCurrentPixelShader != C4JRender::ePixelShaderType_LayeredColourMask || mLayeredParamsDirty))
    {
        SetPixelUniformReg(PIXEL_REG_LAYERED, 0x20, &mLayeredParams[0][0]);
        mLayeredParamsDirty = false;
    }

    if (finalPixelShader != mCurrentPixelShader || mPixelUniformsDirty)
    {
        SetPixelUniformReg(PIXEL_REG_COLOURS, 0x20, &mPixelColours[0][0]);
        mPixelUniformsDirty = false;
    }

    if (finalPixelShader != mCurrentPixelShader)
    {
        context.mGpu.mPixelShader = finalPixelShader;
        mCurrentPixelShader = finalPixelShader;
    }

    if (context.mMatrixDirty[0])
    {
        SetVertexUniformReg(VERTEX_REG_WORLD_VIEW, 0x10, MatrixGet(0));
        context.mMatrixDirty[0] = false;
    }

    if (context.mMatrixDirty[1])
    {
        SetVertexUniformReg(VERTEX_REG_PROJECTION, 0x10, MatrixGet(1));
        context.mMatrixDirty[1] = false;
    }

    if (context.mMatrixDirty[2])
    {
        SetVertexUniformReg(VERTEX_REG_TEXTURE, 0x10, MatrixGet(2));
        context.mMatrixDirty[2] = false;
    }

    float identity[16];
    MatrixIdentity(identity);
    SetVertexUniformReg(VERTEX_REG_WORLD_VIEW2, 0x10, identity);

    UpdateFogState();
    UpdateViewportState();
    UpdateLightingState();
    UpdateTexGenState();
}

void Renderer::DrawVertices(C4JRender::ePrimitiveType primitiveType, int numVertices, void* vertices,
                            C4JRender::eVertexType vertexType, C4JRender::ePixelShaderType pixelShaderType,
                            unsigned char* layers)
{
    Context& context = getContext();

    if (context.mRecordingBuffer == NULL)
    {
        context.mPendingQuadData = NULL;
    }

    if (context.mRecordingBuffer != NULL)
    {
        int recordedVertexType = vertexType;
        if (vertexType == C4JRender::eVertexType_Default && context.mLightingEnable)
        {
            recordedVertexType = C4JRender::eVertexType_DefaultLighting;
        }

        context.mRecordingVertexType = (C4JRender::eVertexType)recordedVertexType;
        context.mRecordingPrimitiveType = primitiveType;
        context.mRecordingPixelShaderType = pixelShaderType;
        context.mRecordingBuffer->AddVertices(VERTEX_STRIDES[recordedVertexType], numVertices, vertices, context,
                                              layers);
        return;
    }

    if (!GetDynamicVertexMemory(vertexType, numVertices))
    {
        return;
    }

    DrawVertexSetup(vertexType, pixelShaderType, primitiveType);

    unsigned int size = numVertices * VERTEX_STRIDES[vertexType];
    StateUpdate();

    if (mDevice != NULL && numVertices > 0)
    {
        mDevice->DrawStream(*this, context, vertices, size, VERTEX_STRIDES[vertexType],
                            LayoutForVertexType(vertexType), primitiveType, (unsigned int)numVertices);
    }
}

void Renderer::CBuffLockStaticCreations()
{
    mAllowStaticCommandBufferIds = false;
}

int Renderer::CBuffCreate(int count)
{
    EnterCriticalSection(&mCommandBufferMutex);

    int first = mNextCommandBufferId;
    for (;;)
    {
        if (first + count > COMMAND_BUFFER_ID_COUNT)
        {
            LeaveCriticalSection(&mCommandBufferMutex);
            return -1;
        }

        bool bFree = true;
        for (int index = 0; index < count; ++index)
        {
            if (mCommandBufferSlots[first + index] != -1)
            {
                bFree = false;
                break;
            }
        }

        if (bFree)
        {
            break;
        }

        ++first;
    }

    for (int index = 0; index < count; ++index)
    {
        mCommandBufferSlots[first + index] = -2;
    }

    if (mAllowStaticCommandBufferIds)
    {
        mNextCommandBufferId = first + count;
    }

    LeaveCriticalSection(&mCommandBufferMutex);
    return first;
}

void Renderer::DeleteInternalBuffer(int slot)
{
    EnterCriticalSection(&mCommandBufferMutex);

    int retired = mNumRetiredCommandBuffers + 1;
    int retiredSlot = COMMAND_BUFFER_COUNT - retired;
    mCommandBuffers[retiredSlot] = mCommandBuffers[slot];
    mNumRetiredCommandBuffers = retired;
    memcpy(&mCommandBufferMatrices[retiredSlot * 16], &mCommandBufferMatrices[slot * 16], sizeof(float) * 16);

    int last = mNumCommandBuffers - 1;
    mNumCommandBuffers = last;

    if (last != 0)
    {
        mCommandBuffers[slot] = mCommandBuffers[last];
        memcpy(&mCommandBufferMatrices[slot * 16], &mCommandBufferMatrices[last * 16], sizeof(float) * 16);
        mCommandBufferVertexTypes[slot] = mCommandBufferVertexTypes[mNumCommandBuffers];
        mCommandBufferPixelShaderTypes[slot] = mCommandBufferPixelShaderTypes[mNumCommandBuffers];
        mCommandBufferPrimitiveTypes[slot] = mCommandBufferPrimitiveTypes[mNumCommandBuffers];
        int id = mCommandBufferIds[mNumCommandBuffers];
        mCommandBufferIds[slot] = id;
        mCommandBufferSlots[id] = slot;
    }

    LeaveCriticalSection(&mCommandBufferMutex);
}

void Renderer::NoteCommandBufferUse()
{
    const int used = mNumCommandBuffers + mNumRetiredCommandBuffers;
    if (used > mPeakCommandBuffers)
    {
        mPeakCommandBuffers = used;
        mPeakCommandBufferFrame = mFrameCount;
    }
    if (!mCommandBufferWarned && used * 4 > COMMAND_BUFFER_COUNT * 3)
    {
        mCommandBufferWarned = true;
        LOG_WARN(sLog, "Command buffers are over 75%% full: %d of %d (%d in use, %d retired) at render distance %d",
                 used, COMMAND_BUFFER_COUNT, mNumCommandBuffers, mNumRetiredCommandBuffers,
                 VideoSettings::GetRenderDistance());
    }
}

void Renderer::WriteCommandBufferReport(std::string& out)
{
    Renderer* renderer = gRendererInstance;
    if (renderer == NULL || renderer->mCommandBuffers == NULL)
    {
        out += "The renderer was not initialised.\n";
        return;
    }

    const int live = renderer->mNumCommandBuffers;
    const int retired = renderer->mNumRetiredCommandBuffers;
    int busy = 0;
    for (int index = 0; index < retired && index < COMMAND_BUFFER_COUNT; ++index)
    {
        CommandBuffer* buffer = renderer->mCommandBuffers[COMMAND_BUFFER_COUNT - 1 - index];
        if (buffer != NULL && buffer->IsBusy())
            ++busy;
    }

    char line[256];
    snprintf(line, sizeof(line), "Capacity: %d (sized for render distance %d)\n", COMMAND_BUFFER_COUNT,
             (int)VideoSettings::RENDER_DISTANCE_MAX);
    out += line;
    snprintf(line, sizeof(line), "Render distance: %d\n", VideoSettings::GetRenderDistance());
    out += line;
    snprintf(line, sizeof(line), "In use: %d, retired: %d (%d still busy), total %d\n", live, retired, busy,
             live + retired);
    out += line;
    snprintf(line, sizeof(line), "Peak: %d at frame %d; this is frame %d\n", renderer->mPeakCommandBuffers,
             renderer->mPeakCommandBufferFrame, renderer->mFrameCount);
    out += line;

    std::map<int, int> ranges;
    for (int index = 0; index < live && index < COMMAND_BUFFER_COUNT; ++index)
        ++ranges[renderer->mCommandBufferIds[index] >> 14];
    out += "In use by id range:";
    for (std::map<int, int>::iterator it = ranges.begin(); it != ranges.end(); ++it)
    {
        snprintf(line, sizeof(line), " %d:%d", it->first << 14, it->second);
        out += line;
    }
    out += "\n";
}

void Renderer::CBuffDelete(int firstId, int count)
{
    EnterCriticalSection(&mCommandBufferMutex);

    for (int index = 0; index < count; ++index)
    {
        if (mCommandBufferSlots[firstId + index] >= 0)
        {
            DeleteInternalBuffer(mCommandBufferSlots[firstId + index]);
        }

        mCommandBufferSlots[firstId + index] = -1;
    }

    LeaveCriticalSection(&mCommandBufferMutex);
}

void Renderer::CBuffStart(int id, bool bRecordState)
{
    Context& context = getContext();

    CommandBuffer* buffer = new CommandBuffer(bRecordState);
    context.mMatrixMode = 3;
    context.mRecordingBuffer = buffer;
    context.mMatrixStackDepth[3] = 0;
    context.mRecordingId = id;
    MatrixIdentity(context.mMatrixStack[3][0]);
    context.mMatrixDirty[3] = true;
}

void Renderer::CBuffClear(int id)
{
    EnterCriticalSection(&mCommandBufferMutex);

    if (mCommandBufferSlots[id] >= 0)
    {
        DeleteInternalBuffer(mCommandBufferSlots[id]);
        mCommandBufferSlots[id] = -2;
    }

    LeaveCriticalSection(&mCommandBufferMutex);
}

int Renderer::CBuffSize(int id)
{
    if (id == -1)
    {
        return sTotalCommandBufferBytes < 0 ? 0 : sTotalCommandBufferBytes;
    }

    EnterCriticalSection(&mCommandBufferMutex);

    int size = 0;
    int slot = mCommandBufferSlots[id];
    if (slot >= 0)
    {
        size = (int)mCommandBuffers[slot]->mVerticesCapacity;
    }

    LeaveCriticalSection(&mCommandBufferMutex);
    return size;
}

void Renderer::CBuffEnd()
{
    Context& context = getContext();

    EnterCriticalSection(&mCommandBufferMutex);

    if (context.mDeferredMode)
    {
        context.mDeferredBuffers.push_back(DeferredCBuff(context.mRecordingId, context.mRecordingBuffer,
                                                         context.mRecordingVertexType,
                                                         context.mRecordingPixelShaderType,
                                                         context.mRecordingPrimitiveType, context.mMatrixStack[3][0]));
        context.mMatrixMode = 0;
        context.mRecordingBuffer->EndRecording(VERTEX_STRIDES[context.mRecordingVertexType]);
        context.mRecordingBuffer = NULL;
        LeaveCriticalSection(&mCommandBufferMutex);
        return;
    }

    if (mCommandBufferSlots[context.mRecordingId] >= 0)
    {
        DeleteInternalBuffer(mCommandBufferSlots[context.mRecordingId]);
    }

    if (mNumCommandBuffers + mNumRetiredCommandBuffers + 10 > COMMAND_BUFFER_COUNT)
    {
        FatalRendererError("Out of command buffers");
    }

    int slot = mNumCommandBuffers;
    mNumCommandBuffers = slot + 1;
    NoteCommandBufferUse();
    mCommandBufferIds[slot] = context.mRecordingId;
    mCommandBufferSlots[context.mRecordingId] = slot;
    mCommandBufferVertexTypes[slot] = (unsigned char)context.mRecordingVertexType;
    mCommandBufferPixelShaderTypes[slot] = context.mRecordingPixelShaderType;
    mCommandBufferPrimitiveTypes[slot] = (unsigned char)context.mRecordingPrimitiveType;
    mCommandBuffers[slot] = context.mRecordingBuffer;
    memcpy(&mCommandBufferMatrices[slot * 16], context.mMatrixStack[3][0], sizeof(float) * 16);

    context.mMatrixMode = 0;
    context.mRecordingBuffer->EndRecording(VERTEX_STRIDES[context.mRecordingVertexType]);
    context.mRecordingBuffer = NULL;
    LeaveCriticalSection(&mCommandBufferMutex);
}

bool Renderer::CBuffCall(int id, bool bApplyMatrix, unsigned char layerMask)
{
    EnterCriticalSection(&mCommandBufferMutex);

    int slot = mCommandBufferSlots[id];
    if (slot < 0)
    {
        LeaveCriticalSection(&mCommandBufferMutex);
        return false;
    }

    Context& context = getContext();
    int vertexType = mCommandBufferVertexTypes[slot];
    unsigned char primitiveType = mCommandBufferPrimitiveTypes[slot];
#if LCE_PLATFORM_SWITCH
    if (bApplyMatrix && IsGlThread())
        mDevice->mTerrainRasterReady = false;
#endif

    if (!bApplyMatrix)
    {
        mCommandBuffers[slot]->Render((C4JRender::eVertexType)vertexType, context, primitiveType, false, layerMask);
        LeaveCriticalSection(&mCommandBufferMutex);
        return true;
    }

    if (context.mMatrixDirty[0])
    {
        SetVertexUniformReg(VERTEX_REG_WORLD_VIEW, 0x10, MatrixGet(0));
        context.mMatrixDirty[0] = false;
    }

    if (context.mMatrixDirty[1])
    {
        SetVertexUniformReg(VERTEX_REG_PROJECTION, 0x10, MatrixGet(1));
        context.mMatrixDirty[1] = false;
    }

    if (context.mMatrixDirty[2])
    {
        SetVertexUniformReg(VERTEX_REG_TEXTURE, 0x10, MatrixGet(2));
        context.mMatrixDirty[2] = false;
    }

    if (context.mNumBoneMatrices > 0)
    {
        SetVertexUniformReg(VERTEX_REG_BONES, (unsigned int)context.mNumBoneMatrices << 4, &context.mBoneMatrix[0][0]);
        context.mNumBoneMatrices = 0;
    }

    UpdateFogState();
    UpdateLightingState();

    if (vertexType == C4JRender::eVertexType_Default && context.mLightingEnable)
    {
        vertexType = C4JRender::eVertexType_DefaultLighting;
    }
    else if (vertexType == C4JRender::eVertexType_DefaultLighting && !context.mLightingEnable)
    {
        vertexType = C4JRender::eVertexType_Default;
    }
    else if (vertexType == C4JRender::eVertexType_Entity && context.mLightingEnable)
    {
        vertexType = C4JRender::eVertexType_EntityLighting;
    }
    else if (vertexType == C4JRender::eVertexType_EntityLighting && !context.mLightingEnable)
    {
        vertexType = C4JRender::eVertexType_Entity;
    }

    UpdateViewportState();
    UpdateTexGenState();

    if (vertexType != mCurrentVertexShader)
    {
        context.mGpu.mVertexShader = vertexType;
        mCurrentVertexShader = vertexType;
    }

    C4JRender::ePixelShaderType pixelShaderType = (C4JRender::ePixelShaderType)mCommandBufferPixelShaderTypes[slot];

    if (context.mForceLOD >= 0)
    {
        float forceLOD[4];
        forceLOD[0] = (float)context.mForceLOD;
        forceLOD[1] = 0.0f;
        forceLOD[2] = 0.0f;
        forceLOD[3] = 0.0f;
        pixelShaderType = C4JRender::ePixelShaderType_TerrainForceLod;
        SetPixelUniformReg(PIXEL_REG_FORCE_LOD, 4, forceLOD);
    }

    C4JRender::ePixelShaderType finalPixelShader = FinalisePixelShaderType(pixelShaderType);

    if (finalPixelShader != mCurrentPixelShader || mPixelUniformsDirty)
    {
        SetPixelUniformReg(PIXEL_REG_COLOURS, 0x20, &mPixelColours[0][0]);
        mPixelUniformsDirty = false;
    }

    if (finalPixelShader == C4JRender::ePixelShaderType_LayeredColourMask
        && (mCurrentPixelShader != C4JRender::ePixelShaderType_LayeredColourMask || mLayeredParamsDirty))
    {
        SetPixelUniformReg(PIXEL_REG_LAYERED, 0x20, &mLayeredParams[0][0]);
        mLayeredParamsDirty = false;
    }

    if (finalPixelShader != mCurrentPixelShader)
    {
        context.mGpu.mPixelShader = finalPixelShader;
        mCurrentPixelShader = finalPixelShader;
    }

    mCommandBuffers[slot]->Render((C4JRender::eVertexType)vertexType, context, primitiveType, bApplyMatrix,
                                  layerMask);

    MultWithStack(&mCommandBufferMatrices[slot * 16]);
    MatrixIdentity(context.mMatrixStack[3][0]);
    context.mMatrixDirty[3] = true;

    LeaveCriticalSection(&mCommandBufferMutex);
    return true;
}

void Renderer::CBuffTick()
{
    EnterCriticalSection(&mCommandBufferMutex);

    int remaining = mNumRetiredCommandBuffers;
    int examined = 0;
    int slot = COMMAND_BUFFER_COUNT - 1;

    while (examined < remaining)
    {
        CommandBuffer* buffer = mCommandBuffers[slot];

        if (buffer->IsBusy())
        {
            ++examined;
            --slot;
            continue;
        }

        delete buffer;
        remaining = mNumRetiredCommandBuffers - 1;
        mNumRetiredCommandBuffers = remaining;
        mCommandBuffers[slot] = NULL;

        if (remaining == 0)
        {
            break;
        }

        mCommandBuffers[slot] = mCommandBuffers[COMMAND_BUFFER_COUNT - 1 - remaining];
    }

    LeaveCriticalSection(&mCommandBufferMutex);
}

void Renderer::CBuffDeferredModeStart()
{
    Context& context = getContext();
    context.mDeferredMode = true;
}

void Renderer::CBuffDeferredModeEnd()
{
    Context& context = getContext();

    if (!context.mDeferredMode)
    {
        return;
    }

    EnterCriticalSection(&mCommandBufferMutex);
    context.mDeferredMode = false;

    for (unsigned int index = 0; index < context.mDeferredBuffers.size(); ++index)
    {
        const DeferredCBuff& deferred = context.mDeferredBuffers[index];

        if (mCommandBufferSlots[deferred.mId] >= 0)
        {
            DeleteInternalBuffer(mCommandBufferSlots[deferred.mId]);
        }

        if (mNumCommandBuffers + mNumRetiredCommandBuffers + 10 > COMMAND_BUFFER_COUNT)
        {
            FatalRendererError("Out of command buffers");
        }

        int slot = mNumCommandBuffers;
        mNumCommandBuffers = slot + 1;
        NoteCommandBufferUse();
        mCommandBufferIds[slot] = deferred.mId;
        mCommandBufferSlots[deferred.mId] = slot;
        mCommandBufferVertexTypes[slot] = (unsigned char)deferred.mVertexType;
        mCommandBufferPixelShaderTypes[slot] = deferred.mPixelShaderType;
        mCommandBufferPrimitiveTypes[slot] = (unsigned char)deferred.mPrimitiveType;
        mCommandBuffers[slot] = deferred.mBuffer;
        memcpy(&mCommandBufferMatrices[slot * 16], deferred.mMatrix, sizeof(float) * 16);
    }

    context.mDeferredBuffers.clear();
    LeaveCriticalSection(&mCommandBufferMutex);
}

Renderer::CommandBuffer::CommandBuffer(bool bRecordState)
{
    mRecordState = bRecordState;
    mVerticesUsed = 0;
    mVertices = static_cast<unsigned char*>(malloc(COMMAND_BUFFER_INITIAL_CAPACITY));
    mVerticesCapacity = COMMAND_BUFFER_INITIAL_CAPACITY;
    mHasVertices = true;
    mBuffer = 0;
    mVertexArray = 0;
    mVertexArrayLayout = -1;
    mQuadBuffer = 0;
    mQuadTexture = 0;
    mVertexTexture = 0;

    EnterCriticalSection(&sTotalAllocMutex);
    sTotalCommandBufferBytes += mVerticesCapacity;
    LeaveCriticalSection(&sTotalAllocMutex);

    mLastRenderedFrame = 0;
}

Renderer::CommandBuffer::~CommandBuffer()
{
    free(mVertices);

    Device* device = gRendererInstance->mDevice;
    if (device != NULL)
    {
        if (device->IsGlThread())
        {
            if (mVertexTexture != 0)
            {
                Gl::DeleteTextures(1, &mVertexTexture);
            }

            if (mQuadTexture != 0)
            {
                Gl::DeleteTextures(1, &mQuadTexture);
            }

            if (mQuadBuffer != 0)
            {
                Gl::DeleteBuffers(1, &mQuadBuffer);
            }
        }
        else
        {
            device->QueueTextureDelete(mVertexTexture);
            device->QueueTextureDelete(mQuadTexture);
            device->QueueBufferDelete(mQuadBuffer, 0);
        }
    }

    if (device != NULL && (mBuffer != 0 || mVertexArray != 0))
    {
        if (device->IsGlThread())
        {
            if (mVertexArray != 0)
            {
                if (device->mCurrentVertexArray == mVertexArray)
                {
                    device->mCurrentVertexArray = 0xFFFFFFFFu;
                }

                Gl::DeleteVertexArrays(1, &mVertexArray);
            }

            if (mBuffer != 0)
            {
                Gl::DeleteBuffers(1, &mBuffer);
            }
        }
        else
        {
            device->QueueBufferDelete(mBuffer, mVertexArray);
        }
    }

    EnterCriticalSection(&sTotalAllocMutex);
    sTotalCommandBufferBytes -= mVerticesCapacity;
    LeaveCriticalSection(&sTotalAllocMutex);
}

void Renderer::CommandBuffer::StartRecording()
{
}

void Renderer::CommandBuffer::EndRecording(unsigned int vertexStride)
{
    if (mVerticesUsed != 0)
    {
        return;
    }

    free(mVertices);
    mVertices = NULL;
    mHasVertices = false;
}

void Renderer::CommandBuffer::Render(C4JRender::eVertexType vertexType, Context& context, int primitiveType,
                                     bool bApplyMatrix, unsigned char layerMask)
{
    if (!mHasVertices)
    {
        return;
    }

    Renderer* renderer = gRendererInstance;
    Device* device = renderer->mDevice;
    static thread_local std::vector<unsigned int> batchOffset;
    static thread_local std::vector<int> batchCount;
    batchOffset.clear();
    batchCount.clear();
    bool bMatrixPushed = false;
    int shaderVertexType = vertexType;

    for (unsigned int index = 0; index < mCommands.size(); ++index)
    {
        const Command& command = mCommands[index];

        if (command.mType == eCommandType_Matrix)
        {
            if (shaderVertexType == C4JRender::eVertexType_Terrain)
            {
                if (!bApplyMatrix && command.mMatrix[13] == context.mLastQuadUniform[2]
                    && command.mMatrix[14] == context.mLastQuadUniform[3])
                {
                    continue;
                }

                renderer->SetVertexUniformReg(VERTEX_REG_LIGHTING, 4, &command.mMatrix[12]);
                context.mLastQuadUniform[2] = command.mMatrix[13];
                context.mLastQuadUniform[3] = command.mMatrix[14];
            }
            else
            {
                renderer->SetVertexUniformReg(VERTEX_REG_WORLD_VIEW2, 0x10, command.mMatrix);
                bMatrixPushed = true;
            }
        }
        else if (command.mType == eCommandType_Vertices)
        {
            if (mRecordState)
            {
                renderer->UpdateLightingState();

                if (shaderVertexType == C4JRender::eVertexType_Default && context.mLightingEnable)
                {
                    shaderVertexType = C4JRender::eVertexType_DefaultLighting;
                }
                else if (shaderVertexType == C4JRender::eVertexType_DefaultLighting && !context.mLightingEnable)
                {
                    shaderVertexType = C4JRender::eVertexType_Default;
                }
                else if (shaderVertexType == C4JRender::eVertexType_Entity && context.mLightingEnable)
                {
                    shaderVertexType = C4JRender::eVertexType_EntityLighting;
                }
                else if (shaderVertexType == C4JRender::eVertexType_EntityLighting && !context.mLightingEnable)
                {
                    shaderVertexType = C4JRender::eVertexType_Entity;
                }

                if (shaderVertexType != renderer->mCurrentVertexShader)
                {
                    context.mGpu.mVertexShader = shaderVertexType;
                    renderer->mCurrentVertexShader = shaderVertexType;
                }
            }

            unsigned int stride = VERTEX_STRIDES[shaderVertexType];

            if (shaderVertexType == C4JRender::eVertexType_Terrain)
            {
                if (!batchOffset.empty()
                    && command.mVertices.mOffset
                           == batchOffset.back() + (unsigned int)batchCount.back() * VERTEX_STRIDES[C4JRender::eVertexType_Terrain])
                {
                    batchCount.back() += (int)command.mVertices.mCount;
                }
                else
                {
                    batchOffset.push_back(command.mVertices.mOffset);
                    batchCount.push_back((int)command.mVertices.mCount);
                }
            }
            else
            {
                int layout = LayoutForVertexType(shaderVertexType);
                bool bPrepared = false;
                if (device != NULL)
                {
                    device->mPendingPrimitive = layout == VertexLayout_Standard ? primitiveType : -1;
                    bPrepared = device->PrepareDraw(*renderer, context);
                    device->mPendingPrimitive = -1;
                }

                if (bPrepared && EnsureUploaded(device, layout))
                {
                    device->BindVertexArray(mVertexArray);
                    if (device->mPackDraw && layout == VertexLayout_Standard && EnsureTerrainTextures())
                    {
                        ShaderPipeline::Get().BindTerrainData(mVertexTexture, 0);
                    }
                    device->DrawRange(primitiveType, command.mVertices.mOffset / stride, command.mVertices.mCount);
                }

                renderer->mCommandBufferVertsDrawn += command.mVertices.mCount;
            }
        }
    }

    if (!batchOffset.empty())
    {
        unsigned int stride = VERTEX_STRIDES[shaderVertexType];
        if (device != NULL)
        {
            device->mPendingTerrainQuadData = !mQuadData.empty() || mQuadTexture != 0;
        }

        bool reuseTerrainRaster = false;
#if LCE_PLATFORM_SWITCH
        reuseTerrainRaster = !bApplyMatrix && !mRecordState;
#endif
        bool bDraw = device != NULL && device->PrepareDraw(*renderer, context, reuseTerrainRaster)
                     && EnsureUploaded(device, LayoutForVertexType(shaderVertexType));

        if (device != NULL)
        {
            device->mPendingTerrainQuadData = false;
        }

        if (bDraw)
        {
            device->BindVertexArray(mVertexArray);
            if (device->mPackDraw && EnsureTerrainTextures())
            {
                ShaderPipeline::Get().BindTerrainData(mVertexTexture, mQuadTexture);
            }
        }

        for (size_t batch = 0; batch < batchOffset.size(); ++batch)
        {
            if (bDraw)
            {
                device->DrawRange(primitiveType, batchOffset[batch] / stride, (unsigned int)batchCount[batch]);
            }

            renderer->mCommandBufferVertsDrawn += batchCount[batch];
        }
    }

    if (bMatrixPushed)
    {
        float identity[16];
        MatrixIdentity(identity);
        renderer->SetVertexUniformReg(VERTEX_REG_WORLD_VIEW2, 0x10, identity);
    }

    mLastRenderedFrame = renderer->mFrameCount;
}

bool Renderer::CommandBuffer::EnsureUploaded(Device* device, int layout)
{
    if (mBuffer == 0)
    {
        if (mVertices == NULL || mVerticesUsed == 0)
        {
            return false;
        }

        Gl::GenBuffers(1, &mBuffer);
        Gl::BindBuffer(Gl::ARRAY_BUFFER, mBuffer);
        Gl::BufferData(Gl::ARRAY_BUFFER, (GLsizeiptr)mVerticesUsed, mVertices, Gl::STATIC_DRAW);
        free(mVertices);
        mVertices = NULL;
    }

    if (mVertexArray == 0)
    {
        Gl::GenVertexArrays(1, &mVertexArray);
        mVertexArrayLayout = -1;
    }

    if (mVertexArrayLayout != layout)
    {
        device->SetupVertexArray(mVertexArray, mBuffer, layout);
        mVertexArrayLayout = layout;
    }

    return true;
}

bool Renderer::CommandBuffer::EnsureTerrainTextures()
{
    if (mBuffer == 0)
    {
        return false;
    }

    if (mVertexTexture == 0)
    {
        Gl::GenTextures(1, &mVertexTexture);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, mVertexTexture);
        Gl::TexBuffer(Gl::TEXTURE_BUFFER, Gl::RGBA16I, mBuffer);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, 0);
    }

    if (mQuadTexture == 0 && !mQuadData.empty())
    {
        Gl::GenBuffers(1, &mQuadBuffer);
        Gl::BindBuffer(Gl::TEXTURE_BUFFER, mQuadBuffer);
        Gl::BufferData(Gl::TEXTURE_BUFFER, (GLsizeiptr)(mQuadData.size() * sizeof(unsigned short)), &mQuadData[0],
                       Gl::STATIC_DRAW);
        Gl::BindBuffer(Gl::TEXTURE_BUFFER, 0);
        Gl::GenTextures(1, &mQuadTexture);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, mQuadTexture);
        Gl::TexBuffer(Gl::TEXTURE_BUFFER, Gl::R16UI, mQuadBuffer);
        Gl::BindTexture(Gl::TEXTURE_BUFFER, 0);
        std::vector<unsigned short>().swap(mQuadData);
    }

    return true;
}

bool Renderer::CommandBuffer::IsBusy()
{
    return mLastRenderedFrame >= gRendererInstance->mFrameCount - 3;
}

void Renderer::CommandBuffer::AddMatrix(const float* matrix)
{
    Command command;
    command.mType = eCommandType_Matrix;
    memcpy(command.mMatrix, matrix, sizeof(command.mMatrix));
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::AddVertices(unsigned int vertexStride, unsigned int numVertices, void* vertices,
                                          Context& context, unsigned char* layers)
{
    if (context.mMatrixDirty[3])
    {
        AddMatrix(gRendererInstance->MatrixGet(3));
        context.mMatrixDirty[3] = false;
    }

    const unsigned short* quadData = context.mPendingQuadData;
    context.mPendingQuadData = NULL;

    Command command;
    unsigned int offset = mVerticesUsed;
    command.mType = eCommandType_Vertices;
    mVerticesUsed = offset + numVertices * vertexStride;

    bool bTrackQuads = vertexStride == VERTEX_STRIDES[C4JRender::eVertexType_Terrain]
                       && (quadData != NULL || !mQuadData.empty());
    if (bTrackQuads)
    {
        mQuadData.resize(offset / (vertexStride * 4), 0);
    }
    command.mVertices.mOffset = offset;
    command.mVertices.mCount = numVertices;

    if (mVerticesCapacity < mVerticesUsed)
    {
        EnterCriticalSection(&sTotalAllocMutex);
        sTotalCommandBufferBytes -= mVerticesCapacity;
        LeaveCriticalSection(&sTotalAllocMutex);

        mVerticesCapacity = (mVerticesUsed + 0xFFF) & ~0xFFFu;
        mVertices = static_cast<unsigned char*>(realloc(mVertices, mVerticesCapacity));

        EnterCriticalSection(&sTotalAllocMutex);
        sTotalCommandBufferBytes += mVerticesCapacity;
        LeaveCriticalSection(&sTotalAllocMutex);
    }

    if (layers == NULL)
    {
        memcpy(mVertices + offset, vertices, numVertices * vertexStride);
        if (bTrackQuads)
        {
            for (unsigned int quad = 0; quad < numVertices / 4; ++quad)
            {
                mQuadData.push_back(quadData != NULL ? quadData[quad] : 0);
            }
        }
        command.mVertices.mLayer = 0;
        mCommands.push_back(command);
        return;
    }

    unsigned int quadSize = vertexStride * 4;
    unsigned int destination = offset;

    for (unsigned char layer = 0; layer < 7; ++layer)
    {
        unsigned int count = 0;

        for (unsigned int quad = 0; quad < numVertices / 4; ++quad)
        {
            if (layers[quad] == layer)
            {
                memcpy(mVertices + destination, static_cast<unsigned char*>(vertices) + quad * quadSize, quadSize);
                if (bTrackQuads)
                {
                    mQuadData.push_back(quadData != NULL ? quadData[quad] : 0);
                }
                count += 4;
                destination += quadSize;
            }
        }

        if (count > 0)
        {
            command.mVertices.mLayer = layer;
            command.mVertices.mOffset = offset;
            command.mVertices.mCount = count;
            mCommands.push_back(command);
        }

        offset = destination;
    }
}

void Renderer::CommandBuffer::BindTexture(int texture)
{
    Command command;
    command.mType = eCommandType_BindTexture;
    command.mTexture = texture;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetColor(float red, float green, float blue, float alpha)
{
    Command command;
    command.mType = eCommandType_Colour;
    command.mColour[0] = red;
    command.mColour[1] = green;
    command.mColour[2] = blue;
    command.mColour[3] = alpha;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetDepthFunc(int depthFunc)
{
    Command command;
    command.mType = eCommandType_DepthFunc;
    command.mDepthFunc = depthFunc;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetDepthMask(bool bDepthMask)
{
    Command command;
    command.mType = eCommandType_DepthMask;
    command.mEnable = bDepthMask;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetDepthTestEnable(bool bDepthTest)
{
    Command command;
    command.mType = eCommandType_DepthTestEnable;
    command.mEnable = bDepthTest;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetLightingEnable(bool bLighting)
{
    Command command;
    command.mType = eCommandType_LightingEnable;
    command.mEnable = bLighting;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetLightEnable(int light, bool bEnable)
{
    Command command;
    command.mType = eCommandType_LightEnable;
    command.mLightEnable.mIndex = light;
    command.mLightEnable.mEnable = bEnable;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetLightDirection(int light, float x, float y, float z)
{
    Command command;
    command.mType = eCommandType_LightDirection;
    command.mLight.mIndex = light;

    Context& context = gRendererInstance->getContext();
    const float* matrix = context.mMatrixStack[3][context.mMatrixStackDepth[3]];
    float direction[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        direction[axis] = matrix[0 * 4 + axis] * x + matrix[1 * 4 + axis] * y + matrix[2 * 4 + axis] * z;
    }
    NormalizeVector(direction);

    command.mLight.mValue[0] = direction[0];
    command.mLight.mValue[1] = direction[1];
    command.mLight.mValue[2] = direction[2];
    command.mLight.mValue[3] = 0.0f;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetLightColour(int light, float red, float green, float blue)
{
    Command command;
    command.mType = eCommandType_LightColour;
    command.mLight.mIndex = light;
    command.mLight.mValue[0] = red;
    command.mLight.mValue[1] = green;
    command.mLight.mValue[2] = blue;
    command.mLight.mValue[3] = 0.0f;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetLightAmbientColour(float red, float green, float blue)
{
    Command command;
    command.mType = eCommandType_LightAmbientColour;
    command.mLight.mIndex = 0;
    command.mLight.mValue[0] = red;
    command.mLight.mValue[1] = green;
    command.mLight.mValue[2] = blue;
    command.mLight.mValue[3] = 0.0f;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetBlendEnable(bool bBlend)
{
    Command command;
    command.mType = eCommandType_BlendEnable;
    command.mEnable = bBlend;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetBlendFunc(int source, int destination)
{
    Command command;
    command.mType = eCommandType_BlendFunc;
    command.mBlendFunc.mSource = source;
    command.mBlendFunc.mDestination = destination;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetBlendFactor(unsigned int factor)
{
    Command command;
    command.mType = eCommandType_BlendFactor;
    command.mBlendFactor = factor;
    mCommands.push_back(command);
}

void Renderer::CommandBuffer::SetFaceCull(bool bFaceCull)
{
    Command command;
    command.mType = eCommandType_FaceCull;
    command.mEnable = bFaceCull;
    mCommands.push_back(command);
}

int Renderer::CommandBuffer::getNumVertices()
{
    if (!mHasVertices)
    {
        return 0;
    }

    int numVertices = 0;

    for (unsigned int index = 0; index < mCommands.size(); ++index)
    {
        if (mCommands[index].mType == eCommandType_Vertices)
        {
            numVertices += (int)mCommands[index].mVertices.mCount;
        }
    }

    return numVertices;
}

const char* PcGpuName()
{
    return sGpuName.c_str();
}
