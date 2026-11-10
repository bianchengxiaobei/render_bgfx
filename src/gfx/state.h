#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace hbg::gfx::state {

inline constexpr uint64_t kOpaque = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                    BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

inline constexpr uint64_t kOpaqueNoCull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;

inline constexpr uint64_t kDepthOnly = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW;

inline constexpr uint64_t kShadowCaster = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW;

inline constexpr uint64_t kAlphaTest = kOpaque | BGFX_STATE_ALPHA_REF(128);

inline constexpr uint64_t kForwardOpaque =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

inline constexpr uint64_t kForwardOpaqueNoCull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_MSAA;

inline constexpr uint64_t kPrepass = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                     BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

inline constexpr uint64_t kPrepassNoCull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;

inline constexpr uint64_t kMotionVectors =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_CULL_CW;

inline constexpr uint64_t kMotionVectorsNoCull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL;

inline constexpr uint64_t kCameraMotionVectors =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL;

inline constexpr uint64_t kCameraMotionVectorsFull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS;

inline constexpr uint64_t kTransparent = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
                                         BGFX_STATE_CULL_CW | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;

inline constexpr uint64_t kAdditive = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
                                      BGFX_STATE_CULL_CW | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA;

inline constexpr uint64_t kFullscreen = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS;

inline constexpr uint64_t kNoDepth = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

inline constexpr uint64_t kWireframe = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
                                       BGFX_STATE_PT_LINES | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;

inline constexpr uint64_t kDebugMaterial =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

inline constexpr uint64_t kDebugMaterialNoCull =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;

} // namespace hbg::gfx::state
