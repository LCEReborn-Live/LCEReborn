#include "game/ui/UIScene_MainMenu.hpp"
#include "game/core/StringIds.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/BufferedReader.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/InputStreamReader.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/core/UnityClient6.hpp"
#include "game/core/GameType.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/User.hpp"
#include "game/core/StringConversion.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/minigame/MiniGameDef.hpp"
#include "platform/CProfile.hpp"
#include "platform/CStorage.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/PackTransfer.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Mth.hpp"
#include "game/core/System.hpp"
#include "game/core/StringUtils.hpp"
#include "game/client/render/Font.hpp"
#include "game/client/render/Renderer.hpp"
#include "game/ui/UILayer.hpp"
#include "game/ui/UIPacksPage.hpp"
#include "game/ui/UIScene_LiveFriends.hpp"
#include "game/ui/FJ_Base.hpp"
#include "game/ui/FJ_Factory.hpp"
#include "game/ui/FJ_FuiNode.hpp"
#if LCE_PLATFORM_PC
#include "platform/pc/PcLiveBridge.hpp"
#endif
#include "game/ui/ScreenSizeCalculator.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRect.hpp"
#include "game/ui/UIString.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelGenerationOptions.hpp"
#include "game/world/generation/LevelGenerators.hpp"
#include "game/world/generation/Player.hpp"
#include "platform/Runtime.hpp"
#include "game/ui/UIScene_FullscreenProgress.hpp"
#include "vendor/_LoadingInputParams.hpp"
#include "vendor/_NetworkGameInitData.hpp"

extern ConsoleUIController gConsoleUIController;
extern CConsoleMinecraftApp gApp;

namespace
{
static const EUIScene s_componentDRC = EUIScene_Panorama;
static const EUIScene s_componentTV = EUIScene_Logo;
static EUIScene s_nextScene = EUIScene_Count;
static Random* s_splashRandom = new Random();

}

UIScene_MainMenu::UIScene_MainMenu(int sceneType, void*, UILayer* layer)
    : UIScene(sceneType, layer),
      m_timer(),
      m_splashes(),
      m_splash()
{
    Entity::fjDebugCheckSmallIdLeaks();
    gApp.SetDRCIconEnabled(true);
    gApp.SetDRCCheckPlayerCountEnabled(true);
    initialiseMovie();
    layer->addComponent(sceneType, s_componentDRC, NULL);
    layer->addComponent(sceneType, s_componentTV, NULL);
    m_pendingAction = 0;
    m_loading = false;
    m_buttons[0].init(UIString(IDS_PLAY_GAME), 0);
    m_buttons[1].init(UIString(IDS_MINI_GAMES), 1);
    m_buttons[2].init(UIString(IDS_LEADERBOARDS), 2);
    m_buttons[3].init(UIString(IDS_ACHIEVEMENTS), 3);
    m_buttons[4].init(UIString(IDS_HELP_AND_OPTIONS), 4);
    m_buttons[5].init(UIString(L"Packs"), 5);
    m_buttons[6].init(UIString(IDS_EXIT_GAME), 6);
    m_buttons[7].init(UIString(L"LCE Live"), 7);
    handleReload();
    doHorizontalResizeCheck();
    layoutLiveMenuButtons();
    m_splash = L"Default Text";
    std::wstring splashesFile(L"splashes.txt");
    if (gApp.hasArchiveFile(splashesFile, false))
    {
        arrayWithLength<unsigned char> data = gApp.getArchiveFile(splashesFile, false);
        ByteArrayInputStream input(data);
        InputStreamReader reader(&input);
        BufferedReader buffered(&reader);
        std::wstring line = L"";
        while (!(line = buffered.readLine()).empty())
        {
            line = trimString(line);
            if (!line.empty())
                m_splashes.push_back(line);
        }
        buffered.close();
    }
    m_loading = false;
    m_signedIn = false;
    m_trial = false;
    m_miniGamesSelected = false;
    gApp.setLevelGenerationOptions(NULL);
    Minecraft::GetInstance()->SetupMiniGameInstance(*MiniGameDef::GetCustomGameModeById(eMiniGameId_Any, true), 0);
    CGameNetworkManager::sInstance.ResetLeavingGame();
    gConsoleUIController.TouchBoxRebuild(this);
    gConsoleUIController.ResetSelectedItem();
}

