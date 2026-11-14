#include "game/ui/UIScene_CustomMenu.hpp"
#include "game/core/StringIds.hpp"

#include "game/client/audio/SoundEvents.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/System.hpp"
#include "game/ui/FJ_FuiNode.hpp"
#include "game/ui/FJ_HtmlText.hpp"
#include "game/ui/FJ_MultilistMenu.hpp"
#include "game/ui/UICustomMenuPage.hpp"
#include "game/ui/UILayer.hpp"
#include "game/ui/UIString.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRenderNodeEditText.hpp"

extern ConsoleUIController gConsoleUIController;

namespace
{
const char* const kPanelNames[] = {"MultiList", "BackgroundPanel_List", "Description", "BackgroundPanel_Text"};
const float kDescriptionSlideMilliseconds = 220.0f;

float EaseOut(float amount)
{
    float remaining = 1.0f - amount;
    return 1.0f - remaining * remaining * remaining;
}
} // namespace

UICustomMenuPage::~UICustomMenuPage()
{
}

void UICustomMenuPage::OnPress(UIScene_CustomMenu&, int)
{
}

void UICustomMenuPage::OnToggle(UIScene_CustomMenu&, int, bool)
{
}

void UICustomMenuPage::OnSlide(UIScene_CustomMenu&, int, int)
{
}

void UICustomMenuPage::OnClose(UIScene_CustomMenu&)
{
}

void UICustomMenuPage::OnGainFocus(UIScene_CustomMenu&)
{
}

bool UICustomMenuPage::OnAction(UIScene_CustomMenu&, int)
{
    return false;
}

bool UICustomMenuPage::KeepDescriptionOpen() const
{
    return false;
}

bool UICustomMenuPage::GetTooltips(int&, int&) const
{
    return false;
}

bool UICustomMenuPage::IsOverlay() const
{
    return false;
}

bool UICustomMenuPage::ShowLogo() const
{
    return true;
}

UIScene_CustomMenu::UIScene_CustomMenu(int playerId, void* data, UILayer* layer)
    : UIScene(playerId, layer),
      m_page(static_cast<UICustomMenuPage*>(data)),
      m_maximumDescriptionHeight(0.0f),
      m_focusId(-1),
      m_descriptionOpen(false),
      m_descriptionAmount(0.0f),
      m_lastAnimationTime(0)
{
    for (int panel = 0; panel < ePanel_Count; ++panel)
    {
        m_panels[panel] = NULL;
        m_openX[panel] = 0.0f;
        m_closedX[panel] = 0.0f;
    }
    initialiseMovie();
    m_description.init(UIString(L""));
    Build();
}

UIScene_CustomMenu::~UIScene_CustomMenu()
{
    delete m_page;
}

std::wstring UIScene_CustomMenu::getMoviePath()
{
    return gApp.GetLocalPlayerCount() > 1 ? std::wstring(L"MultilistMenuSplit") : std::wstring(L"MultilistMenu");
}

unsigned int UIScene_CustomMenu::mapElementsAndNames()
{
    UIScene::mapElementsAndNames();
    fuiRenderNode* root = m_movie->getRootNode();
    m_list.setupControl(this, root, std::string("MultiList"));
    m_controls.push_back(&m_list);
    m_description.setupControl(this, root, std::string("Description"));
    m_controls.push_back(&m_description);
    return 1;
}

int UIScene_CustomMenu::getSceneType()
{
    return EUIScene_CustomMenu;
}

void UIScene_CustomMenu::Build()
{
    m_descriptions.clear();
    if (m_page != NULL)
        m_page->Build(*this);

    FJ_MultilistMenu* menu = static_cast<FJ_MultilistMenu*>(m_movie->getRootNode()->getFuiNode());
    FJ_FuiNode* description = menu->getChildByName(kPanelNames[ePanel_Description]);
    m_maximumDescriptionHeight = description != NULL ? description->getHeight() : 0.0f;
    menu->DoVerticalResizeCheck();
    doHorizontalResizeCheck();
    for (int panel = 0; panel < ePanel_Count; ++panel)
    {
        m_panels[panel] = menu->getChildByName(kPanelNames[panel]);
        m_openX[panel] = m_panels[panel] != NULL ? m_panels[panel]->getX() : 0.0f;
    }
    PlaceDescriptionBehindList(menu);
    menu->HideDescription();
    for (int panel = 0; panel < ePanel_Count; ++panel)
        m_closedX[panel] = m_panels[panel] != NULL ? m_panels[panel]->getX() : 0.0f;
    m_closedX[ePanel_Description] = m_closedX[ePanel_List];
    m_closedX[ePanel_DescriptionBackground] = m_closedX[ePanel_ListBackground];
    if (m_page != NULL && m_page->KeepDescriptionOpen())
    {
        m_descriptionOpen = true;
        m_descriptionAmount = 1.0f;
    }
    else if (m_descriptions.empty())
    {
        m_descriptionOpen = false;
        m_descriptionAmount = 0.0f;
    }
    ApplyDescriptionPanel();
    gConsoleUIController.TouchBoxRebuild(this);
}

