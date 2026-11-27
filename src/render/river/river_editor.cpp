#include "render/river/river_editor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace hbg::render {
namespace {

Vec2 subtract(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

Vec2 add(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

Vec2 scale(Vec2 value, float factor) {
    return Vec2(value.x * factor, value.y * factor);
}

float dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

float lengthSq(Vec2 value) {
    return dot(value, value);
}

float pointSegmentDistanceSq(Vec2 point, Vec2 start, Vec2 end) {
    const Vec2 segment = subtract(end, start);
    const float segmentLengthSq = dot(segment, segment);
    if (segmentLengthSq <= 1.0e-12f) {
        return lengthSq(subtract(point, start));
    }
    const float t = std::clamp(dot(subtract(point, start), segment) / segmentLengthSq, 0.0f, 1.0f);
    return lengthSq(subtract(point, add(start, scale(segment, t))));
}

Vec4 transform(const Mat4& matrix, const Vec4& value) {
    return Vec4(matrix.values[0] * value.x + matrix.values[4] * value.y + matrix.values[8] * value.z +
                    matrix.values[12] * value.w,
                matrix.values[1] * value.x + matrix.values[5] * value.y + matrix.values[9] * value.z +
                    matrix.values[13] * value.w,
                matrix.values[2] * value.x + matrix.values[6] * value.y + matrix.values[10] * value.z +
                    matrix.values[14] * value.w,
                matrix.values[3] * value.x + matrix.values[7] * value.y + matrix.values[11] * value.z +
                    matrix.values[15] * value.w);
}

Vec3 divideByW(const Vec4& value) {
    if (std::abs(value.w) <= 1.0e-8f) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const float inverseW = 1.0f / value.w;
    return Vec3(value.x * inverseW, value.y * inverseW, value.z * inverseW);
}

} // namespace

bool RiverEditor::init(const RiverGraph& graph, const RiverMeshSettings& mesh, float baseHeight) {
    if (graph.axis.size() < 2 || graph.width <= 0.0f) {
        m_valid = false;
        return false;
    }

    m_graph = graph;
    m_initialGraph = graph;
    m_mesh = mesh;
    m_baseHeight = std::isfinite(baseHeight) ? baseHeight : mesh.baseHeight;
    m_riverWidth = graph.width;
    m_leftPotential = graph.leftBank.potential;
    m_rightPotential = graph.rightBank.potential;

    const std::size_t controlPointCount = std::min<std::size_t>(9, graph.axis.size());
    m_controlPoints.clear();
    m_controlPoints.reserve(controlPointCount);
    for (std::size_t index = 0; index < controlPointCount; ++index) {
        const float t =
            controlPointCount > 1 ? static_cast<float>(index) / static_cast<float>(controlPointCount - 1) : 0.0f;
        m_controlPoints.push_back(sampleAxis(t));
    }
    m_initialState.controlPoints = m_controlPoints;
    m_initialState.width = m_riverWidth;
    m_initialState.leftPotential = m_leftPotential;
    m_initialState.rightPotential = m_rightPotential;
    m_undo.clear();
    m_selected = -1;
    m_hovered = -1;
    m_dragging = -1;
    m_prevLeftDown = false;
    m_prevInsert = false;
    m_prevDelete = false;
    m_prevReset = false;
    m_prevUndo = false;
    m_dirty = false;
    m_active = true;
    m_valid = true;
    return true;
}

bool RiverEditor::update(const RiverEditorInput& input, const Mat4& viewProj, const Mat4& invViewProj,
                         uint16_t viewportWidth, uint16_t viewportHeight) {
    m_viewProj = viewProj;
    m_invViewProj = invViewProj;
    m_viewportWidth = viewportWidth;
    m_viewportHeight = viewportHeight;

    if (!m_valid || !m_active || viewportWidth == 0 || viewportHeight == 0) {
        m_dragging = -1;
        m_prevLeftDown = input.leftDown;
        m_prevInsert = input.insertPressed;
        m_prevDelete = input.deletePressed;
        m_prevReset = input.resetPressed;
        m_prevUndo = input.undoPressed;
        return false;
    }

    updateHover(input);
    const bool insertPressed = input.insertPressed && !m_prevInsert;
    const bool deletePressed = input.deletePressed && !m_prevDelete;
    const bool resetPressed = input.resetPressed && !m_prevReset;
    const bool undoPressed = input.undoPressed && !m_prevUndo;
    const bool mouseInView = !input.mouseCaptured && input.mouseX >= 0.0f && input.mouseY >= 0.0f &&
                             input.mouseX < static_cast<float>(m_viewportWidth) &&
                             input.mouseY < static_cast<float>(m_viewportHeight);
    bool changed = false;

    if (!input.mouseCaptured) {
        if (insertPressed) {
            changed = addPointAtScreen(input.mouseX, input.mouseY) || changed;
        }
        if (deletePressed) {
            changed = removeSelected() || changed;
        }
        if (resetPressed) {
            changed = reset() || changed;
        }
        if (undoPressed) {
            changed = undo() || changed;
        }
    }

    if (mouseInView && input.leftDown && !m_prevLeftDown) {
        m_selected = m_hovered;
        if (m_selected >= 0) {
            pushUndo();
            m_dragging = m_selected;
        }
    }

    if (m_dragging >= 0 && input.leftDown && mouseInView) {
        Vec2 world;
        if (screenToWorld(input.mouseX, input.mouseY, m_invViewProj, m_viewportWidth, m_viewportHeight, world)) {
            if (setSelectedPointWorld(world)) {
                rebuildGraph();
                m_dirty = true;
                changed = true;
            }
        }
    }

    if (!input.leftDown || input.mouseCaptured) {
        m_dragging = -1;
    }

    m_prevLeftDown = input.leftDown;
    m_prevInsert = input.insertPressed;
    m_prevDelete = input.deletePressed;
    m_prevReset = input.resetPressed;
    m_prevUndo = input.undoPressed;
    return changed;
}

bool RiverEditor::consumeDirty() {
    const bool dirty = m_dirty;
    m_dirty = false;
    return dirty;
}

bool RiverEditor::setWidth(float width) {
    const float clamped = std::clamp(width, 4.0f, 400.0f);
    if (!m_valid || std::abs(clamped - m_riverWidth) <= 1.0e-4f) {
        return false;
    }
    pushUndo();
    m_riverWidth = clamped;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::setPotentials(float left, float right) {
    const float clampedLeft = std::clamp(left, -1.0f, 1.0f);
    const float clampedRight = std::clamp(right, -1.0f, 1.0f);
    if (!m_valid ||
        (std::abs(clampedLeft - m_leftPotential) <= 1.0e-4f && std::abs(clampedRight - m_rightPotential) <= 1.0e-4f)) {
        return false;
    }
    pushUndo();
    m_leftPotential = clampedLeft;
    m_rightPotential = clampedRight;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::addPointAtScreen(float screenX, float screenY) {
    if (!m_valid) {
        return false;
    }
    const int segment = pickSegment(screenX, screenY);
    if (segment < 0) {
        return false;
    }
    Vec2 world;
    if (!screenToWorld(screenX, screenY, m_invViewProj, m_viewportWidth, m_viewportHeight, world)) {
        return false;
    }
    pushUndo();
    m_controlPoints.insert(m_controlPoints.begin() + segment + 1, world);
    m_selected = segment + 1;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::removeSelected() {
    if (!m_valid || m_selected < 0 || m_selected >= static_cast<int>(m_controlPoints.size()) ||
        m_controlPoints.size() <= 2) {
        return false;
    }
    pushUndo();
    m_controlPoints.erase(m_controlPoints.begin() + m_selected);
    m_selected = std::min(m_selected, static_cast<int>(m_controlPoints.size()) - 1);
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::reset() {
    if (!m_valid) {
        return false;
    }
    pushUndo();
    m_controlPoints = m_initialState.controlPoints;
    m_riverWidth = m_initialState.width;
    m_leftPotential = m_initialState.leftPotential;
    m_rightPotential = m_initialState.rightPotential;
    m_selected = -1;
    m_dragging = -1;
    m_graph = m_initialGraph;
    m_dirty = true;
    return true;
}

bool RiverEditor::undo() {
    if (!m_valid || m_undo.empty()) {
        return false;
    }
    const State state = m_undo.back();
    m_undo.pop_back();
    m_controlPoints = state.controlPoints;
    m_riverWidth = state.width;
    m_leftPotential = state.leftPotential;
    m_rightPotential = state.rightPotential;
    m_selected = -1;
    m_dragging = -1;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::applyDemoEdit() {
    if (!m_valid || m_controlPoints.size() < 4) {
        return false;
    }
    pushUndo();
    m_controlPoints[3].x += 16.0f;
    m_riverWidth = std::clamp(m_riverWidth * 1.1f, 4.0f, 400.0f);
    m_selected = 3;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::save(const std::string& path, std::string* error) const {
    if (!m_valid) {
        if (error != nullptr) {
            *error = "river editor is not initialized";
        }
        return false;
    }

    nlohmann::json root;
    root["version"] = 1;
    root["controlPoints"] = nlohmann::json::array();
    for (const Vec2& point : m_controlPoints) {
        root["controlPoints"].push_back({point.x, point.y});
    }
    root["width"] = m_riverWidth;
    root["leftPotential"] = m_leftPotential;
    root["rightPotential"] = m_rightPotential;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open river edit file for writing: " + path;
        }
        return false;
    }
    const std::string text = root.dump(2);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    if (!file) {
        if (error != nullptr) {
            *error = "failed to write river edit file: " + path;
        }
        return false;
    }
    return true;
}

bool RiverEditor::load(const std::string& path, std::string* error) {
    if (!m_valid) {
        if (error != nullptr) {
            *error = "river editor is not initialized";
        }
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open river edit file: " + path;
        }
        return false;
    }

    std::vector<Vec2> controlPoints;
    float width = m_riverWidth;
    float leftPotential = m_leftPotential;
    float rightPotential = m_rightPotential;
    try {
        nlohmann::json root;
        file >> root;
        if (!root.is_object()) {
            throw std::runtime_error("root must be an object");
        }
        const auto points = root.find("controlPoints");
        if (points == root.end() || !points->is_array() || points->size() < 2 || points->size() > 4096) {
            throw std::runtime_error("controlPoints must contain between 2 and 4096 points");
        }
        controlPoints.reserve(points->size());
        for (const nlohmann::json& point : *points) {
            if (!point.is_array() || point.size() != 2) {
                throw std::runtime_error("each control point must be [x, y]");
            }
            const float x = point[0].get<float>();
            const float y = point[1].get<float>();
            if (!std::isfinite(x) || !std::isfinite(y)) {
                throw std::runtime_error("control point coordinates must be finite");
            }
            controlPoints.emplace_back(x, y);
        }
        width = root.value("width", width);
        leftPotential = root.value("leftPotential", leftPotential);
        rightPotential = root.value("rightPotential", rightPotential);
        if (!std::isfinite(width) || width <= 0.0f || !std::isfinite(leftPotential) || !std::isfinite(rightPotential)) {
            throw std::runtime_error("width and potentials must be finite");
        }
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("invalid river edit file: ") + exception.what();
        }
        return false;
    }

    pushUndo();
    m_controlPoints = std::move(controlPoints);
    m_riverWidth = std::clamp(width, 4.0f, 400.0f);
    m_leftPotential = std::clamp(leftPotential, -1.0f, 1.0f);
    m_rightPotential = std::clamp(rightPotential, -1.0f, 1.0f);
    m_selected = -1;
    m_dragging = -1;
    rebuildGraph();
    m_dirty = true;
    return true;
}

bool RiverEditor::projectToScreen(Vec2 graphPosition, const Mat4& viewProj, uint16_t viewportWidth,
                                  uint16_t viewportHeight, Vec2& screen) const {
    if (viewportWidth == 0 || viewportHeight == 0) {
        return false;
    }
    const Vec4 clip = transform(viewProj, Vec4(graphPosition.x, m_baseHeight, graphPosition.y, 1.0f));
    if (clip.w <= 1.0e-6f) {
        return false;
    }
    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    screen = Vec2((ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth),
                  (0.5f - ndcY * 0.5f) * static_cast<float>(viewportHeight));
    return true;
}

bool RiverEditor::screenToWorld(float screenX, float screenY, const Mat4& invViewProj, uint16_t viewportWidth,
                                uint16_t viewportHeight, Vec2& graphPosition) const {
    if (viewportWidth == 0 || viewportHeight == 0) {
        return false;
    }
    const float ndcX = (screenX / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (screenY / static_cast<float>(viewportHeight)) * 2.0f;
    const Vec4 near = transform(invViewProj, Vec4(ndcX, ndcY, 0.0f, 1.0f));
    const Vec4 far = transform(invViewProj, Vec4(ndcX, ndcY, 1.0f, 1.0f));
    const Vec3 nearWorld = divideByW(near);
    const Vec3 farWorld = divideByW(far);
    const Vec3 direction = farWorld - nearWorld;
    if (std::abs(direction.y) <= 1.0e-6f) {
        return false;
    }
    const float t = (m_baseHeight - nearWorld.y) / direction.y;
    if (t < 0.0f || t > 1.0e7f) {
        return false;
    }
    const Vec3 world = nearWorld + direction * t;
    graphPosition = Vec2(world.x, world.z);
    return std::isfinite(graphPosition.x) && std::isfinite(graphPosition.y);
}

void RiverEditor::rebuildGraph() {
    m_graph = RiverGraph::fromControlPoints(m_controlPoints, m_riverWidth, m_mesh.segments);
    if (m_graph.axis.size() < 2) {
        return;
    }
    m_graph.leftBank.potential = m_leftPotential;
    m_graph.rightBank.potential = m_rightPotential;
}

bool RiverEditor::pushUndo() {
    if (!m_valid) {
        return false;
    }
    if (m_undo.size() >= 32) {
        m_undo.erase(m_undo.begin());
    }
    State state;
    state.controlPoints = m_controlPoints;
    state.width = m_riverWidth;
    state.leftPotential = m_leftPotential;
    state.rightPotential = m_rightPotential;
    m_undo.push_back(std::move(state));
    return true;
}

bool RiverEditor::setSelectedPointWorld(Vec2 world) {
    if (m_dragging < 0 || m_dragging >= static_cast<int>(m_controlPoints.size())) {
        return false;
    }
    Vec2& point = m_controlPoints[static_cast<std::size_t>(m_dragging)];
    if (lengthSq(subtract(point, world)) <= 1.0e-6f) {
        return false;
    }
    point = world;
    return true;
}

bool RiverEditor::updateHover(const RiverEditorInput& input) {
    if (input.mouseCaptured) {
        m_hovered = -1;
        return false;
    }
    m_hovered = pickPoint(input.mouseX, input.mouseY);
    return m_hovered >= 0;
}

int RiverEditor::pickPoint(float screenX, float screenY) const {
    int best = -1;
    float bestDistanceSq = 12.0f * 12.0f;
    for (std::size_t index = 0; index < m_controlPoints.size(); ++index) {
        Vec2 screen;
        if (!projectToScreen(m_controlPoints[index], m_viewProj, m_viewportWidth, m_viewportHeight, screen)) {
            continue;
        }
        const float distanceSq = lengthSq(subtract(screen, Vec2(screenX, screenY)));
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = static_cast<int>(index);
        }
    }
    return best;
}

int RiverEditor::pickSegment(float screenX, float screenY) const {
    if (m_controlPoints.size() < 2) {
        return -1;
    }
    int best = -1;
    float bestDistanceSq = 18.0f * 18.0f;
    const Vec2 mouse(screenX, screenY);
    for (std::size_t index = 1; index < m_controlPoints.size(); ++index) {
        Vec2 start;
        Vec2 end;
        if (!projectToScreen(m_controlPoints[index - 1], m_viewProj, m_viewportWidth, m_viewportHeight, start) ||
            !projectToScreen(m_controlPoints[index], m_viewProj, m_viewportWidth, m_viewportHeight, end)) {
            continue;
        }
        const float distanceSq = pointSegmentDistanceSq(mouse, start, end);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = static_cast<int>(index - 1);
        }
    }
    return best;
}

Vec2 RiverEditor::sampleAxis(float t) const {
    if (m_graph.axis.empty()) {
        return Vec2(0.0f, 0.0f);
    }
    if (m_graph.axis.size() == 1) {
        return m_graph.axis.front();
    }
    const float position = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(m_graph.axis.size() - 1);
    const std::size_t index = std::min(static_cast<std::size_t>(std::floor(position)), m_graph.axis.size() - 2);
    const float local = position - static_cast<float>(index);
    return add(m_graph.axis[index], scale(subtract(m_graph.axis[index + 1], m_graph.axis[index]), local));
}

} // namespace hbg::render
