#include "render/vfx/vfx_reference.h"

#include "core/log.h"
#include "gfx/encoder.h"
#include "render/vfx/hbg_vfx_backend.h"

#include "vfx/asset.h"
#include "vfx/attribute_layout.h"
#include "vfx/reference.h"
#include "vfx/runtime.h"
#include "../../../vfx/src/runtime/runtime_asset.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace hbg {
namespace {

constexpr std::uint32_t kReferenceFrames = 10u;
constexpr float kReferenceDelta = 0.1f;
constexpr float kReferenceEpsilon = 1.0e-3f;

bool readBuffer(gfx::ResourceManager& resources, gfx::BufferHandle buffer, void* data, std::uint32_t size,
                const char* label) {
    if (!buffer.valid() || data == nullptr || size == 0u) {
        HBG_LOGE("vfx reference: {} readback has invalid input", label);
        return false;
    }
    const std::uint32_t expectedFrame =
        bgfx::read({.handle = resources.native(buffer), .offset = 0, .size = size}, data);
    if (expectedFrame == 0u) {
        HBG_LOGE("vfx reference: {} readback request failed", label);
        return false;
    }
    std::uint32_t currentFrame = bgfx::frame();
    resources.endFrame();
    while (currentFrame < expectedFrame) {
        currentFrame = bgfx::frame();
        resources.endFrame();
    }
    return true;
}

float asFloat(const vfx::AttributeWords& words, std::size_t index) {
    return std::bit_cast<float>(words[index]);
}

bool approx(float actual, float expected) {
    return std::abs(actual - expected) <= kReferenceEpsilon;
}

bool approx(const vfx::Vec3& actual, const vfx::Vec3& expected) {
    return approx(actual.x, expected.x) && approx(actual.y, expected.y) && approx(actual.z, expected.z);
}


} // namespace