void UIScene_CustomMenu::HandleDLCMountingComplete()
{
    int focusId = m_focusId;
    reloadMovie(true);
    RestoreFocus(focusId);
}

void UIScene_CustomMenu::Rebuild()
{
    int focusId = m_focusId;
    m_list.clearList();
    m_descriptions.clear();
    if (m_page != NULL)
        m_page->Build(*this);
    FJ_MultilistMenu* menu = static_cast<FJ_MultilistMenu*>(m_movie->getRootNode()->getFuiNode());
    menu->DoVerticalResizeCheck();
    ApplyDescriptionPanel();
    gConsoleUIController.TouchBoxRebuild(this);
    RestoreFocus(focusId);
}

void UIScene_CustomMenu::RestoreFocus(int focusId)
{
    int index = m_list.getListIndex(focusId);
    if (index >= 0)
        m_list.setCurrentSelection(index, false, true);
    m_focusId = focusId;
    ShowDescription(focusId);
    updateTooltips();
}

void UIScene_CustomMenu::PlaceDescriptionBehindList(FJ_FuiNode* menu)
{
    FJ_FuiNode* listBackground = m_panels[ePanel_ListBackground];
    FJ_FuiNode* description = m_panels[ePanel_Description];
    FJ_FuiNode* descriptionBackground = m_panels[ePanel_DescriptionBackground];
    if (listBackground == NULL || description == NULL || descriptionBackground == NULL)
        return;

    fuiRenderNode* behindList[3] = {descriptionBackground->m_pRenderNode, description->m_pRenderNode, NULL};
    if (description->isFJ_HtmlText())
        behindList[2] = description->asFJ_HtmlText()->m_pText;

    for (int node = 0; node < 3; ++node)
    {
        if (behindList[node] == NULL)
            continue;
        menu->removeChild(behindList[node], false);
        menu->addChildAt(behindList[node], menu->getChildIndex(listBackground));
    }
}

void UIScene_CustomMenu::tick()
{
    UIScene::tick();
    int now = static_cast<int>(System::processTimeInMilliSecs());
    float target = m_descriptionOpen ? 1.0f : 0.0f;
    if (m_descriptionAmount != target)
    {
        float step = static_cast<float>(now - m_lastAnimationTime) / kDescriptionSlideMilliseconds;
        if (step < 0.0f || step > 1.0f)
            step = 1.0f;
        if (m_descriptionAmount < target)
            m_descriptionAmount = m_descriptionAmount + step > target ? target : m_descriptionAmount + step;
        else
            m_descriptionAmount = m_descriptionAmount - step < target ? target : m_descriptionAmount - step;
    }
    m_lastAnimationTime = now;
    ApplyDescriptionPanel();
}

void UIScene_CustomMenu::ToggleDescriptionPanel()
{
    if (m_page != NULL && m_page->KeepDescriptionOpen())
        return;
    if (!m_descriptionOpen && !HasDescription(m_focusId))
        return;
    m_descriptionOpen = !m_descriptionOpen;
    m_lastAnimationTime = static_cast<int>(System::processTimeInMilliSecs());
    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    updateTooltips();
}

