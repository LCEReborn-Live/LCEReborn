#include "game/ui/FJ_TradingMenu.hpp"

#include "game/ui/FJ_Event.hpp"
#include "game/ui/FJ_FuiNode.hpp"
#include "game/ui/FJ_FuiNodeStage.hpp"
#include "game/ui/FJ_HtmlTextPanel.hpp"
#include "game/ui/FJ_Label.hpp"
#include "game/ui/FJ_ProgressBar.hpp"
#include "game/ui/FJ_ScrollArrowH.hpp"
#include "game/ui/FJ_SlotList.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/ui/fuiRenderNodeCodeGenRect.hpp"
#include "game/ui/fui.hpp"
#include "game/util/Math.hpp"

FJ_TradingMenu::FJ_TradingMenu(fuiRenderNode* renderNode)
    : FJ_Document(renderNode),
      m_scrollRight(0),
      m_scrollLeft(0),
      m_slotWidth(0),
      m_visibleSlots(0),
      m_selectorSlot(0)
{
}

FJ_Document* FJ_TradingMenu::Create(fuiRenderNode* renderNode)
{
    return new FJ_TradingMenu(renderNode);
}

unsigned int FJ_TradingMenu::getType()
{
    return 78;
}

void FJ_TradingMenu::init(FJ_Event* event)
{
    FJ_FuiNode* mainPanel = getChildByName("MainPanel");
    FJ_FuiNode* tradingBar = mainPanel->getChildByName("TradingBar");
    FJ_FuiNode* selector = mainPanel->getChildByName("TradingSelector");
    FJ_FuiNode* offer = mainPanel->getChildByName("OfferDescription");

    m_slotWidth = tradingBar->asFJ_SlotList()->m_slotSize;
    m_visibleSlots = Math::round(tradingBar->getWidth() / static_cast<float>(m_slotWidth));
    m_selectorSlot = 0;

    selector->setX(tradingBar->getX()
                   - static_cast<float>(Math::round((selector->getWidth() - static_cast<float>(m_slotWidth)) * 0.5f))
                   + static_cast<float>(m_selectorSlot * m_slotWidth));
    selector->setY(tradingBar->getY()
                   - static_cast<float>(Math::round((selector->getHeight() - static_cast<float>(m_slotWidth)) * 0.5f)));

    m_scrollRight = mainPanel->getChildByName("ScrollRight");
    m_scrollLeft = mainPanel->getChildByName("ScrollLeft");
    static_cast<FJ_ScrollArrowH*>(m_scrollRight)->ChangeState(2);
    static_cast<FJ_ScrollArrowH*>(m_scrollLeft)->ChangeState(0);

    if (FJ_FuiNode* experience = mainPanel->getChildByName("ExperienceBar"))
    {
        fuiRect arrowBounds;
        m_scrollRight->m_pRenderNode->getParentSpaceBounds(&arrowBounds);
        experience->setWidth(arrowBounds.xMax - experience->getX());
    }

    for (const char* name : {"TradingLevelLabel", "ExperienceBar", "PadLock", "RedCross"})
        if (FJ_FuiNode* node = mainPanel->getChildByName(name))
            node->setVisible(false);

    const auto makeSlotOverlays = [&](const char* symbol, std::vector<FJ_FuiNode*>& overlays, float maxWidth) {
        for (int slot = 0; slot < m_visibleSlots; ++slot)
        {
            auto* renderNode = gFui->createNodeFromSymbol(symbol, m_pRenderNode);
            if (!renderNode)
                break;
            auto* overlay = new FJ_FuiNode(renderNode);
            renderNode->setFuiNode(overlay);
            mainPanel->addChild(overlay);
            if (overlay->getWidth() > maxWidth)
            {
                const float scale = maxWidth / overlay->getWidth();
                overlay->setScaleX(overlay->getScaleX() * scale);
                overlay->setScaleY(overlay->getScaleY() * scale);
            }
            const fuiRect bounds = overlay->getRect(overlay);
            overlay->setX(tradingBar->getX() + slot * m_slotWidth + (m_slotWidth - overlay->getWidth()) * 0.5f
                          - bounds.xMin * overlay->getScaleX());
            overlay->setY(tradingBar->getY() + (m_slotWidth - overlay->getHeight()) * 0.5f
                          - bounds.yMin * overlay->getScaleY());
            overlay->setVisible(false);
            overlays.push_back(overlay);
        }
    };
    makeSlotOverlays("Padlock_Small", m_offerLocks, m_slotWidth);
    makeSlotOverlays("AnvilCrossLarge", m_offerCrosses, m_slotWidth * 0.75f);

    offer->setX(selector->getX() + selector->getWidth());
    offer->setY(selector->getY());

    fuiRect leftRect = m_scrollLeft->getRect(m_scrollLeft);
    if (m_scrollLeft->getX() + leftRect.xMin * m_scrollLeft->getScaleX() + m_scrollLeft->getWidth()
        > tradingBar->getX())
    {
        fuiRect rightRect = m_scrollRight->getRect(m_scrollRight);
        const int overlap = static_cast<int>((m_scrollRight->getX() + rightRect.xMin * m_scrollRight->getScaleX())
                                             - (tradingBar->getX() + tradingBar->getWidth()));
        fuiRect currentLeftRect = m_scrollLeft->getRect(m_scrollLeft);
        m_scrollLeft->setX(tradingBar->getX() - m_scrollLeft->getWidth()
                           - currentLeftRect.xMin * m_scrollLeft->getScaleX() - static_cast<float>(overlap));
    }
}

