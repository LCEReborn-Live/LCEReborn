#include "game/ui/UIScene_LiveFriends.hpp"
#include "game/ui/UIMenuPainter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>
#if LCE_PLATFORM_PC
#include <chrono>
#elif LCE_PLATFORM_WIIU
#include "platform/CProfile.hpp"
#include "platform/wiiu/WiiULiveSocial.hpp"
#include "platform/wiiu/WiiULiveGames.hpp"
#include "platform/wiiu/WiiULiveCosmetics.hpp"
#endif
#include "game/ui/UILiveAvatar.hpp"
#include "game/core/System.hpp"
#include "game/ui/UIScene_AchievementsMenu.hpp"

#include "game/client/render/Font.hpp"
#include "game/client/render/Tesselator.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/BufferBuilder.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/GuiComponent.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/StringIds.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/GameSettings.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/CPlatformNetworkManager.hpp"
#include "game/ui/FJ_Base.hpp"
#include "game/ui/FJ_FuiNode.hpp"
#include "game/ui/FJ_LoadCreateJoinMenu.hpp"
#include "game/ui/FJ_List.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRenderNodeCodeGenRect.hpp"
#include "game/ui/fuiRenderNodeStage.hpp"
#include "game/ui/UILayer.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#include "platform/CInput.hpp"
#include "platform/C4JInput.hpp"

extern ConsoleUIController gConsoleUIController;

namespace
{

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t index = 0; index <= line.size(); ++index)
    {
        if (index == line.size() || line[index] == '\t')
        {
            fields.push_back(line.substr(start, index - start));
            start = index + 1;
        }
    }
    return fields;
}

std::wstring Widen(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

bool ReadSkin(const std::wstring& name, std::vector<unsigned char>& data)
{
#if LCE_PLATFORM_PC
    FILE* file = PcLiveBridge::Open(name.c_str(), L"rb");
    if (file == NULL) return false;
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length <= 0 || length > 512 * 1024 || std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }
    data.resize(static_cast<size_t>(length));
    const bool ok = std::fread(&data[0], 1, data.size(), file) == data.size();
    std::fclose(file);
    return ok;
#else
    (void)name;
    (void)data;
    return false;
#endif
}

void CenterNodeOnStageX(FJ_FuiNode* node, float stageWidth)
{
    if (node == NULL) return;
    std::vector<FJ_FuiNode*> ancestors;
    for (FJ_FuiNode* parent = node->getParent(); parent != NULL; parent = parent->getParent())
        ancestors.push_back(parent);

    float parentX = 0.0f;
    float parentScaleX = 1.0f;
    for (std::vector<FJ_FuiNode*>::reverse_iterator parent = ancestors.rbegin(); parent != ancestors.rend(); ++parent)
    {
        parentX += (*parent)->getX() * parentScaleX;
        parentScaleX *= (*parent)->getScaleX();
    }
    if (parentScaleX <= 0.0f) return;
    node->setX((stageWidth * 0.5f - parentX) / parentScaleX - node->getWidth() * 0.5f);
}

bool NodeBoundsOnStage(fuiRenderNode* node, fuiRect& bounds)
{
    if (node == NULL || !node->isVisible()) return false;
    node->getParentSpaceBounds(&bounds);
    for (fuiRenderNode* parent = node->getParent(); parent != NULL; parent = parent->getParent())
    {
        if (!parent->isVisible()) return false;
        bounds.xMin = bounds.xMin * parent->getScaleX() + parent->getX();
        bounds.xMax = bounds.xMax * parent->getScaleX() + parent->getX();
        bounds.yMin = bounds.yMin * parent->getScaleY() + parent->getY();
        bounds.yMax = bounds.yMax * parent->getScaleY() + parent->getY();
    }
    return true;
}

bool Contains(const fuiRect& bounds, float x, float y)
{
    return x >= bounds.xMin && x < bounds.xMax && y >= bounds.yMin && y < bounds.yMax;
}

std::string ReadLiveText(const wchar_t* name)
{
    std::vector<unsigned char> bytes;
    if (!ReadSkin(name, bytes)) return std::string();
    return std::string(bytes.begin(), bytes.end());
}

void DrawTexture(Textures& textures, int texture, int x, int y, int size)
{
    if (texture < 0) return;
    GlStateManager::enableTexture();
    GlStateManager::color4f(1, 1, 1, 1);
    textures.bind(texture);
    BufferBuilder* builder = Tesselator::getInstance()->getBuilder();
    builder->begin();
    builder->vertexUV(static_cast<float>(x), static_cast<float>(y + size), 0.0f, 0.0f, 1.0f);
    builder->vertexUV(static_cast<float>(x + size), static_cast<float>(y + size), 0.0f, 1.0f, 1.0f);
    builder->vertexUV(static_cast<float>(x + size), static_cast<float>(y), 0.0f, 1.0f, 0.0f);
    builder->vertexUV(static_cast<float>(x), static_cast<float>(y), 0.0f, 0.0f, 0.0f);
    builder->end();
}

}

UIScene_LiveFriends::UIScene_LiveFriends(int playerId, void* data, UILayer* layer)
    : UIScene(playerId, layer),
      m_gui(new GuiComponent()),
      m_tab(eTab_Friends)
{
    m_inviteMode = reinterpret_cast<uintptr_t>(data) == 1;

    // Reuse the game's Load/Create/Join FUI, tabs, list rows, focus and controller navigation.
    initialiseMovie();
    for (int tab = 0; tab < eTab_Count; ++tab)
    {
        m_lists[tab].init(tab);
        m_touches[tab].init(tab + 4);
        m_timers[tab].setVisible(false);
    }

    FJ_LoadCreateJoinMenu* document = static_cast<FJ_LoadCreateJoinMenu*>(m_movie->getRootNode()->getFuiNode());
    document->ShowSaveSizeBar(false);
    document->SetActiveTab(eTab_Friends);
    ApplyReferenceLayout();
    SetTabTitles();
    ReadSocial();
    PopulateLists();
}

