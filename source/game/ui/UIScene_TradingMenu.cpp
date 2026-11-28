#include "game/ui/UIScene_TradingMenu.hpp"
#include "game/core/StringIds.hpp"

#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/HtmlString.hpp"
#include "game/core/L10N.hpp"
#include "game/core/GameModeConstraints.hpp"
#include "game/core/Merchant.hpp"
#include "game/core/Rarity.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Mth.hpp"
#include "game/core/StringUtils.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/core/Slot.hpp"
#include "game/minigame/Tutorial.hpp"
#include "game/network/ClientPacketListener.hpp"
#include "game/network/ServerboundCustomPayloadPacket.hpp"
#include "game/network/TradeItemPacket.hpp"
#include "game/ui/FJ_TradingMenu.hpp"
#include "game/ui/MerchantMenu.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRect.hpp"
#include "game/world/generation/MultiPlayerGameMode.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/Player.hpp"
#include "game/core/GameSettings.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>

extern ConsoleUIController gConsoleUIController;

struct TradingMenuData
{
    boost::shared_ptr<Inventory> inventory;
    boost::shared_ptr<Merchant> merchant;
    Level* level;
};

// Display-only slots follow the merchant container slots and seven offer icons.
static constexpr int TradeResultSlot = 48;

UIScene_TradingMenu::UIScene_TradingMenu(int iPad, void* initData, UILayer* layer)
    : UIScene(iPad, layer),
      m_merchant(),
      m_trades(),
      m_mainPanel(),
      m_tradingBar(),
      m_request1(),
      m_request2(),
      m_result(),
      m_hotBar(),
      m_inventory(),
      m_inventoryLabel(),
      m_villagerText(),
      m_requiredLabel(),
      m_request1Label(),
      m_request2Label()
{
    initialiseMovie();
    m_tradeFocusSet = false;
    m_scrollRightShown = true;
    m_merchantMenu = NULL;
    m_tradeRow = 0;
    m_tradeOffset = 0;
    m_scrollLeftShown = true;
    m_villagerText.init(UIString(L""));
    m_inventoryLabel.init(UIString(L10N::GetString(IDS_INVENTORY)));
    m_requiredLabel.init(UIString(L10N::GetString(IDS_REQUIRED_ITEMS_FOR_TRADE)));
    m_request1Label.init(UIString(L""));
    m_request2Label.init(UIString(L""));

    TradingMenuData* sceneData = static_cast<TradingMenuData*>(initData);
    m_merchant = sceneData->merchant;

    if (Minecraft::GetInstance()->mGameModes[iPad] != 0)
    {
        Tutorial* tutorial = Minecraft::GetInstance()->mGameModes[iPad]->getTutorial();
        if (tutorial != 0)
        {
            m_previousTutorialState = tutorial->currentState;
            tutorial->changeTutorialState(eTutorial_State_TradingMenu, this);
        }
    }

    m_merchantMenu = new MerchantMenu(sceneData->inventory, sceneData->merchant, sceneData->level);
    Minecraft::GetInstance()->mLocalPlayers[m_playerId].get()->mContainerMenu = m_merchantMenu;

    m_request1.addSlots(0x27, 1);
    m_request2.addSlots(0x28, 1);
    m_tradingBar.addSlots(0x29, 7);
    m_result.addSlot(TradeResultSlot);
    m_inventory.addSlots(3, 0x1b);
    m_hotBar.addSlots(0x1e, 9);

    delete sceneData;

    gConsoleUIController.OverrideSFX(m_playerId, 0, true);
    gConsoleUIController.OverrideSFX(m_playerId, 0x14, true);
    gConsoleUIController.OverrideSFX(m_playerId, 0xb, true);
    gConsoleUIController.OverrideSFX(m_playerId, 10, true);
    gConsoleUIController.OverrideSFX(m_playerId, 7, true);
    gConsoleUIController.OverrideSFX(m_playerId, 6, true);
    gConsoleUIController.OverrideSFX(m_playerId, 4, true);
    gConsoleUIController.OverrideSFX(m_playerId, 5, true);
    gApp.SetRichPresenceContext(iPad, 0xd);
}

