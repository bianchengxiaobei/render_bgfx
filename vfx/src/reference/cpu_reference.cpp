#include "vfx/reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace vfx::reference {
namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kMaxDeltaSeconds = 0.1f;
constexpr double kSpawnerBoundaryEpsilon = 1.0e-6;

std::uint32_t wangHash(std::uint32_t seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return seed;
}

std::uint32_t blockSeed(std::string_view path, std::string_view type) {
    std::uint32_t hash = 2166136261u;
    const auto feed = [&hash](std::string_view text) {
        for (const char character : text) {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619u;
        }
    };
    feed(path);
    hash ^= 0x2eu;
    hash *= 16777619u;
    feed(type);
    hash ^= 0u;
    hash *= 16777619u;
    return hash;
}

float vfxRand(std::uint32_t seed) {
    return static_cast<float>(wangHash(seed) & 0x00ffffffu) / 16777215.0f;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

Vec3 random4(std::uint32_t seed) {
    return Vec3{vfxRand(seed), vfxRand(seed + 0x9e3779b9u), vfxRand(seed + 0x3c6ef372u)};
}

Vec3 randomUnitVector(std::uint32_t seed) {
    const float z = vfxRand(seed) * 2.0f - 1.0f;
    const float angle = vfxRand(seed + 0x9e3779b9u) * kTwoPi;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Vec3{std::cos(angle) * radius, std::sin(angle) * radius, z};
}

Vec3 add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& value, float factor) {
    return Vec3{value.x * factor, value.y * factor, value.z * factor};
}
Vec3 multiply(const Vec3& a, const Vec3& b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(const Vec3& value) {
    const float length = std::sqrt(dot(value, value));
    return length > 1.0e-8f ? scale(value, 1.0f / length) : Vec3{};
}

float noiseCell(std::int32_t x, std::int32_t y) {
    const std::uint32_t seed = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
                               static_cast<std::uint32_t>(y) * 0xd8163841u;
    return vfxRand(seed);
}

float valueNoise2D(float x, float y, float& derivativeX, float& derivativeY) {
    const std::int32_t cellX = static_cast<std::int32_t>(std::floor(x));
    const std::int32_t cellY = static_cast<std::int32_t>(std::floor(y));
    const float fx = x - static_cast<float>(cellX);
    const float fy = y - static_cast<float>(cellY);
    const std::array<float, 4> hash{noiseCell(cellX, cellY), noiseCell(cellX + 1, cellY),
                                    noiseCell(cellX, cellY + 1), noiseCell(cellX + 1, cellY + 1)};

    const float blendX = fx * fx * (fx * (fx * (fx * 6.0f - 15.0f) + 10.0f));
    const float blendY = fy * fy * (fy * (fy * (fy * 6.0f - 15.0f) + 10.0f));
    const float derivativeBlendX = fx * fx * (fx * (fx * 30.0f - 60.0f) + 30.0f);
    const float derivativeBlendY = fy * fy * (fy * (fy * 30.0f - 60.0f) + 30.0f);

    const float r0 = lerp(hash[0], hash[2], blendY);
    const float r1 = lerp(hash[1], hash[3], blendY);
    const float r2 = lerp(hash[0], hash[1], blendX);
    const float r3 = lerp(hash[2], hash[3], blendX);
    const float deltaX = r1 - r0;
    const float deltaY = r3 - r2;
    derivativeX = deltaX * derivativeBlendX;
    derivativeY = deltaY * derivativeBlendY;
    return r0 + deltaX * blendX;
}

Vec3 curlNoise3D(const Vec3& coordinate, float frequency, int octaves, float roughness,
                 float lacunarity) {
    const int count = std::clamp(octaves, 1, 8);
    float amplitude = 1.0f;
    float totalAmplitude = 0.0f;
    float t0x = 0.0f;
    float t0y = 0.0f;
    float t1x = 0.0f;
    float t1y = 0.0f;
    float t2x = 0.0f;
    float t2y = 0.0f;

    for (int octave = 0; octave < count; ++octave) {
        float dx = 0.0f;
        float dy = 0.0f;
        valueNoise2D(coordinate.z * frequency, coordinate.y * frequency, dx, dy);
        t0x += dx * amplitude;
        t0y += dy * amplitude;
        valueNoise2D((coordinate.x + 100.0f) * frequency, (coordinate.z + 100.0f) * frequency,
                      dx, dy);
        t1x += dx * amplitude;
        t1y += dy * amplitude;
        valueNoise2D((coordinate.y + 200.0f) * frequency, (coordinate.x + 200.0f) * frequency,
                      dx, dy);
        t2x += dx * amplitude;
        t2y += dy * amplitude;
        totalAmplitude += amplitude;
        amplitude *= roughness;
        frequency *= lacunarity;
    }

    if (!(totalAmplitude > 0.0f)) {
        return Vec3{};
    }
    const float scaleValue = 1.0f / totalAmplitude;
    return Vec3{(t2x - t1y) * scaleValue, (t0x - t2y) * scaleValue, (t1x - t0y) * scaleValue};
}

std::uint32_t burstOccurrences(const Burst& burst, double time) noexcept {
    if (time < static_cast<double>(burst.time)) {
        return 0u;
    }
    if (!burst.period.has_value() || !(*burst.period > 0.0f)) {
        return 1u;
    }
    const double period = static_cast<double>(*burst.period);
    const double raw = 1.0 + std::floor((time - static_cast<double>(burst.time)) / period +
                                       kSpawnerBoundaryEpsilon);
    const double capped = std::min(raw, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
    std::uint32_t occurrences = static_cast<std::uint32_t>(std::max(0.0, capped));
    if (burst.repeat.has_value()) {
        occurrences = std::min(occurrences, *burst.repeat);
    }
    return occurrences;
}

Vec3 randomCone(const Vec3& axis, float angle, std::uint32_t seed) {
    const Vec3 normalizedAxis = normalize(axis);
    const Vec3 reference = std::abs(normalizedAxis.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f}
                                                              : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 tangent = normalize(cross(normalizedAxis, reference));
    const Vec3 bitangent = cross(normalizedAxis, tangent);
    const float cosMax = std::cos(std::max(angle, 0.0f) * 3.14159265359f / 180.0f);
    const float z = lerp(cosMax, 1.0f, vfxRand(seed));
    const float azimuth = vfxRand(seed + 0x9e3779b9u) * kTwoPi;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return normalize(add(add(scale(tangent, std::cos(azimuth) * radius),
                             scale(bitangent, std::sin(azimuth) * radius)),
                         scale(normalizedAxis, z)));
}

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool valueFloat(const Value& value, float& out) {
    if (value.kind != Value::Kind::Number) {
        return false;
    }
    out = static_cast<float>(value.number);
    return true;
}

bool valueFloat3(const Value& value, Vec3& out) {
    if (value.kind == Value::Kind::Array && value.array.size() >= 3u) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!valueFloat(value.array[0], x) || !valueFloat(value.array[1], y) ||
            !valueFloat(value.array[2], z)) {
            return false;
        }
        out = Vec3{x, y, z};
        return true;
    }
    if (value.kind == Value::Kind::Object) {
        const Value* x = value.find("x");
        const Value* y = value.find("y");
        const Value* z = value.find("z");
        float fx = 0.0f;
        float fy = 0.0f;
        float fz = 0.0f;
        if (x == nullptr || y == nullptr || z == nullptr || !valueFloat(*x, fx) ||
            !valueFloat(*y, fy) || !valueFloat(*z, fz)) {
            return false;
        }
        out = Vec3{fx, fy, fz};
        return true;
    }
    return false;
}

