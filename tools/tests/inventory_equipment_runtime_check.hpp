#include "tu80_ui_runtime_check.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/User.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GameSettings.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/Items.hpp"
#include "game/item/ArmorItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/MultiPlayerGameMode.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/ui/UILayer.hpp"
#include "game/ui/UIScene_InventoryMenu.hpp"
class InventoryCheckPlayer : public MultiplayerLocalPlayer
{
public:
    using MultiplayerLocalPlayer::MultiplayerLocalPlayer;
    void awardStat(Stat*, arrayWithLength<unsigned char> params) override { delete[] params.data; }
};
static int CheckInventoryScene()
{
    std::setvbuf(stdout, NULL, _IONBF, 0);
    const bool classic = std::getenv("CHECK_CLASSIC") != NULL;
    const bool split = std::getenv("CHECK_SPLIT") != NULL;
    Minecraft* mc = Minecraft::GetInstance();
    std::puts("creating local level");
    mc->mLevel = new MultiPlayerLevel(NULL, new LevelSettings(1, GameType::SURVIVAL, false, false, false,
        LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0), 0, Difficulty::NORMAL, 63);
    std::puts("creating test player");
    User user(L"InventoryCheck", L"");
    mc->mPlayer.reset(new InventoryCheckPlayer(mc, mc->mLevel, &user, NULL));
    mc->mLocalPlayers[0] = mc->mPlayer;
    if (split)
        mc->mLocalPlayers[1].reset(new InventoryCheckPlayer(mc, mc->mLevel, &user, NULL));
    mc->mPlayer->mUserIndex = 0;
    mc->mGameModes[0] = mc->mGameMode = new MultiPlayerGameMode(0, NULL, 0, false, false);
    mc->mPlayer->mInventory->setItem(0, not_null_ptr<ItemInstance>(new ItemInstance(Items::arrow, 7)));
    mc->mPlayer->mInventory->setItem(9, not_null_ptr<ItemInstance>(new ItemInstance(Items::apple, 3)));
    GameSettings::SetGameSettings(0, eGameSetting_ClassicCrafting, classic);
    const C4JRender::eViewportType viewport = split ? C4JRender::eViewportType_TopHalf : C4JRender::eViewportType_FullScreen;
    if (split)
    {
        mc->mLocalPlayers[0]->mViewport = viewport;
        mc->mLocalPlayers[1]->mViewport = C4JRender::eViewportType_BottomHalf;
        gConsoleUIController.UpdatePlayerBasePositions();
    }
    gApp.SetGameStarted(true);
    gConsoleUIController.CloseAllPlayersScenes(true);
    for (int tick = 0; tick < 12; ++tick) gConsoleUIController.CheckMenuDisplayed();
    std::puts("opening player inventory");
    if (!gApp.LoadInventoryMenu(0, mc->mPlayer, false))
        return 2;
    UIScene* scene = gConsoleUIController.FindScene(EUIScene_InventoryMenu);
    if (!scene || scene->getMoviePath() != (split ? L"InventoryMenuSplit" : L"InventoryMenu"))
        return 3;
    auto inventoryScene = static_cast<UIScene_InventoryMenu*>(scene);
    auto* root = inventoryScene->getSection(IUIScene_AbstractContainerMenu::ESceneSection_InventoryArmor)
                     ->getFuiRenderNode()->getRoot();
    fuiRenderNode* shield = root->findNode("MainPanel.ShieldSlot");
    if (!shield || !shield->asTimeline())
        return 4;
    auto* shieldGroup = static_cast<fuiRenderNodeTimeline*>(shield);
    if (shieldGroup->getNumChildren() != 2)
        return 5;
    fuiRenderNode* icons[] = {
        root->findNode("MainPanel.EquipmentHolder.Slot_Head.Icon_Head"),
        root->findNode("MainPanel.EquipmentHolder.Slot_Body.Icon_Body"),
        root->findNode("MainPanel.EquipmentHolder.Slot_Legs.Icon_Legs"),
        root->findNode("MainPanel.EquipmentHolder.Slot_Feet.Icon_Feet"),
        shieldGroup->getChildAt(1)
    };
    Item* equipment[] = {Items::ironHelmet, Items::ironChestplate, Items::ironLeggings, Items::ironBoots, Items::fireworks};
    const int slots[] = {39, 38, 37, 36, 40};
    const float background[] = {0.15f, 0.2f, 0.25f, 1.f};
    int width, height;
    gConsoleUIController.getRenderDimensions(viewport, width, height);
    const std::string prefix = std::string(std::getenv("LCE_FUI_DIR")) + (split ? "/split" : "/full")
                               + (classic ? "-classic" : "-inventory");
    for (int occupied : {0, 31, 10, 0})
    {
        for (int i = 0; i < 5; ++i)
        {
            if (occupied & (1 << i))
                mc->mPlayer->mInventory->setItem(slots[i], not_null_ptr<ItemInstance>(new ItemInstance(equipment[i], 1)));
            else
                mc->mPlayer->mInventory->setItem(slots[i], ItemInstance::EMPTY);
        }
        scene->tick();
        gRenderer->StartFrame();
        gRenderer->SetClearColour(background);
        gRenderer->Clear(3);
        scene->render(width, height, viewport);
        for (int i = 0; i < 5; ++i)
        {
            const bool empty = (occupied & (1 << i)) == 0;
            if (!icons[i] || icons[i]->isVisible() != empty)
            {
                std::printf("FAIL: placeholder %d with equipment mask %d\n", i, occupied);
                return 6;
            }
        }
        if (!shieldGroup->getChildAt(0)->isVisible())
            return 7;
        SaveUiImage(prefix + "-" + std::to_string(occupied) + ".png");
    }
    std::puts("PASS: armor and offhand placeholders follow slot contents");
    return 0;
}