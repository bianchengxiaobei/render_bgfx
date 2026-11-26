#pragma once

#include "vfx/types.h"

namespace vfx {

template <typename Tag>
struct Handle {
    Index index = kInvalidIndex;

    constexpr bool valid() const { return index != kInvalidIndex; }
    constexpr explicit operator bool() const { return valid(); }

    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

struct EffectTag {};
struct SystemTag {};
struct BufferTag {};
struct ProgramTag {};
struct TextureTag {};
struct UniformTag {};
struct SamplerTag {};
struct ReadbackTag {};

using EffectHandle = Handle<EffectTag>;
using SystemHandle = Handle<SystemTag>;
using BufferHandle = Handle<BufferTag>;
using ProgramHandle = Handle<ProgramTag>;
using TextureHandle = Handle<TextureTag>;
using UniformHandle = Handle<UniformTag>;
using SamplerHandle = Handle<SamplerTag>;
using ReadbackHandle = Handle<ReadbackTag>;

using AttributeBufferHandle = BufferHandle;
using IndirectionBufferHandle = BufferHandle;
using IndirectArgsHandle = BufferHandle;

} // namespace vfx