UIScene_TradingMenu::~UIScene_TradingMenu()
{
    const auto& player = Minecraft::GetInstance()->mLocalPlayers[m_playerId];
    if (player && player->mContainerMenu == m_merchantMenu)
        player->closeContainer();
    delete m_merchantMenu;
}

unsigned int UIScene_TradingMenu::mapElementsAndNames()
{
    UIScene::mapElementsAndNames();
    fuiRenderNode* currentNode = m_movie->getRootNode();
    {
        m_mainPanel.setupControl(this, currentNode, std::string("MainPanel"));
        m_controls.push_back(&m_mainPanel);
    }
    {
        fuiRenderNode* parentNode = currentNode;
        currentNode = m_mainPanel.getFuiRenderNode();
        {
            m_tradingBar.setupControl(this, currentNode, std::string("TradingBar"));
            m_controls.push_back(&m_tradingBar);
        }
        {
            m_request1.setupControl(this, currentNode, std::string("Request1"));
            m_controls.push_back(&m_request1);
        }
        {
            m_request2.setupControl(this, currentNode, std::string("Request2"));
            m_controls.push_back(&m_request2);
        }
        if (m_result.setupControl(this, currentNode, "Result"))
            m_controls.push_back(&m_result);
        {
            m_villagerText.setupControl(this, currentNode, std::string("VillagerText"));
            m_controls.push_back(&m_villagerText);
        }
        {
            if (m_requiredLabel.setupControl(this, currentNode, "RequiredLabel"))
                m_controls.push_back(&m_requiredLabel);
        }
        {
            if (m_request1Label.setupControl(this, currentNode, "Request1Label"))
                m_controls.push_back(&m_request1Label);
        }
        {
            if (m_request2Label.setupControl(this, currentNode, "Request2Label"))
                m_controls.push_back(&m_request2Label);
        }
        {
            m_hotBar.setupControl(this, currentNode, std::string("HotBar"));
            m_controls.push_back(&m_hotBar);
        }
        {
            m_inventory.setupControl(this, currentNode, std::string("Inventory"));
            m_controls.push_back(&m_inventory);
        }
        {
            m_inventoryLabel.setupControl(this, currentNode, std::string("InventoryLabel"));
            m_controls.push_back(&m_inventoryLabel);
        }
        currentNode = parentNode;
    }
    return 1;
}

std::wstring UIScene_TradingMenu::getMoviePath()
{
    return gApp.GetLocalPlayerCount() > 1 ? std::wstring(L"TradingMenuSplit") : std::wstring(L"TradingMenu");
}

void UIScene_TradingMenu::updateTooltips()
{
    gConsoleUIController.SetTooltips(m_playerId, (int)IDS_TOOLTIPS_TRADE, IDS_TOOLTIPS_BACK, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, false, false);
}

void UIScene_TradingMenu::handleDestroy()
{
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft->mGameModes[m_playerId] != 0)
    {
        Minecraft* current = Minecraft::GetInstance();
        if (current->mGameModes[m_playerId] != 0)
        {
            Tutorial* tutorial = current->mGameModes[m_playerId]->getTutorial();
            if (tutorial != 0)
                tutorial->changeTutorialState(m_previousTutorialState, 0);
        }
    }

    if (minecraft->mLocalPlayers[m_playerId])
        minecraft->mLocalPlayers[m_playerId].get()->closeContainer();

    gConsoleUIController.OverrideSFX(m_playerId, 0, false);
    gConsoleUIController.OverrideSFX(m_playerId, 0x14, false);
    gConsoleUIController.OverrideSFX(m_playerId, 0xb, false);
    gConsoleUIController.OverrideSFX(m_playerId, 10, false);
    gConsoleUIController.OverrideSFX(m_playerId, 7, false);
    gConsoleUIController.OverrideSFX(m_playerId, 6, false);
    gConsoleUIController.OverrideSFX(m_playerId, 4, false);
    gConsoleUIController.OverrideSFX(m_playerId, 5, false);
}

