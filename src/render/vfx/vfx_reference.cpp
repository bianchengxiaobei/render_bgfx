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
#include <chrono>
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

bool isFloatType(vfx::AttributeType type) {
    return type == vfx::AttributeType::Float || type == vfx::AttributeType::Float2 ||
           type == vfx::AttributeType::Float3 || type == vfx::AttributeType::Float4;
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
    if (graph.systems.size() != 1u || graph.systems[0].capacity == 0u ||
        graph.systems[0].capacity > 262144u) {
        HBG_LOGE("vfx reference: graph must contain one system with capacity in [1, 262144]");
        return false;
    }
    const vfx::SystemDesc& referenceSystem = graph.systems[0];
    const std::uint32_t spawnCapacity = referenceSystem.capacity;
    const vfx::AttributeSet attributeSet = vfx::defaultAttributeSet(graph);
    const vfx::AttributeLayout layout =
        vfx::buildAttributeLayout(attributeSet, referenceSystem.requestedCapacity);
    const std::uint32_t particleCapacity = layout.capacity;
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
    const vfx::BufferHandle renderKvpBuffer = backend.findBuffer("vfx." + systemName + ".buffer7");
    if (!attributesBuffer.valid() || !deadListBuffer.valid() || !indirectionBuffer.valid() ||
        !renderCountBuffer.valid() || !indirectBuffer.valid() || !renderKvpBuffer.valid()) {
        HBG_LOGE("vfx reference: failed to locate runtime buffers");
        runtime.destroyEffect(effect);
        return false;
    }

    hbg::gfx::Encoder encoder(resources);
    encoder.setViewRect(0, 0, 0, 64, 64);
    backend.begin(encoder, 0);
    const vfx::FrameContext frame{};
    const auto submitStart = std::chrono::steady_clock::now();
    std::vector<double> frameTimesMs;
    frameTimesMs.reserve(kReferenceFrames);
    for (std::uint32_t frameIndex = 0; frameIndex < kReferenceFrames; ++frameIndex) {
        const auto frameStart = std::chrono::steady_clock::now();
        runtime.update(kReferenceDelta);
        runtime.encodeSimulation(backend, frame);
        bgfx::frame();
        resources.endFrame();
        frameTimesMs.push_back(std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - frameStart)
                                   .count());
    }
    backend.end();
    const auto submitEnd = std::chrono::steady_clock::now();

    std::vector<std::uint32_t> attributes(layout.bufferSizeWords(), 0u);
    std::vector<std::uint32_t> deadList(particleCapacity + 2u, 0u);
    std::vector<std::uint32_t> indirection(particleCapacity, 0u);
    std::vector<std::uint32_t> renderCount(1u, 0u);
    std::vector<std::uint32_t> indirect(8u, 0u);
    std::vector<std::uint32_t> renderKvp((particleCapacity + 1u) * 2u, 0u);
    const gfx::BufferHandle nativeAttributes = backend.nativeBuffer(attributesBuffer);
    const gfx::BufferHandle nativeDeadList = backend.nativeBuffer(deadListBuffer);
    const gfx::BufferHandle nativeIndirection = backend.nativeBuffer(indirectionBuffer);
    const gfx::BufferHandle nativeRenderCount = backend.nativeBuffer(renderCountBuffer);
    const gfx::BufferHandle nativeIndirect = backend.nativeBuffer(indirectBuffer);
    const gfx::BufferHandle nativeRenderKvp = backend.nativeBuffer(renderKvpBuffer);
    bool ok = readBuffer(resources, nativeAttributes,
                         attributes.data(), static_cast<std::uint32_t>(attributes.size() * sizeof(std::uint32_t)),
                         "attributes") &&
              readBuffer(resources, nativeDeadList, deadList.data(),
                         static_cast<std::uint32_t>(deadList.size() * sizeof(std::uint32_t)), "dead list") &&
              readBuffer(resources, nativeIndirection, indirection.data(),
                         static_cast<std::uint32_t>(indirection.size() * sizeof(std::uint32_t)), "indirection") &&
              readBuffer(resources, nativeRenderCount, renderCount.data(),
                         static_cast<std::uint32_t>(renderCount.size() * sizeof(std::uint32_t)), "render count") &&
              readBuffer(resources, nativeRenderKvp, renderKvp.data(),
                         static_cast<std::uint32_t>(renderKvp.size() * sizeof(std::uint32_t)), "render KVP") &&
              readBuffer(resources, nativeIndirect, indirect.data(),
                         static_cast<std::uint32_t>(indirect.size() * sizeof(std::uint32_t)), "indirect");
    const auto readbackEnd = std::chrono::steady_clock::now();
    if (!ok) {
        runtime.destroyEffect(effect);
        return false;
    }

    const std::uint32_t kvpCount = renderKvp.size() >= 2u ? renderKvp[1] : 0u;
    ok = deadList.size() >= 2u && deadList[0] == 0u && deadList[1] == particleCapacity;
    ok = ok && renderCount[0] == spawnCapacity && kvpCount == renderCount[0] &&
         kvpCount <= particleCapacity && indirect[0] == spawnCapacity * 6u && indirect[1] == 1u;
    if (!ok) {
        HBG_LOGE("vfx reference: counters mismatch dead_list={}/{} render_count={} kvp_count={} indirect=[{} {}]",
                 deadList.size() > 0u ? deadList[0] : 0u, deadList.size() > 1u ? deadList[1] : 0u,
                 renderCount[0], kvpCount, indirect[0], indirect[1]);
    }

    std::vector<bool> seen(spawnCapacity, false);
    std::vector<bool> drawn(particleCapacity, false);
    std::vector<float> expectedKey(particleCapacity, 0.0f);
    std::uint32_t aliveCount = 0u;
    for (std::uint32_t particleIndex = 0; particleIndex < particleCapacity && ok; ++particleIndex) {
        vfx::AttributeWords words{};
        const std::uint32_t particleSpawn =
            vfx::readAttribute(layout, *spawnIndex, attributes, particleIndex, words) ? words[0] : 0xffffffffu;
        if (particleSpawn >= spawnCapacity || seen[particleSpawn]) {
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
            HBG_LOGE("vfx reference: particle {} mismatch spawn={} id={}/{} seed={}/{} "
                     "pos=({},{},{}) expected=({},{},{}) vel=({},{},{}) expected=({},{},{}) "
                     "color=({},{},{}) expected=({},{},{}) alpha={}/{} size={}/{} age={}/{} lifetime={}/{}",
                     particleIndex, particleSpawn, particleIdWords[0], expected.particleId, seedWords[0],
                     expected.seed, actualPosition.x, actualPosition.y, actualPosition.z, expected.position.x,
                     expected.position.y, expected.position.z, actualVelocity.x, actualVelocity.y,
                     actualVelocity.z, expected.velocity.x, expected.velocity.y, expected.velocity.z,
                     actualColor.x, actualColor.y, actualColor.z, expected.color.x, expected.color.y,
                     expected.color.z, asFloat(alphaWords, 0u), expected.alpha, asFloat(sizeWords, 0u),
                     expected.size, asFloat(ageWords, 0u), expected.age, asFloat(lifetimeWords, 0u),
                     expected.lifetime);
            ok = false;
            break;
        }
        if (referenceSystem.sort.mode == vfx::SortMode::Distance) {
            vfx::Vec3 worldPosition = expected.position;
            if (referenceSystem.space == vfx::SystemSpace::Local) {
                const float* matrix = desc.transform.m.data();
                worldPosition = vfx::Vec3{
                    matrix[0] * expected.position.x + matrix[4] * expected.position.y +
                        matrix[8] * expected.position.z + matrix[12],
                    matrix[1] * expected.position.x + matrix[5] * expected.position.y +
                        matrix[9] * expected.position.z + matrix[13],
                    matrix[2] * expected.position.x + matrix[6] * expected.position.y +
                        matrix[10] * expected.position.z + matrix[14]};
            }
            const float dx = worldPosition.x - frame.cameraPosition.x;
            const float dy = worldPosition.y - frame.cameraPosition.y;
            const float dz = worldPosition.z - frame.cameraPosition.z;
            const float key = dx * dx + dy * dy + dz * dz;
            expectedKey[particleIndex] = key;
        } else if (referenceSystem.sort.mode == vfx::SortMode::Custom) {
            HBG_LOGE("vfx reference: custom sort key readback is not supported");
            ok = false;
            break;
        }
    }
    for (std::uint32_t spawn = 0; spawn < spawnCapacity && ok; ++spawn) {
        if (!seen[spawn]) {
            HBG_LOGE("vfx reference: spawnIndex {} was not allocated", spawn);
            ok = false;
        }
    }
    std::uint32_t drawCount = 0u;
    for (std::uint32_t index = 0; index < indirection.size() && ok; ++index) {
        const std::uint32_t particle = indirection[index];
        if (particle >= particleCapacity || drawn[particle]) {
            HBG_LOGE("vfx reference: invalid indirection entry {} -> {}", index, particle);
            ok = false;
            break;
        }
        drawn[particle] = true;
        ++drawCount;
    }
    if (ok && drawCount != spawnCapacity) {
        HBG_LOGE("vfx reference: indirection count mismatch {}", drawCount);
        ok = false;
    }
    if (ok && kvpCount != drawCount) {
        HBG_LOGE("vfx reference: KVP count mismatch {} != {}", kvpCount, drawCount);
        ok = false;
    }
    std::vector<bool> kvpDrawn(particleCapacity, false);
    for (std::uint32_t index = 0; index < kvpCount && ok; ++index) {
        const std::uint32_t particle = renderKvp[3u + index * 2u];
        if (particle >= particleCapacity || kvpDrawn[particle] || !drawn[particle]) {
            HBG_LOGE("vfx reference: invalid render KVP {} -> {}", index, particle);
            ok = false;
            break;
        }
        kvpDrawn[particle] = true;
        if (referenceSystem.sort.mode == vfx::SortMode::Distance) {
            const float actualKey = std::bit_cast<float>(renderKvp[2u + index * 2u]);
            if (!approx(actualKey, expectedKey[particle])) {
                HBG_LOGE("vfx reference: KVP key mismatch at particle {} actual={} expected={}",
                         particle, actualKey, expectedKey[particle]);
                ok = false;
                break;
            }
        }
    }
    if (ok && referenceSystem.sort.mode == vfx::SortMode::Distance) {
        std::vector<vfx::reference::SortKVP> gpuKvp;
        gpuKvp.reserve(kvpCount);
        for (std::uint32_t index = 0; index < kvpCount; ++index) {
            gpuKvp.push_back({std::bit_cast<float>(renderKvp[2u + index * 2u]),
                              renderKvp[3u + index * 2u]});
        }
        vfx::reference::sortKVP(gpuKvp, referenceSystem.sort.order);
        for (std::uint32_t index = 0; index < drawCount; ++index) {
            if (indirection[index] != gpuKvp[index].value) {
                HBG_LOGE("vfx reference: sorted indirection mismatch at {} actual={} expected={}",
                         index, indirection[index], gpuKvp[index].value);
                ok = false;
                break;
            }
        }
    }
    if (ok && aliveCount != spawnCapacity) {
        HBG_LOGE("vfx reference: alive count mismatch {}", aliveCount);
        ok = false;
    }
    HBG_LOGI("vfx reference: {} frames={} particles={} alive={} draws={} kvp={} dead_counter={} "
             "render_count={} indirect=[{} {}] sort={}",
             ok ? "pass" : "FAIL", kReferenceFrames, spawnCapacity, aliveCount, drawCount, kvpCount,
             deadList.size() > 0u ? deadList[0] : 0u, renderCount[0], indirect[0], indirect[1],
             vfx::toString(referenceSystem.sort.mode));
    const double submitMs =
        std::chrono::duration<double, std::milli>(submitEnd - submitStart).count() / kReferenceFrames;
    const double readbackMs = std::chrono::duration<double, std::milli>(readbackEnd - submitEnd).count();
    double steadyFrameMs = 0.0;
    std::uint32_t steadyFrameCount = 0;
    for (std::size_t index = 2u; index < frameTimesMs.size(); ++index) {
        steadyFrameMs += frameTimesMs[index];
        ++steadyFrameCount;
    }
    if (steadyFrameCount > 0u) {
        steadyFrameMs /= static_cast<double>(steadyFrameCount);
    }
    const vfx::Diagnostics diagnostics = runtime.diagnostics();
    HBG_LOGI("vfx reference performance: frames={} submit_ms_per_frame={:.3f} steady_ms_per_frame={:.3f} "
             "first_frame_ms={:.3f} readback_ms={:.3f} dispatches={} sort_passes={}",
             kReferenceFrames, submitMs, steadyFrameMs, frameTimesMs.empty() ? 0.0 : frameTimesMs.front(),
             readbackMs, diagnostics.dispatches, diagnostics.sortPasses);

    runtime.destroyEffect(effect);
    return ok;
}

