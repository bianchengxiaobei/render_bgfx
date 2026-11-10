#include "render/scene/render_scene.h"

namespace hbg::render {

void RenderScene::recomputeBounds() {
    bounds = {};
    for (const RenderObject& object : objects) {
        bounds.expand(object.worldBounds);
    }
}

} // namespace hbg::render
