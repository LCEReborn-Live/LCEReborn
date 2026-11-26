// Load real converted FUI resources through the game's UI factory. Uses isolated
// profiles via run_texture_pack_regression.py; never touches the running game.
#include "game/core/ConsoleUIController.hpp"
#include "game/ui/fui.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiReference.hpp"
#include "game/ui/fuiBitmap.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRenderNodeTimeline.hpp"
#include "game/ui/FJ_Label.hpp"
#include "game/ui/FJ_Label_Blink.hpp"
#include "game/client/render/Renderer.hpp"
#include "platform/pc/GlApi.hpp"
#include <png.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <set>

extern fui* gFui;

static void SaveUiImage(const std::string& path)
{
    GLint viewport[4];
    Gl::GetIntegerv(0x0BA2 /* GL_VIEWPORT */, viewport);
    const int width = viewport[2], height = viewport[3];
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    Gl::ReadPixels(0, 0, width, height, Gl::RGBA, Gl::UNSIGNED_BYTE, pixels.data());
    for (size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] = 255;
    png_image image = {};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_RGBA;
    // The renderer's offscreen UI framebuffer is already inverted for presentation.
    png_image_write_to_file(&image, path.c_str(), 0, pixels.data(), width * 4, NULL);
}

static fuiFile* LoadCheckMovie(const std::string& folder, const std::string& name, bool root)
{
    std::ifstream input(folder + "/movies/" + name, std::ios::binary | std::ios::ate);
    if (!input)
        return NULL;
    std::vector<unsigned char> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return reinterpret_cast<fuiFile*>(gFui->load({bytes.data(), static_cast<unsigned int>(bytes.size())}, root, 0));
}

static bool SetUiPreviewLabel(fuiFile* file, const char* path, const wchar_t* text)
{
    fuiRenderNode* node = file->getRootNode()->findNode(path);
    FJ_FuiNode* control = node ? node->getFuiNode() : NULL;
    if (control && control->isFJ_Label())
        control->asFJ_Label()->Init(text);
    else if (control && control->isFJ_Label_Blink())
        control->asFJ_Label_Blink()->Init(text);
    else
    {
        std::printf("MISSING PREVIEW LABEL: %s\n", path);
        return false;
    }
    return true;
}