std::vector<HtmlString>* UIScene_TradingMenu::GetItemDescription(not_null_ptr<ItemInstance> item)
{
    std::vector<HtmlString>* lines = item.get()->getTooltipLines(boost::shared_ptr<Player>(), false);
    if (!lines->empty())
    {
        ItemInstance* instance = item.get();
        lines->at(0).mColour = instance->getRarity()->colour;
    }
    return lines;
}

bool UIScene_TradingMenu::canMake(MerchantRecipe* recipe)
{
    if (!recipe || recipe->isDeprecated() || recipe->getTradeLevel() > m_tradeLevel)
        return false;

    const auto buyA = recipe->getBuyAItem();
    const auto buyB = recipe->getBuyBItem();
    const auto& inventory = Minecraft::GetInstance()->mLocalPlayers[m_playerId]->mInventory;
    const int requiredA = buyA->getCount() + (buyA->sameItem(buyB) ? buyB->getCount() : 0);
    return !buyA->isEmpty() && inventory->countMatches(buyA) >= requiredA
        && (buyB->isEmpty() || inventory->countMatches(buyB) >= buyB->getCount());
}

void UIScene_TradingMenu::setTradeRedBox(int trade, bool show)
{
    m_tradingBar.showSlotRedBox(trade, show);
}

void UIScene_TradingMenu::setTitle(const std::wstring& title)
{
    m_villagerText.setLabel(UIString(title), false, false);
}

void UIScene_TradingMenu::setOfferDescription(std::vector<HtmlString>* description)
{
    std::wstring composed = HtmlString::Compose(description);
    fuiRenderNode* root = m_movie->getRootNode();
    static_cast<FJ_TradingMenu*>(root->getFuiNode())->SetOfferDescription(composed);
}

void UIScene_TradingMenu::setRequest1Name(const std::wstring& name)
{
    m_request1Label.setLabel(UIString(name), false, false);
}

void UIScene_TradingMenu::setRequest2Name(const std::wstring& name)
{
    m_request2Label.setLabel(UIString(name), false, false);
}

void UIScene_TradingMenu::setRequest1RedBox(bool show)
{
    m_request1.showSlotRedBox(0, show);
}

void UIScene_TradingMenu::setRequest2RedBox(bool show)
{
    m_request2.showSlotRedBox(0, show);
}