bool requireFloat(const Block& block, std::string_view name, float& out, const std::string& path,
                  std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr || !valueFloat(*value, out)) {
        return fail(error, path + "." + std::string(name) + ": expected a float");
    }
    return true;
}

bool requireFloat3(const Block& block, std::string_view name, Vec3& out, const std::string& path,
                   std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr || !valueFloat3(*value, out)) {
        return fail(error, path + "." + std::string(name) + ": expected a float3");
    }
    return true;
}

bool optionalFloat(const Block& block, std::string_view name, float& out, const std::string& path,
                   std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr) {
        return true;
    }
    if (!valueFloat(*value, out)) {
        return fail(error, path + "." + std::string(name) + ": expected a float");
    }
    return true;
}

bool optionalFloat3(const Block& block, std::string_view name, Vec3& out, const std::string& path,
                    std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr) {
        return true;
    }
    if (!valueFloat3(*value, out)) {
        return fail(error, path + "." + std::string(name) + ": expected a float3");
    }
    return true;
}

bool optionalBool(const Block& block, std::string_view name, bool& out, const std::string& path,
                  std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr) {
        return true;
    }
    if (value->kind != Value::Kind::Bool) {
        return fail(error, path + "." + std::string(name) + ": expected a bool");
    }
    out = value->boolean;
    return true;
}

