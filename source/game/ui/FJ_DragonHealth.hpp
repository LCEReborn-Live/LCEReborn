#ifndef GAME_UI_FJ_DRAGONHEALTH_HPP
#define GAME_UI_FJ_DRAGONHEALTH_HPP

#include <string>
#include <vector>

#include "game/ui/FJ_ProgressBar.hpp"

class FJ_Label;
class fuiRenderNode;

class FJ_DragonHealth : public FJ_ProgressBar
{
public:
    explicit FJ_DragonHealth(fuiRenderNode* renderNode);
    virtual ~FJ_DragonHealth() {}
    virtual void Init(const std::wstring& label);
    virtual void SetLabel(const std::wstring& label);
    void SetStyle(int color, int overlay);

private:
    FJ_Label* m_pLabel;
    int mOverlay = -1;
    std::vector<fuiRenderNode*> mNotches;
};

#endif