bool runVfxReference(gfx::ResourceManager& resources, const std::string& assetPath,
                     const std::string& graphPath) {
    const std::filesystem::path graphFile =
        graphPath.empty() ? std::filesystem::path(assetPath).replace_extension(".vfx.json")
                          : std::filesystem::path(graphPath);
    const vfx::ParseResult parsed = vfx::loadGraphFile(graphFile);
    if (!parsed.ok()) {
        HBG_LOGE("vfx reference: failed to load graph '{}'", graphFile.string());
        for (const vfx::ParseError& error : parsed.errors) {
            HBG_LOGE("vfx reference: {}: {}", error.path, error.message);
        }
        return false;
    }
    const vfx::Graph& graph = *parsed.graph;
    if (graph.systems.size() != 1u || graph.systems[0].capacity != 1024u) {
        HBG_LOGE("vfx reference: graph must contain one 1024-particle system");
        return false;
    }
    const vfx::AttributeSet attributeSet = vfx::defaultAttributeSet(graph);
    const vfx::AttributeLayout layout =
        vfx::buildAttributeLayout(attributeSet, graph.systems[0].requestedCapacity);
    const vfx::AttributeLayoutEntry* position = layout.find("position");
    const vfx::AttributeLayoutEntry* velocity = layout.find("velocity");
    const vfx::AttributeLayoutEntry* color = layout.find("color");
    const vfx::AttributeLayoutEntry* alpha = layout.find("alpha");
    const vfx::AttributeLayoutEntry* size = layout.find("size");
    const vfx::AttributeLayoutEntry* age = layout.find("age");
    const vfx::AttributeLayoutEntry* lifetime = layout.find("lifetime");
    const vfx::AttributeLayoutEntry* seed = layout.find("seed");
    const vfx::AttributeLayoutEntry* particleId = layout.find("particleId");
    const vfx::AttributeLayoutEntry* spawnIndex = layout.find("spawnIndex");
    const vfx::AttributeLayoutEntry* alive = layout.find("alive");
    if (position == nullptr || velocity == nullptr || color == nullptr || alpha == nullptr || size == nullptr ||
        age == nullptr || lifetime == nullptr || seed == nullptr || particleId == nullptr ||
        spawnIndex == nullptr || alive == nullptr) {
        HBG_LOGE("vfx reference: required attributes are missing from the layout");
        return false;
    }

    hbg::vfxbridge::HbgVfxBackend backend(resources);
    vfx::Runtime runtime(backend.device(), &backend.shading());
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset = runtime.loadAsset(assetPath, &error);
    if (asset == nullptr) {
        HBG_LOGE("vfx reference: {}", error);
        return false;
    }
    const std::string systemName = graph.systems[0].name;
    vfx::EffectDesc desc;
    desc.randomSeed = 0u;
    desc.playOnAwake = true;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    if (effect == nullptr) {
        HBG_LOGE("vfx reference: {}", error);
        return false;
    }

    const vfx::BufferHandle attributesBuffer = backend.findBuffer("vfx." + systemName + ".buffer0");
    const vfx::BufferHandle deadListBuffer = backend.findBuffer("vfx." + systemName + ".buffer1");
    const vfx::BufferHandle indirectionBuffer = backend.findBuffer("vfx." + systemName + ".buffer2");
    const vfx::BufferHandle renderCountBuffer = backend.findBuffer("vfx." + systemName + ".buffer3");
    const vfx::BufferHandle indirectBuffer = backend.findBuffer("vfx." + systemName + ".buffer5");
    if (!attributesBuffer.valid() || !deadListBuffer.valid() || !indirectionBuffer.valid() ||
        !renderCountBuffer.valid() || !indirectBuffer.valid()) {
        HBG_LOGE("vfx reference: failed to locate runtime buffers");
        runtime.destroyEffect(effect);
        return false;
    }

    hbg::gfx::Encoder encoder(resources);
    encoder.setViewRect(0, 0, 0, 64, 64);
    backend.begin(encoder, 0);
    const vfx::FrameContext frame{};
    for (std::uint32_t frameIndex = 0; frameIndex < kReferenceFrames; ++frameIndex) {
        runtime.update(kReferenceDelta);
        runtime.encodeSimulation(backend, frame);
        bgfx::frame();
        resources.endFrame();
    }
    backend.end();

    constexpr std::uint32_t capacity = 1024u;
    std::vector<std::uint32_t> attributes(layout.bufferSizeWords(), 0u);
    std::vector<std::uint32_t> deadList(capacity + 2u, 0u);
    std::vector<std::uint32_t> indirection(capacity, 0u);
    std::vector<std::uint32_t> renderCount(1u, 0u);
    std::vector<std::uint32_t> indirect(8u, 0u);
    const gfx::BufferHandle nativeAttributes = backend.nativeBuffer(attributesBuffer);
    const gfx::BufferHandle nativeDeadList = backend.nativeBuffer(deadListBuffer);
    const gfx::BufferHandle nativeIndirection = backend.nativeBuffer(indirectionBuffer);
    const gfx::BufferHandle nativeRenderCount = backend.nativeBuffer(renderCountBuffer);
    const gfx::BufferHandle nativeIndirect = backend.nativeBuffer(indirectBuffer);
    bool ok = readBuffer(resources, nativeAttributes,
                         attributes.data(), static_cast<std::uint32_t>(attributes.size() * sizeof(std::uint32_t)),
                         "attributes") &&
              readBuffer(resources, nativeDeadList, deadList.data(),
                         static_cast<std::uint32_t>(deadList.size() * sizeof(std::uint32_t)), "dead list") &&
              readBuffer(resources, nativeIndirection, indirection.data(),
                         static_cast<std::uint32_t>(indirection.size() * sizeof(std::uint32_t)), "indirection") &&
              readBuffer(resources, nativeRenderCount, renderCount.data(),
                         static_cast<std::uint32_t>(renderCount.size() * sizeof(std::uint32_t)), "render count") &&
              readBuffer(resources, nativeIndirect, indirect.data(),
                         static_cast<std::uint32_t>(indirect.size() * sizeof(std::uint32_t)), "indirect");
    if (!ok) {
        runtime.destroyEffect(effect);
        return false;
    }

    ok = deadList.size() >= 2u && deadList[0] == 0u && deadList[1] == 1024u;
    ok = ok && renderCount[0] == 1024u && indirect[0] == 6144u && indirect[1] == 1u;
    if (!ok) {
        HBG_LOGE("vfx reference: counters mismatch dead_list={}/{} render_count={} indirect=[{} {}]",
                 deadList.size() > 0u ? deadList[0] : 0u, deadList.size() > 1u ? deadList[1] : 0u,
                 renderCount[0], indirect[0], indirect[1]);
    }

    std::vector<bool> seen(1024u, false);
    std::vector<bool> drawn(1024u, false);
    std::uint32_t aliveCount = 0u;
    for (std::uint32_t particleIndex = 0; particleIndex < 1024u && ok; ++particleIndex) {
        vfx::AttributeWords words{};
        const std::uint32_t particleSpawn =
            vfx::readAttribute(layout, *spawnIndex, attributes, particleIndex, words) ? words[0] : 0xffffffffu;
        if (particleSpawn >= 1024u || seen[particleSpawn]) {
            HBG_LOGE("vfx reference: invalid or duplicate spawnIndex {} at particle {}", particleSpawn,
                     particleIndex);
            ok = false;
            break;
        }
        vfx::reference::ParticleState expected;
        if (!vfx::reference::simulateParticle(graph, 0u, particleSpawn, desc.randomSeed, kReferenceDelta,
                                              kReferenceFrames, expected, &error)) {
            HBG_LOGE("vfx reference: {}", error);
            ok = false;
            break;
        }
        seen[particleSpawn] = true;

        vfx::AttributeWords particleWords{};
        const bool particleAlive =
            vfx::readAttribute(layout, *alive, attributes, particleIndex, particleWords) && particleWords[0] != 0u;
        if (particleAlive != expected.alive) {
            HBG_LOGE("vfx reference: alive mismatch at particle {} expected={} actual={}", particleIndex,
                     expected.alive ? 1 : 0, particleAlive ? 1 : 0);
            ok = false;
            break;
        }
        if (!particleAlive) {
            continue;
        }
        ++aliveCount;
        vfx::AttributeWords particleIdWords{};
        vfx::AttributeWords seedWords{};
        vfx::readAttribute(layout, *particleId, attributes, particleIndex, particleIdWords);
        vfx::readAttribute(layout, *seed, attributes, particleIndex, seedWords);
        vfx::AttributeWords positionWords{};
        vfx::AttributeWords velocityWords{};
        vfx::AttributeWords colorWords{};
        vfx::AttributeWords alphaWords{};
        vfx::AttributeWords sizeWords{};
        vfx::AttributeWords ageWords{};
        vfx::AttributeWords lifetimeWords{};
        if (!vfx::readAttribute(layout, *position, attributes, particleIndex, positionWords) ||
            !vfx::readAttribute(layout, *velocity, attributes, particleIndex, velocityWords) ||
            !vfx::readAttribute(layout, *color, attributes, particleIndex, colorWords) ||
            !vfx::readAttribute(layout, *alpha, attributes, particleIndex, alphaWords) ||
            !vfx::readAttribute(layout, *size, attributes, particleIndex, sizeWords) ||
            !vfx::readAttribute(layout, *age, attributes, particleIndex, ageWords) ||
            !vfx::readAttribute(layout, *lifetime, attributes, particleIndex, lifetimeWords)) {
            HBG_LOGE("vfx reference: attribute read failed at particle {}", particleIndex);
            ok = false;
            break;
        }
        const vfx::Vec3 actualPosition{asFloat(positionWords, 0u), asFloat(positionWords, 1u),
                                       asFloat(positionWords, 2u)};
        const vfx::Vec3 actualVelocity{asFloat(velocityWords, 0u), asFloat(velocityWords, 1u),
                                       asFloat(velocityWords, 2u)};
        const vfx::Vec3 actualColor{asFloat(colorWords, 0u), asFloat(colorWords, 1u), asFloat(colorWords, 2u)};
        if (particleIdWords[0] != expected.particleId || seedWords[0] != expected.seed ||
            !approx(actualPosition, expected.position) || !approx(actualVelocity, expected.velocity) ||
            !approx(actualColor, expected.color) || !approx(asFloat(alphaWords, 0u), expected.alpha) ||
            !approx(asFloat(sizeWords, 0u), expected.size) ||
            !approx(asFloat(ageWords, 0u), expected.age) ||
            !approx(asFloat(lifetimeWords, 0u), expected.lifetime)) {
            HBG_LOGE("vfx reference: particle {} mismatch spawn={} pos=({},{},{}) expected=({},{},{})",
                     particleIndex, particleSpawn, actualPosition.x, actualPosition.y, actualPosition.z,
                     expected.position.x, expected.position.y, expected.position.z);
            ok = false;
            break;
        }
    }
    for (std::uint32_t spawn = 0; spawn < 1024u && ok; ++spawn) {
        if (!seen[spawn]) {
            HBG_LOGE("vfx reference: spawnIndex {} was not allocated", spawn);
            ok = false;
        }
    }
    std::uint32_t drawCount = 0u;
    for (std::uint32_t index = 0; index < indirection.size() && ok; ++index) {
        const std::uint32_t particle = indirection[index];
        if (particle >= 1024u || drawn[particle]) {
            HBG_LOGE("vfx reference: invalid indirection entry {} -> {}", index, particle);
            ok = false;
            break;
        }
        drawn[particle] = true;
        ++drawCount;
    }
    if (ok && drawCount != 1024u) {
        HBG_LOGE("vfx reference: indirection count mismatch {}", drawCount);
        ok = false;
    }
    if (ok && aliveCount != 1024u) {
        HBG_LOGE("vfx reference: alive count mismatch {}", aliveCount);
        ok = false;
    }
    HBG_LOGI("vfx reference: {} frames={} particles={} alive={} draws={} dead_counter={} render_count={} "
             "indirect=[{} {}]",
             ok ? "pass" : "FAIL", kReferenceFrames, 1024u, aliveCount, drawCount,
             deadList.size() > 0u ? deadList[0] : 0u, renderCount[0], indirect[0], indirect[1]);

    runtime.destroyEffect(effect);
    return ok;
}

} // namespace hbg
