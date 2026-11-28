#include "game/client/render/VillagerRenderer.hpp"

#include <boost/shared_ptr.hpp>
#include <vector>

#include "game/client/render/TextureAtlasSprite.hpp"
#include "game/core/BufferBuilder.hpp"
#include "game/client/render/EntityRenderDispatcher.hpp"
#include "game/client/render/LivingEntityRenderer.hpp"
#include "game/client/render/Model.hpp"
#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/TexturePack.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/client/render/Tesselator.hpp"
#include "game/client/render/Textures.hpp"
#include "game/client/render/VillagerModel.hpp"
#include "game/client/render/ModelPart.hpp"
#include "game/client/render/RenderLayer.hpp"
#include "game/client/render/ItemInHandRenderer.hpp"
#include "game/item/ItemTransforms.hpp"
#include <cmath>
#include "game/core/GlStateManager.hpp"
#include "game/core/File.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Options.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/item/Item.hpp"
#include "game/item/Items.hpp"
#include "game/item/not_null_ptr.hpp"
#include "game/util/ResourceLocation.hpp"
#include "game/world/Level.hpp"
#include "game/world/biome/Biome.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/generation/CustomHeadLayer.hpp"
#include "game/world/generation/Villager.hpp"

static ResourceLocation VILLAGER_LOCATION(TN_MOB_VILLAGER_VILLAGER);
static const ResourceLocation WANDERING_TRADER_LOCATION(L"mob/wandering_trader");

namespace
{
const ResourceLocation tradeBadges[] = {
    ResourceLocation(L"mob/villager/levels/level_iron"),
    ResourceLocation(L"mob/villager/levels/level_gold"),
    ResourceLocation(L"mob/villager/levels/level_diamond"),
};

class WanderingTraderModel : public VillagerModel
{
public:
    explicit WanderingTraderModel(const ModelDefinition& definition = ModelDefinition())
        : VillagerModel(0.0f, definition) {}

    void setupAnim(float swing, float amount, float age, float yaw, float pitch, float scale,
                   boost::shared_ptr<Entity> entity, _SkinAdjustments adjustments) override
    {
        VillagerModel::setupAnim(swing, amount, age, yaw, pitch, scale, entity, adjustments);
        auto living = boost::static_pointer_cast<LivingEntity>(entity);
        if (living->isUsingItem())
        {
            mArms->mXRot = -1.2f;
            mArms->mY = 1.0f;
        }
    }
};

class TraderDrinkLayer : public RenderLayer
{
public:
    explicit TraderDrinkLayer(WanderingTraderRenderer* renderer) : mRenderer(renderer) {}

    void render(boost::shared_ptr<LivingEntity>& entity, float, float, float partialTicks,
                float, float, float, float scale, bool) override
    {
        const auto item = entity->getMainHandItem();
        if (item->isEmpty() || (item->getItem() != Items::potion && item->getItem() != Items::milkBucket))
            return;

        GlStateManager::pushMatrix();
        auto* model = static_cast<VillagerModel*>(mRenderer->getModel());
        model->mHead->translateTo(scale);
        const float bob = entity->isUsingItem()
                              ? 0.015f * std::sin((entity->mTickCount + partialTicks) * 1.5f) : 0.0f;
        GlStateManager::translatef(0.0f, -0.15f + bob, -0.45f);
        GlStateManager::rotatef(180.0f, 0.0f, 0.0f, 1.0f);
        GlStateManager::rotatef(140.0f, 1.0f, 0.0f, 0.0f);
        GlStateManager::rotatef(10.0f, 0.0f, 0.0f, 1.0f);
        Minecraft::GetInstance()->getItemInHandRenderer()->renderItem(
            entity, item, ItemTransforms::GROUND, false, 0, true);
        GlStateManager::popMatrix();
    }

    bool colorsOnDamage() override { return false; }

private:
    WanderingTraderRenderer* mRenderer;
};

class VillagerTradeLayer : public RenderLayer
{
public:
    explicit VillagerTradeLayer(VillagerRenderer* renderer) : mRenderer(renderer) {}