bool optionalInt(const Block& block, std::string_view name, int& out, const std::string& path,
                 std::string* error) {
    const Value* value = block.parameter(name);
    if (value == nullptr) {
        return true;
    }
    if (value->kind != Value::Kind::Number || std::floor(value->number) != value->number) {
        return fail(error, path + "." + std::string(name) + ": expected an integer");
    }
    out = static_cast<int>(value->number);
    return true;
}

bool parseRelativeMode(const Block& block, bool defaultRelative, bool& out,
                       const std::string& path, std::string* error) {
    out = defaultRelative;
    const Value* value = block.parameter("mode");
    if (value == nullptr) {
        return true;
    }
    if (value->kind != Value::Kind::String ||
        (value->string != "absolute" && value->string != "relative")) {
        return fail(error, path + ".mode: expected 'absolute' or 'relative'");
    }
    out = value->string == "relative";
    return true;
}

bool applyInitBlock(const Block& block, const std::string& path, ParticleState& state,
                    std::string* error) {
    const std::uint32_t seed = state.seed ^ blockSeed(path, block.type);
    if (block.type == "set_position_box") {
        Vec3 center;
        Vec3 size;
        if (!requireFloat3(block, "center", center, path, error) ||
            !requireFloat3(block, "size", size, path, error)) {
            return false;
        }
        const Vec3 random = random4(seed);
        state.position = add(center, scale(multiply(Vec3{random.x * 2.0f - 1.0f,
                                                         random.y * 2.0f - 1.0f,
                                                         random.z * 2.0f - 1.0f},
                                                  size),
                                         0.5f));
        return true;
    }
    if (block.type == "set_position_sphere") {
        Vec3 center;
        float radius = 0.0f;
        if (!requireFloat3(block, "center", center, path, error) ||
            !requireFloat(block, "radius", radius, path, error)) {
            return false;
        }
        const Vec3 direction = randomUnitVector(seed);
        const Value* surface = block.parameter("surface");
        if (surface != nullptr && surface->kind == Value::Kind::Bool && surface->boolean) {
            state.position = add(center, scale(direction, radius));
        } else {
            const float radial = radius * std::pow(vfxRand(seed ^ 0x9e3779b9u), 1.0f / 3.0f);
            state.position = add(center, scale(direction, radial));
        }
        return true;
    }
    if (block.type == "set_velocity_random") {
        float minSpeed = 0.0f;
        float maxSpeed = 0.0f;
        if (!requireFloat(block, "minSpeed", minSpeed, path, error) ||
            !requireFloat(block, "maxSpeed", maxSpeed, path, error)) {
            return false;
        }
        const float speed = lerp(minSpeed, maxSpeed, vfxRand(seed ^ 0x3c6ef372u));
        const Value* directionValue = block.parameter("direction");
        if (directionValue == nullptr) {
            state.velocity = scale(randomUnitVector(seed), speed);
            return true;
        }
        Vec3 direction;
        if (!valueFloat3(*directionValue, direction)) {
            return fail(error, path + ".direction: expected a float3");
        }
        const Value* coneValue = block.parameter("coneAngle");
        if (coneValue == nullptr) {
            state.velocity = scale(normalize(direction), speed);
            return true;
        }
        float coneAngle = 0.0f;
        if (!valueFloat(*coneValue, coneAngle)) {
            return fail(error, path + ".coneAngle: expected a float");
        }
        state.velocity = scale(randomCone(direction, coneAngle, seed), speed);
        return true;
    }
    if (block.type == "set_velocity_direction") {
        Vec3 direction;
        float speed = 0.0f;
        if (!requireFloat3(block, "direction", direction, path, error) ||
            !requireFloat(block, "speed", speed, path, error)) {
            return false;
        }
        state.velocity = scale(normalize(direction), speed);
        return true;
    }
    if (block.type == "set_color") {
        Vec3 color;
        if (!requireFloat3(block, "color", color, path, error)) {
            return false;
        }
        state.color = color;
        return true;
    }
    if (block.type == "set_alpha") {
        return requireFloat(block, "alpha", state.alpha, path, error);
    }
    if (block.type == "set_size") {
        return requireFloat(block, "size", state.size, path, error);
    }
    if (block.type == "set_lifetime") {
        return requireFloat(block, "lifetime", state.lifetime, path, error);
    }
    if (block.type == "set_angle") {
        return requireFloat(block, "angle", state.angle, path, error);
    }
    if (block.type == "set_scale") {
        return requireFloat3(block, "scale", state.scale, path, error);
    }
    return fail(error, path + ".type: unsupported reference init block '" + block.type + "'");
}

