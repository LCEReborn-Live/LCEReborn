// Included by the regression runner in a copy of the PC entry point after the
// usual game initialisation. Uses prepared game data and the bundled 1.14 pack.
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include <cstring>

static int RunTexturePackRegression()
{
    Minecraft* minecraft = Minecraft::GetInstance();
    TexturePackRepository* repository = minecraft->mTexturePackRepository;
    gApp.StartInstallDLCProcess(0);
    for (int attempt = 0; attempt < 5000; ++attempt)
    {
        gStorage->Tick();
        PcPumpEvents();
        TexturePack* builtIn = TexturePackRepository::defaultTexturePack;
        if (gApp.DLCInstallProcessCompleted() && builtIn->hasData() && !builtIn->isLoadingData())
            break;
        Sleep(1);
    }
    TexturePack* builtIn = TexturePackRepository::defaultTexturePack;
    TexturePack* legacy = repository->getTexturePackById(0x00fffffe);
    if (legacy == NULL || builtIn->getDLCPack() == NULL || !builtIn->hasValidData())
    {
        std::fprintf(stderr, "FAIL: bundled 1.14 pack and Old Textures must both be loaded\n");
        return 2;
    }

    int structures = 0;
    for (TexturePack* pack : {builtIn, legacy})
    {
        repository->selectSkin(pack);
        // Recreate the manager to exercise loading after a texture pack reload.
        for (int reload = 0; reload < 2; ++reload)
        {
            pack->loadStructureManager();
            StructureManager* manager = pack->getStructureManager();
            for (int i = 0; i < eMinecraftStructureTemplate_Count; ++i)
            {
                const StructureTemplate* structure = manager->get(static_cast<eMinecraftStructureTemplate>(i));
                BlockPos size = structure->getSize();
                if (size.x <= 0 || size.y <= 0 || size.z <= 0)
                {
                    std::fprintf(stderr, "FAIL: empty structure %d in pack %u\n", i, pack->getId());
                    return 3;
                }
                ++structures;
            }
        }
    }
    std::printf("PASS: %d structure loads across both packs and manager reloads\n", structures);
    std::fflush(stdout);

    for (int pass = 0; pass < 4; ++pass)
    {
        TexturePack* pack = pass % 2 == 0 ? legacy : builtIn;
        repository->selectSkin(pack);
        minecraft->mTextures->reloadAll();
        if (pack == legacy)
        {
            BufferedImage* actual = minecraft->mTextures->readImage(TN_MOB_CAVESPIDER, L"mob/cavespider");
            BufferedImage expected(L"/1_2_2/mob/cavespider.png", false, false, L"");
            bool same = actual->getWidth() == expected.getWidth() && actual->getHeight() == expected.getHeight()
                && std::memcmp(actual->getData(), expected.getData(), expected.getWidth() * expected.getHeight() * sizeof(int)) == 0;
            delete actual;
            if (!same)
            {
                std::fprintf(stderr, "FAIL: Old Textures cave spider pixels differ from the base game\n");
                return 4;
            }
        }
        std::printf("PASS: pack %u indexed textures, atlases and colour maps reloaded\n", pack->getId());
        std::fflush(stdout);
    }
    std::printf("PASS: Old Textures / 1.14 switching and original cave spider pixels\n");
    Log::Stop();
    return 0;
}
