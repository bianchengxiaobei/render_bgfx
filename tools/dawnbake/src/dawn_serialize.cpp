#include "hbg/dawn/dawn_serialize.h"

#include "hbg/dawn/dawn_gzip.h"

#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace hbg::dawn {
namespace {

template <typename T>
void writeTexture2D(Writer& writer, const Texture2D<T>& texture) {
    writer.pod(texture.header);
    writer.array(texture.colors);
}

template <typename T>
void readTexture2D(Reader& reader, Texture2D<T>& texture) {
    texture.header = reader.pod<Texture2DInfoHeader>();
    texture.colors = reader.array<T>();
}

template <typename T, typename WriteOne>
void writeCustomArray(Writer& writer, const std::vector<T>& values, WriteOne&& writeOne) {
    writer.i32(static_cast<int32_t>(values.size()));
    for (const T& value : values) {
        writeOne(writer, value);
    }
}

template <typename T, typename ReadOne>
std::vector<T> readCustomArray(Reader& reader, ReadOne&& readOne) {
    const int32_t count = reader.i32();
    if (count < 0) {
        throw std::runtime_error("negative custom array count at offset " + std::to_string(reader.offset()));
    }
    std::vector<T> values;
    values.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        T value{};
        readOne(reader, value);
        values.push_back(std::move(value));
    }
    return values;
}

void writeMaterialInfo(Writer& writer, const MaterialInfo& material) {
    writer.pod(material.header);
    writeTexture2D(writer, material.diffuseTexture);
    writeTexture2D(writer, material.transmissionTexture);
    writeTexture2D(writer, material.emissiveTexture);
    writeTexture2D(writer, material.normalTexture);
    writeTexture2D(writer, material.specularTexture);
}

void readMaterialInfo(Reader& reader, MaterialInfo& material) {
    material.header = reader.pod<MaterialInfoHeader>();
    readTexture2D(reader, material.diffuseTexture);
    readTexture2D(reader, material.transmissionTexture);
    readTexture2D(reader, material.emissiveTexture);
    readTexture2D(reader, material.normalTexture);
    readTexture2D(reader, material.specularTexture);
}

void writeMeshInfo(Writer& writer, const MeshInfo& mesh, int32_t version) {
    writer.pod(mesh.header);
    writer.array(mesh.index16);
    writer.array(mesh.elements);
    writer.array(mesh.vertices);
    writer.array(mesh.normals);
    writer.array(mesh.tangents);
    writer.array(mesh.bitangents);
    writer.i32(static_cast<int32_t>(mesh.texcoords.size()));
    for (const auto& buffer : mesh.texcoords) {
        writer.array(buffer);
    }
    if (version >= kExportVersion1001 && mesh.index16.empty() && !mesh.vertices.empty()) {
        writer.array(mesh.index32);
    }
}

void readMeshInfo(Reader& reader, MeshInfo& mesh, int32_t version) {
    mesh.header = reader.pod<MeshInfoHeader>();
    mesh.index16 = reader.array<uint16_t>();
    mesh.elements = reader.array<MeshElementInfo>();
    mesh.vertices = reader.array<Float4>();
    mesh.normals = reader.array<Float4>();
    mesh.tangents = reader.array<Float4>();
    mesh.bitangents = reader.array<Float4>();

    const int32_t texcoordCount = reader.i32();
    if (texcoordCount < 0) {
        throw std::runtime_error("negative texcoord buffer count");
    }
    mesh.texcoords.clear();
    mesh.texcoords.reserve(static_cast<size_t>(texcoordCount));
    for (int32_t i = 0; i < texcoordCount; ++i) {
        mesh.texcoords.push_back(reader.array<Float2>());
    }

    mesh.index32.clear();
    if (version >= kExportVersion1001 && mesh.index16.empty() && !mesh.vertices.empty()) {
        mesh.index32 = reader.array<uint32_t>();
    }
}

void writeMeshInstanceInfo(Writer& writer, const MeshInstanceInfo& instance) {
    writer.pod(instance.header);
    writer.array(instance.materialOverrides);
    writer.array(instance.relevantLights);
}

void readMeshInstanceInfo(Reader& reader, MeshInstanceInfo& instance) {
    instance.header = reader.pod<MeshInstanceInfoHeader>();
    instance.materialOverrides = reader.array<uint32_t>();
    instance.relevantLights = reader.array<uint32_t>();
}