bool applyUpdateBlock(const Block& block, const std::string& path, float deltaSeconds, float time,
                      ParticleState& state, std::string* error) {
    if (block.type == "integrate") {
        state.position = add(state.position, scale(state.velocity, deltaSeconds));
        return true;
    }
    if (block.type == "age") {
        state.age += deltaSeconds;
        return true;
    }
    if (block.type == "reap") {
        if (block.parameter("condition") != nullptr) {
            return fail(error, path + ".condition: reference expressions are not supported");
        }
        if (state.age >= state.lifetime) {
            state.alive = false;
        }
        return true;
    }
    if (block.type == "gravity") {
        Vec3 gravity;
        if (!requireFloat3(block, "gravity", gravity, path, error)) {
            return false;
        }
        state.velocity = add(state.velocity, scale(gravity, deltaSeconds));
        return true;
    }
    if (block.type == "drag") {
        float drag = 0.5f;
        bool useSize = false;
        if (!optionalFloat(block, "drag", drag, path, error) ||
            !optionalBool(block, "useSize", useSize, path, error)) {
            return false;
        }
        if (useSize) {
            drag *= state.size * state.size * state.scale.x * state.scale.y;
        }
        state.velocity = scale(state.velocity, std::exp(-drag * deltaSeconds));
        return true;
    }
    if (block.type == "force") {
        Vec3 force;
        if (!requireFloat3(block, "force", force, path, error)) {
            return false;
        }
        bool relative = false;
        float drag = 1.0f;
        if (!parseRelativeMode(block, false, relative, path, error) ||
            !optionalFloat(block, "drag", drag, path, error)) {
            return false;
        }
        if (relative) {
            const Vec3 delta = sub(force, state.velocity);
            state.velocity = add(state.velocity,
                                 scale(delta, std::min(1.0f, drag * deltaSeconds)));
        } else {
            state.velocity = add(state.velocity, scale(force, deltaSeconds));
        }
        return true;
    }
    if (block.type == "attractor") {
        Vec3 position;
        float strength = 0.0f;
        if (!requireFloat3(block, "position", position, path, error) ||
            !requireFloat(block, "strength", strength, path, error)) {
            return false;
        }
        float falloff = 1.0f;
        const Value* falloffValue = block.parameter("falloff");
        if (falloffValue != nullptr && !valueFloat(*falloffValue, falloff)) {
            return fail(error, path + ".falloff: expected a float");
        }
        const Vec3 delta = sub(state.position, position);
        const float distanceSquared = std::max(dot(delta, delta), 1.0e-4f);
        const Vec3 direction = scale(delta, 1.0f / std::sqrt(distanceSquared));
        state.velocity = add(state.velocity,
                             scale(direction, (strength / (distanceSquared + falloff)) * deltaSeconds));
        return true;
    }
    if (block.type == "turbulence") {
        float intensity = 1.0f;
        float frequency = 1.0f;
        float roughness = 0.5f;
        float lacunarity = 2.0f;
        float drag = 1.0f;
        int octaves = 1;
        Vec3 scroll{};
        bool relative = true;
        if (!optionalFloat(block, "intensity", intensity, path, error) ||
            !optionalFloat(block, "frequency", frequency, path, error) ||
            !optionalFloat(block, "roughness", roughness, path, error) ||
            !optionalFloat(block, "lacunarity", lacunarity, path, error) ||
            !optionalFloat(block, "drag", drag, path, error) ||
            !optionalInt(block, "octaves", octaves, path, error) ||
            !optionalFloat3(block, "scroll", scroll, path, error) ||
            !parseRelativeMode(block, true, relative, path, error)) {
            return false;
        }
        const Vec3 fieldPosition = add(add(state.position, scale(scroll, time)),
                                       Vec3{0.5f, 0.5f, 0.5f});
        const Vec3 value = scale(curlNoise3D(fieldPosition, frequency, octaves, roughness, lacunarity),
                                 intensity);
        if (relative) {
            const Vec3 delta = sub(value, state.velocity);
            state.velocity = add(state.velocity,
                                 scale(delta, std::min(1.0f, drag * deltaSeconds)));
        } else {
            state.velocity = add(state.velocity, scale(value, deltaSeconds));
        }
        return true;
    }
    return fail(error, path + ".type: unsupported reference update block '" + block.type + "'");
}

} // namespace

