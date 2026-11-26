#include "tu80_ui_runtime_check.hpp"
#include "game/core/Achievements.hpp"
#include "game/core/Achievement.hpp"
#include "game/core/CAchievements.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/L10N.hpp"
#include "game/item/BannerRecipes.hpp"
#include "game/item/CraftingContainer.hpp"
#include "game/item/Recipes.hpp"
#include "game/item/Items.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/ui/UIScene_AchievementsMenu.hpp"
#include "game/ui/UILayer.hpp"
#include "platform/CProfile.hpp"
#include <cstring>
#include <set>

static int RunTu80AchievementsRegression()
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::set<int> registrations;
    for (int i = 0; i < UIScene_AchievementsMenu::DISPLAY_COUNT; ++i)
    {
        const int id = UIScene_AchievementsMenu::sDisplayList[i];
        const AchievementInfo* info = UIScene_AchievementsMenu::GetInfo(id);
        if (info->registrationId < 0 || info->registrationId >= 128
            || !registrations.insert(info->registrationId).second)
            return 2;
        if (id >= Achievements::TU80_FIRST)
        {
            Stat* stat = GenericStats::achievement(static_cast<eAward>(id));
            if (!stat || !stat->isAchievement() || stat->id != 5242880 + id || !stat->isAwardedFromServer())
                return 3;
            const wchar_t* title = L10N::GetString(info->title);
            const wchar_t* description = L10N::GetString(info->description);
            if (!title || !*title || !description || !*description)
                return 4;
            const std::wstring path = std::wstring(L"Graphics\\Achievements\\") + info->icon + L".png";
            if (!gApp.hasArchiveFile(path, false))
                return 5;
            std::printf("ACHIEVEMENT: %d %ls\n", id, title);
        }
    }
    CAchievements awards(3);
    if (!awards.IsIdle())
        return 6;
    unsigned char old[16] = {};
    for (int i = 0; i < 12; ++i)
        old[i] = 0xa5;
    old[12] = 3; // Existing registration IDs 96 and 97.
    awards.ReadFrom(old, sizeof(old));
    for (int id = 97; id <= 113; ++id)
    {
        awards.SetAwardedBit(id, true);
        if (!awards.GetAwardedBit(id))
            return 7;
    }
    unsigned char saved[16] = {};
    awards.WriteTo(saved, sizeof(saved));
    if (std::memcmp(old, saved, 12) != 0 || (saved[12] & 3) != 3)
        return 8;
    CAchievements restored(3);
    restored.ReadFrom(saved, sizeof(saved));
    for (int id = 97; id <= 113; ++id)
        if (!restored.GetAwardedBit(id))
            return 9;
    std::printf("PASS: original profile bits preserved and all 17 new award bits survive reload\n");

    boost::shared_ptr<CraftingContainer> crafting(new CraftingContainer(NULL, 3, 3));
    crafting->setItem(0, not_null_ptr<ItemInstance>(new ItemInstance(Items::banner)));
    crafting->setItem(1, not_null_ptr<ItemInstance>(new ItemInstance(Items::goldenApple, 1, 1)));
    crafting->setItem(2, not_null_ptr<ItemInstance>(new ItemInstance(Items::dye)));
    Recipes* recipes = Recipes::getInstance();
    if (recipes->hasAchievementAttachedToRecipe(crafting, NULL, BannerRecipes::mAddPatternRecipe,
                                                Recipes::eRecipeCategory_Crafting) != eAward_FruitOnTheLoom)
        return 12;
    crafting->setItem(1, not_null_ptr<ItemInstance>(new ItemInstance(Items::goldenApple, 1, 0)));
    if (recipes->hasAchievementAttachedToRecipe(crafting, NULL, BannerRecipes::mAddPatternRecipe,
                                                Recipes::eRecipeCategory_Crafting) != eAward_None)
        return 13;
    std::printf("PASS: enchanted-apple banner recipe awards the trophy; normal apple does not\n");

    std::printf("OPENING ACHIEVEMENTS MENU\n");
    if (!gConsoleUIController.NavigateToScene(0, EUIScene_AchievementsMenu, reinterpret_cast<void*>(97),
                                             EUILayer_Scene, EUIGroup_Fullscreen))
        return 10;
    UIScene* scene = gConsoleUIController.FindScene(EUIScene_AchievementsMenu);
    if (!scene)
        return 11;
    scene->tick();
    const float background[] = {0.15f, 0.2f, 0.25f, 1.0f};
    gRenderer->StartFrame();
    gRenderer->SetClearColour(background);
    gRenderer->Clear(3);
    scene->render(1280, 720, C4JRender::eViewportType_FullScreen);
    const char* folder = std::getenv("LCE_FUI_DIR");
    if (folder)
        SaveUiImage(std::string(folder) + "/achievements-menu.png");
    std::printf("PASS: TU80 achievements metadata, persistence and 110-entry menu\n");
    return 0;
}
