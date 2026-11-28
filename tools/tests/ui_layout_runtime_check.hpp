// Check initialized controls, not just whether FUI files can be loaded.
#include "tu80_ui_runtime_check.hpp"
#include "game/ui/FJ_InventoryMenu.hpp"
#include "game/ui/FJ_ProgressBar.hpp"
#include "game/ui/FJ_SlotList.hpp"
#include "game/ui/FJ_TradingMenu.hpp"
#include <cmath>

static bool CheckSlotGrid(fuiFile* file, const char* path, int columns, int rows, int firstSlot)
{
    fuiRenderNode* node = file->getRootNode()->findNode(path);
    if (!node || !node->getFuiNode()->asFJ_SlotList())
        return false;
    FJ_SlotList* list = node->getFuiNode()->asFJ_SlotList();
    list->Init();
    std::printf("GRID %s: %d x %d\n", path, list->getColumns(), list->getRows());
    if (list->getColumns() != columns || list->getRows() != rows)
        return false;
    for (int i = 0; i < columns * rows; ++i)
        list->addSlot(firstSlot + i, false, false);
    return true;
}

static int RunUiLayoutRegression()
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    gConsoleUIController.SetupFont();
    const std::string folder(std::getenv("LCE_FUI_DIR"));
    const char* scenes[] = {"LoadMenu720.fui", "CreateWorldMenu720.fui",
        "InventoryMenu720.fui", "InventoryMenuSplit720.fui",
        "TradingMenu720.fui", "TradingMenuSplit720.fui"};
    for (const char* name : scenes)
    {
        std::printf("INITIALIZED LAYOUT: %s\n", name);
        fuiFile* file = LoadCheckMovie(folder, name, true);
        if (!file || !file->getRootNode())
            return 2;
        fuiRenderNode* root = file->getRootNode();
        if (std::string(name).find("InventoryMenu") == 0)
        {
            FJ_InventoryMenu* inventory = static_cast<FJ_InventoryMenu*>(root->getFuiNode());
            inventory->InitEffectsGroup();
            fuiRenderNode* effectsNode = root->findNode("EffectsGroup");
            fuiRenderNodeTimeline* effects = effectsNode
                ? static_cast<fuiRenderNodeTimeline*>(effectsNode->asTimeline()) : NULL;
            if (!effects || effects->getNumChildren() != 27)
                return 3;
            for (int i = 0; i < effects->getNumChildren(); ++i)
                if (effects->getChildAt(i)->isVisible())
                    return 4;
            inventory->AddEffect(1, L"Speed", 5);
            unsigned int visibleEffects = 0;
            for (int i = 0; i < effects->getNumChildren(); ++i)
                visibleEffects += effects->getChildAt(i)->isVisible() ? 1 : 0;
            if (visibleEffects != 1)
                return 5;
            int expired[] = {1, 0};
            inventory->UpdateEffects(2, expired);
            inventory->UpdateEffects(0, NULL);
            for (int i = 0; i < effects->getNumChildren(); ++i)
                if (effects->getChildAt(i)->isVisible())
                    return 5;
            if (!CheckSlotGrid(file, "MainPanel.inventoryList", 9, 3, 9)
                || !CheckSlotGrid(file, "MainPanel.hotbarList", 9, 1, 36)
                || !CheckSlotGrid(file, "MainPanel.armorList", 1, 4, 5)
                || !CheckSlotGrid(file, "MainPanel.shieldList", 1, 1, 45)
                || !CheckSlotGrid(file, "MainPanel.craftingList", 2, 2, 1)
                || !CheckSlotGrid(file, "MainPanel.Result", 1, 1, 0))
                return 6;
            SetUiPreviewLabel(file, "MainPanel.inventoryLabel", L"Inventory");
            SetUiPreviewLabel(file, "MainPanel.craftingLabel", L"Crafting");
            std::printf("PASS: no placeholder effects; add/remove effect; 27 inventory and 9 hotbar slots\n");
        }
        else if (std::string(name).find("TradingMenu") == 0)
        {
            if (!CheckSlotGrid(file, "MainPanel.Inventory", 9, 3, 3)
                || !CheckSlotGrid(file, "MainPanel.HotBar", 9, 1, 30)
                || !CheckSlotGrid(file, "MainPanel.TradingBar", 7, 1, 41)
                || !CheckSlotGrid(file, "MainPanel.Request1", 1, 1, 39)
                || !CheckSlotGrid(file, "MainPanel.Request2", 1, 1, 40))
                return 7;
            SetUiPreviewLabel(file, "MainPanel.InventoryLabel", L"Inventory");
            if (root->findNode("MainPanel.Result"))
            {
                auto* trading = static_cast<FJ_TradingMenu*>(root->getFuiNode());
                auto* cross = trading->getChildByName("MainPanel.RedCross");
                auto* experience = trading->getChildByName("MainPanel.ExperienceBar");
                auto* lock = trading->getChildByName("MainPanel.PadLock");
                if (!CheckSlotGrid(file, "MainPanel.Result", 1, 1, 48)
                    || !cross || cross->isVisible() || !experience || experience->isVisible()
                    || !lock || lock->isVisible() || trading->getTotalTradingSlots() != 7)
                    return 7;
                trading->ShowTradeUnavailable(true);
                if (!cross->isVisible())
                    return 7;
                trading->SetSelectorSlot(6);
                trading->SetSelectorSlot(2);
                trading->SetOfferState(0, false, true);
                trading->SetOfferState(2, true, false);
                trading->SetTradeProgress(L"Level 2\nApprentice", 0.4f, 0.6f, true);
                auto* label = trading->getChildByName("MainPanel.TradingLevelLabel");
                if (!label || !label->isVisible() || !experience->isVisible() || !lock->isVisible()
                    || !experience->asFJ_ProgressBar()
                    || std::fabs(experience->asFJ_ProgressBar()->getProgress() - 0.4f) > 0.01f)
                    return 7;
                trading->ShowTradeUnavailable(false);
                SetUiPreviewLabel(file, "MainPanel.VillagerText", L"Level 1 - Fletcher");
            }
            else
            {
                SetUiPreviewLabel(file, "MainPanel.RequiredLabel", L"Required Items For Trade");
                SetUiPreviewLabel(file, "MainPanel.VillagerText", L"Weapon Smith offers Emerald");
            }
        }
        else
        {
            fuiRenderNode* background = root->findNode("MainPanel.ComparisonPicBG");
            fuiRenderNode* picture = root->findNode("MainPanel.ComparisonPic");
            fuiRenderNode* panel = root->findNode("MainPanel.TextPanel");
            if (!picture)
                return 8;
            std::printf("PREVIEW picture %.2f x %.2f\n", picture->getWidth(), picture->getHeight());
            if (std::fabs(picture->getWidth() - 290.0f) > 1.0f
                || std::fabs(picture->getHeight() - 144.0f) > 1.0f
                || (background && (std::fabs(background->getWidth() - picture->getWidth()) > 1.0f
                                   || std::fabs(background->getHeight() - picture->getHeight()) > 1.0f))
                || (panel && picture->getWidth() > panel->getWidth()))
                return 9;
            if (std::string(name) == "LoadMenu720.fui")
            {
                SetUiPreviewLabel(file, "MainPanel.GameName", L"Tutorial");
                SetUiPreviewLabel(file, "MainPanel.CreatedMode", L"Created in Survival Mode");
                SetUiPreviewLabel(file, "MainPanel.Seed", L"Seed: 22873568274");
            }
        }
        const float color[] = {0.15f, 0.2f, 0.25f, 1.0f};
        gRenderer->StartFrame();
        gRenderer->SetLayout(Renderer::ELayout_Interface);
        gRenderer->SetClearColour(color);
        gRenderer->Clear(3);
        gFui->render(file, 0.0f, 0.0f, 1280.0f, 720.0f);
        SaveUiImage(folder + "/initialized-" + name + ".png");
        gFui->unloadScene(file);
    }
    std::printf("PASS: initialized inventory, trading, effects and preview layout\n");
    return 0;
}
