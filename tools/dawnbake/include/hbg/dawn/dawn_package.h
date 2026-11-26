#pragma once

#include "hbg/dawn/dawn_protocol.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::dawn {

inline constexpr int32_t kDawnFileVersion = 2;

struct LightmapSidecarAtlas {
    std::string file;
    uint32_t size = 0;
    uint32_t layers = 3;
    std::string format = "rgba16f";
};

struct LightmapSidecarObject {
    std::string name;
    uint32_t atlas = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t size = 0;
    float scale = 1.0f;
    bool contributeGI = true;
};

struct LightmapSidecar {
    uint32_t version = 1;
    float texelPerUnit = 6.0f;
    std::vector<LightmapSidecarAtlas> atlases;
    std::vector<LightmapSidecarObject> objects;
};

struct PackageFiles {
    std::filesystem::path sceneGz;
    std::filesystem::path jobsGz;
    std::filesystem::path scene;
    std::filesystem::path jobs;
    std::filesystem::path lightmap;
    std::vector<std::filesystem::path> meshGz;
    std::vector<std::filesystem::path> materialGz;
    std::size_t sceneBytes = 0;
    std::size_t jobBytes = 0;
};

std::string formatGuid(const Guid& guid);
std::string formatShaHash(const ShaHash& hash);
std::string lightmapResultFileName(const Guid& guid);
std::string shadowmaskResultFileName(const Guid& guid);
std::string lightmapAtlasName(uint32_t atlasIndex);
std::string lightmapAtlasDdsFileName(uint32_t atlasIndex, uint32_t shChannel);
std::string meshFileName(const Guid& guid, uint32_t lodIndex, uint32_t index);
std::string materialFileName(const ShaHash& hash, uint32_t index);

PackageFiles writeDawnPackage(const std::filesystem::path& directory, const std::string& sceneGuid,
                              const SceneInfo& scene, const BakingJobInputs& jobs,
                              const LightmapSidecar* lightmap = nullptr);

void mergeLightmapSidecar(const std::filesystem::path& sceneSidecar, const std::filesystem::path& lightmapSidecar,
                          const std::filesystem::path& output);

LightmapSidecar readLightmapSidecar(const std::filesystem::path& path);

} // namespace hbg::dawn
