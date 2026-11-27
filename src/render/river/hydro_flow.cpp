#include "render/river/hydro_flow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hbg::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;

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

float length(Vec2 value) {
    return std::sqrt(dot(value, value));
}

Vec2 normalize(Vec2 value) {
    const float magnitude = length(value);
    return magnitude > 1.0e-8f ? scale(value, 1.0f / magnitude) : Vec2(0.0f, 0.0f);
}

float pointSegmentDistanceSq(Vec2 point, Vec2 start, Vec2 end) {
    const Vec2 segment = subtract(end, start);
    const float lengthSq = dot(segment, segment);
    if (lengthSq <= 1.0e-12f) {
        const Vec2 delta = subtract(point, start);
        return dot(delta, delta);
    }
    const float t = std::clamp(dot(subtract(point, start), segment) / lengthSq, 0.0f, 1.0f);
    const Vec2 closest = add(start, scale(segment, t));
    const Vec2 delta = subtract(point, closest);
    return dot(delta, delta);
}

float distanceToCurvePoints(const std::vector<Vec2>& points, Vec2 position) {
    if (points.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    if (points.size() == 1) {
        return length(subtract(position, points.front()));
    }
    float best = std::numeric_limits<float>::infinity();
    for (std::size_t index = 1; index < points.size(); ++index) {
        best = std::min(best, pointSegmentDistanceSq(position, points[index - 1], points[index]));
    }
    return std::sqrt(best);
}

std::vector<Vec2> offsetCurve(const std::vector<Vec2>& axis, float offset) {
    std::vector<Vec2> result;
    result.reserve(axis.size());
    for (std::size_t index = 0; index < axis.size(); ++index) {
        const Vec2 previous = axis[index > 0 ? index - 1 : index];
        const Vec2 next = axis[index + 1 < axis.size() ? index + 1 : index];
        Vec2 tangent = normalize(subtract(next, previous));
        if (dot(tangent, tangent) <= 1.0e-12f) {
            tangent = Vec2(0.0f, 1.0f);
        }
        const Vec2 normal(-tangent.y, tangent.x);
        result.push_back(add(axis[index], scale(normal, offset)));
    }
    return result;
}

RiverGraph makeGraph(std::vector<Vec2> axis, float width, float segments) {
    RiverGraph graph;
    graph.width = width;
    graph.axis = std::move(axis);
    if (graph.axis.size() < 2 || width <= 0.0f) {
        return graph;
    }
    const float halfWidth = width * 0.5f;
    graph.leftBank.points = offsetCurve(graph.axis, halfWidth);
    graph.leftBank.potential = 0.0f;
    graph.leftBank.width = width;
    graph.rightBank.points = offsetCurve(graph.axis, -halfWidth);
    graph.rightBank.potential = 1.0f;
    graph.rightBank.width = width;
    (void)segments;
    return graph;
}

float smoothFunc(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 6.0f * std::pow(t, 5.0f) - 15.0f * std::pow(t, 4.0f) + 10.0f * std::pow(t, 3.0f);
}

} // namespace

RiverGraph RiverGraph::straight(float length, float width, int segments) {
    const int count = std::max(segments, 1);
    std::vector<Vec2> axis;
    axis.reserve(static_cast<std::size_t>(count) + 1);
    for (int index = 0; index <= count; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(count);
        axis.emplace_back(0.0f, -length * 0.5f + length * t);
    }
    return makeGraph(std::move(axis), width, static_cast<float>(count));
}

RiverGraph RiverGraph::meander(float length, float width, float amplitude, float waves, int segments) {
    const int count = std::max(segments, 1);
    std::vector<Vec2> axis;
    axis.reserve(static_cast<std::size_t>(count) + 1);
    for (int index = 0; index <= count; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(count);
        const float y = -length * 0.5f + length * t;
        axis.emplace_back(amplitude * std::sin(2.0f * kPi * waves * t), y);
    }
    return makeGraph(std::move(axis), width, static_cast<float>(count));
}

RiverGraph RiverGraph::fromControlPoints(const std::vector<Vec2>& controlPoints, float width, int segments) {
    if (controlPoints.size() < 2 || width <= 0.0f) {
        return {};
    }

    const int count = std::max(segments, 1);
    const int pointCount = static_cast<int>(controlPoints.size());
    std::vector<Vec2> axis;
    axis.reserve(static_cast<std::size_t>(count) + 1);
    for (int index = 0; index <= count; ++index) {
        const float u = static_cast<float>(index) / static_cast<float>(count) *
                        static_cast<float>(pointCount - 1);
        const int segment = std::min(static_cast<int>(std::floor(u)), pointCount - 2);
        const float t = u - static_cast<float>(segment);
        const Vec2 p0 = controlPoints[static_cast<std::size_t>(std::max(segment - 1, 0))];
        const Vec2 p1 = controlPoints[static_cast<std::size_t>(segment)];
        const Vec2 p2 = controlPoints[static_cast<std::size_t>(segment + 1)];
        const Vec2 p3 = controlPoints[static_cast<std::size_t>(std::min(segment + 2, pointCount - 1))];

        const float t2 = t * t;
        const float t3 = t2 * t;
        const float b0 = -0.5f * t3 + t2 - 0.5f * t;
        const float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
        const float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
        const float b3 = 0.5f * t3 - 0.5f * t2;
        axis.emplace_back(b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                          b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y);
    }
    return makeGraph(std::move(axis), width, static_cast<float>(count));
}