void UIScene_TradingMenu::updateDisplay()
{
    auto player = Minecraft::GetInstance()->mLocalPlayers[m_playerId];
    auto* offers = m_merchant->getOffers(player);
    auto* trading = static_cast<FJ_TradingMenu*>(m_movie->getRootNode()->getFuiNode());
    m_tradeLevel = offers ? offers->getTradeLevel() : 0;
    m_trades.clear();

    if (offers)
    {
        for (int index = 0; index < offers->size(); ++index)
        {
            auto* recipe = offers->at(index);
            if (m_tradeLevel > 0 || !recipe->isDeprecated())
                m_trades.emplace_back(recipe, index);
        }
    }

    constexpr int visibleSlots = 7;
    const int last = std::max(0, static_cast<int>(m_trades.size()) - 1);
    const int selected = std::min(m_tradeOffset + m_tradeRow, last);
    m_tradeOffset = Mth::clamp(m_tradeOffset, 0, std::max(0, last + 1 - visibleSlots));
    m_tradeRow = selected - m_tradeOffset;
    trading->SetSelectorSlot(m_tradeRow);

    for (int slot = 0; slot < visibleSlots; ++slot)
    {
        const int index = m_tradeOffset + slot;
        auto* recipe = index < m_trades.size() ? m_trades[index].first : nullptr;
        const bool locked = recipe && offers->isLocked(recipe);
        const bool soldOut = recipe && recipe->isDeprecated();
        trading->SetOfferState(slot, locked, soldOut);
        setTradeRedBox(slot, recipe && !locked && !soldOut && !canMake(recipe));
    }

    const auto levelName = [](int level) {
        constexpr unsigned int levelFormat = 0xF77D295E; // TU80: Level %s
        return replaceAll(std::wstring(L10N::GetString(levelFormat)), std::wstring(L"%s"), std::to_wstring(level));
    };
    const std::wstring merchantName = m_merchant->getDisplayName();
    setTitle(m_tradeLevel ? levelName(m_tradeLevel) + L" - " + merchantName : merchantName);
    std::vector<HtmlString> emptyDescription;
    setOfferDescription(&emptyDescription);
    setRequest1Name(L"");
    setRequest2Name(L"");
    setRequest1RedBox(false);
    setRequest2RedBox(false);

    std::wstring tradeLabel;
    float progress = 0.0f;
    float preview = 0.0f;
    bool locked = false;
    bool soldOut = false;
    bool canTrade = false;
    if (!m_trades.empty())
    {
        auto* recipe = m_trades[selected].first;
        locked = offers->isLocked(recipe);
        soldOut = recipe->isDeprecated();
        canTrade = canMake(recipe);
        const auto buyA = recipe->getBuyAItem();
        const auto buyB = recipe->getBuyBItem();
        setRequest1Name(buyA->isEmpty() ? L"" : buyA->getHoverName());
        setRequest2Name(buyB->isEmpty() ? L"" : buyB->getHoverName());
        setRequest1RedBox(!buyA->isEmpty() && player->mInventory->countMatches(buyA) < buyA->getCount());
        setRequest2RedBox(!buyB->isEmpty() && player->mInventory->countMatches(buyB) < buyB->getCount());
        std::unique_ptr<std::vector<HtmlString>> description(GetItemDescription(recipe->getSellItem()));
        setOfferDescription(description.get());

        if (m_tradeLevel)
        {
            constexpr unsigned int ranks[] = {0x07960F18, 0x1536049B, 0xD279B4E2, 0xE293078E, 0x5A64E9BC};
            const int required = recipe->getTradeLevel() > 0 ? recipe->getTradeLevel() : m_tradeLevel;
            tradeLabel = levelName(required) + L"\n" + L10N::GetString(ranks[Mth::clamp(required, 1, 5) - 1]);

            const int start = MerchantRecipeList::experienceForLevel(m_tradeLevel);
            const int next = MerchantRecipeList::experienceForLevel(m_tradeLevel + 1);
            const int xp = offers->getTradeExperience() - start;
            const int gain = canTrade ? recipe->getTradeExperience() : 0;
            progress = next == start ? 1.0f : Mth::clamp(float(xp) / (next - start), 0.0f, 1.0f);
            preview = next == start ? 1.0f : Mth::clamp(float(xp + gain) / (next - start), progress, 1.0f);
        }

        if (!m_result.getFuiRenderNode())
        {
            std::wstring title = replaceAll(std::wstring(L10N::GetString(IDS_VILLAGER_OFFERS_ITEM)),
                                            std::wstring(L"{*VILLAGER_TYPE*}"), merchantName);
            setTitle(replaceAll(title, std::wstring(L"%s"), recipe->getSellItem()->getHoverName()));
        }
    }

    trading->SetTradeProgress(tradeLabel, progress, preview, locked);
    trading->ShowTradeUnavailable(soldOut && !locked);
    m_tradeFocusSet = offers != nullptr;
    gConsoleUIController.SetTooltips(m_playerId, canTrade ? static_cast<int>(IDS_TOOLTIPS_TRADE) : -1, IDS_TOOLTIPS_EXIT,
                                    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, false, false);
}

void UIScene_TradingMenu::handleReload()
{
    m_request1.addSlots(0x27, 1);
    m_request2.addSlots(0x28, 1);
    m_tradingBar.addSlots(0x29, 7);
    m_result.addSlot(TradeResultSlot);
    m_inventory.addSlots(3, 0x1b);
    m_hotBar.addSlots(0x1e, 9);
    updateDisplay();
    fuiRenderNode* root = m_movie->getRootNode();
    static_cast<FJ_TradingMenu*>(root->getFuiNode())->SetSelectorSlot(m_tradeRow);
}