UIScene_LiveFriends::~UIScene_LiveFriends()
{
    ReleaseFaceTextures();
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft && minecraft->getTextureManager())
        for (const auto& item : m_achievementTextures) minecraft->getTextureManager()->releaseTexture(item.second);
    delete m_gui;
}

std::wstring UIScene_LiveFriends::getMoviePath()
{
    return L"LoadCreateJoinMenu";
}

unsigned int UIScene_LiveFriends::mapElementsAndNames()
{
    fuiRenderNode* root = m_movie->getRootNode();
    const char* tabNames[eTab_Count] = {"LoadGame", "NewGame", "JoinGame"};
    const char* listNames[eTab_Count] = {"LoadGameList", "NewGameList", "JoinGameGamesList"};
    const char* timerNames[eTab_Count] = {"LoadGameTimer", "NewGameTimer", "JoinGamesTimer"};
    const char* touchNames[eTab_Count] = {"TouchTabLoad", "TouchTabCreate", "TouchTabJoin"};

    for (int tab = 0; tab < eTab_Count; ++tab)
    {
        m_tabControls[tab].setupControl(this, root, tabNames[tab]);
        m_controls.push_back(&m_tabControls[tab]);
        m_lists[tab].setupControl(this, m_tabControls[tab].getFuiRenderNode(), listNames[tab]);
        m_controls.push_back(&m_lists[tab]);
        m_timers[tab].setupControl(this, m_tabControls[tab].getFuiRenderNode(), timerNames[tab]);
        m_controls.push_back(&m_timers[tab]);
        m_touches[tab].setupControl(this, m_tabControls[tab].getFuiRenderNode(), touchNames[tab]);
        m_controls.push_back(&m_touches[tab]);
    }
    return 1;
}

int UIScene_LiveFriends::getSceneType()
{
    return EUIScene_LiveFriends;
}

void UIScene_LiveFriends::tick()
{
    UIScene::tick();
    if (m_hasFocus && HandleMouseInput()) return;
    const long long now = System::processTimeInMilliSecs();
    if (now < m_nextPoll) return;
    m_nextPoll = now + 500;
    if (ReadSocial()) PopulateLists();
    PollLive();
}

bool UIScene_LiveFriends::handlesPointer()
{
    return true;
}

bool UIScene_LiveFriends::HandleMouseInput()
{
#if LCE_PLATFORM_PC
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    if (!g_pInput->GetMenuPointer(m_playerId, &pointerX, &pointerY))
        return false;

    int offsetX = 0, offsetY = 0;
    gConsoleUIController.GetViewportTouchOffset(m_layer->getViewport(), &offsetX, &offsetY);
    const float x = pointerX * gConsoleUIController.getScreenWidth() / 1280.0f - offsetX;
    const float y = pointerY * gConsoleUIController.getScreenHeight() / 720.0f - offsetY;
    const bool moved = x != m_lastPointerX || y != m_lastPointerY;
    m_lastPointerX = x;
    m_lastPointerY = y;
    const unsigned int clicks = g_pInput->ConsumeMenuClicks(m_playerId);
    if ((clicks & C_4JInput::EPcMenuClick_Secondary) != 0)
    {
        navigateBack();
        return true;
    }
    const bool clicked = (clicks & (C_4JInput::EPcMenuClick_Primary | C_4JInput::EPcMenuClick_Quick)) != 0;
    if (clicked && !m_inviteMode)
    {
        const char* titles[eTab_Count] = {"LoadGameTabTitle", "NewGameTabTitle", "JoinGameTabTitle"};
        FJ_FuiNode* root = m_movie->getRootNode()->getFuiNode();
        fuiRect tabBounds[eTab_Count];
        bool valid = true;
        for (int tab = 0; tab < eTab_Count; ++tab)
        {
            fuiRenderNode* title = root->getRenderNodeChildByName(titles[tab]);
            if (!NodeBoundsOnStage(title, tabBounds[tab])) { valid = false; break; }
        }
        if (valid)
        {
            const float first = (tabBounds[0].xMin + tabBounds[0].xMax) * 0.5f;
            const float middle = (tabBounds[1].xMin + tabBounds[1].xMax) * 0.5f;
            const float last = (tabBounds[2].xMin + tabBounds[2].xMax) * 0.5f;
            const float top = std::min(tabBounds[0].yMin, std::min(tabBounds[1].yMin, tabBounds[2].yMin)) - 12.0f;
            const float bottom = std::max(tabBounds[0].yMax, std::max(tabBounds[1].yMax, tabBounds[2].yMax)) + 12.0f;
            if (y >= top && y < bottom && x >= first - (middle - first) * 0.5f
                && x < last + (last - middle) * 0.5f)
            {
                SelectTab(x < (first + middle) * 0.5f ? eTab_Friends
                          : x < (middle + last) * 0.5f ? eTab_Requests : eTab_Invites);
                return false;
            }
        }
    }

    FJ_List* list = m_lists[m_tab].getFuiRenderNode()->getFuiNode()->asFJ_List();
    if (list != NULL && (moved || clicked))
    {
        for (int index = 0; index < list->getINumButtons(); ++index)
        {
            FJ_FuiNode* item = list->ReturnItem(index);
            fuiRect bounds;
            if (item == NULL || !NodeBoundsOnStage(item->m_pRenderNode, bounds) || !Contains(bounds, x, y))
                continue;
            if (m_lists[m_tab].getCurrentSelection() != static_cast<unsigned int>(index))
            {
                list->HighlightItem(index, false);
                m_lists[m_tab].updateChildFocus(index);
            }
            if (clicked)
            {
                ActivateEntry(index);
                gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
            }
            break;
        }
    }
    int wheel = g_pInput->ConsumeMenuWheel(m_playerId);
    while (wheel != 0)
    {
        const int action = wheel > 0 ? EControllerActions_MenuUp : EControllerActions_MenuDown;
        sendInputToMovie(action, false, true, false);
        sendInputToMovie(action, false, false, true);
        wheel += wheel > 0 ? -1 : 1;
    }
#endif
    return false;
}