UIScene_MainMenu::~UIScene_MainMenu()
{
#if LCE_PLATFORM_WIIU
    if (m_liveSignInPad >= 0)
        WiiULiveSignIn::Cancel();
#endif
    m_layer->removeComponent(s_componentDRC);
    m_layer->removeComponent(s_componentTV);
}

unsigned int UIScene_MainMenu::mapElementsAndNames()
{
    UIScene::mapElementsAndNames();
    fuiRenderNode* root = m_movie->getRootNode();
    {
        m_buttons[0].setupControl(this, root, std::string("Button1"));
        m_controls.push_back(&m_buttons[0]);
    }
    {
        m_buttons[1].setupControl(this, root, std::string("Button2"));
        m_controls.push_back(&m_buttons[1]);
    }
    {
        m_buttons[2].setupControl(this, root, std::string("Button3"));
        m_controls.push_back(&m_buttons[2]);
    }
    {
        m_buttons[3].setupControl(this, root, std::string("Button4"));
        m_controls.push_back(&m_buttons[3]);
    }
    {
        m_buttons[4].setupControl(this, root, std::string("Button5"));
        m_controls.push_back(&m_buttons[4]);
    }
    {
        m_buttons[5].setupControl(this, root, std::string("Button6"));
        m_controls.push_back(&m_buttons[5]);
    }
    {
        m_buttons[6].setupControl(this, root, std::string("Button7"));
        m_controls.push_back(&m_buttons[6]);
    }
    {
        m_buttons[7].setupControl(this, root, std::string("ButtonUpsell"));
        m_controls.push_back(&m_buttons[7]);
    }
    {
        m_timer.setupControl(this, root, std::string("Timer"));
        m_controls.push_back(&m_timer);
    }
    return 1;
}

void UIScene_MainMenu::proceedToScene(int, EUIScene scene)
{
    s_nextScene = scene;
}

void UIScene_MainMenu::handleReload()
{
#if LCE_PLATFORM_WIIU
    m_buttons[7].setLabel(UIString(L"LCE Live"), false, false);
    layoutLiveMenuButtons();
#else
#if LCE_PLATFORM_PC
    if (PcLiveBridge::Enabled())
    {
        m_buttons[7].setLabel(UIString(L"LCE Live"), false, false);
        layoutLiveMenuButtons();
        return;
    }
#endif
    removeControl(&m_buttons[7], false);
#endif
}

void UIScene_MainMenu::layoutLiveMenuButtons()
{
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
#if LCE_PLATFORM_PC
    if (!PcLiveBridge::Enabled())
        return;
#endif
    if (m_buttons[5].getFuiRenderNode() == NULL
        || m_buttons[6].getFuiRenderNode() == NULL || m_buttons[7].getFuiRenderNode() == NULL)
        return;

    FJ_FuiNode* packsNode = m_buttons[5].getFuiRenderNode()->getFuiNode();
    FJ_FuiNode* liveNode = m_buttons[7].getFuiRenderNode()->getFuiNode();
    if (packsNode == NULL || liveNode == NULL || packsNode->asFJ_Base() == NULL || liveNode->asFJ_Base() == NULL)
        return;

    // Keep the established two-button row. Snap its geometry to whole FUI
    // pixels so the stock borders and texture pixels stay crisp at native scale.
    // Packs used to be narrower than the regular rows, so its original width
    // cannot be used as the width of the split row. Anchor the two buttons to
    // the full-width Help & Options row instead.
    const float left = std::floor(m_buttons[4].getXPos() + 0.5f);
    const float fullWidth = std::floor(m_buttons[4].getWidth() + 0.5f);
    const float gap = 4.0f;
    const float combinedButtonWidth = std::max(2.0f, fullWidth - gap);
    const float packsWidth = std::floor(combinedButtonWidth * 0.5f);
    const float liveWidth = combinedButtonWidth - packsWidth;
    const float rowGap = 5.0f;
    const float rowY = std::floor(m_buttons[4].getYPos() + m_buttons[4].getHeight() + rowGap + 0.5f);
    const float rowHeight = std::floor(m_buttons[5].getHeight() + 0.5f);

    packsNode->asFJ_Base()->ResizeObject(packsWidth, rowHeight);
    packsNode->setX(left);
    packsNode->setY(rowY);
    m_buttons[5].UpdateControl();

    liveNode->asFJ_Base()->ResizeObject(liveWidth, rowHeight);
    liveNode->setX(left + packsWidth + gap);
    liveNode->setY(rowY);
    m_buttons[7].UpdateControl();

    const float splitRowHeight = std::max(m_buttons[5].getHeight(), m_buttons[7].getHeight());
    fuiRenderNode* exitNode = m_buttons[6].getFuiRenderNode();
    exitNode->setY(std::floor(rowY + splitRowHeight + rowGap + 0.5f));
    m_buttons[6].UpdateControl();

    gConsoleUIController.TouchBoxRebuild(this);
#endif
}