HydroFlow::HydroFlow(RiverGraph graph, float slip, float searchRadiusFactor)
    : m_graph(std::move(graph)), m_slip(std::max(slip, 0.0f)), m_searchRadiusFactor(std::max(searchRadiusFactor, 0.01f)) {
    // HydroFlowTile stores potentials on a cache grid and uses
    // pot = max(1, tile size / cache size) for the finite difference.
    m_step = std::max(m_graph.width / 8.0f, 1.0f);
}

bool HydroFlow::inside(Vec2 position) const {
    if (m_graph.axis.size() < 2 || m_graph.width <= 0.0f) {
        return false;
    }
    return distanceToCurvePoints(m_graph.axis, position) <= m_graph.width * 0.5f;
}

float HydroFlow::distanceToCurve(const RiverCurve& curve, Vec2 position) const {
    return distanceToCurvePoints(curve.points, position);
}

bool HydroFlow::samplePotential(Vec2 position, float& potential) const {
    // HydroFlowTile::getPotential interpolates the bank potentials with a
    // smooth radial kernel and an inverse-distance exponent. Distances are
    // clamped to the maximum bank width, as in the original implementation.
    if (!inside(position) || m_graph.leftBank.points.empty() || m_graph.rightBank.points.empty()) {
        return false;
    }

    const float maxWidth = std::max(m_graph.leftBank.width, m_graph.rightBank.width);
    if (maxWidth <= 0.0f) {
        return false;
    }
    const float searchWidth = maxWidth * m_searchRadiusFactor;
    const float leftDistance =
        std::min(distanceToCurve(m_graph.leftBank, position), maxWidth);
    const float rightDistance =
        std::min(distanceToCurve(m_graph.rightBank, position), maxWidth);

    const float distances[2] = {leftDistance, rightDistance};
    const float potentials[2] = {m_graph.leftBank.potential, m_graph.rightBank.potential};
    float weights[2] = {0.0f, 0.0f};
    for (int bank = 0; bank < 2; ++bank) {
        const float kernel = smoothFunc(1.0f - distances[bank] / searchWidth);
        float product = 1.0f;
        for (int other = 0; other < 2; ++other) {
            if (other != bank) {
                product *= std::pow(std::max(distances[other], 1.0e-4f), m_slip);
            }
        }
        weights[bank] = product * kernel;
    }

    const float denominator = weights[0] + weights[1];
    if (denominator <= 1.0e-8f) {
        return false;
    }
    potential = (weights[0] * potentials[0] + weights[1] * potentials[1]) / denominator;
    return true;
}

bool HydroFlow::cachedPotential(long long cellX, long long cellY, float& potential) const {
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
                              static_cast<std::uint32_t>(cellY);
    const auto cached = m_potentialCache.find(key);
    if (cached != m_potentialCache.end()) {
        if (!std::isfinite(cached->second)) {
            return false;
        }
        potential = cached->second;
        return true;
    }

    const Vec2 corner(static_cast<float>(cellX) * m_step, static_cast<float>(cellY) * m_step);
    float value = 0.0f;
    const bool valid = samplePotential(corner, value);
    m_potentialCache.emplace(key, valid ? value : std::numeric_limits<float>::quiet_NaN());
    if (!valid) {
        return false;
    }
    potential = value;
    return true;
}

HydroFlowSample HydroFlow::sample(Vec2 position) const {
    HydroFlowSample result;
    if (!inside(position)) {
        return result;
    }

    // HydroFlowTile::getFourPotentials uses the lower-left corner of the
    // cache cell containing the query, then getVelocity differentiates the
    // four cached corner potentials. The velocity is therefore constant
    // inside one cache cell, as in the original tile.
    const long long cellX = static_cast<long long>(std::floor(position.x / m_step));
    const long long cellY = static_cast<long long>(std::floor(position.y / m_step));
    float p0 = 0.0f;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float p3 = 0.0f;
    if (!cachedPotential(cellX, cellY, p0) ||
        !cachedPotential(cellX + 1, cellY, p1) ||
        !cachedPotential(cellX, cellY + 1, p2) ||
        !cachedPotential(cellX + 1, cellY + 1, p3)) {
        return result;
    }

    const float inverse = 1.0f / (4.0f * m_step);
    result.velocity = Vec2(-(p2 - p0 + p3 - p1) * inverse, (p3 - p2 + p1 - p0) * inverse);
    result.potential = p0;
    result.inside = std::isfinite(result.velocity.x) && std::isfinite(result.velocity.y) &&
                    std::isfinite(result.potential);
    if (!result.inside) {
        result.velocity = Vec2(0.0f, 0.0f);
        result.potential = 0.0f;
    }
    return result;
}

} // namespace hbg::render
