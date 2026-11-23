#pragma once

#include "vfx/graph.h"
#include "vfx/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vfx::reference {

struct SortKVP {
    float key = 0.0f;
    std::uint32_t value = 0;
};

void sortKVP(std::vector<SortKVP>& kvp, SortOrder order);

struct SpawnerState {
    double time = 0.0;
    double accumulator = 0.0;
    std::vector<std::uint32_t> burstFired;
};

void resetSpawner(const SpawnerDesc& spawner, SpawnerState& state);
std::uint32_t updateSpawner(const SpawnerDesc& spawner, float deltaSeconds, SpawnerState& state);

struct ParticleState {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    float size = 0.1f;
    float age = 0.0f;
    float lifetime = 1.0f;
    float angle = 0.0f;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::uint32_t seed = 0;
    std::uint32_t particleId = 0;
    std::uint32_t spawnIndex = 0;
    bool alive = true;
};

bool simulateParticle(const Graph& graph, std::uint32_t systemIndex, std::uint32_t spawnIndex,
                      std::uint32_t systemSeed, float deltaSeconds, std::uint32_t frameCount,
                      ParticleState& out, std::string* error = nullptr);

std::uint32_t simulateEventCount(const Graph& graph, std::uint32_t producerSystem,
                                 float deltaSeconds, std::uint32_t frameCount,
                                 std::string* error = nullptr);

} // namespace vfx::reference
