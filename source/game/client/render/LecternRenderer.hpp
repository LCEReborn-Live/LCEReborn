#ifndef GAME_CLIENT_RENDER_LECTERNRENDERER_HPP
#define GAME_CLIENT_RENDER_LECTERNRENDERER_HPP

#include "game/client/render/BlockEntityRenderer.hpp"
#include "game/client/render/Model.hpp"
#include "game/client/render/ModelPart.hpp"

class LecternRenderer : public BlockEntityRenderer
{
public:
    LecternRenderer();
    void render(boost::shared_ptr<BlockEntity>, double, double, double, float, int, bool, float, bool) override;

private:
    Model mModel;
    ModelPart mSpine;
    ModelPart mLeft;
    ModelPart mRight;
};

#endif
