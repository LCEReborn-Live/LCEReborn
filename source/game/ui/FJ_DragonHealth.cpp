#include "game/ui/FJ_DragonHealth.hpp"

#include <string>

#include "game/ui/FJ_FuiNode.hpp"
#include "game/ui/FJ_Label.hpp"
#include "game/core/BossEvent.hpp"
#include "game/ui/fuiRenderNodeCodeGenRect.hpp"

FJ_DragonHealth::FJ_DragonHealth(fuiRenderNode* renderNode) : FJ_ProgressBar(renderNode)
{
    GetBarFiller();
    initFiller();
    m_pLabel = getChildByName("DragonHealthLabel")->asFJ_Label();
    m_pLabel->SetLabelAlignment(0);
    m_pLabel->SetTextAlignment(FJ_Label::eTextAlignment_Center);
}

void FJ_DragonHealth::Init(const std::wstring& label)
{
    m_pLabel->Init(label);
}

void FJ_DragonHealth::SetLabel(const std::wstring& label)
{
    m_pLabel->SetLabel(label);
}

void FJ_DragonHealth::SetStyle(int color, int overlay)
{
    m_pFiller->setColorMultiplier(1, color == BossEvent::RED ? .25f : 1, color == BossEvent::RED ? .25f : 1);
    if (overlay == mOverlay) return;
    for (auto* notch : mNotches) removeChild(notch, true);
    mNotches.clear(); mOverlay = overlay;
    const int divisions = overlay == BossEvent::NOTCHED_6 ? 6 : overlay == BossEvent::NOTCHED_10 ? 10
        : overlay == BossEvent::NOTCHED_12 ? 12 : overlay == BossEvent::NOTCHED_20 ? 20 : 0;
    for (int i = 1; i < divisions; ++i)
    {
        fuiRect rect;
        rect.xMin = m_fillerX + m_fillerWidth * i / divisions;
        rect.xMax = rect.xMin + 1;
        rect.yMin = m_fillerY; rect.yMax = rect.yMin + m_fillerHeight;
        auto* notch = new fuiRenderNodeCodeGenRect(m_pRenderNode, &rect, "BossBarNotch");
        notch->setSolidColor(0, 0, 0, .8f);
        addChild(notch); mNotches.push_back(notch);
    }
}