void UIScene_TradingMenu::showScrollRightArrow(bool show)
{
    if (m_scrollRightShown != show)
    {
        fuiRenderNode* root = m_movie->getRootNode();
        static_cast<FJ_TradingMenu*>(root->getFuiNode())->ShowScrollRightArrow(show);
        m_scrollRightShown = show;
    }
}

void UIScene_TradingMenu::showScrollLeftArrow(bool show)
{
    if (m_scrollLeftShown != show)
    {
        fuiRenderNode* root = m_movie->getRootNode();
        static_cast<FJ_TradingMenu*>(root->getFuiNode())->ShowScrollLeftArrow(show);
        m_scrollLeftShown = show;
    }
}

void UIScene_TradingMenu::handleTick()
{
    Merchant* merchant = m_merchant.get();
    MerchantRecipeList* offers = merchant->getOffers(Minecraft::GetInstance()->mLocalPlayers[m_playerId]);

    if (offers != 0 && !m_tradeFocusSet)
        updateDisplay();

    const unsigned int tradeCount = static_cast<unsigned int>(m_trades.size());
    showScrollRightArrow(static_cast<unsigned int>(m_tradeOffset + 7) < tradeCount);
    showScrollLeftArrow(m_tradeOffset > 0);

    if (!m_merchant.get()->isAvailableToTrade())
    {
        gConsoleUIController.ShowTooltip(m_playerId, 2, false);
        gConsoleUIController.ShowTooltip(m_playerId, 1, false);
        gConsoleUIController.ShowTooltip(m_playerId, 0, false);
        gConsoleUIController.ShowTooltip(m_playerId, 7, false);
        gConsoleUIController.CloseUIScenes(m_playerId, false);
    }
}

void UIScene_TradingMenu::tick()
{
    UIScene::tick();
    handleTick();
}

void UIScene_TradingMenu::moveSelector(bool right)
{
    fuiRenderNode* root = m_movie->getRootNode();
    static_cast<FJ_TradingMenu*>(root->getFuiNode())->MoveSelector(right);
}

bool UIScene_TradingMenu::handleKeyDown(int iPad, int action, bool)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft->mGameModes[m_playerId] != 0)
    {
        Tutorial* tutorial = minecraft->mGameModes[m_playerId]->getTutorial();
        if (tutorial != 0)
        {
            tutorial->handleUIInput(action);
            if (gConsoleUIController.IsTutorialVisible(m_playerId) && !tutorial->popupPending)
            {
                GameModeConstraints* constraints = minecraft->mGameModes[m_playerId]->getGameModeConstraints();
                if (!constraints->isInputAllowed(action))
                    return false;
            }
        }
    }

    int selectedIndex;
    switch (action)
    {
    case EControllerActions_MenuB:
        gConsoleUIController.ShowTooltip(iPad, 2, false);
        gConsoleUIController.ShowTooltip(iPad, 1, false);
        gConsoleUIController.ShowTooltip(iPad, 0, false);
        gConsoleUIController.ShowTooltip(iPad, 7, false);
        gConsoleUIController.CloseUIScenes(iPad, false);
        return true;

    case EControllerActions_MenuA:
    {
        if (m_trades.empty())
            return true;

        selectedIndex = m_tradeRow + m_tradeOffset;
        if (static_cast<unsigned int>(selectedIndex) >= m_trades.size())
            return true;

        MerchantRecipe* recipe = m_trades.at(selectedIndex).first;
        if (!canMake(recipe))
            return true;

        const int originalIndex = m_trades.at(selectedIndex).second;
        auto player = minecraft->mLocalPlayers[m_playerId];
        player->mPacketListener->send(
            boost::shared_ptr<Packet>(new TradeItemPacket(m_merchantMenu->m_containerId, originalIndex)));

        return true;
    }

    case EControllerActions_MenuLeft:
        if (m_tradeRow == 0)
        {
            if (m_tradeOffset < 1)
                return true;
            --m_tradeOffset;
        }
        else
        {
            --m_tradeRow;
            moveSelector(false);
        }
        updateDisplay();
        break;

    case EControllerActions_MenuRight:
        if (static_cast<unsigned int>(m_tradeOffset + m_tradeRow + 1) >= m_trades.size())
            return true;
        if (m_tradeRow == 6)
        {
            ++m_tradeOffset;
        }
        else
        {
            ++m_tradeRow;
            moveSelector(true);
        }
        updateDisplay();
        break;

    default:
        return false;
    }

    selectedIndex = m_tradeRow + m_tradeOffset;
    if (m_trades.empty() || static_cast<unsigned int>(selectedIndex) >= m_trades.size())
        return true;

    const int originalIndex = m_trades.at(selectedIndex).second;
    m_merchantMenu->setSelectionHint(originalIndex);

    ByteArrayOutputStream byteStream;
    DataOutputStream dataStream(&byteStream);
    dataStream.writeInt(originalIndex);

    Minecraft::GetInstance()
        ->getConnection(m_playerId)
        ->send(boost::shared_ptr<Packet>(new ServerboundCustomPayloadPacket(
            ServerboundCustomPayloadPacket::TRADER_SELECTION_PACKET, byteStream.toByteArray())));

    return true;
}