void FJ_TradingMenu::SetSafeZone(int top, int bottom, int left, int right)
{
    FJ_Document::SetSafeZone(top, bottom, left, right);
    if (getStage()->getStageHeight() == 360.0f || getStage()->getStageHeight() == 540.0f)
    {
        setX(-right);
        setY(top);
    }
}

void FJ_TradingMenu::ScrollRight()
{
    static_cast<FJ_ScrollArrowH*>(m_scrollRight)->ChangeState(3);
}

void FJ_TradingMenu::ScrollLeft()
{
    static_cast<FJ_ScrollArrowH*>(m_scrollLeft)->ChangeState(1);
}

void FJ_TradingMenu::PositionOfferDescription()
{
    FJ_FuiNode* offer = getChildByName("MainPanel.OfferDescription");
    FJ_FuiNode* selector = getChildByName("MainPanel.TradingSelector");
    if (m_selectorSlot < m_visibleSlots / 2)
    {
        offer->setX(Math::round(selector->getX() + selector->getWidth() * 1.1f));
    }
    else
    {
        offer->setX(Math::round(selector->getX() - offer->getWidth() - selector->getWidth() * 0.1f));
    }
    offer->setY(Math::round(selector->getY() + selector->getHeight() * 0.5f - offer->getHeight() * 0.5f));
}

void FJ_TradingMenu::MoveSelector(bool right)
{
    FJ_FuiNode* mainPanel = getChildByName("MainPanel");
    FJ_FuiNode* tradingBar = mainPanel->getChildByName("TradingBar");
    FJ_FuiNode* selector = mainPanel->getChildByName("TradingSelector");

    if (right)
    {
        if (m_selectorSlot < m_visibleSlots - 1)
        {
            ++m_selectorSlot;
        }
        else if (m_scrollRight->isVisible())
        {
            ScrollRight();
        }
    }
    else
    {
        if (m_selectorSlot > 0)
        {
            --m_selectorSlot;
        }
        else if (m_scrollLeft->isVisible())
        {
            ScrollLeft();
        }
    }

    selector->setX(tradingBar->getX()
                   - static_cast<float>(Math::round((selector->getWidth() - static_cast<float>(m_slotWidth)) * 0.5f))
                   + static_cast<float>(m_selectorSlot * m_slotWidth));
    selector->setY(tradingBar->getY()
                   - static_cast<float>(Math::round((selector->getHeight() - static_cast<float>(m_slotWidth)) * 0.5f)));
    PositionOfferDescription();
}

