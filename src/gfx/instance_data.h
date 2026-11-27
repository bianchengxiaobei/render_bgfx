#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace hbg::gfx {

class InstanceData {
  public:
    InstanceData() = default;
    InstanceData(const InstanceData&) = delete;
    InstanceData& operator=(const InstanceData&) = delete;

    static uint32_t available(uint32_t count, uint16_t stride);
    bool allocate(uint32_t count, uint16_t stride);

    bool valid() const { return m_buffer.data != nullptr; }
    float* floats() { return reinterpret_cast<float*>(m_buffer.data); }
    uint32_t count() const { return m_count; }
    const bgfx::InstanceDataBuffer* native() const { return &m_buffer; }

  private:
    bgfx::InstanceDataBuffer m_buffer{};
    uint32_t m_count = 0;
};

} // namespace hbg::gfx