void UIScene_MainMenu::updateTooltips()
{
    UIScene* topScene = gConsoleUIController.GetTopScene(0, EUILayer_Fullscreen, EUIGroup_PlayerDefault);
    if (!gConsoleUIController.IsShowingAlertMessage(0) && !gConsoleUIController.IsShowingErrorMessage(0)
        && topScene != NULL && topScene->getSceneType() == EUIScene_MainMenu)
    {
        int tooltip = -1;
        if (!m_loading)
            tooltip = IDS_TOOLTIPS_SELECT;
        gConsoleUIController.SetTooltips(0, tooltip, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, false,
                                         false);
    }
}

void UIScene_MainMenu::updateComponents()
{
    m_layer->showComponent(m_playerId, s_componentDRC, true);
    m_layer->showComponent(m_playerId, s_componentTV, true);
}

void UIScene_MainMenu::customDrawSplash(fuiRect* rect)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    ScreenSizeCalculator calculator(minecraft->mOptions, minecraft->mFullWidth, minecraft->mFullHeight, -1);
    m_splashWidth = static_cast<float>(calculator.mWidthScale);
    m_splashX = static_cast<float>(minecraft->mFullWidth);
    m_splashHeight = static_cast<float>(calculator.mHeightScale);
    m_splashY = static_cast<float>(minecraft->mFullHeight);
    gRenderer->TextureBindVertex(-1);

    Font* font = minecraft->mFont;
    GlStateManager::disableCull();
    GlStateManager::disableDepthTest();
    GlStateManager::disableLighting();
    GlStateManager::pushMatrix();
    GlStateManager::translatef((rect->xMax - rect->xMin) * 0.5f, rect->yMax - rect->yMin, 0.0f);
    GlStateManager::rotatef(-17.0f, 0.0f, 0.0f, 1.0f);

    float phase = static_cast<float>(System::processTimeInMilliSecs() % 1000) / 1000.0f * 3.1415927f;
    float scale = (1.8f - Mth::abs(Mth::sin(phase + phase) * 0.1f)) * (m_splashX / m_splashWidth) * 100.0f;
    float factor = scale / static_cast<float>(font->width(m_splash) + 32);
    GlStateManager::scalef(factor, factor, factor);
    font->drawShadow(m_splash, -(font->width(m_splash) / 2), -8, 0xffffff00);
    GlStateManager::popMatrix();

    GlStateManager::disableRescaleNormal();
    GlStateManager::enableDepthTest();
}

void UIScene_MainMenu::customDraw(const char* name, fuiRect* rect)
{
    if (std::strcmp(name, "Splash") == 0)
        customDrawSplash(rect);
}

std::wstring UIScene_MainMenu::getMoviePath()
{
    return std::wstring(L"MainMenu");
}

void UIScene_MainMenu::tick()
{
    UIScene::tick();
#if LCE_PLATFORM_WIIU
    updateLiveSignIn();
#endif
    if (hasFocus(m_playerId))
    {
        PackReceiver::Get().releasePacks();
        PackReceiver::Get().showLeaveMessage();
    }
    if (s_nextScene != EUIScene_Count && gApp.GetTMSAction(m_playerId) == 0)
    {
        int playerId = gProfile->GetLockedProfile();
        void* sceneData = NULL;
        if (!m_miniGamesSelected)
        {
            gConsoleUIController.NavigateToScene(playerId, s_nextScene, sceneData, EUILayer_Scene,
                                                 EUIGroup_PlayerDefault);
            s_nextScene = EUIScene_Count;
        }
        else
        {
            if (s_nextScene == EUIScene_LoadCreateJoinMenu)
                sceneData = reinterpret_cast<void*>(1);
            gConsoleUIController.NavigateToScene(playerId, s_nextScene, sceneData, EUILayer_Scene,
                                                 EUIGroup_PlayerDefault);
            s_nextScene = EUIScene_Count;
        }
    }
}

