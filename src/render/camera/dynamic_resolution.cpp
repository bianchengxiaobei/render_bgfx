#include "render/camera/dynamic_resolution.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr float kMinScale = 0.1f;
constexpr float kMaxScale = 1.0f;
constexpr float kMinFrameMs = 1.0e-3f;

float sanitizeScale(float scale) {
    return std::clamp(scale, kMinScale, kMaxScale);
}

} // namespace

void DynamicResolutionController::configure(const DynamicResolutionSettings& settings) {
    m_settings = settings;
    m_settings.minScale = sanitizeScale(m_settings.minScale);
    m_settings.maxScale = sanitizeScale(m_settings.maxScale);
    if (m_settings.minScale > m_settings.maxScale) {
        std::swap(m_settings.minScale, m_settings.maxScale);
    }
    if (!std::isfinite(m_settings.targetFrameMs) || m_settings.targetFrameMs < kMinFrameMs) {
        m_settings.targetFrameMs = 1000.0f / 60.0f;
    }
    if (!std::isfinite(m_settings.scaleStep) || m_settings.scaleStep <= 0.0f) {
        m_settings.scaleStep = 0.05f;
    }
    m_settings.scaleStep = std::clamp(m_settings.scaleStep, 0.01f, 0.5f);
    m_settings.evaluationFrames = std::max(m_settings.evaluationFrames, 1u);
    if (!std::isfinite(m_settings.increaseThreshold) || m_settings.increaseThreshold <= 0.0f) {
        m_settings.increaseThreshold = 0.90f;
    }
    if (!std::isfinite(m_settings.decreaseThreshold) ||
        m_settings.decreaseThreshold < m_settings.increaseThreshold) {
        m_settings.decreaseThreshold = std::max(1.10f, m_settings.increaseThreshold + 0.01f);
    }
    reset(m_currentScale);
}

void DynamicResolutionController::reset(float scale) {
    m_currentScale = std::clamp(sanitizeScale(scale), m_settings.minScale, m_settings.maxScale);
    m_accumulatedFrameMs = 0.0f;
    m_averageFrameMs = 0.0f;
    m_sampleCount = 0;
    m_adjustmentCount = 0;
}

std::optional<float> DynamicResolutionController::update(double frameSeconds) {
    if (!m_settings.enabled) {
        m_accumulatedFrameMs = 0.0f;
        m_averageFrameMs = 0.0f;
        m_sampleCount = 0;
        return std::nullopt;
    }
    if (!std::isfinite(frameSeconds) || frameSeconds <= 0.0) {
        return std::nullopt;
    }

    m_accumulatedFrameMs += static_cast<float>(frameSeconds * 1000.0);
    ++m_sampleCount;
    if (m_sampleCount < m_settings.evaluationFrames) {
        return std::nullopt;
    }

    m_averageFrameMs = m_accumulatedFrameMs / static_cast<float>(m_sampleCount);
    m_accumulatedFrameMs = 0.0f;
    m_sampleCount = 0;

    const float upperFrameMs = m_settings.targetFrameMs * m_settings.decreaseThreshold;
    const float lowerFrameMs = m_settings.targetFrameMs * m_settings.increaseThreshold;
    float nextScale = m_currentScale;
    if (m_averageFrameMs > upperFrameMs) {
        nextScale = std::max(m_settings.minScale, m_currentScale - m_settings.scaleStep);
    } else if (m_averageFrameMs < lowerFrameMs) {
        nextScale = std::min(m_settings.maxScale, m_currentScale + m_settings.scaleStep);
    }
    nextScale = sanitizeScale(nextScale);
    if (std::fabs(nextScale - m_currentScale) < 1.0e-5f) {
        return std::nullopt;
    }

    m_currentScale = nextScale;
    ++m_adjustmentCount;
    return m_currentScale;
}

} // namespace hbg::render