    void setupForTexturePack(TexturePack* pack) override
    {
        const std::wstring root = pack->getPath(true, nullptr) + L"res/";
        for (size_t i = 0; i < sizeof(tradeBadges) / sizeof(*tradeBadges); ++i)
        {
            mHasBadge[i] = mRenderer->getModelName() == L"villager_v2"
                && File(root + tradeBadges[i].getPath() + L".png").exists();
        }
    }

    void render(boost::shared_ptr<LivingEntity>& entity, float swing, float amount, float, float age,
                float yaw, float pitch, float scale, bool displayList) override
    {
        auto villager = boost::static_pointer_cast<Villager>(entity);
        auto* model = static_cast<VillagerModel*>(mRenderer->getModel());
        const int level = villager->getTradeLevel();
        const size_t badge = level == 1 ? 0 : level == 5 ? 2 : 1;
        if (level > 0 && !villager->isBaby() && mHasBadge[badge])
        {
            mRenderer->bindTexture(&tradeBadges[badge]);
            boost::shared_ptr<Entity> base = entity;
            model->render(base, swing, amount, age, yaw, pitch, scale, displayList);
        }

        const auto preview = villager->getTradePreview();
        if (preview->isEmpty())
            return;

        GlStateManager::pushMatrix();
        model->mArms->translateTo(scale);
        GlStateManager::translatef(0.0f, 0.18f, -0.24f);
        GlStateManager::rotatef(180.0f, 0.0f, 0.0f, 1.0f);
        GlStateManager::rotatef(120.0f, 1.0f, 0.0f, 0.0f);
        Minecraft::GetInstance()->getItemInHandRenderer()->renderItem(
            entity, preview, ItemTransforms::GROUND, false, 0, true);
        GlStateManager::popMatrix();
    }

    bool colorsOnDamage() override { return true; }

private:
    VillagerRenderer* mRenderer;
    bool mHasBadge[sizeof(tradeBadges) / sizeof(*tradeBadges)] = {};
};
}

WanderingTraderRenderer::WanderingTraderRenderer(EntityRenderDispatcher* dispatcher)
    : MobRenderer(dispatcher, new WanderingTraderModel(), 0.5f)
{
    addLayer(new TraderDrinkLayer(this));
}

const ResourceLocation* WanderingTraderRenderer::getTextureLocation(boost::shared_ptr<Entity>&)
{
    return &WANDERING_TRADER_LOCATION;
}

Model* WanderingTraderRenderer::createModel(const ModelDefinition& definition)
{
    return new WanderingTraderModel(definition);
}
static ResourceLocation FARMER_LOCATION(TN_MOB_VILLAGER_FARMER);
static ResourceLocation LIBRARIAN_LOCATION(TN_MOB_VILLAGER_LIBRARIAN);
static ResourceLocation PRIEST_LOCATION(TN_MOB_VILLAGER_PRIEST);
static ResourceLocation SMITH_LOCATION(TN_MOB_VILLAGER_SMITH);
static ResourceLocation BUTCHER_LOCATION(TN_MOB_VILLAGER_BUTCHER);

