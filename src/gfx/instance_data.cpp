#include "gfx/instance_data.h"

namespace hbg::gfx {

uint32_t InstanceData::available(uint32_t count, uint16_t stride) {
    return bgfx::getAvailInstanceDataBuffer(count, stride);
}

bool InstanceData::allocate(uint32_t count, uint16_t stride) {
    m_count = 0;
    if (count == 0 || stride == 0 || available(count, stride) < count) {
        return false;
    }
    bgfx::allocInstanceDataBuffer(&m_buffer, count, stride);
    if (m_buffer.data == nullptr) {
        return false;
    }
    m_count = count;
    return true;
}

} // namespace hbg::gfx
