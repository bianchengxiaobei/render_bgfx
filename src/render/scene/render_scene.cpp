#include "render/scene/render_scene.h"

namespace hbg::render {

void RenderScene::recomputeBounds() {
    bounds = {};
    for (const RenderObject& object : objects) {
        if (object.visible) {
            bounds.expand(object.worldBounds);
        }
    }
}

} // namespace hbg::render