void UIScene_CustomMenu::ApplyDescriptionPanel()
{
    float eased = EaseOut(m_descriptionAmount);
    bool showDescription = m_descriptionAmount > 0.0f;
    for (int panel = 0; panel < ePanel_Count; ++panel)
    {
        if (m_panels[panel] == NULL)
            continue;
        m_panels[panel]->setX(m_closedX[panel] + (m_openX[panel] - m_closedX[panel]) * eased);
        if (panel == ePanel_Description || panel == ePanel_DescriptionBackground)
        {
            if (m_panels[panel]->isVisible() != showDescription)
                m_panels[panel]->setVisible(showDescription);
            m_panels[panel]->setAlpha(eased);
        }
    }

    FJ_FuiNode* description = m_panels[ePanel_Description];
    if (description != NULL && description->isFJ_HtmlText())
    {
        float textAlpha = eased * 2.0f - 1.0f;
        description->asFJ_HtmlText()->m_pText->setAlpha(textAlpha > 0.0f ? textAlpha : 0.0f);
    }
}

bool UIScene_CustomMenu::HasDescription(int id) const
{
    return m_descriptions.find(id) != m_descriptions.end();
}

void UIScene_CustomMenu::updateTooltips()
{
    int tooltipX =
        m_descriptionOpen || HasDescription(m_focusId) ? static_cast<int>(IDS_TOOLTIPS_SHOW_DESCRIPTION) : -1;
    int tooltipY = -1;
    if (m_page != NULL && m_page->KeepDescriptionOpen())
        tooltipX = -1;
    if (m_page != NULL)
        m_page->GetTooltips(tooltipX, tooltipY);
    gConsoleUIController.SetTooltips(m_playerId, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, tooltipX, tooltipY, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, false, false);
}

void UIScene_CustomMenu::updateComponents()
{
    bool showLogo = m_page == NULL || m_page->ShowLogo();
    if (m_page != NULL && m_page->IsOverlay() && Minecraft::GetInstance()->mLevel != NULL)
        return;
    if (Minecraft::GetInstance()->mLevel == NULL)
    {
        m_layer->showComponent(m_playerId, EUIScene_Panorama, true);
        m_layer->showComponent(m_playerId, EUIScene_Logo, showLogo);
    }
    else
    {
        m_layer->showComponent(m_playerId, EUIScene_Panorama, false);
        m_layer->showComponent(m_playerId, EUIScene_Logo, showLogo && gApp.GetLocalPlayerCount() == 1);
    }
}

bool UIScene_CustomMenu::hidesLowerScenes()
{
    return m_page != NULL && m_page->IsOverlay() ? false : UIScene::hidesLowerScenes();
}

bool UIScene_CustomMenu::blocksInput()
{
    return m_page != NULL && m_page->IsOverlay() ? false : UIScene::blocksInput();
}

void UIScene_CustomMenu::handleInput(int playerId, int action, bool repeat, bool pressed, bool released,
                                     bool& handled)
{
    gConsoleUIController.AnimateKeyPress(playerId, action, repeat, pressed, released);
    if (pressed && m_page != NULL
        && (action == EControllerActions_MenuX || action == EControllerActions_MenuY
            || action == EControllerActions_MenuLeft || action == EControllerActions_MenuRight
            || action == EControllerActions_MenuLeftScroll || action == EControllerActions_MenuRightScroll)
        && m_page->OnAction(*this, action))
    {
        handled = true;
        return;
    }
    switch (action)
    {
    case EControllerActions_MenuCancel:
        if (pressed)
        {
            if (m_page != NULL)
                m_page->OnClose(*this);
            navigateBack();
            handled = true;
        }
        break;
    case EControllerActions_MenuX:
        if (pressed)
        {
            ToggleDescriptionPanel();
            handled = true;
        }
        break;
    case EControllerActions_MenuUp:
    case EControllerActions_MenuDown:
    case EControllerActions_MenuRight:
    case EControllerActions_MenuLeft:
    case EControllerActions_MenuPageUp:
    case EControllerActions_MenuPageDown:
    case EControllerActions_MenuOtherStickUp:
    case EControllerActions_MenuOtherStickDown:
    case EControllerActions_MenuOk:
        sendInputToMovie(action, repeat, pressed, released);
        break;
    }
}

void UIScene_CustomMenu::handleReload()
{
    Build();
}

void UIScene_CustomMenu::handlePress(int, int childId)
{
    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    if (m_page != NULL)
        m_page->OnPress(*this, childId);
}

void UIScene_CustomMenu::handleCheckboxElementToggled(int, int childId, bool checked)
{
    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    if (m_page != NULL)
        m_page->OnToggle(*this, childId, checked);
}

void UIScene_CustomMenu::handleSliderElementMove(int, int childId, int value)
{
    gConsoleUIController.PlayUISFX(SoundEvents::UI_SCROLL);
    if (m_page != NULL)
        m_page->OnSlide(*this, childId, value);
}