int UIScene_LiveFriends::GetFaceTexture(const Entry& entry)
{
    std::string revision = entry.revision;
    std::vector<unsigned char> bytes;
#if LCE_PLATFORM_WIIU
    const std::string* png = WiiULiveCosmetics::AvatarSkin(entry.id, revision);
    if (png == NULL) return -1;
#endif
    for (size_t index = 0; index < m_faceTextures.size(); ++index)
        if (m_faceTextures[index].userId == entry.id && m_faceTextures[index].revision == revision) return m_faceTextures[index].texture;

#if LCE_PLATFORM_WIIU
    bytes.assign(png->begin(), png->end());
#else
    wchar_t fileName[64];
    std::swprintf(fileName, 64, L"friend-%u.skin.png", entry.id);
    if (!ReadSkin(fileName, bytes)) return -1;
#endif

    BufferedImage skin(&bytes[0], static_cast<unsigned int>(bytes.size()));
    if (skin.getData() == NULL || skin.getWidth() < 64 || skin.getHeight() < 16) return -1;
    arrayWithLength<int> base(64, false), hat(64, false);
    skin.getRGB(8, 8, 8, 8, base, 0, 8, 0);
    skin.getRGB(40, 8, 8, 8, hat, 0, 8, 0);
    BufferedImage face(8, 8, 1);
    int* pixels = face.getData();
    for (int pixel = 0; pixel < 64; ++pixel)
    {
        const unsigned int under = static_cast<unsigned int>(base[pixel]);
        const unsigned int over = static_cast<unsigned int>(hat[pixel]);
        const unsigned int alpha = over >> 24;
        const unsigned int underAlpha = under >> 24;
        const unsigned int outAlpha = alpha + (underAlpha * (255 - alpha) + 127) / 255;
        unsigned int channels[3] = {(under >> 16) & 255, (under >> 8) & 255, under & 255};
        const unsigned int top[3] = {(over >> 16) & 255, (over >> 8) & 255, over & 255};
        if (outAlpha != 0)
            for (int channel = 0; channel < 3; ++channel)
                channels[channel] = (top[channel] * alpha + channels[channel] * underAlpha * (255 - alpha) / 255
                                     + outAlpha / 2) / outAlpha;
        pixels[pixel] = static_cast<int>((outAlpha << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2]);
    }
    face.preMultiplyAlpha();
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft == NULL || minecraft->getTextureManager() == NULL) return -1;
    const int texture = minecraft->getTextureManager()->getTexture(&face, C4JRender::eTextureFormat_RGBA8, false);
    m_faceTextures.push_back(FaceTexture(entry.id, texture, revision));
    GlStateManager::bindTexture(texture);
    GlStateManager::texParameteri(0, 1, 1);
    return texture;
}

void UIScene_LiveFriends::ReleaseFaceTextures()
{
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft != NULL && minecraft->getTextureManager() != NULL)
        for (size_t index = 0; index < m_faceTextures.size(); ++index)
            minecraft->getTextureManager()->releaseTexture(m_faceTextures[index].texture);
    m_faceTextures.clear();
}