namespace
{
enum VillagerBiome { Plains, Desert, Jungle, Savanna, Snow, Swamp, Taiga };
enum VillagerJob
{
    Unskilled, Farmer, Fisherman, Shepherd, Fletcher, Librarian, Cartographer,
    Cleric, Armorer, Weaponsmith, Toolsmith, Butcher, Leatherworker, Nitwit, Mason
};

constexpr const wchar_t* BIOMES[] = {L"plains", L"desert", L"jungle", L"savanna", L"snow", L"swamp", L"taiga"};
constexpr const wchar_t* JOBS[] = {
    L"unskilled", L"farmer", L"fisherman", L"shepherd", L"fletcher", L"librarian", L"cartographer",
    L"cleric", L"armorer", L"weaponsmith", L"toolsmith", L"butcher", L"leatherworker", L"nitwit", L"stonemason",
};

const std::vector<ResourceLocation>& tu80Textures()
{
    static const auto locations = [] {
        std::vector<ResourceLocation> result;
        result.reserve(sizeof(BIOMES) / sizeof(*BIOMES) * (sizeof(JOBS) / sizeof(*JOBS)));
        for (const auto* biome : BIOMES)
            for (const auto* job : JOBS)
                result.emplace_back(std::wstring(L"mob/villager/tu80/") + biome + L"_" + job);
        return result;
    }();
    return locations;
}

VillagerBiome getOutfitBiome(Villager& villager)
{
    Biome* biome = villager.mLevel->getBiome(BlockPos(&villager));
    if (biome->isSnowCovered())
        return Snow;
    switch (biome->getBaseClass())
    {
    case Biome::eBiomeBaseClass_Desert: return Desert;
    case Biome::eBiomeBaseClass_Jungle: return Jungle;
    case Biome::eBiomeBaseClass_Savanna: return Savanna;
    case Biome::eBiomeBaseClass_Swamp: return Swamp;
    case Biome::eBiomeBaseClass_Taiga: return Taiga;
    default: return Plains;
    }
}

VillagerJob getOutfitJob(Villager& villager)
{
    if (villager.isBaby())
        return Unskilled;
    if (villager.getProfession() == Villager::UNEMPLOYED)
        return Unskilled;
    if (villager.getProfession() == Villager::MASON)
        return Mason;

    static constexpr VillagerJob careers[][5] = {
        {Unskilled, Farmer, Fisherman, Shepherd, Fletcher},
        {Unskilled, Librarian, Cartographer, Unskilled, Unskilled},
        {Cleric, Cleric, Unskilled, Unskilled, Unskilled},
        {Unskilled, Armorer, Weaponsmith, Toolsmith, Unskilled},
        {Unskilled, Butcher, Leatherworker, Unskilled, Unskilled},
        {Nitwit, Nitwit, Unskilled, Unskilled, Unskilled},
    };
    const unsigned int career = villager.getCareer();
    return career < 5 ? careers[villager.getProfession()][career] : Unskilled;
}
}

VillagerRenderer::VillagerRenderer(EntityRenderDispatcher* dispatcher)
    : MobRenderer(dispatcher, new VillagerModel(0.0F), 0.5F)
{
    mHeadLayer = new CustomHeadLayer(static_cast<VillagerModel*>(getModel())->mHead);
    addLayer(new VillagerTradeLayer(this));
    addLayer(mHeadLayer);
    if (TexturePackRepository::defaultTexturePack != NULL)
        setupForTexturePack(TexturePackRepository::defaultTexturePack);
}

void VillagerRenderer::setupForTexturePack(TexturePack* pack)
{
    mUseTu80Appearance = pack == TexturePackRepository::defaultTexturePack && pack->HasCustomModel(L"villager_v2");
    mMasonOutfits.assign(sizeof(BIOMES) / sizeof(*BIOMES), false);
    if (mUseTu80Appearance)
    {
        const std::wstring path = pack->getPath(true, NULL) + L"res/";
        constexpr auto jobCount = sizeof(JOBS) / sizeof(*JOBS);
        for (size_t i = 0; i < tu80Textures().size(); ++i)
        {
            const bool found = File(path + tu80Textures()[i].getPath() + L".png").exists();
            if (i % jobCount == Mason)
            {
                mMasonOutfits[i / jobCount] = found;
            }
            else if (!found)
            {
                mUseTu80Appearance = false;
                break;
            }
        }
    }
    MobRenderer::setupForTexturePack(pack);
    mHeadLayer->mHead = static_cast<VillagerModel*>(getModel())->mHead;
}

std::wstring VillagerRenderer::getModelName()
{
    return mUseTu80Appearance ? L"villager_v2" : L"";
}

Model* VillagerRenderer::getModel()
{
    return LivingEntityRenderer::getModel();
}