void writeLandscapeInfo(Writer& writer, const LandscapeInfo& landscape) {
    writer.pod(landscape.header);
    writer.array(landscape.materialOverrides);
    writer.array(landscape.relevantLights);
    writer.array(landscape.heightMap);
    writer.array(landscape.holeMask);
}

void readLandscapeInfo(Reader& reader, LandscapeInfo& landscape) {
    landscape.header = reader.pod<LandscapeInfoHeader>();
    landscape.materialOverrides = reader.array<uint32_t>();
    landscape.relevantLights = reader.array<uint32_t>();
    landscape.heightMap = reader.array<UByte4>();
    landscape.holeMask = reader.array<uint8_t>();
}

void writeScenePartitionInfo(Writer& writer, const ScenePartitionInfo& partition) {
    writer.pod(partition.scenePartitionGuid);
    writer.array(partition.materialIndices);
    writer.array(partition.meshIndices);
    writer.array(partition.meshInstanceIndices);
    writer.array(partition.extendMeshInstanceIndices);
    writer.array(partition.landscapeIndices);
    writer.array(partition.lightIndices);
    writer.array(partition.globalToLocalMaterialIndices);
    writer.array(partition.globalToLocalMeshIndices);
    writer.array(partition.detailVolumeLightProbeJobIndices);
    writer.array(partition.importantVolumeLightProbeJobIndices);
    writer.array(partition.customLightProbeJobIndices);
    writer.array(partition.volumetricLightProbeJobIndices);
    // C# FScenePartitionInfo.Serialize intentionally omits CustomVolumetric...
}

void readScenePartitionInfo(Reader& reader, ScenePartitionInfo& partition) {
    partition.scenePartitionGuid = reader.pod<Guid>();
    partition.materialIndices = reader.array<int32_t>();
    partition.meshIndices = reader.array<int32_t>();
    partition.meshInstanceIndices = reader.array<int32_t>();
    partition.extendMeshInstanceIndices = reader.array<int32_t>();
    partition.landscapeIndices = reader.array<int32_t>();
    partition.lightIndices = reader.array<int32_t>();
    partition.globalToLocalMaterialIndices = reader.array<int32_t>();
    partition.globalToLocalMeshIndices = reader.array<int32_t>();
    partition.detailVolumeLightProbeJobIndices = reader.array<int32_t>();
    partition.importantVolumeLightProbeJobIndices = reader.array<int32_t>();
    partition.customLightProbeJobIndices = reader.array<int32_t>();
    partition.volumetricLightProbeJobIndices = reader.array<int32_t>();
}

void writeSceneNoPartitionInfo(Writer& writer, const SceneNoPartitionInfo& info) {
    writer.array(info.meshIndices);
    writer.array(info.meshGuid);
    writer.array(info.meshLodIndex);
    writer.array(info.materialIndices);
    writer.array(info.materialHash);
    writer.array(info.landscapeIndices);
    writer.array(info.landscapeGuid);
    writer.array(info.lightIndices);
    writer.array(info.lights);
    writer.array(info.meshInstanceIndices);
    writeCustomArray(writer, info.meshInstances, [](Writer& w, const MeshInstanceInfo& value) {
        writeMeshInstanceInfo(w, value);
    });
}

void readSceneNoPartitionInfo(Reader& reader, SceneNoPartitionInfo& info) {
    info.meshIndices = reader.array<int32_t>();
    info.meshGuid = reader.array<Guid>();
    info.meshLodIndex = reader.array<uint32_t>();
    info.materialIndices = reader.array<int32_t>();
    info.materialHash = reader.array<ShaHash>();
    info.landscapeIndices = reader.array<int32_t>();
    info.landscapeGuid = reader.array<Guid>();
    info.lightIndices = reader.array<int32_t>();
    info.lights = reader.array<LightInfo>();
    info.meshInstanceIndices = reader.array<int32_t>();
    info.meshInstances = readCustomArray<MeshInstanceInfo>(reader, [](Reader& r, MeshInstanceInfo& value) {
        readMeshInstanceInfo(r, value);
    });
}

}  // namespace

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("failed to open " + path.string());
    }
    const std::streamsize size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to size " + path.string());
    }
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return bytes;
}

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    errno = 0;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        std::string message = "failed to open " + path.string();
        if (errno == ENOSPC) {
            message += " (disk full)";
        }
        throw std::runtime_error(message);
    }
    if (!bytes.empty()) {
        errno = 0;
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
    }
    if (!stream) {
        std::string message = "failed to write " + path.string();
        if (errno == ENOSPC) {
            message += " (disk full)";
        }
        throw std::runtime_error(message);
    }
}