void UIScene_LiveFriends::customDraw(const char* name, fuiRect* rect)
{
    if (std::strcmp(name, "LiveProfile") != 0) return;
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft == NULL || minecraft->mFont == NULL) return;

    // This callback inherits the movie transform, just like the native list and tabs.
    // Never replace its projection with screen-space coordinates.
    GlStateManager::disableCull();
    GlStateManager::disableDepthTest();
    GlStateManager::depthMask(false);
    GlStateManager::disableAlphaTest();
    GlStateManager::pushMatrix();
    GlStateManager::translatef(rect->xMin, rect->yMin, 0.0f);
    GlStateManager::scalef(2.0f, 2.0f, 1.0f);
    const int cardWidth = static_cast<int>((rect->xMax - rect->xMin) / 2);
    const int cardHeight = static_cast<int>((rect->yMax - rect->yMin) / 2);
    MenuFrame localFrame;
    localFrame.unit = 2.0f;
    MenuPainter painter(*m_gui, *minecraft->mFont, localFrame);
    painter.Fill(0, 0, cardWidth, cardHeight, 0xffcad1d3);
    painter.Fill(1, 1, cardWidth - 1, cardHeight - 1, 0xff434f55);

    const unsigned int rawSelection = m_lists[m_tab].getCurrentSelection();
    const bool hasAddRow = m_tab == eTab_Friends && !m_inviteMode;
    const unsigned int selection = hasAddRow && rawSelection > 0 ? rawSelection - 1 : rawSelection;
    if (!(hasAddRow && rawSelection == 0) && selection < m_entries[m_tab].size())
    {
        const Entry& profile = m_entries[m_tab][selection];
        const int face = GetFaceTexture(profile);
        painter.Bevel(6, 6, 36, 36, 0xff858585, 0xffeeeeee, 0xff383838);
        if (face >= 0) DrawTexture(*minecraft->getTextureManager(), face, 7, 7, 28);
        else painter.Centred(Widen(profile.username.substr(0, 1)), 21, 16, MenuColour::kTextWhite, true);
        painter.Text(painter.Trim(Widen(profile.username), cardWidth - 45), 39, 10,
                     MenuColour::kTextWhite, true);
        painter.Text(painter.Trim(L"@" + Widen(profile.username), cardWidth - 45), 39, 24,
                     MenuColour::kTextLight, true);
        std::wstring details = L"Offline";
        if (profile.kind == 'R') details = L"Wants to be your friend";
        else if (profile.kind == 'S') details = L"Friend request sent";
        else if (profile.kind == 'I') details = L"Invited you to play";
        else if (const wchar_t* status = profile.WiiUStatus()) details = status;
        else if (profile.online && profile.presence == "minigame")
        {
            if (profile.activity == "battle") details = L"Playing Battle";
            else if (profile.activity == "tumble") details = L"Playing Tumble";
            else if (profile.activity == "glide") details = L"Playing Glide";
            else details = L"Playing Minecraft";
        }
        else if (profile.online && profile.presence == "game") details = L"Playing Minecraft";
        else if (profile.online) details = L"Online";
        const std::vector<std::wstring> lines = painter.Wrap(details, cardWidth - 12);
        for (size_t line = 0; line < lines.size() && line < 3; ++line)
            painter.Text(lines[line], 6, 42 + static_cast<int>(line) * 12, MenuColour::kTextWhite, true);

        // Invite Friend uses the existing profile card, but only needs identity
        // and presence. Keep the full join/achievement details on Friends.
        if (m_inviteMode)
        {
            if (!m_message.empty())
            {
                const std::vector<std::wstring> messageLines = painter.Wrap(m_message, cardWidth - 12);
                const unsigned int color = m_message == L"Invitation sent"
                    ? 0xffb9e299 : (m_message == L"Sending invitation..." ? 0xffffdd88 : MenuColour::kTextWhite);
                for (size_t line = 0; line < messageLines.size() && line < 2; ++line)
                    painter.Text(messageLines[line], 6, 64 + static_cast<int>(line) * 12, color, true);
            }
            GlStateManager::popMatrix();
            GlStateManager::depthMask(true);
            GlStateManager::enableDepthTest();
            GlStateManager::enableAlphaTest();
            return;
        }

        const std::wstring action = profile.kind == 'R' ? L"Select to accept request"
            : profile.kind == 'S' ? L"Select to cancel request"
            : profile.joinable ? L"Select to join world" : L"No joinable world";
        painter.Text(action, 6, 78, profile.joinable ? 0xffb9e299 : MenuColour::kTextLight, true);
        const std::wstring message = !m_connectionText.empty() ? m_connectionText : m_message;
        const auto messageLines = painter.Wrap(message, cardWidth - 12);
        for (size_t line = 0; line < messageLines.size() && line < 3; ++line)
            painter.Text(messageLines[line], 6, 94 + static_cast<int>(line) * 11, 0xffffdd88, true);

        const int gridTop = cardHeight - 86;
        painter.Text(L"Achievements", 6, gridTop - 27, MenuColour::kTextWhite, true);
        const int pages = (UIScene_AchievementsMenu::DISPLAY_COUNT + 14) / 15;
        const std::wstring count = m_achievementsReady && m_profileId == profile.id
            ? std::to_wstring(m_unlocked.size()) + L" / 93  -  " + std::to_wstring(m_achievementPage + 1) + L"/" + std::to_wstring(pages)
            : L"Not available yet";
        painter.Text(count, 6, gridTop - 15, MenuColour::kTextLight, true);
        std::vector<int> ordered = m_unlocked;
        for (int index = 0; index < UIScene_AchievementsMenu::DISPLAY_COUNT; ++index)
        {
            const int id = UIScene_AchievementsMenu::sDisplayList[index];
            if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) ordered.push_back(id);
        }
        for (int cell = 0; cell < 15; ++cell)
        {
            const int index = m_achievementPage * 15 + cell;
            if (index >= static_cast<int>(ordered.size())) break;
            const int x = 6 + (cell % 5) * 26, y = gridTop + (cell / 5) * 26;
            const bool unlocked = m_achievementsReady && m_profileId == profile.id
                && std::find(m_unlocked.begin(), m_unlocked.end(), ordered[index]) != m_unlocked.end();
            painter.Bevel(x, y, x + 23, y + 23, unlocked ? 0xffb7b7b7 : 0xff82898b, 0xffeeeeee, 0xff505050);
            painter.Outline(x, y, x + 23, y + 23, 0xff171717);
            if (unlocked)
            {
                const int texture = GetAchievementTexture(ordered[index]);
                DrawTexture(*minecraft->getTextureManager(), texture, x + 3, y + 3, 17);
            }
            else
            {
                painter.Outline(x + 8, y + 5, x + 15, y + 13, 0xff53595c);
                painter.Fill(x + 6, y + 10, x + 17, y + 18, 0xff53595c);
                painter.Fill(x + 11, y + 12, x + 12, y + 15, 0xffbfc5c7);
            }
        }
    }
    else
    {
        const wchar_t* empty[] = {L"No friends yet", L"No friend requests", L"No invitations"};
        painter.Text(m_haveSnapshot ? empty[m_tab] : L"Loading Live...", 6, 10, MenuColour::kTextWhite, true);
        const auto lines = painter.Wrap(m_connectionText, cardWidth - 12);
        for (size_t line = 0; line < lines.size() && line < 3; ++line)
            painter.Text(lines[line], 6, 34 + static_cast<int>(line) * 11, 0xffffdd88, true);
    }
    GlStateManager::popMatrix();
}

int UIScene_LiveFriends::GetAchievementTexture(int id)
{
    const auto found = m_achievementTextures.find(id);
    if (found != m_achievementTextures.end()) return found->second;
    if (id < 0 || id > 96) return -1;
    wchar_t path[96];
    std::swprintf(path, 96, L"Graphics\\Achievements\\%ls.png", UIScene_AchievementsMenu::GetInfo(id)->icon);
    if (!gApp.hasArchiveFile(path, false)) return -1;
    auto bytes = gApp.getArchiveFile(path, false);
    if (!bytes.data || !bytes.length) return -1;
    BufferedImage image(bytes.data, bytes.length);
    delete[] bytes.data;
    if (!image.getData()) return -1;
    const int texture = Minecraft::GetInstance()->getTextureManager()->getTexture(&image, C4JRender::eTextureFormat_RGBA8, false);
    m_achievementTextures[id] = texture;
    return texture;
}

std::wstring UIScene_LiveFriends::RegisterFaceIcon(const Entry& entry)
{
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
    return RegisterLiveFaceIcon(*this, entry.id, entry.revision);
#else
    (void)entry;
    return std::wstring();
#endif
}

