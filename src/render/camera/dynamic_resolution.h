#pragma once

#include <cstdint>
#include <optional>

namespace hbg::render {

struct DynamicResolutionSettings {
    bool enabled = false;
    float minScale = 0.5f;
    float maxScale = 1.0f;
    float targetFrameMs = 1000.0f / 60.0f;
    float scaleStep = 0.05f;
    uint32_t evaluationFrames = 30;
    float increaseThreshold = 0.90f;
    float decreaseThreshold = 1.10f;
};

class DynamicResolutionController {
  public:
    void configure(const DynamicResolutionSettings& settings);
    void reset(float scale = 1.0f);

    std::optional<float> update(double frameSeconds);

    const DynamicResolutionSettings& settings() const { return m_settings; }
    float currentScale() const { return m_currentScale; }
    float averageFrameMs() const { return m_averageFrameMs; }
    uint32_t sampleCount() const { return m_sampleCount; }
    uint32_t adjustmentCount() const { return m_adjustmentCount; }

  private:
    DynamicResolutionSettings m_settings;
    float m_currentScale = 1.0f;
    float m_accumulatedFrameMs = 0.0f;
    float m_averageFrameMs = 0.0f;
    uint32_t m_sampleCount = 0;
    uint32_t m_adjustmentCount = 0;
};

} // namespace hbg::render