namespace {

void requireSupportedVersion(int32_t version, const char* kind) {
    if (version < kExportVersion1000 || version > kExportVersion) {
        throw std::runtime_error(std::string(kind) + " version " + std::to_string(version) +
                                 " is not supported (expected " + std::to_string(kExportVersion1000) + ".." +
                                 std::to_string(kExportVersion) + ")");
    }
}

}  // namespace

void writeMesh(Writer& writer, const MeshInfo& mesh, int32_t version) {
    writeMeshInfo(writer, mesh, version);
}

void readMesh(Reader& reader, MeshInfo& mesh, int32_t version) {
    readMeshInfo(reader, mesh, version);
}

void writeMaterial(Writer& writer, const MaterialInfo& material, int32_t version) {
    (void)version;
    writeMaterialInfo(writer, material);
}

void readMaterial(Reader& reader, MaterialInfo& material, int32_t version) {
    (void)version;
    readMaterialInfo(reader, material);
}

void writeScene(Writer& writer, const SceneInfo& scene, int32_t version) {
    writer.pod(scene.header);
    writer.array(scene.importanceVolumes);
    writeCustomArray(writer, scene.densityVolumes, [](Writer& w, const DensityVolume& volume) {
        w.array(volume.planes);
        w.pod(volume.allowedMipLevelRange);
    });
    writer.array(scene.detailVolumes);
    writer.pod(scene.skyLight.skyData);
    writer.array(scene.skyLight.envCube);
    writer.array(scene.lights);
    writer.array(scene.lightGuids);
    writer.array(scene.lightPowers);
    writeCustomArray(writer, scene.iesTextureDatas, [](Writer& w, const Texture2D<float>& texture) {
        writeTexture2D(w, texture);
    });
    writeCustomArray(writer, scene.cookieTextureDatas, [](Writer& w, const Texture2D<Float4>& texture) {
        writeTexture2D(w, texture);
    });
    writeCustomArray(writer, scene.materials, [](Writer& w, const MaterialInfo& material) {
        writeMaterialInfo(w, material);
    });
    writeCustomArray(writer, scene.meshes, [version](Writer& w, const MeshInfo& mesh) {
        writeMeshInfo(w, mesh, version);
    });
    writeCustomArray(writer, scene.meshInstances, [](Writer& w, const MeshInstanceInfo& instance) {
        writeMeshInstanceInfo(w, instance);
    });
    writer.array(scene.meshGuids);
    writeCustomArray(writer, scene.landscapes, [](Writer& w, const LandscapeInfo& landscape) {
        writeLandscapeInfo(w, landscape);
    });
    writeCustomArray(writer, scene.scenePartitions, [](Writer& w, const ScenePartitionInfo& partition) {
        writeScenePartitionInfo(w, partition);
    });
    writeSceneNoPartitionInfo(writer, scene.sceneNoPartitionInfo);
}