void UIScene_LiveFriends::SetTabTitles()
{
    if (m_movie == NULL) return;
    FJ_FuiNode* root = m_movie->getRootNode()->getFuiNode();
    const char* names[eTab_Count] = {"LoadGameTabTitle", "NewGameTabTitle", "JoinGameTabTitle"};
    const wchar_t* labels[eTab_Count] = {L"Friends", L"Requests", L"Invites"};
    if (m_inviteMode) labels[0] = L"Friends";
    for (int tab = 0; tab < eTab_Count; ++tab)
    {
        FJ_FuiNode* title = root->getChildByName(names[tab]);
        if (title != NULL && title->asFJ_Base() != NULL)
        {
            title->asFJ_Base()->SetLabel(labels[tab]);
            if (m_inviteMode && tab > 0) title->setVisible(false);
        }
    }
}

void UIScene_LiveFriends::ApplyReferenceLayout()
{
    if (m_movie == NULL) return;
    FJ_FuiNode* root = m_movie->getRootNode()->getFuiNode();
    if (m_inviteMode)
    {
        // This is a scene-local presentation of the existing Load/Create/Join
        // movie. The original menu scene never calls this path and stays intact.
        FJ_FuiNode* load = root->getChildByName("LoadGame");
        FJ_FuiNode* create = root->getChildByName("NewGame");
        FJ_FuiNode* join = root->getChildByName("JoinGame");
        if (load == NULL || load->getNumChildren() == 0) return;

        load->setVisible(true);
        if (create) create->setVisible(false);
        if (join) join->setVisible(false);
        const char* hiddenNodes[] = {"TabOff_Load", "TabOff_Create", "TabOff_Join",
                                     "NewGameTabTitle", "JoinGameTabTitle"};
        for (const char* nodeName : hiddenNodes)
        {
            FJ_FuiNode* node = root->getChildByName(nodeName);
            if (node) node->setVisible(false);
        }

        FJ_FuiNode* title = root->getChildByName("LoadGameTabTitle");
        FJ_FuiNode* selectedTab = load->getChildByName("TabLeft");
        FJ_FuiNode* body = load->getChildAt(0);
        if (title && title->asFJ_Base())
        {
            title->asFJ_Base()->SetLabelAlignment(2);
            // Call the base implementation so the label's text field is laid
            // out with the new centered alignment instead of the tab default.
            title->asFJ_Base()->FJ_Base::SetLabel(L"Friends");
            CenterNodeOnStageX(title, m_movie->getStageWidth());
        }
        // Stretch the native selected tab cap across the header. The row list,
        // bevels, text, selection and controller focus remain native FUI items.
        if (selectedTab && selectedTab->getNumChildren() > 0)
            selectedTab->getChildAt(0)->setWidth(body->getWidth());
    }
    if (root->getChildByName("iggy_LiveProfile") != NULL) return;
    FJ_FuiNode* page = root->getChildByName("LoadGame");
    if (page == NULL || page->getNumChildren() == 0) return;

    // Preserve all native tab/list geometry. Centre the combined composition
    // instead of resizing individual children (which breaks FUI text offsets).
    FJ_FuiNode* body = page->getChildAt(0);
    const float leftWidth = body->getWidth();
    const float cardWidth = leftWidth * 0.46f;
    const float gap = 2.0f;
    const float left = page->getX();
    const float top = page->getY() + body->getY();
    const float bottom = top + body->getHeight();
    fuiRect card = {0.0f, cardWidth, 0.0f, bottom - top};
    auto* renderNode = new fuiRenderNodeCodeGenRect(m_movie->getRootNode()->getStage(), &card,
                                                   "iggy_LiveProfile");
    auto* node = new FJ_FuiNode(renderNode);
    renderNode->setFuiNode(node);
    root->addChild(node);
    node->setX(left + leftWidth + gap);
    node->setY(top);
    root->setX((m_movie->getStageWidth() - leftWidth - gap - cardWidth) * 0.5f - left);
}

void UIScene_LiveFriends::handleReload()
{
    if (m_movie == NULL) return;
    FJ_LoadCreateJoinMenu* document = static_cast<FJ_LoadCreateJoinMenu*>(m_movie->getRootNode()->getFuiNode());
    document->ShowSaveSizeBar(false);
    SetTabTitles();
    document->SetActiveTab(m_tab);
    ApplyReferenceLayout();
    SetTabTitles();
    ReadSocial();
    PopulateLists();
}

