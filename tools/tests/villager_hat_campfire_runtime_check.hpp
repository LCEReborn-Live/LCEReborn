#include "game/client/render/ModelPart.hpp"
#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/ModelPartDefinition.hpp"
#include "game/client/render/TexturePack.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/BufferBuilder.hpp"
#include "game/core/Cube.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Mth.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/LevelListener.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/CampfireBlock.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace HatCampfireCheck
{
class Listener : public LevelListener
{
public:
    int tick = 0;
    std::vector<int> emissions;
    ePARTICLE_TYPE lastType = ePARTICLE_CAMPFIRE_SMOKE;
    void blockChanged(Level*, const BlockPos&, const BlockState*, const BlockState*, int, bool) override {}
    void blockLightChanged(const BlockPos&) override {}
    void setBlocksDirty(int, int, int, int, int, int, Level*) override {}
    void allChanged() override {}
    void playStreamingMusic(const SoundEvent*, const BlockPos&, bool) override {}
    void playSound(boost::shared_ptr<Player>, const SoundEvent*, SoundSource::ESoundSource,
                   double, double, double, float, float, float) override {}
    void addParticle(Level*, ePARTICLE_TYPE type, bool, double, double, double,
                     double, double, double, arrayWithLength<int>) override
    {
        emissions.push_back(tick);
        lastType = type;
    }
    void addParticle(Level* level, ePARTICLE_TYPE type, bool limiter, bool, double x, double y, double z,
                     double xa, double ya, double za, arrayWithLength<int> params) override
    { addParticle(level, type, limiter, x, y, z, xa, ya, za, params); }
    void entityAdded(boost::shared_ptr<Entity>) override {}
    void entityRemoved(boost::shared_ptr<Entity>) override {}
    void playerRemoved(boost::shared_ptr<Entity>) override {}
    void globalLevelEvent(int, const BlockPos&, int) override {}
    void levelEvent(boost::shared_ptr<Player>, int, const BlockPos&, int) override {}
    void destroyBlockProgress(int, const BlockPos&, int) override {}
};

bool bounds(BufferBuilder& buffer, float x0, float y0, float z0, float x1, float y1, float z1)
{
    const auto& b = buffer.mBounds;
    return std::fabs(b.mMinX - x0) < .002f && std::fabs(b.mMinY - y0) < .002f
        && std::fabs(b.mMinZ - z0) < .002f && std::fabs(b.mMaxX - x1) < .002f
        && std::fabs(b.mMaxY - y1) < .002f && std::fabs(b.mMaxZ - z1) < .002f;
}
}

static int RunHatCampfireRegression()
{
    using namespace HatCampfireCheck;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BufferBuilder buffer(4096);
    ModelPart part;
    const float halfPi = 90 * Mth::DEG_TO_RAD;
    const float cases[][9] = {
        {0, 0, 0, 1, 2, 3, 3, 6, 9},
        {halfPi, 0, 0, 1, -9, 2, 3, -3, 6},
        {-halfPi, 0, 0, 1, 3, -6, 3, 9, -2},
        {0, halfPi, 0, 3, 2, -3, 9, 6, -1},
        {0, -halfPi, 0, -9, 2, 1, -3, 6, 3},
        {0, 0, halfPi, -6, 1, 3, -2, 3, 9},
        {0, 0, -halfPi, 2, -3, 3, 6, -1, 9},
    };
    for (const auto& c : cases)
    {
        Cube cube(&part, 0, 0, 1, 2, 3, 2, 4, 6, 0, false, Cube::FACE_ALL, false, c[0], c[1], c[2]);
        buffer.begin();
        cube.renderSkinned(&buffer, 1, 0);
        if (!bounds(buffer, c[3], c[4], c[5], c[6], c[7], c[8])) return 1;
    }
    auto* pack = TexturePackRepository::defaultTexturePack;
    pack->loadCustomModels();
    auto* model = pack->GetCustomModel(L"villager_v2");
    if (!model || !model->hasModelPart(L"brim")) return 2;
    ModelPart brim;
    brim.setTexSize(model->getTextureWidth(), model->getTextureHeight());
    (*model)[L"brim"].SetupModelPart(&brim, nullptr, 0);
    buffer.begin();
    for (auto* cube : brim.mCubes) cube->renderSkinned(&buffer, 1, 0);
    if (!bounds(buffer, -8.1f, -6.1f, -8.1f, 8.1f, -4.9f, 8.1f)) return 3;
    std::puts("PASS: cube rotations on all three axes and horizontal imported farmer hat brim");

    auto* level = new MultiPlayerLevel(nullptr, new LevelSettings(1, GameType::SURVIVAL, false, false, false,
        LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0), 0, Difficulty::NORMAL, 63);
    CGameNetworkManager::sInstance.Initialise();
    level->setChunkVisible(0, 0, true, false);
    level->mIsClientSide = false;
    const BlockPos pos(8, 64, 8);
    const auto* lit = Blocks::campfire->defaultBlockState();
    level->setBlock(pos, lit, Block::UPDATE_CLIENTS, false);
    auto fire = boost::dynamic_pointer_cast<CampfireBlockEntity>(level->getBlockEntity(pos));
    if (!fire) return 4;
    Listener listener;
    level->addListener(&listener);
    level->mIsClientSide = true;
    Random random(42);
    for (listener.tick = 0; listener.tick < 2400; ++listener.tick)
    {
        fire->tick();
        // Animation calls must neither add extra puffs nor control the timer.
        if (listener.tick % 7 == 0)
            Blocks::campfire->animateTick(lit, level, pos, &random);
    }
    if (listener.emissions.size() < 40 || listener.emissions.size() > 61
        || listener.lastType != ePARTICLE_CAMPFIRE_SMOKE) return 5;
    for (size_t i = 1; i < listener.emissions.size(); ++i)
    {
        const int gap = listener.emissions[i] - listener.emissions[i - 1];
        if (gap < 40 || gap > 60) return 6;
    }
    std::printf("PASS: %zu smoke puffs over 120 seconds, every interval within 40-60 ticks\n",
                listener.emissions.size());
    level->setBlock(pos, Blocks::campfire->getBlockState(4), Block::UPDATE_CLIENTS, false);
    const auto count = listener.emissions.size();
    for (int i = 0; i < 100; ++i) fire->tick();
    if (listener.emissions.size() != count) return 7;
    level->setBlock(pos.below(), Blocks::hay_block->defaultBlockState(), Block::UPDATE_CLIENTS, false);
    level->setBlock(pos, lit, Block::UPDATE_CLIENTS, false);
    fire->tick();
    if (listener.emissions.size() != count + 1 || listener.lastType != ePARTICLE_CAMPFIRE_SIGNAL_SMOKE) return 8;
    level->mIsClientSide = false;
    for (int i = 0; i < 100; ++i) fire->tick();
    if (listener.emissions.size() != count + 1) return 9;
    level->removeListener(&listener);
    std::puts("PASS: extinguishing, relighting, hay signal smoke and no server-side duplicate particles");
    std::puts("PASS: farmer hat and campfire smoke regression");
    return 0;
}