void UIScene_CustomMenu::handleFocusChange(int, int childFocus)
{
    m_focusId = childFocus;
    ShowDescription(childFocus);
    updateTooltips();
}

void UIScene_CustomMenu::handleInitFocus(int focus, int childFocus)
{
    handleFocusChange(focus, childFocus);
}

void UIScene_CustomMenu::handleGainFocus(bool hadFocus)
{
    UIScene::handleGainFocus(hadFocus);
    if (hadFocus && m_page != NULL)
        m_page->OnGainFocus(*this);
}

void UIScene_CustomMenu::ShowDescription(int id)
{
    if (m_descriptions.empty())
        return;
    std::map<int, std::wstring>::const_iterator found = m_descriptions.find(id);
    m_description.setLabel(UIString(found != m_descriptions.end() ? found->second : std::wstring()), true, false);
    FitDescriptionPanel();
}

void UIScene_CustomMenu::FitDescriptionPanel()
{
    FJ_FuiNode* description = m_panels[ePanel_Description];
    FJ_FuiNode* background = m_panels[ePanel_DescriptionBackground];
    if (description == NULL || background == NULL || !description->isFJ_HtmlText())
        return;

    FJ_HtmlText* text = description->asFJ_HtmlText();
    float arrowHeight = text->m_bNoBackground ? 0.0f : text->m_pUpArrow->getHeight();
    float height = text->m_pText->getTextHeight() + arrowHeight;
    if (height > m_maximumDescriptionHeight)
        height = m_maximumDescriptionHeight;

    text->setHeight(height);
    background->getChildAt(0)->setHeight(height + 2.0f * (description->getY() - background->getY()));
    text->SetLabel(text->m_pText->getText());
}

void UIScene_CustomMenu::AddButton(int id, const std::wstring& label, const std::wstring& description)
{
    m_list.AddNewButton(label, id, false);
    if (!description.empty())
        m_descriptions[id] = description;
}

void UIScene_CustomMenu::AddCheckbox(int id, const std::wstring& label, bool checked, const std::wstring& description)
{
    m_list.AddNewCheckbox(label, id, checked);
    if (!description.empty())
        m_descriptions[id] = description;
}

void UIScene_CustomMenu::AddSlider(int id, const std::wstring& label, int minimum, int maximum, int value,
                                   const std::wstring& description)
{
    m_list.AddNewSlider(label, id, minimum, maximum, value, true, 7, 3);
    if (!description.empty())
        m_descriptions[id] = description;
}

void UIScene_CustomMenu::AddValue(int id, const std::wstring& label, const std::wstring& value,
                                  const std::wstring& description)
{
    m_list.AddNewControllerButton(label, value, true, id);
    if (!description.empty())
        m_descriptions[id] = description;
}

void UIScene_CustomMenu::AddLabel(int id, const std::wstring& label)
{
    m_list.AddNewLabel(label, id);
}

void UIScene_CustomMenu::SetItemLabel(int id, const std::wstring& label)
{
    m_list.SetItemLabel(id, UIString(label), false);
}

void UIScene_CustomMenu::SetItemValue(int id, const std::wstring& value)
{
    m_list.SetSecondaryLabel(id, UIString(value), true, true, false);
}

bool UIScene_CustomMenu::GetCheckboxValue(int id)
{
    return m_list.GetCheckboxValue(id);
}

int UIScene_CustomMenu::GetSliderValue(int id)
{
    return static_cast<int>(m_list.GetSliderValue(id));
}

void UIScene_CustomMenu::SetCheckboxValue(int id, bool checked)
{
    m_list.SetCheckboxValue(id, checked, true);
}

void UIScene_CustomMenu::SetSliderValue(int id, int value)
{
    m_list.SetSliderValue(id, value, true);
}

void UIScene_CustomMenu::SetDescription(int id, const std::wstring& description)
{
    m_descriptions[id] = description;
    if (id == m_focusId)
        ShowDescription(id);
}

int UIScene_CustomMenu::GetFocusId() const
{
    return m_focusId;
}

void UIScene_CustomMenu::OpenPage(UICustomMenuPage* page)
{
    gConsoleUIController.NavigateToScene(m_playerId, EUIScene_CustomMenu, page, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

int UIScene_CustomMenu::GetPlayer() const
{
    return m_playerId;
}