void readScene(Reader& reader, SceneInfo& scene, int32_t version) {
    scene.header = reader.pod<SceneInfoHeader>();
    scene.importanceVolumes = reader.array<SceneBoxBounds>();
    scene.densityVolumes = readCustomArray<DensityVolume>(reader, [](Reader& r, DensityVolume& volume) {
        volume.planes = r.array<Float4>();
        volume.allowedMipLevelRange = r.pod<Int2>();
    });
    scene.detailVolumes = reader.array<SceneDetailVolume>();
    scene.skyLight.skyData = reader.pod<SkyLightInfoHeader>();
    scene.skyLight.envCube = reader.array<Float4>();
    scene.lights = reader.array<LightInfo>();
    scene.lightGuids = reader.array<Guid>();
    scene.lightPowers = reader.array<float>();
    scene.iesTextureDatas = readCustomArray<Texture2D<float>>(reader, [](Reader& r, Texture2D<float>& texture) {
        readTexture2D(r, texture);
    });
    scene.cookieTextureDatas = readCustomArray<Texture2D<Float4>>(reader, [](Reader& r, Texture2D<Float4>& texture) {
        readTexture2D(r, texture);
    });
    scene.materials = readCustomArray<MaterialInfo>(reader, [](Reader& r, MaterialInfo& material) {
        readMaterialInfo(r, material);
    });
    scene.meshes = readCustomArray<MeshInfo>(reader, [version](Reader& r, MeshInfo& mesh) {
        readMeshInfo(r, mesh, version);
    });
    scene.meshInstances = readCustomArray<MeshInstanceInfo>(reader, [](Reader& r, MeshInstanceInfo& instance) {
        readMeshInstanceInfo(r, instance);
    });
    scene.meshGuids = reader.array<Guid>();
    scene.landscapes = readCustomArray<LandscapeInfo>(reader, [](Reader& r, LandscapeInfo& landscape) {
        readLandscapeInfo(r, landscape);
    });
    scene.scenePartitions = readCustomArray<ScenePartitionInfo>(reader, [](Reader& r, ScenePartitionInfo& partition) {
        readScenePartitionInfo(r, partition);
    });
    readSceneNoPartitionInfo(reader, scene.sceneNoPartitionInfo);
}

void writeBakingJobInputs(Writer& writer, const BakingJobInputs& jobs, int32_t version) {
    writer.pod(jobs.bakingParameters);
    writer.pod(jobs.adaptiveVolumetricLightmapParameter);
    writer.pod(jobs.sparseVolumetricSamplesParameter);
    writer.pod(jobs.shadowSettingParameter);
    writer.pod(jobs.precomputedVisibilityParameter.header);
    writer.array(jobs.precomputedVisibilityParameter.cameraTrackPositions);

    writer.pod(jobs.debugInput.debugInput100);
    if (version >= kExportVersion1001) {
        writer.pod(jobs.debugInput.cameraPosition);
        writer.pod(jobs.debugInput.lookatPosition);
    }

    writer.array(jobs.lightmap2DJobs);
    writeCustomArray(writer, jobs.sdfShadowJobs, [](Writer& w, const SdfShadowInput& input) {
        w.pod(input.header);
        w.array(input.lights);
        w.array(input.shadowmaskLights);
    });
    writeCustomArray(writer, jobs.sdfShadowGroupJobs, [](Writer& w, const SdfShadowGroupInput& group) {
        w.pod(group.jobId);
        w.i32(group.groupSize);
        w.i32(group.upsampleFactor);
        w.pod(group.lightId);
        w.array(group.shadowJobIndices);
        w.array(group.mappingOffsets);
        w.i32(group.scenePartitionIndex);
    });
    writer.u32(jobs.shadowDepthMapJobsCount);
    writer.u32(jobs.customLightProbeJobsCount);
    writer.u32(jobs.importanceVolumeLightProbeJobsCount);
    writer.u32(jobs.detailVolumeLightProbeJobsCount);
    writer.u32(jobs.visibilityBucketGuidsCount);
    writer.u32(jobs.volumetricLightmapTaskGuidsCount);
    writer.u32(jobs.customVolumetricLightmapJobsCount);
    writer.u32(static_cast<uint32_t>(jobs.mappingGroupJobs.size()));
    for (const MappingGroupInput& group : jobs.mappingGroupJobs) {
        writer.pod(group.jobId);
        writer.i32(group.groupedSize);
        writer.i32(group.scenePartitionIndex);
        writer.i32(static_cast<int32_t>(group.mappingOffsets.size()));
        writer.podArray(group.mappingOffsets);
    }
    writer.u32(static_cast<uint32_t>(jobs.mappingSplitJobs.size()));
    for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
        writer.pod(split.jobId);
        writer.i32(split.splitSize);
        writer.i32(split.overlapSize);
        writer.i32(split.scenePartitionIndex);
        writer.pod(split.mappingOffsets);
    }
    writer.u32(jobs.seamFixGroupJobsCount);
    writer.u32(jobs.scenePartitionTasksCount);
    writer.pod(jobs.partialJobsTotalNum);
    writer.u32(jobs.lightmapOcclusionJobsCount);
    writer.u32(jobs.occlusionGroupJobsCount);
    if (version >= kExportVersion1001) {
        writer.u32(jobs.textureMappingJobsCount);
    }
}