bool UIScene_LiveFriends::ReadSocial()
{
#if LCE_PLATFORM_WIIU
    std::string snapshot;
    const bool valid = WiiULiveSocial::ReadSnapshot(snapshot);
#else
    FILE* file = PcLiveBridge::Open(L"social.tsv", L"rb");
    if (file == NULL) return false;
    std::string snapshot;
    char buffer[1024];
    size_t size;
    while ((size = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && snapshot.size() < 65536)
        snapshot.append(buffer, size);
    const bool valid = !std::ferror(file) && snapshot.size() < 65536;
    std::fclose(file);
#endif
    if (!valid) return false;
#if LCE_PLATFORM_WIIU
    const unsigned int generation = WiiULiveCosmetics::AvatarGeneration();
    const bool avatarsChanged = generation != m_avatarGeneration;
    m_avatarGeneration = generation;
    if (avatarsChanged)
        ReleaseFaceTextures();
#else
    const bool avatarsChanged = false;
#endif
    if (!avatarsChanged && m_haveSnapshot && snapshot == m_snapshot) return false;
    for (int tab = 0; tab < eTab_Count; ++tab)
    {
        const unsigned int selected = m_lists[tab].getCurrentSelection();
        const bool hasAddRow = tab == eTab_Friends && !m_inviteMode;
        const unsigned int entryIndex = hasAddRow && selected > 0 ? selected - 1 : selected;
        m_selectedIds[tab] = !(hasAddRow && selected == 0) && entryIndex < m_entries[tab].size()
            ? m_entries[tab][entryIndex].id : 0;
        m_entries[tab].clear();
    }
    m_haveSnapshot = true;
    m_snapshot = snapshot;
    std::istringstream input(snapshot);
    std::string line;
    unsigned int total = 0;
    while (total++ < 200 && std::getline(input, line))
    {
        const auto fields = SplitTabs(line);
        if (fields.size() < 3 || fields[0].size() != 1) continue;
        Entry entry;
        entry.kind = fields[0][0];
        entry.id = static_cast<unsigned int>(std::strtoul(fields[1].c_str(), NULL, 10));
        entry.username = fields[2];
        entry.online = fields.size() > 3 && fields[3] == "1";
        entry.presence = fields.size() > 4 ? fields[4] : "";
        entry.activity = fields.size() > 5 ? fields[5] : "";
        entry.revision = fields.size() > 6 ? fields[6] : "0";
        entry.joinable = fields.size() > 7 && fields[7] == "1";
        entry.world = fields.size() > 8 ? fields[8] : "";
        entry.platform = fields.size() > 9 ? fields[9] : "";
        if (!entry.id || entry.username.empty()) continue;
        if (entry.kind == 'F') m_entries[eTab_Friends].push_back(entry);
        else if (entry.kind == 'R' || entry.kind == 'S') m_entries[eTab_Requests].push_back(entry);
        else if (entry.kind == 'I') m_entries[eTab_Invites].push_back(entry);
    }
    return true;
}

void UIScene_LiveFriends::PollLive()
{
    const long long now = System::processTimeInMilliSecs();
#if LCE_PLATFORM_WIIU
    m_liveConnected = WiiULiveSocial::Connected();
    m_connectionText = m_liveConnected ? L"" : L"Connecting to Live...";
#else
    const auto state = SplitTabs(ReadLiveText(L"social-status.tsv"));
    const unsigned long long nowEpochMs = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const unsigned long long statusEpochMs = state.size() >= 2
        ? std::strtoull(state[1].c_str(), NULL, 10) : 0;
    m_liveConnected = state.size() >= 2 && state[0] == "ok"
        && statusEpochMs <= nowEpochMs && nowEpochMs - statusEpochMs < 30000;
    m_connectionText = m_liveConnected ? L"" : (state.size() > 2 ? Widen(state[2]) : L"Connecting to Live...");
    if (!m_liveConnected && m_connectionText.empty()) m_connectionText = L"Live connection lost. Retrying...";
#endif
    if (!m_pendingAction.empty())
    {
        if (now - m_actionStarted > 15000)
        {
            const bool timedOutInvite = m_pendingOperation == 'V';
            m_pendingAction.clear();
            m_message = L"Request timed out. Please try again.";
            if (timedOutInvite && !m_inviteQueue.empty()) m_nextQueuedInviteAt = now + 800;
        }
#if LCE_PLATFORM_WIIU
        const auto result = SplitTabs(WiiULiveSocial::ActionResult());
#else
        const auto result = SplitTabs(ReadLiveText(L"social-result.tsv"));
#endif
        if (result.size() >= 3 && result[0] == m_pendingAction)
        {
            const char completedOperation = m_pendingOperation;
            const bool joining = completedOperation == 'J' && result[1] == "ok";
            m_pendingAction.clear();
            m_message = Widen(result[2]);
            if (completedOperation == 'V')
            {
                m_inFlightInviteId = 0;
                if (!m_inviteQueue.empty())
                {
                    if (result[1] == "ok") m_message = L"Invitation sent. Next invite queued.";
                    m_nextQueuedInviteAt = now + 1000;
                }
            }
#if LCE_PLATFORM_PC
            PcLiveBridge::Remove(L"social-result.tsv");
#endif
            if (joining) { navigateBack(); return; }
        }
    }
    const unsigned int rawSelected = m_lists[m_tab].getCurrentSelection();
    const bool hasAddRow = m_tab == eTab_Friends && !m_inviteMode;
    const unsigned int selected = hasAddRow && rawSelected > 0 ? rawSelected - 1 : rawSelected;
    const unsigned int id = !(hasAddRow && rawSelected == 0) && selected < m_entries[m_tab].size()
        ? m_entries[m_tab][selected].id : 0;
    if (id != m_profileId)
    {
        m_profileId = id;
        m_achievementPage = 0;
        m_achievementsReady = false;
        m_unlocked.clear();
        if (!m_inviteMode) m_message.clear();
#if LCE_PLATFORM_PC
        FILE* file = PcLiveBridge::Open(L"social-profile.tmp", L"wb");
        if (file) { std::fprintf(file, "%u", id); std::fclose(file); PcLiveBridge::Rename(L"social-profile.tmp", L"social-profile.txt"); }
#elif LCE_PLATFORM_WIIU
        WiiULiveSocial::SelectProfile(m_tab == eTab_Friends || m_tab == eTab_Invites ? id : 0);
#endif
    }
#if LCE_PLATFORM_PC
    const auto profile = SplitTabs(ReadLiveText(L"social-profile.tsv"));
#elif LCE_PLATFORM_WIIU
    const auto profile = SplitTabs(WiiULiveSocial::ProfileResult());
#endif
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
    if (profile.size() >= 3 && std::strtoul(profile[0].c_str(), NULL, 10) == m_profileId)
    {
        m_achievementsReady = profile[1] == "ok";
        m_unlocked.clear();
        std::istringstream ids(profile[2]);
        std::string value;
        while (std::getline(ids, value, ',') && m_unlocked.size() < 93)
        {
            const int achievement = std::atoi(value.c_str());
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos || achievement < 0 || achievement > 96) continue;
            const AchievementInfo* info = UIScene_AchievementsMenu::GetInfo(achievement);
            if (info && info->icon[0] && std::find(m_unlocked.begin(), m_unlocked.end(), achievement) == m_unlocked.end()) m_unlocked.push_back(achievement);
        }
    }
#endif
    if (m_inviteMode && m_pendingAction.empty() && !m_inviteQueue.empty() && now >= m_nextQueuedInviteAt)
    {
        const unsigned int nextInvite = m_inviteQueue.front();
        m_inviteQueue.erase(m_inviteQueue.begin());
        if (m_liveConnected)
            SendAction('V', nextInvite);
        else
        {
            m_inviteQueue.clear();
            m_message = L"Live connection lost. Retrying...";
        }
    }
}

const wchar_t* UIScene_LiveFriends::Entry::WiiUStatus() const
{
    if (!online || platform != "wiiu") return NULL;
    return presence == "game" || presence == "minigame" ? L"Playing on Wii U" : L"Online on Wii U";
}