static int RunTu80UiRegression()
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    const char* folderEnv = std::getenv("LCE_FUI_DIR");
    if (!folderEnv)
        return 2;
    const std::string folder(folderEnv);
    gConsoleUIController.SetupFont();
    const char* skins[] = {"skinWiiU.fui", "skinGraphics.fui", "skinGraphicsHud.fui", "skinGraphicsInGame.fui",
        "skinGraphicsLabels.fui", "skinLabels.fui", "skinInGame.fui", "skinHud.fui", "skin.fui"};
    std::vector<fuiFile*> libraries;
    for (const char* name : skins)
    {
        std::printf("LIBRARY: %s\n", name);
        fuiFile* file = LoadCheckMovie(folder, name, false);
        if (!file)
            return 3;
        if (libraries.empty())
            for (int i = 0; i < file->index; ++i)
                libraries.push_back(reinterpret_cast<fuiFile*>(gFui->getFuiFileFromIndex(i)));
        libraries.push_back(file);
        for (unsigned int i = 0; i < file->header.bitmapCount; ++i)
        {
            fuiBitmap* bitmap = &file->bitmaps[i];
            if (bitmap->imageDataSize == 0)
                continue;
            file->initialiseRenderTexture(bitmap, true);
            if (bitmap->textureId < 0)
            {
                std::printf("BAD TEXTURE: %s bitmap %u\n", name, i);
                return 8;
            }
            gRenderer->TextureFree(bitmap->textureId);
            gFui->removeManagedTexture(file, bitmap);
        }
        for (unsigned int i = 0; i < file->header.referenceCount; ++i)
            if (file->referenceData[i].symbolIndex == static_cast<unsigned int>(-1))
            {
                std::printf("UNRESOLVED: %s: %s\n", name, file->referenceData[i].name);
                return 4;
            }
    }
    std::ifstream scenes(folder + (std::getenv("LCE_FUI_ALL") ? "/verify-scenes.txt" : "/scenes.txt"));
    std::set<std::string> baselineMissing;
    std::ifstream baseline(folder + "/baseline-missing-exports.txt");
    std::string missing;
    while (std::getline(baseline, missing))
        baselineMissing.insert(missing);
    std::string name;
    const char* selected = std::getenv("LCE_FUI_CASE");
    int count = 0;
    while (std::getline(scenes, name))
    {
        if (selected && name != selected)
            continue;
        std::printf("SCENE: %s\n", name.c_str());
        fuiFile* file = LoadCheckMovie(folder, name, true);
        if (!file || !file->getRootNode())
            return 5;
        for (unsigned int i = 0; i < file->header.referenceCount; ++i)
            if (file->referenceData[i].symbolIndex == static_cast<unsigned int>(-1))
            {
                if (baselineMissing.count(name + ":" + file->referenceData[i].name))
                {
                    std::printf("UNCHANGED BASELINE REFERENCE: %s: %s\n", name.c_str(), file->referenceData[i].name);
                    continue;
                }
                std::printf("UNRESOLVED: %s: %s\n", name.c_str(), file->referenceData[i].name);
                return 6;
            }
        std::printf("LOADED: %s (%.1f x %.1f)\n", name.c_str(), file->getStageWidth(), file->getStageHeight());
        // Exercise the same label initialization used by the reported menus,
        // so captures show actual text instead of the FUI authoring placeholders.
        if (std::getenv("LCE_FUI_PREVIEW_LABELS"))
        {
            if (name == "LoadCreateJoinMenu720.fui"
                && (!SetUiPreviewLabel(file, "LoadGameTabTitle", L"Load")
                    || !SetUiPreviewLabel(file, "NewGameTabTitle", L"Create")
                    || !SetUiPreviewLabel(file, "JoinGameTabTitle", L"Join")))
                return 10;
            if (name == "LoadMenu720.fui"
                && (!SetUiPreviewLabel(file, "MainPanel.GameName", L"Tutorial")
                    || !SetUiPreviewLabel(file, "MainPanel.CreatedMode", L"Created in Survival Mode")
                    || !SetUiPreviewLabel(file, "MainPanel.Seed", L"Seed: 22873568274")))
                return 10;
        }
        const float background[] = {0.15f, 0.2f, 0.25f, 1.0f};
        gRenderer->StartFrame();
        gRenderer->SetLayout(Renderer::ELayout_Interface);
        gRenderer->SetClearColour(background);
        gRenderer->Clear(3);
        gFui->render(file, 0.0f, 0.0f, 1280.0f, 720.0f);
        if (std::getenv("LCE_FUI_CAPTURE"))
            SaveUiImage(folder + "/" + name + ".png");
        std::printf("RENDERED: %s\n", name.c_str());
        gFui->unloadScene(file);
        // Rapidly rendering every screen would otherwise outrun the normal
        // timed UI texture eviction and exhaust the renderer's 512 slots.
        for (fuiFile* library : libraries)
            for (unsigned int i = 0; i < library->header.bitmapCount; ++i)
            {
                fuiBitmap* bitmap = &library->bitmaps[i];
                if (bitmap->textureId < 0 && bitmap->imageDataSize != 0)
                {
                    std::printf("BAD TEXTURE AFTER RENDER: %s %u\n", library->header.name, i);
                    return 9;
                }
                if (bitmap->textureId > 0)
                {
                    gRenderer->TextureFree(bitmap->textureId);
                    gFui->removeManagedTexture(library, bitmap);
                }
            }
        ++count;
    }
    std::printf("PASS: TU80 FUI factory loading (%d screens)\n", count);
    return count > 0 ? 0 : 7;
}