void readBakingJobInputs(Reader& reader, BakingJobInputs& jobs, int32_t version) {
    jobs.bakingParameters = reader.pod<BakingJobParameters>();
    jobs.adaptiveVolumetricLightmapParameter = reader.pod<AdaptiveVolumetricLightmapParams>();
    jobs.sparseVolumetricSamplesParameter = reader.pod<SparseVolumetricSamplesParams>();
    jobs.shadowSettingParameter = reader.pod<ShadowSettingParams>();
    jobs.precomputedVisibilityParameter.header = reader.pod<PrecomputedVisibilityParameterHeader>();
    jobs.precomputedVisibilityParameter.cameraTrackPositions = reader.array<Float3>();

    jobs.debugInput.debugInput100 = reader.pod<BakingDebugInput100>();
    if (version >= kExportVersion1001) {
        jobs.debugInput.cameraPosition = reader.pod<Float3>();
        jobs.debugInput.lookatPosition = reader.pod<Float3>();
    }

    jobs.lightmap2DJobs = reader.array<Lightmap2DInput>();

    const auto expectZero = [&reader](const char* name) {
        const uint32_t count = reader.u32();
        if (count != 0) {
            throw std::runtime_error(std::string(name) + " is not empty (count=" + std::to_string(count) +
                                     ") at offset " + std::to_string(reader.offset()));
        }
    };
    jobs.sdfShadowJobs = readCustomArray<SdfShadowInput>(reader, [](Reader& r, SdfShadowInput& input) {
        input.header = r.pod<SdfShadowInputHeader>();
        input.lights = r.array<Guid>();
        input.shadowmaskLights = r.array<Guid>();
    });
    jobs.sdfShadowJobsCount = static_cast<uint32_t>(jobs.sdfShadowJobs.size());
    jobs.sdfShadowGroupJobs = readCustomArray<SdfShadowGroupInput>(reader, [](Reader& r, SdfShadowGroupInput& group) {
        group.jobId = r.pod<Guid>();
        group.groupSize = r.i32();
        group.upsampleFactor = r.i32();
        group.lightId = r.pod<Guid>();
        group.shadowJobIndices = r.array<int32_t>();
        group.mappingOffsets = r.array<Int3>();
        group.scenePartitionIndex = r.i32();
    });
    jobs.sdfShadowGroupJobsCount = static_cast<uint32_t>(jobs.sdfShadowGroupJobs.size());
    expectZero("ShadowDepthMapJobs");
    expectZero("CustomLightProbeJobs");
    expectZero("ImportanceVolumeLightProbeJobs");
    expectZero("DetailVolumeLightProbeJobs");
    expectZero("VisibilityBucketGuids");
    expectZero("VolumetricLightmapTaskGuids");
    expectZero("CustomVolumetricLightmapJobs");
    jobs.mappingGroupJobsCount = reader.u32();
    if (jobs.mappingGroupJobsCount != 0u) {
        jobs.mappingGroupJobs.resize(jobs.mappingGroupJobsCount);
        for (MappingGroupInput& group : jobs.mappingGroupJobs) {
            group.jobId = reader.pod<Guid>();
            group.groupedSize = reader.i32();
            group.scenePartitionIndex = reader.i32();
            group.mappingOffsets = reader.array<Int3>();
        }
    }
    jobs.mappingSplitJobsCount = reader.u32();
    if (jobs.mappingSplitJobsCount != 0u) {
        jobs.mappingSplitJobs.resize(jobs.mappingSplitJobsCount);
        for (MappingSplitInput& split : jobs.mappingSplitJobs) {
            split.jobId = reader.pod<Guid>();
            split.splitSize = reader.i32();
            split.overlapSize = reader.i32();
            split.scenePartitionIndex = reader.i32();
            split.mappingOffsets = reader.pod<Int3>();
        }
    }
    expectZero("SeamFixGroupJobs");
    expectZero("ScenePartitionTasks");

    jobs.partialJobsTotalNum = reader.pod<PartialJobsTotalNum>();
    expectZero("LightmapOcclusionJobs");
    expectZero("OcclusionGroupJobs");
    if (version >= kExportVersion1001) {
        expectZero("TextureMappingJobs");
    }
}

