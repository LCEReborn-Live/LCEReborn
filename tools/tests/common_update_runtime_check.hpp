// Runs against imported Common resources; LCE_COMMON_BASELINE points to the
// unmodified prepared assets used to exercise atlas changes in both directions.
#include "game/client/render/DefaultTexturePack.hpp"
#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/TextureAtlas.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/ByteBuffer.hpp"
#include "game/core/L10N.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/Direction.hpp"
#include "game/item/Items.hpp"
#include "game/item/CrossbowItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

class BaselineAtlasPack : public DefaultTexturePack
{
public:
    BaselineAtlasPack(const std::wstring& path) : DefaultTexturePack(0x00fffffd), mPath(path) {}

    BufferedImage* getImageResource(const std::wstring& name, bool single, bool update, const std::wstring& path)
    {
        if (name == L"items" || name == L"terrain")
            return new BufferedImage(L"/" + name + L".png", single, true, mPath);
        return DefaultTexturePack::getImageResource(name, single, update, path);
    }
private:
    std::wstring mPath;
};

static bool CheckCommonCell(TextureAtlasSprite* icon, int column, int row, int atlasHeight)
{
    if (!icon)
        return false;
    const int cell = icon->getSourceWidth() / 16;
    return icon->getX() == column * cell && icon->getY() == row * cell
        && icon->getWidth() == cell && icon->getHeight() == cell
        && icon->getSourceHeight() == atlasHeight
        && std::fabs(icon->getV0(false) - float(row * cell) / atlasHeight) < 0.00001f;
}

static int RunCommonUpdateRegression()
{
    Minecraft* minecraft = Minecraft::GetInstance();
    TexturePackRepository* repository = minecraft->mTexturePackRepository;
    gApp.StartInstallDLCProcess(0);
    for (int attempt = 0; attempt < 5000 && !gApp.DLCInstallProcessCompleted(); ++attempt)
    {
        gStorage->Tick();
        PcPumpEvents();
        Sleep(1);
    }
    TexturePack* updated = TexturePackRepository::defaultTexturePack;
    if (!gApp.DLCInstallProcessCompleted() || updated->getDLCPack() != NULL)
    {
        std::fprintf(stderr, "FAIL: an older bundled pack replaced the Common update\n");
        return 2;
    }
    const wchar_t* models[] = {L"cat", L"cod", L"cow", L"ghast", L"llama", L"llamaspit", L"ocelot",
        L"panda", L"pig", L"pillager", L"pufferfish.large", L"pufferfish.mid", L"pufferfish.small", L"rabbit",
        L"ravager", L"salmon", L"spider", L"tropicalfish_a", L"tropicalfish_b", L"villager_v2",
        L"villager.witch", L"zombie.villager_v2"};
    for (const wchar_t* name : models)
        if (!updated->HasCustomModel(name) || updated->GetCustomModel(name)->getTextureWidth() < 32)
        {
            std::fprintf(stderr, "FAIL: version 2 model missing or corrupt\n");
            return 3;
        }
    if (!updated->GetCustomModel(L"ocelot")->hasModelPart(L"tail2")
        || !updated->GetCustomModel(L"zombie.villager_v2")->hasModelPart(L"leftLeg"))
        return 3;
    std::printf("PASS: 22 version 2 models parsed through the last part\n");

    if (!gApp.hasArchiveFile(L"Structures\\pillageroutpost\\feature_cage1", true)
        || !gApp.hasArchiveFile(L"Structures\\village\\plains\\houses\\plains_fletcher_house_1", true)
        || std::wcslen(L10N::GetString(IDS_ITEM_DIAMOND)) == 0
        || std::wcslen(L10N::GetString(IDS_TILE_STONE)) == 0)
        return 4;
    updated->loadStructureManager();
    StructureManager* manager = updated->getStructureManager();
    for (int i = 0; i < eMinecraftStructureTemplate_Count; ++i)
    {
        BlockPos size = manager->get(static_cast<eMinecraftStructureTemplate>(i))->getSize();
        if (size.x <= 0 || size.y <= 0 || size.z <= 0)
            return 4;
    }
    std::printf("PASS: existing structures, new archive entries and localized strings\n");

    const char* baseline = std::getenv("LCE_COMMON_BASELINE");
    if (!baseline)
        return 5;
    const std::string baselineText(baseline);
    BaselineAtlasPack old(std::wstring(baselineText.begin(), baselineText.end()) + L"/Common/res/TitleUpdate/");
    for (int pass = 0; pass < 5; ++pass)
    {
        const bool useUpdate = pass % 2 == 0;
        repository->selectSkin(useUpdate ? updated : &old);
        minecraft->mTextures->reloadAll();
        Texture* items = Items::arrow->getIcon(0)->getSource();
        const int itemsHeight = useUpdate ? 288 : 272;
        const int blocksHeight = useUpdate ? 624 : 544;
        not_null_ptr<ItemInstance> crossbow(new ItemInstance(Items::crossbow));
        if (!CheckCommonCell(Items::arrow->getIcon(0), 5, 2, itemsHeight)
            || !CheckCommonCell(Items::phantomMembrane->getIcon(0), 15, 16, itemsHeight)
            || !CheckCommonCell(Items::crossbow->getIcon(0), useUpdate ? 5 : 2, useUpdate ? 17 : 16, itemsHeight)
            || !CheckCommonCell(static_cast<CrossbowItem*>(Items::crossbow)->getPullingIcon(0, crossbow),
                                useUpdate ? 6 : 2, useUpdate ? 17 : 16, itemsHeight)
            || !CheckCommonCell(Blocks::stone->getTexture(Direction::UP), 1, 0, blocksHeight)
            || !CheckCommonCell(Blocks::jungle_trapdoor->getTexture(Direction::UP), 15, 33, blocksHeight))
        {
            std::fprintf(stderr, "FAIL: shifted atlas coordinates on reload %d\n", pass);
            return 6;
        }
        if (useUpdate)
        {
            BufferedImage source(L"/items.png", true, true, L"");
            const unsigned char* actual = items->getData(0)->getBuffer();
            // Include all native crossbow states and adjacent new items. The
            // update must not be painted over by the old placeholder sprites.
            for (int y = 256; y < 288; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    const unsigned int pixel = source.getData()[y * 256 + x];
                    const unsigned char* out = actual + (y * 256 + x) * 4;
                    if (out[0] != (pixel >> 16 & 255) || out[1] != (pixel >> 8 & 255)
                        || out[2] != (pixel & 255) || out[3] != (pixel >> 24))
                    {
                        std::fprintf(stderr, "FAIL: altered native item pixel at %d,%d\n", x, y);
                        return 7;
                    }
                }
        }
        std::printf("PASS: atlas reload %d (%d / %d rows of pixels)\n", pass, itemsHeight, blocksHeight);
        std::fflush(stdout);
    }
    std::printf("PASS: Common update resources, models and atlas switching\n");
    Log::Stop();
    return 0;
}