void resetSpawner(const SpawnerDesc& spawner, SpawnerState& state) {
    state = SpawnerState{};
    state.burstFired.assign(spawner.bursts.size(), 0u);
}

std::uint32_t updateSpawner(const SpawnerDesc& spawner, float deltaSeconds, SpawnerState& state) {
    const float clamped = std::clamp(deltaSeconds, 0.0f, kMaxDeltaSeconds);
    const double delta = std::isfinite(clamped) ? static_cast<double>(clamped) : 0.0;
    state.time += delta;
    state.accumulator += static_cast<double>(spawner.rate) * delta;
    const double accumulated = std::floor(state.accumulator);
    state.accumulator -= accumulated;

    const double maxSpawn = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    std::uint64_t spawnCount = static_cast<std::uint64_t>(std::min(std::max(accumulated, 0.0), maxSpawn));
    if (state.burstFired.size() != spawner.bursts.size()) {
        state.burstFired.assign(spawner.bursts.size(), 0u);
    }
    for (std::size_t burstIndex = 0; burstIndex < spawner.bursts.size(); ++burstIndex) {
        const Burst& burst = spawner.bursts[burstIndex];
        std::uint32_t& fired = state.burstFired[burstIndex];
        const std::uint32_t occurrences = burstOccurrences(burst, state.time);
        if (occurrences > fired) {
            const std::uint64_t additional =
                static_cast<std::uint64_t>(occurrences - fired) * static_cast<std::uint64_t>(burst.count);
            const std::uint64_t maximum = std::numeric_limits<std::uint32_t>::max();
            spawnCount = additional >= maximum - spawnCount ? maximum : spawnCount + additional;
            fired = occurrences;
        }
    }
    return static_cast<std::uint32_t>(spawnCount);
}

bool simulateParticle(const Graph& graph, std::uint32_t systemIndex, std::uint32_t spawnIndex,
                      std::uint32_t systemSeed, float deltaSeconds, std::uint32_t frameCount,
                      ParticleState& out, std::string* error) {
    if (systemIndex >= graph.systems.size()) {
        return fail(error, "reference system index is out of range");
    }
    const SystemDesc& system = graph.systems[systemIndex];
    out = ParticleState{};
    out.spawnIndex = spawnIndex;
    out.particleId = spawnIndex;
    out.seed = wangHash(spawnIndex ^ systemSeed);
    for (std::size_t blockIndex = 0; blockIndex < system.initBlocks.size(); ++blockIndex) {
        const Block& block = system.initBlocks[blockIndex];
        if (!block.enabled) {
            continue;
        }
        const std::string path = "systems[" + std::to_string(systemIndex) + "].init.blocks[" +
                                 std::to_string(blockIndex) + "]";
        if (!applyInitBlock(block, path, out, error)) {
            return false;
        }
    }
    const float delta = std::clamp(deltaSeconds, 0.0f, kMaxDeltaSeconds);
    float time = 0.0f;
    for (std::uint32_t frame = 0; frame < frameCount && out.alive; ++frame) {
        time += delta;
        for (std::size_t blockIndex = 0; blockIndex < system.updateBlocks.size(); ++blockIndex) {
            const Block& block = system.updateBlocks[blockIndex];
            if (!block.enabled) {
                continue;
            }
            const std::string path = "systems[" + std::to_string(systemIndex) + "].update.blocks[" +
                                     std::to_string(blockIndex) + "]";
            if (!applyUpdateBlock(block, path, delta, time, out, error)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace vfx::reference