namespace {

std::vector<uint8_t> readDawnObjectBytes(const std::filesystem::path& path) {
    const std::vector<uint8_t> bytes = readFile(path);
    if (bytes.size() >= 2u && bytes[0] == 0x1Fu && bytes[1] == 0x8Bu) {
        return gzipDecompress(bytes);
    }
    return bytes;
}

}  // namespace

SceneInfo readSceneFile(const std::filesystem::path& path) {
    try {
        const std::vector<uint8_t> bytes = readDawnObjectBytes(path);
        Reader reader(bytes);
        const int32_t version = reader.version();
        requireSupportedVersion(version, "scene");
        SceneInfo scene;
        readScene(reader, scene, version);
        if (reader.remaining() != 0u) {
            throw std::runtime_error("trailing bytes at offset " + std::to_string(reader.offset()));
        }
        return scene;
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to read scene " + path.string() + ": " + error.what());
    }
}

BakingJobInputs readBakingJobInputsFile(const std::filesystem::path& path) {
    try {
        const std::vector<uint8_t> bytes = readDawnObjectBytes(path);
        Reader reader(bytes);
        const int32_t version = reader.version();
        requireSupportedVersion(version, "jobs");
        BakingJobInputs jobs;
        readBakingJobInputs(reader, jobs, version);
        if (reader.remaining() != 0u) {
            throw std::runtime_error("trailing bytes at offset " + std::to_string(reader.offset()));
        }
        return jobs;
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to read jobs " + path.string() + ": " + error.what());
    }
}

void writeLightmap2DOutput(Writer& writer, const Lightmap2DOutput& output) {
    writer.pod(output.header);
    writer.i32(output.jobInputIndex);
    writer.u8(output.bHasPrtHqTextures);
    writer.u8(output.bHasPrtLqTextures);
    writer.u8(output.bHasStaticTextures);
    writer.array(output.lights);
    writer.array(output.lightmapData);
}

void readLightmap2DOutput(Reader& reader, Lightmap2DOutput& output) {
    output.header = reader.pod<Lightmap2DOutputHeader>();
    output.jobInputIndex = reader.i32();
    output.bHasPrtHqTextures = reader.u8();
    output.bHasPrtLqTextures = reader.u8();
    output.bHasStaticTextures = reader.u8();
    output.lights = reader.array<Guid>();
    output.lightmapData = reader.array<Lightmap2DSample>();
}

Lightmap2DOutput readLightmap2DOutputFile(const std::filesystem::path& path) {
    try {
        const std::vector<uint8_t> bytes = readDawnObjectBytes(path);
        Reader reader(bytes);
        Lightmap2DOutput output;
        readLightmap2DOutput(reader, output);
        if (reader.remaining() != 0u) {
            throw std::runtime_error("trailing bytes at offset " + std::to_string(reader.offset()));
        }
        return output;
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to read lmgz " + path.string() + ": " + error.what());
    }
}

void writeSdfShadowOutput(Writer& writer, const SdfShadowOutput& output) {
    writer.pod(output.header);
    writeCustomArray(writer, output.shadowMaps, [](Writer& w, const SdfShadowMap& map) {
        w.pod(map.lightId);
        w.array(map.shadowData);
    });
}

void readSdfShadowOutput(Reader& reader, SdfShadowOutput& output) {
    output.header = reader.pod<Lightmap2DOutputHeader>();
    output.shadowMaps = readCustomArray<SdfShadowMap>(reader, [](Reader& r, SdfShadowMap& map) {
        map.lightId = r.pod<Guid>();
        map.shadowData = r.array<SdfShadowSample>();
    });
}

SdfShadowOutput readSdfShadowOutputFile(const std::filesystem::path& path) {
    try {
        const std::vector<uint8_t> bytes = readDawnObjectBytes(path);
        Reader reader(bytes);
        SdfShadowOutput output;
        readSdfShadowOutput(reader, output);
        if (reader.remaining() != 0u) {
            throw std::runtime_error("trailing bytes at offset " + std::to_string(reader.offset()));
        }
        return output;
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to read sdfgz " + path.string() + ": " + error.what());
    }
}

}  // namespace hbg::dawn