std::wstring UIScene_LiveFriends::EntryLabel(const Entry& entry) const
{
    std::wstring label = Widen(entry.username);
    if (entry.kind == 'F')
    {
        if (const wchar_t* status = entry.WiiUStatus())
            label.append(L"  |  ").append(status);
    }
    else if (entry.kind == 'R') label += L"  |  Friend request";
    else if (entry.kind == 'S') label += L"  |  Request sent";
    else if (entry.kind == 'I')
    {
        label += L"  |  Invited you";
        if (!entry.world.empty()) label += L" to " + Widen(entry.world);
    }
    return label;
}

void UIScene_LiveFriends::PopulateLists()
{
    if (m_movie == NULL) return;
    for (int tab = 0; tab < eTab_Count; ++tab)
    {
        m_lists[tab].clearList();
        if (tab == eTab_Friends && !m_inviteMode)
            m_lists[tab].AddNewItem(L"+  Add Friend", 0, std::wstring());
        for (size_t index = 0; index < m_entries[tab].size(); ++index)
        {
            const std::wstring iconName = RegisterFaceIcon(m_entries[tab][index]);
            const unsigned int row = static_cast<unsigned int>(index) + (tab == eTab_Friends && !m_inviteMode ? 1u : 0u);
            m_lists[tab].AddNewItem(EntryLabel(m_entries[tab][index]), static_cast<int>(row),
                                    iconName);
        }
        if (!m_entries[tab].empty())
        {
            unsigned int selected = 0;
            for (size_t index = 0; index < m_entries[tab].size(); ++index)
                if (m_entries[tab][index].id == m_selectedIds[tab])
                    selected = static_cast<unsigned int>(index) + (tab == eTab_Friends && !m_inviteMode ? 1u : 0u);
            m_lists[tab].setCurrentSelection(selected, false, true);
            m_lists[tab].updateChildFocus(selected);
        }
    }
}

void UIScene_LiveFriends::SendAction(char action, unsigned int id)
{
    if (!m_pendingAction.empty())
    {
        if (m_inviteMode && action == 'V' && m_pendingOperation == 'V')
        {
            if (id == m_inFlightInviteId
                || std::find(m_inviteQueue.begin(), m_inviteQueue.end(), id) != m_inviteQueue.end())
                m_message = L"Invitation already sending or queued.";
            else
            {
                m_inviteQueue.push_back(id);
                m_message = L"Invitation queued.";
            }
        }
        return;
    }
    if (!m_liveConnected) { m_message = L"Live is offline. Try again shortly."; return; }
    const std::string request = std::to_string(System::currentRealTimeMillis());
#if LCE_PLATFORM_WIIU
    std::string target = std::to_string(id);
    if (action == 'V')
        for (const Entry& entry : m_entries[eTab_Friends])
            if (entry.id == id) { target = entry.username; break; }
    if (!WiiULiveSocial::SendAction(request, action, target))
    {
        m_message = L"Could not send the request.";
        return;
    }
#else
    FILE* file = PcLiveBridge::Open(L"social-action.tmp", L"wb");
    if (file == NULL) { m_message = L"Could not send the request."; return; }
    std::fprintf(file, "%s\t%c\t%u", request.c_str(), action, id);
    std::fclose(file);
    if (!PcLiveBridge::Rename(L"social-action.tmp", L"social-action.tsv"))
    {
        m_message = L"Could not send the request.";
        return;
    }
#endif
    m_pendingAction = request;
    m_pendingOperation = action;
    m_actionStarted = System::processTimeInMilliSecs();
    if (action == 'V') m_inFlightInviteId = id;
    m_message = action == 'V' ? L"Sending invitation..." : L"Please wait...";
}

bool UIScene_LiveFriends::JoinEntry(const Entry& entry)
{
#if LCE_PLATFORM_WIIU
    if (!gProfile->AllowedToPlayMultiplayer(m_playerId) || !WiiULiveGames::Join(entry.id))
    {
        m_message = L"No joinable world right now.";
        return false;
    }
    // The network state change opens ConnectingProgress. Returning to the main menu
    // here would clear the profile that JoinServerAddress locked for the connection.
    return true;
#else
    if (!entry.joinable) { m_message = L"No joinable world right now."; return false; }
    SendAction('J', entry.id);
    return !m_pendingAction.empty();
#endif
}

void UIScene_LiveFriends::SelectTab(ETab tab)
{
    if (tab < eTab_Friends || tab >= eTab_Count || tab == m_tab) return;
    m_tab = tab;
    gConsoleUIController.PlayUISFX(SoundEvents::UI_FOCUS);
    static_cast<FJ_LoadCreateJoinMenu*>(m_movie->getRootNode()->getFuiNode())->SetActiveTab(tab);
    SetTabTitles();
    updateTooltips();
}

void UIScene_LiveFriends::ActivateEntry(int selection)
{
    if (m_tab == eTab_Friends && !m_inviteMode && selection == 0)
    { RequestAddFriend(); return; }
    if (m_tab == eTab_Friends && !m_inviteMode) --selection;
    if (selection < 0 || static_cast<size_t>(selection) >= m_entries[m_tab].size()) return;
    const Entry& entry = m_entries[m_tab][static_cast<size_t>(selection)];
    if (m_inviteMode && entry.kind == 'F') SendAction('V', entry.id);
    else if (entry.kind == 'F' || entry.kind == 'I') JoinEntry(entry);
    else if (entry.kind == 'R')
    {
        SendAction('A', entry.id);

    }
    else if (entry.kind == 'S')
    {
        SendAction('C', entry.id);

    }
}

void UIScene_LiveFriends::RequestAddFriend()
{
    if (!m_liveConnected) { m_message = L"Live is offline. Try again shortly."; return; }
    if (m_addFriendKeyboardPending || !m_pendingAction.empty()) return;
    wchar_t initial[21] = {};
    m_addFriendKeyboardPending = true;
    const unsigned int busy = g_pInput->RequestKeyboard(L"Add Friend", initial, m_playerId, 20,
        &AddFriendKeyboardReturned,
        reinterpret_cast<void*>(static_cast<uintptr_t>(GetCallbackUniqueId())), C_4JInput::EKeyboardMode_0);
    if (busy) m_addFriendKeyboardPending = false;
}