#if LCE_PLATFORM_WIIU
int UIScene_MainMenu::closeLiveSignIn(void* context, int, C4JStorage::EMessageResult)
{
    UIScene_MainMenu* menu = static_cast<UIScene_MainMenu*>(context);
    const int pad = menu->m_liveSignInPad;
    const bool openFriends = menu->m_liveDialogStatus == WiiULiveSignIn::Status::SignedIn;
    WiiULiveSignIn::Cancel();
    menu->m_liveSignInPad = -1;
    menu->m_liveDialogStatus = WiiULiveSignIn::Status::Idle;
    if (openFriends && pad >= 0)
        gConsoleUIController.NavigateToScene(pad, EUIScene_LiveFriends, NULL, EUILayer_Scene, EUIGroup_PlayerDefault);
    return 0;
}

void UIScene_MainMenu::updateLiveSignIn()
{
    using WiiULiveSignIn::Status;
    const Status status = WiiULiveSignIn::Update();
    if (m_liveSignInPad < 0 || status == m_liveDialogStatus || gConsoleUIController.IsReloadingSkin())
        return;

    if (m_liveDialogStatus != Status::Idle)
    {
        if (!gConsoleUIController.NavigateBack(m_liveSignInPad, true, EUIScene_None, EUILayer_Alert))
            return;
        m_liveDialogStatus = Status::Idle;
    }

    const bool waiting = status == Status::Starting || status == Status::Waiting || status == Status::Restoring;
    std::wstring text;
    switch (status)
    {
    case Status::Restoring:
        text = L"Checking your saved LCE Live sign-in...";
        break;
    case Status::Starting:
        text = L"Getting a sign-in code for LCE Live...";
        break;
    case Status::Waiting:
    {
        const std::string& code = WiiULiveSignIn::UserCode();
        text = L"On your phone or PC, open lcereborn.xyz/authorize and enter this code:\n\n";
        text.append(code.begin(), code.end());
        break;
    }
    case Status::SignedIn:
    {
        const std::string& username = WiiULiveSignIn::Username();
        text = L"Signed in to LCE Live as ";
        text.append(username.begin(), username.end());
        break;
    }
    case Status::Denied:
        text = L"Sign-in was declined. Select LCE Live to try again.";
        break;
    case Status::Expired:
        text = L"Your code expired. Select LCE Live for a new code.";
        break;
    case Status::Failed:
        text = WiiULiveSignIn::ErrorDetail();
        text += L"\nSelect LCE Live to try again.";
        break;
    default:
        text = L"Could not connect to LCE Live. Select LCE Live to try again.";
        break;
    }

    int button = waiting ? IDS_CONFIRM_CANCEL : IDS_CONFIRM_OK;
    if (gConsoleUIController.RequestErrorMessage(IDS_MUST_SIGN_IN_TITLE, IDS_MUST_SIGN_IN_TEXT, &button, 1,
                                                 m_liveSignInPad, closeLiveSignIn, this, &text[0], true) == 2)
        m_liveDialogStatus = status;
}
#endif

void UIScene_MainMenu::handleInput(int playerId, int action, bool repeat, bool pressed, bool released, bool&)
{
    if (gApp.getIgnoreMainMenuInputDuringInvite() || m_loading || s_nextScene != EUIScene_Count)
        return;
    gConsoleUIController.AnimateKeyPress(m_playerId, action, repeat, pressed, released);
    switch (action)
    {
    case EControllerActions_MenuOk:
        if (!pressed)
            return;
        gProfile->SetPrimaryPad(playerId);
        gProfile->SetLockedProfile(-1);
        sendInputToMovie(action, repeat, pressed, released);
        break;
    case EControllerActions_MenuUp:
    case EControllerActions_MenuDown:
        sendInputToMovie(action, repeat, pressed, released);
        break;
    }
}

void UIScene_MainMenu::PrepareToPlayGame(int playerId)
{
    gProfile->SetLockedProfile(playerId);
    gProfile->QuerySigninStatus();
    if (!gApp.DLCInstallProcessCompleted())
        gApp.StartInstallDLCProcess(playerId);
}

void UIScene_MainMenu::RunPlayGameOffline(int playerId)
{
    PrepareToPlayGame(playerId);
    proceedToScene(gProfile->GetPrimaryPad(), EUIScene_LoadCreateJoinMenu);
}