void VillagerRenderer::render(boost::shared_ptr<Entity>& entity, double x, double y, double z, float yaw,
                              float partialTick)
{
    MobRenderer::render(entity, x, y, z, yaw, partialTick);

    boost::shared_ptr<Villager> villager = boost::static_pointer_cast<Villager>(entity);
    if (villager->getTrading() != 0)
    {
        not_null_ptr<ItemInstance> item = ItemInstance::EMPTY;
        item = not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald, 1));
        Item* itemType = item->getItem();
        TextureAtlasSprite* sprite = itemType->getIcon(item);

        mDispatcher = Minecraft::GetInstance()->getEntityRenderDispatcher();

        const float height = entity->mBbHeight + 0.5F;
        GlStateManager::pushMatrix();
        GlStateManager::translatef((float)x, (float)y + height, (float)z);
        GlStateManager::normal3f(0.0F, 1.0F, 0.0F);
        GlStateManager::rotatef(180.0F - mDispatcher->mCameraYRot, 0.0F, 1.0F, 0.0F);
        const float pitchDirection = mDispatcher->mOptions->mThirdPersonView == 2 ? -1.0F : 1.0F;
        GlStateManager::rotatef(-(pitchDirection * mDispatcher->mCameraXRot), 1.0F, 0.0F, 0.0F);
        GlStateManager::scalef(0.5F, 0.5F, 0.5F);
        GlStateManager::enableBlend();
        GlStateManager::blendFunc(4, 5);

        Tesselator* tesselator = Tesselator::getInstance();
        BufferBuilder* builder = tesselator->getBuilder();
        bindTexture(&ITEM_LOCATION);
        const float u0 = (float)sprite->getU0(false);
        const float u1 = (float)sprite->getU1(false);
        const float v0 = (float)sprite->getV0(false);
        const float v1 = (float)sprite->getV1(false);

        builder->begin();
        builder->color(0xFFFFFF, 0x80);
        builder->normal(0.0F, 1.0F, 0.0F);
        builder->vertexUV(-0.5F, -0.25F, 0.0F, u0, v1);
        builder->vertexUV(0.5F, -0.25F, 0.0F, u1, v1);
        builder->vertexUV(0.5F, 0.75F, 0.0F, u1, v0);
        builder->vertexUV(-0.5F, 0.75F, 0.0F, u0, v0);
        builder->end();

        GlStateManager::disableBlend();
        GlStateManager::popMatrix();
    }
}

const ResourceLocation* VillagerRenderer::getTextureLocation(boost::shared_ptr<Entity>& entity)
{
    boost::shared_ptr<Villager> villager = boost::static_pointer_cast<Villager>(entity);
    if (mUseTu80Appearance)
    {
        constexpr auto jobCount = sizeof(JOBS) / sizeof(*JOBS);
        const auto biome = getOutfitBiome(*villager);
        auto job = getOutfitJob(*villager);
        if (job == Mason && !mMasonOutfits[biome])
            job = Unskilled;
        const auto index = biome * jobCount + job;
        return &tu80Textures()[index];
    }
    switch (villager->getProfession())
    {
    case 0:
        return &FARMER_LOCATION;
    case 1:
        return &LIBRARIAN_LOCATION;
    case 2:
        return &PRIEST_LOCATION;
    case 3:
    case Villager::MASON:
        return &SMITH_LOCATION;
    case 4:
        return &BUTCHER_LOCATION;
    }
    return &VILLAGER_LOCATION;
}

void VillagerRenderer::scale(boost::shared_ptr<LivingEntity>& entity, float)
{
    boost::shared_ptr<Villager> villager = boost::static_pointer_cast<Villager>(entity);
    float scale = 0.9375F;
    if (villager->getAge() < 0)
    {
        scale = (float)(scale * 0.5);
        mShadowRadius = 0.25F;
    }
    else
    {
        mShadowRadius = 0.5F;
    }
    GlStateManager::scalef(scale, scale, scale);
}

void VillagerRenderer::setupRotations(boost::shared_ptr<LivingEntity>& entity, float ageInTicks, float yaw,
                                      float partialTick)
{
    auto* villager = static_cast<Villager*>(entity.get());
    if (!villager->isSleeping())
    {
        MobRenderer::setupRotations(entity, ageInTicks, yaw, partialTick);
        return;
    }
    GlStateManager::rotatef(villager->getSleepRotation(), 0.0f, 1.0f, 0.0f);
    GlStateManager::rotatef(90.0f, 0.0f, 0.0f, 1.0f);
    GlStateManager::rotatef(270.0f, 0.0f, 1.0f, 0.0f);
}

Model* VillagerRenderer::createModel(const ModelDefinition& definition)
{
    return new VillagerModel(0.0F, definition);
}