bool runVfxEventReference(gfx::ResourceManager& resources, const std::string& assetPath,
                          const std::string& graphPath) {
    constexpr std::uint32_t kEventFrames = 2u;
    const std::filesystem::path graphFile =
        graphPath.empty() ? std::filesystem::path(assetPath).replace_extension(".vfx.json")
                          : std::filesystem::path(graphPath);
    const vfx::ParseResult parsed = vfx::loadGraphFile(graphFile);
    if (!parsed.ok()) {
        HBG_LOGE("vfx event reference: failed to load graph '{}'", graphFile.string());
        return false;
    }
    const vfx::Graph& graph = *parsed.graph;
    if (graph.systems.size() < 2u) {
        HBG_LOGE("vfx event reference: graph must contain at least two systems");
        return false;
    }

    hbg::vfxbridge::HbgVfxBackend backend(resources);
    vfx::Runtime runtime(backend.device(), &backend.shading());
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset = runtime.loadAsset(assetPath, &error);
    if (asset == nullptr) {
        HBG_LOGE("vfx event reference: {}", error);
        return false;
    }
    if (asset->eventLinks.empty() || asset->eventBufferSize == 0u) {
        HBG_LOGE("vfx event reference: asset has no GPU event links");
        return false;
    }
    vfx::EffectDesc desc;
    desc.randomSeed = 0u;
    desc.playOnAwake = true;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    if (effect == nullptr) {
        HBG_LOGE("vfx event reference: {}", error);
        return false;
    }

    const vfx::BufferHandle eventBuffer = backend.findBuffer("vfx.events");
    if (!eventBuffer.valid()) {
        HBG_LOGE("vfx event reference: failed to locate event buffer");
        runtime.destroyEffect(effect);
        return false;
    }

    hbg::gfx::Encoder encoder(resources);
    encoder.setViewRect(0, 0, 0, 64, 64);
    backend.begin(encoder, 0);
    const vfx::FrameContext frame{};
    for (std::uint32_t frameIndex = 0; frameIndex < kEventFrames; ++frameIndex) {
        runtime.update(0.1f);
        runtime.encodeSimulation(backend, frame);
        bgfx::frame();
        resources.endFrame();
    }
    backend.end();

    std::vector<std::uint32_t> eventData(asset->eventBufferSize / 4u, 0u);
    bool ok = readBuffer(resources, backend.nativeBuffer(eventBuffer), eventData.data(),
                         static_cast<std::uint32_t>(eventData.size() * sizeof(std::uint32_t)),
                         "events");
    std::vector<vfx::AttributeLayout> layouts;
    layouts.reserve(graph.systems.size());
    for (const vfx::SystemDesc& system : graph.systems) {
        layouts.push_back(vfx::buildAttributeLayout(vfx::defaultAttributeSet(graph),
                                                    system.requestedCapacity));
    }

    std::uint32_t prefixBase = 0u;
    for (const vfx::AssetEventLink& link : asset->eventLinks) {
        if (!ok) {
            break;
        }
        if (link.cpu) {
            continue;
        }
        if (link.producerSystem >= graph.systems.size() ||
            link.consumerSystem >= graph.systems.size()) {
            HBG_LOGE("vfx event reference: event link systems are out of bounds");
            ok = false;
            break;
        }
        const std::size_t offset = link.bufferOffsetWords;
        if (offset + 5u + link.capacity + link.capacity * 16u > eventData.size()) {
            HBG_LOGE("vfx event reference: event link buffer is out of bounds");
            ok = false;
            break;
        }
        const std::uint32_t elementCount = eventData[offset];
        const std::uint32_t totalCount = eventData[offset + 1u];
        const std::uint32_t prefix = eventData[offset + 2u];
        const std::uint32_t dropped = eventData[offset + 3u];
        const std::uint32_t frameCount = eventData[offset + 4u];
        std::string eventError;
        const std::uint32_t expectedEvents =
            vfx::reference::simulateEventCount(graph, link.producerSystem, 0.1f, kEventFrames,
                                               &eventError);
        if (!eventError.empty()) {
            HBG_LOGE("vfx event reference: {}", eventError);
            ok = false;
            break;
        }
        const std::uint32_t acceptedEvents = std::min(expectedEvents, link.capacity);
        if (elementCount != 0u || prefix != prefixBase || frameCount != acceptedEvents ||
            totalCount != acceptedEvents || dropped != expectedEvents - acceptedEvents ||
            frameCount > link.capacity) {
            HBG_LOGE("vfx event reference: event link '{}' counters mismatch element={} total={} prefix={} "
                     "expected_prefix={} dropped={} frame={} expected={}/{} capacity={}",
                     link.name, elementCount, totalCount, prefix, prefixBase, dropped, frameCount,
                     expectedEvents, acceptedEvents, link.capacity);
            ok = false;
            break;
        }
        prefixBase += frameCount;

        const vfx::AssetSystem& producer = asset->systems[link.producerSystem];
        const vfx::AssetSystem& consumer = asset->systems[link.consumerSystem];
        const vfx::BufferHandle producerAttributes =
            backend.findBuffer("vfx." + producer.name + ".buffer0");
        const vfx::BufferHandle consumerAttributes =
            backend.findBuffer("vfx." + consumer.name + ".buffer0");
        const vfx::BufferHandle consumerDeadList =
            backend.findBuffer("vfx." + consumer.name + ".buffer1");
        const vfx::BufferHandle consumerRenderCount =
            backend.findBuffer("vfx." + consumer.name + ".buffer3");
        if (!producerAttributes.valid() || !consumerAttributes.valid() ||
            !consumerDeadList.valid() || !consumerRenderCount.valid()) {
            HBG_LOGE("vfx event reference: failed to locate system buffers for link '{}'", link.name);
            ok = false;
            break;
        }
        std::vector<std::uint32_t> producerWords(layouts[link.producerSystem].bufferSizeWords(), 0u);
        std::vector<std::uint32_t> consumerWords(layouts[link.consumerSystem].bufferSizeWords(), 0u);
        std::vector<std::uint32_t> deadList(consumer.capacity + 2u, 0u);
        std::vector<std::uint32_t> renderCount(1u, 0u);
        ok = readBuffer(resources, backend.nativeBuffer(producerAttributes), producerWords.data(),
                        static_cast<std::uint32_t>(producerWords.size() * sizeof(std::uint32_t)),
                        "producer attributes") &&
             readBuffer(resources, backend.nativeBuffer(consumerAttributes), consumerWords.data(),
                        static_cast<std::uint32_t>(consumerWords.size() * sizeof(std::uint32_t)),
                        "consumer attributes") &&
             readBuffer(resources, backend.nativeBuffer(consumerDeadList), deadList.data(),
                        static_cast<std::uint32_t>(deadList.size() * sizeof(std::uint32_t)),
                        "consumer dead list") &&
             readBuffer(resources, backend.nativeBuffer(consumerRenderCount), renderCount.data(),
                        static_cast<std::uint32_t>(renderCount.size() * sizeof(std::uint32_t)),
                        "consumer render count");
        if (!ok) {
            break;
        }
        if (deadList.size() < 2u || deadList[1] != consumer.capacity ||
            deadList[0] != consumer.capacity - frameCount || renderCount[0] != frameCount) {
            HBG_LOGE("vfx event reference: consumer '{}' counters mismatch dead={}/{} render={} "
                     "frame={}",
                     consumer.name, deadList.size() > 0u ? deadList[0] : 0u,
                     deadList.size() > 1u ? deadList[1] : 0u, renderCount[0], frameCount);
            ok = false;
            break;
        }

        const vfx::AttributeLayout& producerLayout = layouts[link.producerSystem];
        const vfx::AttributeLayout& consumerLayout = layouts[link.consumerSystem];
        const vfx::AttributeLayoutEntry* consumerSpawnIndex = consumerLayout.find("spawnIndex");
        const vfx::AttributeLayoutEntry* consumerParticleId = consumerLayout.find("particleId");
        const vfx::AttributeLayoutEntry* consumerAlive = consumerLayout.find("alive");
        if (consumerSpawnIndex == nullptr || consumerParticleId == nullptr || consumerAlive == nullptr) {
            HBG_LOGE("vfx event reference: consumer attributes are incomplete");
            ok = false;
            break;
        }
        std::vector<bool> seen(frameCount, false);
        std::uint32_t spawned = 0u;
        for (std::uint32_t particleIndex = 0; particleIndex < consumer.capacity && ok;
             ++particleIndex) {
            vfx::AttributeWords aliveWords{};
            const bool alive = vfx::readAttribute(consumerLayout, *consumerAlive, consumerWords,
                                                 particleIndex, aliveWords) &&
                               aliveWords[0] != 0u;
            if (!alive) {
                continue;
            }
            vfx::AttributeWords spawnWords{};
            vfx::AttributeWords idWords{};
            if (!vfx::readAttribute(consumerLayout, *consumerSpawnIndex, consumerWords, particleIndex,
                                    spawnWords) ||
                !vfx::readAttribute(consumerLayout, *consumerParticleId, consumerWords, particleIndex,
                                    idWords) || spawnWords[0] >= frameCount ||
                seen[spawnWords[0]] || idWords[0] != prefix + spawnWords[0]) {
                HBG_LOGE("vfx event reference: consumer '{}' particle {} has invalid spawn/id {}/{}",
                         consumer.name, particleIndex, spawnWords[0], idWords[0]);
                ok = false;
                break;
            }
            const std::uint32_t sourceIndex = eventData[offset + 5u + spawnWords[0]];
            if (sourceIndex >= producer.capacity) {
                HBG_LOGE("vfx event reference: event source {} is out of range", sourceIndex);
                ok = false;
                break;
            }
            seen[spawnWords[0]] = true;
            ++spawned;
            for (const vfx::AssetEventReceive& receive : link.receives) {
                const auto payload = std::find_if(link.payload.begin(), link.payload.end(),
                                                  [&](const vfx::AssetEventPayload& candidate) {
                                                      return candidate.slot == receive.slot;
                                                  });
                const vfx::AttributeLayoutEntry* producerAttribute =
                    payload == link.payload.end() ? nullptr : producerLayout.find(payload->name);
                const vfx::AttributeLayoutEntry* consumerAttribute =
                    consumerLayout.find(receive.attribute);
                if (producerAttribute == nullptr || consumerAttribute == nullptr ||
                    producerAttribute->attribute.type != consumerAttribute->attribute.type) {
                    HBG_LOGE("vfx event reference: receive mapping '{}' is not supported", receive.attribute);
                    ok = false;
                    break;
                }
                vfx::AttributeWords producerValue{};
                vfx::AttributeWords consumerValue{};
                if (!vfx::readAttribute(producerLayout, *producerAttribute, producerWords, sourceIndex,
                                        producerValue) ||
                    !vfx::readAttribute(consumerLayout, *consumerAttribute, consumerWords,
                                        particleIndex, consumerValue)) {
                    HBG_LOGE("vfx event reference: attribute read failed for '{}'", receive.attribute);
                    ok = false;
                    break;
                }
                const std::uint8_t components =
                    vfx::attributeComponentCount(consumerAttribute->attribute.type);
                for (std::uint8_t component = 0; component < components; ++component) {
                    const bool equal = isFloatType(consumerAttribute->attribute.type)
                                           ? approx(asFloat(producerValue, component),
                                                    asFloat(consumerValue, component))
                                           : producerValue[component] == consumerValue[component];
                    if (!equal) {
                        HBG_LOGE("vfx event reference: attribute '{}' component {} mismatch {}/{}",
                                 receive.attribute, component, producerValue[component],
                                 consumerValue[component]);
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    break;
                }
            }
        }
        if (ok && spawned != frameCount) {
            HBG_LOGE("vfx event reference: consumer '{}' spawned {} of {} events", consumer.name,
                     spawned, frameCount);
            ok = false;
        }
    }
    HBG_LOGI("vfx event reference: {} frames={} links={} event_buffer={}bytes prefix_total={}", ok ? "pass" : "FAIL",
             kEventFrames, asset->eventLinks.size(), asset->eventBufferSize, prefixBase);
    runtime.destroyEffect(effect);
    return ok;
}

bool runVfxCpuEventReference(gfx::ResourceManager& resources, const std::string& assetPath,
                             const std::string& graphPath) {
    constexpr std::uint32_t kCpuEventFrames = 1u;
    const std::filesystem::path graphFile =
        graphPath.empty() ? std::filesystem::path(assetPath).replace_extension(".vfx.json")
                          : std::filesystem::path(graphPath);
    const vfx::ParseResult parsed = vfx::loadGraphFile(graphFile);
    if (!parsed.ok()) {
        HBG_LOGE("vfx CPU event reference: failed to load graph '{}'", graphFile.string());
        return false;
    }
    const vfx::Graph& graph = *parsed.graph;
    if (graph.systems.empty()) {
        HBG_LOGE("vfx CPU event reference: graph has no systems");
        return false;
    }

    hbg::vfxbridge::HbgVfxBackend backend(resources);
    vfx::Runtime runtime(backend.device(), &backend.shading());
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset = runtime.loadAsset(assetPath, &error);
    if (asset == nullptr) {
        HBG_LOGE("vfx CPU event reference: {}", error);
        return false;
    }
    if (asset->eventLinks.empty()) {
        HBG_LOGE("vfx CPU event reference: asset has no event links");
        return false;
    }
    const vfx::AssetEventLink* cpuLink = nullptr;
    for (const vfx::AssetEventLink& link : asset->eventLinks) {
        if (link.cpu) {
            cpuLink = &link;
            break;
        }
    }
    if (cpuLink == nullptr) {
        HBG_LOGE("vfx CPU event reference: asset has no CPU event link");
        return false;
    }
    const std::uint32_t consumerIndex = cpuLink->consumerSystem;
    if (consumerIndex >= graph.systems.size() || consumerIndex >= asset->systems.size()) {
        HBG_LOGE("vfx CPU event reference: CPU event consumer is invalid");
        return false;
    }
    vfx::EffectDesc desc;
    desc.randomSeed = 0u;
    desc.playOnAwake = true;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    if (effect == nullptr) {
        HBG_LOGE("vfx CPU event reference: {}", error);
        return false;
    }

    const auto makeAttribute = [](std::string name, vfx::PropertyType type,
                                  std::array<float, 4> value) {
        vfx::EventAttribute attribute;
        attribute.name = std::move(name);
        attribute.value.type = type;
        attribute.value.f = value;
        return attribute;
    };
    const std::array<vfx::EventAttribute, 2> first = {
        makeAttribute("position", vfx::PropertyType::Vector3, {1.0f, 2.0f, 3.0f, 0.0f}),
        makeAttribute("color", vfx::PropertyType::Vector3, {0.2f, 0.4f, 0.6f, 1.0f})};
    const std::array<vfx::EventAttribute, 2> second = {
        makeAttribute("position", vfx::PropertyType::Vector3, {4.0f, 5.0f, 6.0f, 0.0f}),
        makeAttribute("color", vfx::PropertyType::Vector3, {0.8f, 0.5f, 0.2f, 1.0f})};
    const std::array<vfx::EventAttribute, 1> third = {
        makeAttribute("position", vfx::PropertyType::Vector3, {7.0f, 8.0f, 9.0f, 0.0f})};
    effect->sendEvent(cpuLink->name, first);
    effect->sendEvent(cpuLink->name, second);
    effect->sendEvent(cpuLink->name, third);
    const std::uint32_t acceptedEvents = cpuLink->capacity;
    const std::uint32_t sentEvents = acceptedEvents + 2u;
    const std::array<vfx::EventAttribute, 1> invalid = {
        makeAttribute("position", vfx::PropertyType::Float, {42.0f, 0.0f, 0.0f, 0.0f})};
    effect->sendEvent(cpuLink->name, invalid);
    for (std::uint32_t eventIndex = 3u; eventIndex < sentEvents; ++eventIndex) {
        const std::array<vfx::EventAttribute, 1> extra = {
            makeAttribute("position", vfx::PropertyType::Vector3,
                          {static_cast<float>(eventIndex), static_cast<float>(eventIndex + 1u),
                           static_cast<float>(eventIndex + 2u), 0.0f})};
        effect->sendEvent(cpuLink->name, extra);
    }

    hbg::gfx::Encoder encoder(resources);
    encoder.setViewRect(0, 0, 0, 64, 64);
    backend.begin(encoder, 0);
    const vfx::FrameContext frame{};
    for (std::uint32_t frameIndex = 0; frameIndex < kCpuEventFrames; ++frameIndex) {
        runtime.update(0.1f);
        runtime.encodeSimulation(backend, frame);
        bgfx::frame();
        resources.endFrame();
    }
    backend.end();

    const vfx::BufferHandle eventBuffer = backend.findBuffer("vfx.events");
    const std::string consumerName = asset->systems[consumerIndex].name;
    const vfx::BufferHandle attributes = backend.findBuffer("vfx." + consumerName + ".buffer0");
    const vfx::BufferHandle deadListBuffer = backend.findBuffer("vfx." + consumerName + ".buffer1");
    const vfx::BufferHandle renderCountBuffer = backend.findBuffer("vfx." + consumerName + ".buffer3");
    if (!eventBuffer.valid() || !attributes.valid() || !deadListBuffer.valid() ||
        !renderCountBuffer.valid()) {
        HBG_LOGE("vfx CPU event reference: failed to locate consumer buffers");
        runtime.destroyEffect(effect);
        return false;
    }
    std::vector<std::uint32_t> eventData(asset->eventBufferSize / 4u, 0u);
    vfx::AttributeLayout layout =
        vfx::buildAttributeLayout(vfx::defaultAttributeSet(graph), graph.systems[consumerIndex].requestedCapacity);
    std::vector<std::uint32_t> attributeData(layout.bufferSizeWords(), 0u);
    std::vector<std::uint32_t> deadList(asset->systems[consumerIndex].capacity + 2u, 0u);
    std::vector<std::uint32_t> renderCount(1u, 0u);
    bool ok = readBuffer(resources, backend.nativeBuffer(eventBuffer), eventData.data(),
                         static_cast<std::uint32_t>(eventData.size() * sizeof(std::uint32_t)),
                         "CPU events") &&
              readBuffer(resources, backend.nativeBuffer(attributes), attributeData.data(),
                         static_cast<std::uint32_t>(attributeData.size() * sizeof(std::uint32_t)),
                         "CPU event attributes") &&
              readBuffer(resources, backend.nativeBuffer(deadListBuffer), deadList.data(),
                         static_cast<std::uint32_t>(deadList.size() * sizeof(std::uint32_t)),
                         "CPU event dead list") &&
              readBuffer(resources, backend.nativeBuffer(renderCountBuffer), renderCount.data(),
                         static_cast<std::uint32_t>(renderCount.size() * sizeof(std::uint32_t)),
                         "CPU event render count");
    if (!ok) {
        runtime.destroyEffect(effect);
        return false;
    }
    const std::size_t offset = cpuLink->bufferOffsetWords;
    const std::uint32_t droppedEvents = sentEvents - acceptedEvents;
    if (eventData[offset] != 0u || eventData[offset + 1u] != acceptedEvents ||
        eventData[offset + 2u] != 0u || eventData[offset + 3u] != droppedEvents ||
        eventData[offset + 4u] != acceptedEvents) {
        HBG_LOGE("vfx CPU event reference: counters mismatch element={} total={} prefix={} dropped={} "
                 "frame={} expected={}/{}", eventData[offset], eventData[offset + 1u],
                 eventData[offset + 2u], eventData[offset + 3u], eventData[offset + 4u],
                 acceptedEvents, droppedEvents);
        ok = false;
    }
    if (deadList[0] != asset->systems[consumerIndex].capacity - acceptedEvents ||
        deadList[1] != asset->systems[consumerIndex].capacity ||
        renderCount[0] != acceptedEvents) {
        HBG_LOGE("vfx CPU event reference: consumer counters mismatch dead={}/{} render={}",
                 deadList[0], deadList[1], renderCount[0]);
        ok = false;
    }

    const vfx::AttributeLayoutEntry* position = layout.find("position");
    const vfx::AttributeLayoutEntry* color = layout.find("color");
    const vfx::AttributeLayoutEntry* spawnIndex = layout.find("spawnIndex");
    const vfx::AttributeLayoutEntry* particleId = layout.find("particleId");
    const vfx::AttributeLayoutEntry* alive = layout.find("alive");
    if (position == nullptr || color == nullptr || spawnIndex == nullptr || particleId == nullptr ||
        alive == nullptr) {
        HBG_LOGE("vfx CPU event reference: consumer layout is incomplete");
        ok = false;
    }
    std::vector<vfx::Vec3> expectedPositions(acceptedEvents);
    std::vector<vfx::Vec3> expectedColors(acceptedEvents, vfx::Vec3{1.0f, 1.0f, 1.0f});
    for (std::uint32_t eventIndex = 0; eventIndex < acceptedEvents; ++eventIndex) {
        expectedPositions[eventIndex] = vfx::Vec3{static_cast<float>(eventIndex),
                                                  static_cast<float>(eventIndex + 1u),
                                                  static_cast<float>(eventIndex + 2u)};
    }
    expectedPositions[0] = vfx::Vec3{1.0f, 2.0f, 3.0f};
    expectedPositions[1] = vfx::Vec3{4.0f, 5.0f, 6.0f};
    expectedPositions[2] = vfx::Vec3{7.0f, 8.0f, 9.0f};
    expectedColors[0] = vfx::Vec3{0.2f, 0.4f, 0.6f};
    expectedColors[1] = vfx::Vec3{0.8f, 0.5f, 0.2f};
    std::vector<bool> seen(acceptedEvents, false);
    std::uint32_t spawned = 0u;
    for (std::uint32_t particle = 0; particle < asset->systems[consumerIndex].capacity && ok; ++particle) {
        vfx::AttributeWords aliveWords{};
        const bool particleAlive = vfx::readAttribute(layout, *alive, attributeData, particle, aliveWords) &&
                                   aliveWords[0] != 0u;
        if (!particleAlive) {
            continue;
        }
        vfx::AttributeWords spawnWords{};
        vfx::AttributeWords idWords{};
        vfx::AttributeWords positionWords{};
        vfx::AttributeWords colorWords{};
        if (!vfx::readAttribute(layout, *spawnIndex, attributeData, particle, spawnWords) ||
            !vfx::readAttribute(layout, *particleId, attributeData, particle, idWords) ||
            !vfx::readAttribute(layout, *position, attributeData, particle, positionWords) ||
            !vfx::readAttribute(layout, *color, attributeData, particle, colorWords) ||
            spawnWords[0] >= acceptedEvents || seen[spawnWords[0]] ||
            idWords[0] != spawnWords[0]) {
            HBG_LOGE("vfx CPU event reference: invalid consumer particle {}", particle);
            ok = false;
            break;
        }
        const std::uint32_t index = spawnWords[0];
        seen[index] = true;
        ++spawned;
        const vfx::Vec3 actualPosition{asFloat(positionWords, 0u), asFloat(positionWords, 1u),
                                       asFloat(positionWords, 2u)};
        const vfx::Vec3 actualColor{asFloat(colorWords, 0u), asFloat(colorWords, 1u),
                                    asFloat(colorWords, 2u)};
        if (!approx(actualPosition, expectedPositions[index]) ||
            !approx(actualColor, expectedColors[index])) {
            HBG_LOGE("vfx CPU event reference: particle {} attribute mismatch", particle);
            ok = false;
            break;
        }
    }
    if (ok && spawned != acceptedEvents) {
        HBG_LOGE("vfx CPU event reference: spawned {} of {}", spawned, acceptedEvents);
        ok = false;
    }
    HBG_LOGI("vfx CPU event reference: {} events={} dropped={} buffer={}bytes",
             ok ? "pass" : "FAIL", acceptedEvents, eventData[offset + 3u], asset->eventBufferSize);
    runtime.destroyEffect(effect);
    return ok;
}

} // namespace hbg