void FJ_TradingMenu::SetSelectorSlot(int slot)
{
    FJ_FuiNode* mainPanel = getChildByName("MainPanel");
    FJ_FuiNode* tradingBar = mainPanel->getChildByName("TradingBar");
    FJ_FuiNode* selector = mainPanel->getChildByName("TradingSelector");

    m_selectorSlot = slot;
    selector->setX(tradingBar->getX()
                   - static_cast<float>(Math::round((selector->getWidth() - static_cast<float>(m_slotWidth)) * 0.5f))
                   + static_cast<float>(m_selectorSlot * m_slotWidth));
    selector->setY(tradingBar->getY()
                   - static_cast<float>(Math::round((selector->getHeight() - static_cast<float>(m_slotWidth)) * 0.5f)));
    PositionOfferDescription();
}

void FJ_TradingMenu::SetOfferDescription(const std::wstring& description)
{
    getChildByName("MainPanel.OfferDescription")->asFJ_HtmlTextPanel()->SetLabel(description);
    PositionOfferDescription();
}

void FJ_TradingMenu::ShowScrollRightArrow(bool show)
{
    m_scrollRight->setVisible(show);
}

void FJ_TradingMenu::ShowTradeUnavailable(bool show)
{
    if (FJ_FuiNode* cross = getChildByName("MainPanel.RedCross"))
        cross->setVisible(show);
}

void FJ_TradingMenu::SetTradeProgress(const std::wstring& levelLabel, float progress, float preview, bool locked)
{
    const bool show = !levelLabel.empty();
    if (FJ_FuiNode* label = getChildByName("MainPanel.TradingLevelLabel"))
    {
        label->setVisible(show);
        if (show)
            if (auto* text = label->asFJ_Label())
                text->SetLabel(levelLabel);
    }

    if (FJ_FuiNode* experience = getChildByName("MainPanel.ExperienceBar"))
    {
        experience->setVisible(show);
        if (auto* bar = experience->asFJ_ProgressBar())
            bar->setProgress(progress);
        if (FJ_FuiNode* filler = experience->getChildByName("ExpBarFiller"))
        {
            if (!m_previewBar)
            {
                fuiRect rect {0.0f, filler->getWidth(), 0.0f, filler->getHeight()};
                m_previewBar = new fuiRenderNodeCodeGenRect(experience->m_pRenderNode, &rect, "TradeXpPreview");
                auto* node = new FJ_FuiNode(m_previewBar);
                m_previewBar->setFuiNode(node);
                m_previewBar->setSolidColor(0.8f, 0.94f, 0.64f, 0.7f);
                experience->addChild(node);
            }
            auto* node = m_previewBar->getFuiNode();
            node->setVisible(show && preview > progress);
            node->setX(filler->getX() + filler->getWidth() * progress);
            node->setY(filler->getY());
            node->setWidth(filler->getWidth() * (preview - progress));
        }
    }
    if (FJ_FuiNode* lock = getChildByName("MainPanel.PadLock"))
        lock->setVisible(locked);
}

void FJ_TradingMenu::SetOfferState(int slot, bool locked, bool soldOut)
{
    if (slot >= 0 && static_cast<size_t>(slot) < m_offerLocks.size())
        m_offerLocks[slot]->setVisible(locked);
    if (slot >= 0 && static_cast<size_t>(slot) < m_offerCrosses.size())
        m_offerCrosses[slot]->setVisible(soldOut && !locked);
}

void FJ_TradingMenu::ShowScrollLeftArrow(bool show)
{
    m_scrollLeft->setVisible(show);
}

int FJ_TradingMenu::getTotalTradingSlots()
{
    return m_visibleSlots;
}
