#pragma once

#include "render/river/river_spec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

struct RiverEditorInput {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool leftDown = false;
    bool insertPressed = false;
    bool deletePressed = false;
    bool resetPressed = false;
    bool undoPressed = false;
    bool mouseCaptured = false;
};

class RiverEditor {
  public:
    RiverEditor() = default;

    bool init(const RiverGraph& graph, const RiverMeshSettings& mesh, float baseHeight);
    bool valid() const { return m_valid; }

    bool active() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    bool update(const RiverEditorInput& input, const Mat4& viewProj, const Mat4& invViewProj, uint16_t viewportWidth,
                uint16_t viewportHeight);

    bool consumeDirty();
    const RiverGraph& graph() const { return m_graph; }
    const RiverMeshSettings& mesh() const { return m_mesh; }
    const std::vector<Vec2>& controlPoints() const { return m_controlPoints; }
    int selectedPoint() const { return m_selected; }
    int hoveredPoint() const { return m_hovered; }
    float width() const { return m_riverWidth; }
    float leftPotential() const { return m_leftPotential; }
    float rightPotential() const { return m_rightPotential; }

    bool setWidth(float width);
    bool setPotentials(float left, float right);
    bool addPointAtScreen(float screenX, float screenY);
    bool removeSelected();
    bool reset();
    bool undo();
    bool applyDemoEdit();
    bool save(const std::string& path, std::string* error) const;
    bool load(const std::string& path, std::string* error);

    bool projectToScreen(Vec2 graphPosition, const Mat4& viewProj, uint16_t viewportWidth, uint16_t viewportHeight,
                         Vec2& screen) const;
    bool screenToWorld(float screenX, float screenY, const Mat4& invViewProj, uint16_t viewportWidth,
                       uint16_t viewportHeight, Vec2& graphPosition) const;

  private:
    struct State {
        std::vector<Vec2> controlPoints;
        float width = 0.0f;
        float leftPotential = 0.0f;
        float rightPotential = 1.0f;
    };

    void rebuildGraph();
    bool pushUndo();
    bool setSelectedPointWorld(Vec2 world);
    int pickPoint(float screenX, float screenY) const;
    int pickSegment(float screenX, float screenY) const;
    bool updateHover(const RiverEditorInput& input);
    Vec2 sampleAxis(float t) const;

    RiverGraph m_graph;
    RiverGraph m_initialGraph;
    RiverMeshSettings m_mesh;
    std::vector<Vec2> m_controlPoints;
    std::vector<State> m_undo;
    State m_initialState;
    Mat4 m_viewProj = Mat4::identity();
    Mat4 m_invViewProj = Mat4::identity();
    uint16_t m_viewportWidth = 0;
    uint16_t m_viewportHeight = 0;
    float m_baseHeight = 0.0f;
    float m_riverWidth = 0.0f;
    float m_leftPotential = 0.0f;
    float m_rightPotential = 1.0f;
    int m_selected = -1;
    int m_hovered = -1;
    int m_dragging = -1;
    bool m_prevLeftDown = false;
    bool m_prevInsert = false;
    bool m_prevDelete = false;
    bool m_prevReset = false;
    bool m_prevUndo = false;
    bool m_dirty = false;
    bool m_active = true;
    bool m_valid = false;
};

} // namespace hbg::render