void UIScene_MainMenu::RunLeaderboards(int playerId)
{
    int messageParameter = IDS_CONFIRM_OK;
    if (gProfile->IsGuest(playerId))
    {
        gConsoleUIController.RequestErrorMessage(0xe6bf122, 0x8ba9896f, &messageParameter, 1, 0xff, NULL, NULL, NULL,
                                                 true);
    }
    else
    {
        gProfile->SetLockedProfile(playerId);
        gProfile->QuerySigninStatus();
        proceedToScene(playerId, EUIScene_LeaderboardsMenu);
    }
}

int UIScene_MainMenu::Leaderboards_SignInReturned(void* context, bool signedIn, int userIndex, int)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (signedIn)
    {
        gProfile->SetCurrentGameActivity(userIndex, 1, false);
        scene->RunLeaderboards(userIndex);
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

void UIScene_MainMenu::RunPacks(int playerId)
{
    gProfile->QuerySigninStatus();
    gProfile->SetLockedProfile(playerId);
    gApp.StartInstallDLCProcess(playerId);
    gConsoleUIController.NavigateToScene(playerId, EUIScene_CustomMenu, new UIPacksPage(), EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

int UIScene_MainMenu::Packs_SignInReturned(void* context, bool signedIn, int userIndex, int playerIndex)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (signedIn)
    {
        gProfile->SetCurrentGameActivity(userIndex, 1, false);
        gProfile->SetPrimaryPad(playerIndex);
        scene->RunPacks(userIndex);
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

int UIScene_MainMenu::MustConnectReturnedNintendo(void* context, int userIndex, C4JStorage::EMessageResult result)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (result == C4JStorage::EMessageResult_Accept)
    {
        switch (scene->m_pendingAction)
        {
        case 2:
            CGameNetworkManager::sInstance.ShowConnectOnlineNintendoProgress(userIndex, Leaderboards_SignInReturned,
                                                                             scene);
            break;
        case 1:
            CGameNetworkManager::sInstance.ShowConnectOnlineNintendoProgress(userIndex, CreateLoad_SignInReturned,
                                                                             scene);
            break;
        }
    }
    else if (scene->m_pendingAction == 1)
    {
        if (result == C4JStorage::EMessageResult_Decline)
            CreateLoad_SignInReturned(scene, true, 0, 0);
        else
            CreateLoad_SignInReturned(scene, false, 0, 0);
    }
    else
    {
        scene->m_loading = false;
    }
    return 0;
}

void UIScene_MainMenu::RunPlayGame(int playerId)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    gApp.ReleaseSaveThumbnail();
    if (gProfile->IsGuest(playerId))
    {
        int messageParameter = IDS_CONFIRM_OK;
        m_loading = false;
        gConsoleUIController.RequestErrorMessage(0xe6bf122, 0x8ba9896f, &messageParameter, 1, 0xff, NULL, NULL, NULL,
                                                 true);
        return;
    }

    PrepareToPlayGame(playerId);
    User* user = minecraft->mUser;
    user->name = convStringToWstring(gProfile->GetGamertag(gProfile->GetPrimaryPad()));
    if (!gProfile->IsSignedInLive(playerId))
    {
        int messageParameters[2] = {IDS_PRO_NOTONLINE_ACCEPT, IDS_PRO_NOTONLINE_DECLINE};
        m_pendingAction = 1;
        gConsoleUIController.RequestErrorMessage(IDS_PRO_NOTONLINE_TITLE, IDS_PRO_NOTONLINE_TEXT, messageParameters, 2,
                                                 playerId, MustConnectReturnedNintendo, this, NULL, true);
        return;
    }
    GameSettings::ApplyGameSettingsChanged(playerId);
    proceedToScene(gProfile->GetPrimaryPad(), EUIScene_LoadCreateJoinMenu);
}

int UIScene_MainMenu::CreateLoad_SignInReturned(void* context, bool signedIn, int userIndex, int playerIndex)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    gProfile->SetPrimaryPad(playerIndex);
    if (signedIn)
    {
        gProfile->SetCurrentGameActivity(userIndex, 1, false);
        bool online = gProfile->IsSignedInLive(userIndex);
        if (!gProfile->IsGuest(gProfile->GetPrimaryPad()) && !online)
            scene->RunPlayGameOffline(userIndex);
        else
            scene->RunPlayGame(userIndex);
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

void UIScene_MainMenu::RunAchievements(int playerId)
{
    int messageParameter = IDS_CONFIRM_OK;
    if (gProfile->IsGuest(playerId))
    {
        gConsoleUIController.RequestErrorMessage(0xe6bf122, 0x8ba9896f, &messageParameter, 1, 0xff, NULL, NULL, NULL,
                                                 true);
        return;
    }
    gProfile->SetLockedProfile(playerId);
    proceedToScene(playerId, EUIScene_AchievementsMenu);
}

int UIScene_MainMenu::Achievements_SignInReturned(void* context, bool signedIn, int userIndex, int)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (signedIn)
    {
        gProfile->SetCurrentGameActivity(userIndex, 1, false);
        scene->RunAchievements(userIndex);
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

void UIScene_MainMenu::RunHelpAndOptions(int playerId)
{
    if (gProfile->IsGuest(playerId))
    {
        int messageParameter = IDS_CONFIRM_OK;
        gConsoleUIController.RequestErrorMessage(0xe6bf122, 0x8ba9896f, &messageParameter, 1, 0xff, NULL, NULL, NULL,
                                                 true);
        return;
    }
    gProfile->QuerySigninStatus();
    gProfile->SetLockedProfile(playerId);
    proceedToScene(playerId, EUIScene_HelpAndOptionsMenu);
}

int UIScene_MainMenu::HelpAndOptions_SignInReturned(void* context, bool signedIn, int userIndex, int)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (signedIn)
    {
        gProfile->SetCurrentGameActivity(userIndex, 1, false);
        scene->RunHelpAndOptions(userIndex);
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

int UIScene_MainMenu::ExitGameReturned(void*, int, C4JStorage::EMessageResult result)
{
    if (result == C4JStorage::EMessageResult_Decline)
        gApp.ExitGame();
    return 0;
}

void UIScene_MainMenu::RunAction(int playerId)
{
    switch (m_pendingAction)
    {
    case 1:
        RunPlayGame(playerId);
        break;
    case 2:
        RunLeaderboards(playerId);
        break;
    case 3:
        RunAchievements(playerId);
        break;
    case 4:
        RunHelpAndOptions(playerId);
        break;
    case 5:
        RunPacks(playerId);
        break;
    default:
        break;
    }
}

int UIScene_MainMenu::MustSignInReturned(void* context, int userIndex, C4JStorage::EMessageResult result)
{
    UIScene_MainMenu* scene = static_cast<UIScene_MainMenu*>(context);
    if (result == C4JStorage::EMessageResult_Accept)
    {
        switch (scene->m_pendingAction)
        {
        case 1:
            gProfile->RequestSignInUI(false, true, true, false, true, CreateLoad_SignInReturned, scene, userIndex);
            break;
        case 4:
            gProfile->RequestSignInUI(false, false, true, false, true, HelpAndOptions_SignInReturned, scene, userIndex);
            break;
        case 2:
            gProfile->RequestSignInUI(false, false, true, false, true, Leaderboards_SignInReturned, scene, userIndex);
            break;
        case 3:
            gProfile->RequestSignInUI(false, false, true, false, true, Achievements_SignInReturned, scene, userIndex);
            break;
        case 5:
            gProfile->RequestSignInUI(false, false, true, false, true, Packs_SignInReturned, scene, userIndex);
            break;
        default:
            break;
        }
    }
    else
    {
        scene->m_loading = false;
        gProfile->SetLockedProfile(-1);
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }
    return 0;
}

void UIScene_MainMenu::handlePress(int controlId, int)
{
    int (*signInReturnedFunc)(void*, bool, int, int) = NULL;
    int pad = gProfile->GetPrimaryPad();
    switch (controlId)
    {
    default:
        m_miniGamesSelected = false;
        return;
    case 0:
        m_pendingAction = 1;
        m_miniGamesSelected = false;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        signInReturnedFunc = &CreateLoad_SignInReturned;
        break;
    case 1:
        m_miniGamesSelected = true;
        m_pendingAction = 1;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        signInReturnedFunc = &CreateLoad_SignInReturned;
        break;
    case 2:
        m_miniGamesSelected = false;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_pendingAction = 2;
        signInReturnedFunc = &Leaderboards_SignInReturned;
        break;
    case 3:
        m_miniGamesSelected = false;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_pendingAction = 3;
        signInReturnedFunc = &Achievements_SignInReturned;
        break;
    case 4:
        m_miniGamesSelected = false;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_pendingAction = 4;
        signInReturnedFunc = &HelpAndOptions_SignInReturned;
        break;
    case 5:
        m_miniGamesSelected = false;
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_pendingAction = 5;
        signInReturnedFunc = &Packs_SignInReturned;
        break;
    case 6:
    {
        int messageParameters[2];
        messageParameters[0] = IDS_CONFIRM_CANCEL;
        messageParameters[1] = IDS_CONFIRM_OK;
        m_miniGamesSelected = false;
        gConsoleUIController.RequestErrorMessage(0x5d7629ce, 0xde9a48c3, messageParameters, 2, 0xff, ExitGameReturned,
                                                 this, NULL, true);
    }
        return;
    case 7:
    {
#if LCE_PLATFORM_PC
        if (PcLiveBridge::Enabled())
        {
            gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
            gConsoleUIController.NavigateToScene(pad, EUIScene_LiveFriends, NULL, EUILayer_Scene,
                                                 EUIGroup_PlayerDefault);
        }
#elif LCE_PLATFORM_WIIU
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        if (WiiULiveSignIn::IsSignedIn())
        {
            gConsoleUIController.NavigateToScene(pad, EUIScene_LiveFriends, NULL, EUILayer_Scene, EUIGroup_PlayerDefault);
            return;
        }
        m_liveSignInPad = pad;
        WiiULiveSignIn::Start();
        updateLiveSignIn();
#endif
        return;
    }
    }

    if (signInReturnedFunc != NULL)
    {
        if (gProfile->IsSignedIn(pad))
        {
            RunAction(pad);
        }
        else
        {
            int messageParameters[2] = {IDS_CONFIRM_OK, IDS_CONFIRM_CANCEL};
            gConsoleUIController.RequestErrorMessage(IDS_MUST_SIGN_IN_TITLE, IDS_MUST_SIGN_IN_TEXT, messageParameters,
                                                     2, pad, MustSignInReturned, this, NULL, true);
        }
    }
}

void UIScene_MainMenu::handleGainFocus(bool hadFocus)
{
    UIScene::handleGainFocus(hadFocus);
    gConsoleUIController.ShowPlayerDisplayname(false);
    const bool noSceneChangePending = (s_nextScene == EUIScene_Count);
    m_loading = false;
    if (!noSceneChangePending)
        return;

    if (!hadFocus)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (gProfile->IsSignedIn(i))
                gProfile->SetCurrentGameActivity(i, 1, false);
        }
    }

    if (!CGameNetworkManager::sInstance.IsHandlingBootInvite())
    {
        gProfile->SetLockedProfile(-1);
        gProfile->ClearGameUsers();
        m_loading = false;
        updateTooltips();
    }
    else
    {
        if (gProfile->IsSignedIn(gProfile->GetPrimaryPad()))
            GameSettings::ApplyGameSettingsChanged(gProfile->GetPrimaryPad());
        m_loading = false;
        updateTooltips();
    }

    if (hadFocus)
        m_buttons[5].setLabel(UIString(L"Packs"), false, false);

    m_timer.setVisible(false);
    m_buttons[0].setVisible(true);
    m_buttons[1].setVisible(true);
    m_buttons[2].setVisible(true);
    m_buttons[3].setVisible(true);
    m_buttons[4].setVisible(true);
    m_buttons[5].setVisible(true);
    m_buttons[6].setVisible(true);
#if LCE_PLATFORM_PC
    m_buttons[7].setVisible(PcLiveBridge::Enabled());
#elif LCE_PLATFORM_WIIU
    m_buttons[7].setVisible(true);
#else
    m_buttons[7].setVisible(false);
#endif

    int splashIndex = static_cast<int>(m_splashes.size());
    splashIndex = s_splashRandom->nextInt(splashIndex - 4) + 4;

    SYSTEMTIME localTime;
    GetLocalTime(&localTime);
    if (localTime.month == 0xc && localTime.day == 0x18)
    {
        m_splash = m_splashes.at(0);
        return;
    }
    if (localTime.month == 1 && localTime.day == 1)
    {
        m_splash = m_splashes.at(1);
        return;
    }
    if (localTime.month == 10 && localTime.day == 0x1f)
        splashIndex = 2;
    m_splash = m_splashes.at(splashIndex);
}

bool UIScene_MainMenu::hasFocus(int)
{
    return m_hasFocus;
}

int UIScene_MainMenu::getSceneType()
{
    return EUIScene_MainMenu;
}

unsigned long long UIScene_MainMenu::getDefaultGtcButtons()
{
    return 0;
}