void UIScene_TradingMenu::handleInput(int, int action, bool repeat, bool pressed, bool released, bool& handled)
{
    gConsoleUIController.AnimateKeyPress(m_playerId, action, repeat, pressed, released);
    if (pressed)
        handled = handleKeyDown(m_playerId, action, repeat);
}

void UIScene_TradingMenu::customDraw(const char* name, fuiRect* rect)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    if (!minecraft->mLocalPlayers[m_playerId])
        return;
    if (minecraft->mGameModes[m_playerId] == 0)
        return;

    not_null_ptr<ItemInstance> item = ItemInstance::EMPTY;
    int slotId = -1;
    if (std::sscanf(name, "slot_%d", &slotId) != 1 || slotId < 0 || slotId > TradeResultSlot)
        return;

    if (slotId < 0x27)
    {
        Slot* slot = m_merchantMenu->getSlot(slotId);
        item = slot->getItem();
    }
    else if (slotId < 0x29 || slotId == TradeResultSlot)
    {
        if (!m_trades.empty() && static_cast<unsigned int>(m_tradeRow + m_tradeOffset) < m_trades.size())
        {
            MerchantRecipe* recipe = m_trades.at(m_tradeRow + m_tradeOffset).first;
            if (slotId == TradeResultSlot)
                item = recipe->getSellItem();
            else
                item = slotId == 0x27 ? recipe->getBuyAItem() : recipe->getBuyBItem();
        }
    }
    else if (!m_trades.empty() && static_cast<unsigned int>(slotId - 0x29 + m_tradeOffset) < m_trades.size())
    {
        item = m_trades.at(slotId - 0x29 + m_tradeOffset).first->getSellItem();
    }

    if (!item.get()->isEmpty())
        customDrawSlotControlFui(rect, m_playerId, item, 1.0f, item.get()->getItem()->isFoil(item), true, false);
}

void UIScene_TradingMenu::handleInventoryUpdated(void*)
{
    updateDisplay();
}

void UIScene_TradingMenu::HandleMessage(EUIMessage message, void* data)
{
    if (message == EUIMessage_Unknown)
        handleInventoryUpdated(data);
}

boost::shared_ptr<Merchant> UIScene_TradingMenu::getMerchant()
{
    return m_merchant;
}

void UIScene_TradingMenu::setOffers(MerchantRecipeList* offers)
{
    m_trades.clear();
    m_merchant->overrideOffers(offers);
    m_merchantMenu->slotsChanged();
    updateDisplay();
}

int UIScene_TradingMenu::getSceneType()
{
    return EUIScene_TradingMenu;
}