int UIScene_LiveFriends::AddFriendKeyboardReturned(void* context, bool accepted)
{
    gConsoleUIController.EnterCallbackIdCriticalSection();
    UIScene_LiveFriends* scene = static_cast<UIScene_LiveFriends*>(gConsoleUIController.GetSceneFromCallbackId(
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(context))));
    if (scene)
    {
        scene->m_addFriendKeyboardPending = false;
        if (accepted)
        {
            wchar_t entered[21] = {};
            g_pInput->GetText(entered, 20);
            std::string username;
            for (const wchar_t* at = entered; *at; ++at)
            {
                if ((*at >= L'a' && *at <= L'z') || (*at >= L'A' && *at <= L'Z')
                    || (*at >= L'0' && *at <= L'9') || *at == L'_') username.push_back(static_cast<char>(*at));
                else { username.clear(); break; }
            }
            if (username.size() < 3 || username.size() > 20) scene->m_message = L"Enter a valid username.";
            else
            {
                const std::string request = std::to_string(System::currentRealTimeMillis());
#if LCE_PLATFORM_WIIU
                if (WiiULiveSocial::SendAction(request, 'N', username))
                {
                    scene->m_pendingAction = request;
                    scene->m_pendingOperation = 'N';
                    scene->m_actionStarted = System::processTimeInMilliSecs();
                    scene->m_message = L"Sending friend request...";
                }
                else scene->m_message = L"Could not send the request.";
#else
                FILE* file = PcLiveBridge::Open(L"social-action.tmp", L"wb");
                if (file)
                {
                    std::fprintf(file, "%s\tN\t%s", request.c_str(), username.c_str());
                    std::fclose(file);
                    if (PcLiveBridge::Rename(L"social-action.tmp", L"social-action.tsv"))
                    {
                        scene->m_pendingAction = request;
                        scene->m_pendingOperation = 'N';
                        scene->m_actionStarted = System::processTimeInMilliSecs();
                        scene->m_message = L"Sending friend request...";
                    }
                }
#endif
            }
        }
    }
    gConsoleUIController.LeaveCallbackIdCriticalSection();
    return 0;
}

void UIScene_LiveFriends::handlePress(int controlId, int childId)
{
    if (controlId == static_cast<int>(m_tab)) ActivateEntry(childId);
    else if (controlId >= 0 && controlId < eTab_Count)
    {
        SelectTab(static_cast<ETab>(controlId));
        ActivateEntry(childId);
    }
}

void UIScene_LiveFriends::handleTouchInput(unsigned int, int, int, int touchBox, bool pressed, bool, bool)
{
    if (!pressed || m_inviteMode || touchBox < 4 || touchBox >= 4 + eTab_Count)
        return;
    SelectTab(static_cast<ETab>(touchBox - 4));
}

void UIScene_LiveFriends::handleFocusChange(int controlId, int childId)
{
    if (controlId >= 0 && controlId < eTab_Count)
        m_lists[controlId].updateChildFocus(childId);
}

void UIScene_LiveFriends::handleInput(int playerId, int action, bool repeat, bool pressed, bool released,
                                     bool& handled)
{
    gConsoleUIController.AnimateKeyPress(playerId, action, repeat, pressed, released);
    if (!pressed)
    {
        sendInputToMovie(action, repeat, pressed, released);
        handled = true;
        return;
    }
    if (action == EControllerActions_MenuCancel)
    {
        navigateBack();
        handled = true;
    }
    else if (action == EControllerActions_MenuLeft || action == EControllerActions_MenuLeftScroll)
    {
        if (!m_inviteMode) SelectTab(static_cast<ETab>((m_tab + eTab_Count - 1) % eTab_Count));
        handled = true;
    }
    else if (action == EControllerActions_MenuRight || action == EControllerActions_MenuRightScroll)
    {
        if (!m_inviteMode) SelectTab(static_cast<ETab>((m_tab + 1) % eTab_Count));
        handled = true;
    }
    else if (action == EControllerActions_MenuStart)
    {
        ReadSocial();
        PopulateLists();
        handled = true;
    }
    else if (action == EControllerActions_MenuX && (m_tab == eTab_Requests || m_tab == eTab_Invites))
    {
        const unsigned int selection = m_lists[m_tab].getCurrentSelection();
        if (selection < m_entries[m_tab].size())
        {
            const Entry& entry = m_entries[m_tab][selection];
            if (entry.kind == 'R') SendAction('D', entry.id);
            else if (entry.kind == 'S') SendAction('C', entry.id);
            else if (entry.kind == 'I') SendAction('X', entry.id);

        }
        handled = true;
    }
    else if (action == EControllerActions_MenuY && m_tab == eTab_Friends && !m_inviteMode)
    {
        m_achievementPage = (m_achievementPage + 1) % ((UIScene_AchievementsMenu::DISPLAY_COUNT + 14) / 15);
        handled = true;
    }
    else
    {
        sendInputToMovie(action, repeat, pressed, released);
        handled = true;
    }
}

void UIScene_LiveFriends::updateTooltips()
{
    gConsoleUIController.SetTooltips(m_playerId, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK,
                                     m_tab == eTab_Friends ? -1 : IDS_PRO_NOTONLINE_DECLINE,
                                     m_tab == eTab_Friends && !m_inviteMode ? IDS_TOOLTIPS_NEXT_PAGE : -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1, false, false);
}

void UIScene_LiveFriends::updateComponents()
{
    // Pause-menu subscenes inherit the in-world backdrop from PauseMenu. Keep
    // it for the invite picker instead of replacing it with the title panorama.
    m_layer->showComponent(m_playerId, EUIScene_Panorama, !m_inviteMode);
    if (m_inviteMode)
        m_layer->showComponent(m_playerId, EUIScene_MenuBackground, true);
    m_layer->showComponent(m_playerId, EUIScene_Logo, false);
}
