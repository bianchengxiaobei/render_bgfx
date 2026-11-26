#include "hbg/dawn/dawn_package.h"

#include "hbg/dawn/dawn_gzip.h"
#include "hbg/dawn/dawn_serialize.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hbg::dawn {
namespace {

std::string upperHex(const std::string& text) {
    std::string result = text;
    for (char& value : result) {
        value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    }
    return result;
}

template <typename WriteOne> std::vector<uint8_t> serializeObject(WriteOne&& writeOne) {
    Writer writer;
    writer.version(kExportVersion);
    writeOne(writer);
    return writer.take();
}

using OrderedJson = nlohmann::ordered_json;

OrderedJson readSidecarJson(const std::filesystem::path& path, const char* kind) {
    std::string text;
    try {
        const std::vector<uint8_t> bytes = readFile(path);
        text.assign(bytes.begin(), bytes.end());
    } catch (const std::exception& exception) {
        throw std::runtime_error("failed to read " + std::string(kind) + " " + path.string() + ": " + exception.what());
    }

    OrderedJson root;
    try {
        root = OrderedJson::parse(text);
    } catch (const std::exception& exception) {
        throw std::runtime_error("failed to parse " + std::string(kind) + " " + path.string() + ": " +
                                 exception.what());
    }
    if (!root.is_object()) {
        throw std::runtime_error(std::string(kind) + " root must be an object: " + path.string());
    }
    return root;
}

uint64_t requireUnsignedInteger(const OrderedJson& value, const std::string& name) {
    if (value.is_number_unsigned()) {
        return value.get<uint64_t>();
    }
    if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue >= 0) {
            return static_cast<uint64_t>(signedValue);
        }
    }
    throw std::runtime_error(name + " must be a non-negative integer");
}

double requireNumber(const OrderedJson& value, const std::string& name) {
    if (!value.is_number()) {
        throw std::runtime_error(name + " must be a number");
    }
    return value.get<double>();
}

void validateLightmapSidecarJson(const OrderedJson& lightmap, const std::filesystem::path& path) {
    const std::string prefix = "lightmap sidecar " + path.string() + ": ";
    const auto fail = [&prefix](const std::string& message) { throw std::runtime_error(prefix + message); };

    if (!lightmap.is_object()) {
        fail("lightmap must be an object");
    }

    const auto version = lightmap.find("version");
    if (version == lightmap.end() || requireUnsignedInteger(*version, "lightmap.version") == 0u) {
        fail("version must be a positive integer");
    }
    const auto texelPerUnit = lightmap.find("texelPerUnit");
    if (texelPerUnit == lightmap.end()) {
        fail("missing texelPerUnit");
    }
    if (!(requireNumber(*texelPerUnit, "lightmap.texelPerUnit") > 0.0)) {
        fail("texelPerUnit must be positive");
    }

    const auto atlases = lightmap.find("atlases");
    if (atlases == lightmap.end() || !atlases->is_array()) {
        fail("atlases must be an array");
    }
    std::vector<uint64_t> atlasSizes;
    atlasSizes.reserve(atlases->size());
    for (std::size_t index = 0; index < atlases->size(); ++index) {
        const OrderedJson& atlas = atlases->at(index);
        if (!atlas.is_object()) {
            fail("atlas " + std::to_string(index) + " must be an object");
        }
        const auto file = atlas.find("file");
        if (file == atlas.end() || !file->is_string() || file->get_ref<const std::string&>().empty()) {
            fail("atlas " + std::to_string(index) + " must have a file name");
        }
        const auto size = atlas.find("size");
        if (size == atlas.end()) {
            fail("atlas " + std::to_string(index) + " is missing size");
        }
        const uint64_t atlasSize = requireUnsignedInteger(*size, "atlas.size");
        if (atlasSize == 0u) {
            fail("atlas " + std::to_string(index) + " size must be positive");
        }
        const auto layers = atlas.find("layers");
        if (layers != atlas.end() && requireUnsignedInteger(*layers, "atlas.layers") == 0u) {
            fail("atlas " + std::to_string(index) + " layers must be positive");
        }
        const auto format = atlas.find("format");
        if (format != atlas.end() && !format->is_string()) {
            fail("atlas " + std::to_string(index) + " format must be a string");
        }
        atlasSizes.push_back(atlasSize);
    }

    const auto objects = lightmap.find("objects");
    if (objects == lightmap.end() || !objects->is_object()) {
        fail("objects must be an object");
    }
    for (const auto& [name, object] : objects->items()) {
        if (name.empty()) {
            fail("object name must not be empty");
        }
        if (!object.is_object()) {
            fail("object " + name + " must be an object");
        }
        const auto atlas = object.find("atlas");
        if (atlas == object.end()) {
            fail("object " + name + " is missing atlas");
        }
        const uint64_t atlasIndex = requireUnsignedInteger(*atlas, "object.atlas");
        if (atlasIndex >= atlasSizes.size()) {
            fail("object " + name + " references an invalid atlas");
        }
        const auto rect = object.find("rect");
        if (rect == object.end() || !rect->is_array() || rect->size() != 4u) {
            fail("object " + name + " rect must contain four numbers");
        }
        uint64_t values[4] = {};
        for (std::size_t index = 0; index < 4u; ++index) {
            values[index] = requireUnsignedInteger(rect->at(index), "object.rect");
        }
        if (values[2] == 0u || values[3] == 0u || values[0] + values[2] > atlasSizes[atlasIndex] ||
            values[1] + values[3] > atlasSizes[atlasIndex]) {
            fail("object " + name + " rect is outside its atlas");
        }
        const auto st = object.find("st");
        if (st != object.end()) {
            if (!st->is_array() || st->size() != 4u) {
                fail("object " + name + " st must contain four numbers");
            }
            for (const OrderedJson& value : *st) {
                requireNumber(value, "object.st");
            }
        }
        const auto scale = object.find("scale");
        if (scale != object.end() && !(requireNumber(*scale, "object.scale") > 0.0)) {
            fail("object " + name + " scale must be positive");
        }
        const auto contributeGI = object.find("contributeGI");
        if (contributeGI != object.end() && !contributeGI->is_boolean()) {
            fail("object " + name + " contributeGI must be a boolean");
        }
    }
}

} // namespace

void writeLightmapSidecar(const std::filesystem::path& path, const LightmapSidecar& sidecar) {
    OrderedJson root;
    auto& lightmap = root["lightmap"];
    lightmap["version"] = sidecar.version;
    lightmap["texelPerUnit"] = sidecar.texelPerUnit;

    lightmap["atlases"] = OrderedJson::array();
    for (const LightmapSidecarAtlas& atlas : sidecar.atlases) {
        if (atlas.file.empty() || atlas.size == 0u) {
            throw std::runtime_error("lightmap sidecar atlas must have a file and nonzero size");
        }
        lightmap["atlases"].push_back(
            {{"file", atlas.file}, {"size", atlas.size}, {"layers", atlas.layers}, {"format", atlas.format}});
    }

    lightmap["objects"] = OrderedJson::object();
    std::set<std::string> names;
    for (const LightmapSidecarObject& object : sidecar.objects) {
        if (object.name.empty()) {
            throw std::runtime_error("lightmap sidecar object must have a name");
        }
        if (!names.insert(object.name).second) {
            throw std::runtime_error("lightmap sidecar contains duplicate object: " + object.name);
        }
        if (object.atlas >= sidecar.atlases.size()) {
            throw std::runtime_error("lightmap sidecar object references an invalid atlas: " + object.name);
        }
        const LightmapSidecarAtlas& atlas = sidecar.atlases[object.atlas];
        const uint64_t objectX = static_cast<uint64_t>(object.x) + object.size;
        const uint64_t objectY = static_cast<uint64_t>(object.y) + object.size;
        if (object.size == 0u || objectX > atlas.size || objectY > atlas.size) {
            throw std::runtime_error("lightmap sidecar object rect is outside its atlas: " + object.name);
        }
        if (object.scale <= 0.0f) {
            throw std::runtime_error("lightmap sidecar object has an invalid scale: " + object.name);
        }

        const float atlasSize = static_cast<float>(atlas.size);
        lightmap["objects"][object.name] = {
            {"atlas", object.atlas},
            {"rect", {object.x, object.y, object.size, object.size}},
            {"st",
             {static_cast<float>(object.size) / atlasSize, static_cast<float>(object.size) / atlasSize,
              static_cast<float>(object.x) / atlasSize, static_cast<float>(object.y) / atlasSize}},
            {"scale", object.scale},
            {"contributeGI", object.contributeGI},
        };
    }

    const std::string text = root.dump(2) + "\n";
    writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

LightmapSidecar readLightmapSidecar(const std::filesystem::path& path) {
    const OrderedJson root = readSidecarJson(path, "lightmap sidecar");
    const auto lightmapField = root.find("lightmap");
    const OrderedJson& lightmap = lightmapField != root.end() ? *lightmapField : root;
    validateLightmapSidecarJson(lightmap, path);

    LightmapSidecar sidecar;
    sidecar.version = static_cast<uint32_t>(requireUnsignedInteger(lightmap.at("version"), "lightmap.version"));
    sidecar.texelPerUnit = static_cast<float>(requireNumber(lightmap.at("texelPerUnit"), "lightmap.texelPerUnit"));

    for (std::size_t index = 0; index < lightmap.at("atlases").size(); ++index) {
        const OrderedJson& atlas = lightmap.at("atlases").at(index);
        const uint64_t size = requireUnsignedInteger(atlas.at("size"), "atlas.size");
        if (size > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("lightmap sidecar " + path.string() + ": atlas " + std::to_string(index) +
                                     " size exceeds uint32");
        }
        LightmapSidecarAtlas parsed;
        parsed.file = atlas.at("file").get<std::string>();
        parsed.size = static_cast<uint32_t>(size);
        if (atlas.contains("layers")) {
            const uint64_t layers = requireUnsignedInteger(atlas.at("layers"), "atlas.layers");
            if (layers > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("lightmap sidecar " + path.string() + ": atlas " + std::to_string(index) +
                                         " layers exceeds uint32");
            }
            parsed.layers = static_cast<uint32_t>(layers);
        }
        if (atlas.contains("format")) {
            parsed.format = atlas.at("format").get<std::string>();
        }
        sidecar.atlases.push_back(std::move(parsed));
    }

    for (const auto& [name, object] : lightmap.at("objects").items()) {
        const auto rect = object.at("rect");
        const uint64_t values[4] = {
            requireUnsignedInteger(rect.at(0), "object.rect"),
            requireUnsignedInteger(rect.at(1), "object.rect"),
            requireUnsignedInteger(rect.at(2), "object.rect"),
            requireUnsignedInteger(rect.at(3), "object.rect"),
        };
        if (values[2] != values[3]) {
            throw std::runtime_error("lightmap sidecar " + path.string() + ": object " + name +
                                     " has a non-square rect");
        }
        for (std::size_t index = 0; index < 4u; ++index) {
            if (values[index] > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("lightmap sidecar " + path.string() + ": object " + name +
                                         " rect exceeds uint32");
            }
        }
        LightmapSidecarObject parsed;
        parsed.name = name;
        parsed.atlas = static_cast<uint32_t>(requireUnsignedInteger(object.at("atlas"), "object.atlas"));
        parsed.x = static_cast<uint32_t>(values[0]);
        parsed.y = static_cast<uint32_t>(values[1]);
        parsed.size = static_cast<uint32_t>(values[2]);
        if (object.contains("scale")) {
            parsed.scale = static_cast<float>(requireNumber(object.at("scale"), "object.scale"));
        }
        if (object.contains("contributeGI")) {
            parsed.contributeGI = object.at("contributeGI").get<bool>();
        }
        sidecar.objects.push_back(std::move(parsed));
    }

    return sidecar;
}

void mergeLightmapSidecar(const std::filesystem::path& sceneSidecar, const std::filesystem::path& lightmapSidecar,
                          const std::filesystem::path& output) {
    if (sceneSidecar.empty()) {
        throw std::runtime_error("scene sidecar path is empty");
    }
    if (lightmapSidecar.empty()) {
        throw std::runtime_error("lightmap sidecar path is empty");
    }
    if (output.empty()) {
        throw std::runtime_error("merged sidecar output path is empty");
    }

    OrderedJson scene = readSidecarJson(sceneSidecar, "scene sidecar");
    const OrderedJson fragment = readSidecarJson(lightmapSidecar, "lightmap sidecar");
    const auto lightmap = fragment.find("lightmap");
    if (lightmap == fragment.end()) {
        throw std::runtime_error("lightmap sidecar " + lightmapSidecar.string() + " has no lightmap object");
    }
    validateLightmapSidecarJson(*lightmap, lightmapSidecar);

    scene["lightmap"] = *lightmap;
    const std::string text = scene.dump(2) + "\n";
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    writeFile(output, std::vector<uint8_t>(text.begin(), text.end()));
}

std::string formatGuid(const Guid& guid) {
    char buffer[33] = {};
    std::snprintf(buffer, sizeof(buffer), "%08X%08X%08X%08X", guid.a, guid.b, guid.c, guid.d);
    return buffer;
}

std::string formatShaHash(const ShaHash& hash) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(sizeof(hash.hash) * 2u);
    for (uint8_t value : hash.hash) {
        result.push_back(kHex[value >> 4u]);
        result.push_back(kHex[value & 0x0Fu]);
    }
    return result;
}

std::string lightmapResultFileName(const Guid& guid) {
    return "v2." + formatGuid(guid) + ".lmgz";
}

std::string shadowmaskResultFileName(const Guid& guid) {
    return "v2." + formatGuid(guid) + ".sdfgz";
}

std::string lightmapAtlasName(uint32_t atlasIndex) {
    return "lm" + std::to_string(atlasIndex);
}

std::string lightmapAtlasDdsFileName(uint32_t atlasIndex, uint32_t shChannel) {
    return lightmapAtlasName(atlasIndex) + "_sh" + std::to_string(shChannel) + ".dds";
}

std::string meshFileName(const Guid& guid, uint32_t lodIndex, uint32_t index) {
    return "v" + std::to_string(kDawnFileVersion) + "." + std::to_string(index) + "." + formatGuid(guid) +
           std::to_string(lodIndex) + ".meshgz";
}

std::string materialFileName(const ShaHash& hash, uint32_t index) {
    return "v" + std::to_string(kDawnFileVersion) + "." + std::to_string(index) + "." + formatShaHash(hash) + ".matgz";
}

PackageFiles writeDawnPackage(const std::filesystem::path& directory, const std::string& sceneGuid,
                              const SceneInfo& scene, const BakingJobInputs& jobs, const LightmapSidecar* lightmap) {
    if (sceneGuid.size() != 32u) {
        throw std::runtime_error("scene guid must be 32 hex characters");
    }

    std::filesystem::create_directories(directory);
    PackageFiles files;
    const std::string channelGuid = upperHex(sceneGuid);
    files.sceneGz = directory / ("v" + std::to_string(kDawnFileVersion) + "." + channelGuid + ".scenegz");
    files.jobsGz = directory / ("v" + std::to_string(kDawnFileVersion) + "." + channelGuid + ".jobgz");
    files.scene = directory / (sceneGuid + ".scene");
    files.jobs = directory / (sceneGuid + ".jobs");
    if (lightmap != nullptr) {
        files.lightmap = directory / (sceneGuid + ".lightmap.json");
    }

    const std::vector<uint8_t> sceneBytes =
        serializeObject([&scene](Writer& writer) { writeScene(writer, scene, kExportVersion); });
    const std::vector<uint8_t> jobBytes =
        serializeObject([&jobs](Writer& writer) { writeBakingJobInputs(writer, jobs, kExportVersion); });
    files.sceneBytes = sceneBytes.size();
    files.jobBytes = jobBytes.size();

    writeGzipFile(files.sceneGz, sceneBytes);
    writeGzipFile(files.jobsGz, jobBytes);
    writeFile(files.scene, sceneBytes);
    writeFile(files.jobs, jobBytes);
    if (lightmap != nullptr) {
        writeLightmapSidecar(files.lightmap, *lightmap);
    }

    files.meshGz.reserve(scene.meshes.size());
    for (std::size_t index = 0; index < scene.meshes.size(); ++index) {
        const MeshInfo& mesh = scene.meshes[index];
        const std::vector<uint8_t> bytes =
            serializeObject([&mesh](Writer& writer) { writeMesh(writer, mesh, kExportVersion); });
        const std::filesystem::path path =
            directory / meshFileName(mesh.header.guid, mesh.header.lodIndex, static_cast<uint32_t>(index));
        writeGzipFile(path, bytes);
        files.meshGz.push_back(path);
    }

    files.materialGz.reserve(scene.materials.size());
    for (std::size_t index = 0; index < scene.materials.size(); ++index) {
        const MaterialInfo& material = scene.materials[index];
        const std::vector<uint8_t> bytes =
            serializeObject([&material](Writer& writer) { writeMaterial(writer, material, kExportVersion); });
        const std::filesystem::path path =
            directory / materialFileName(material.header.hash, static_cast<uint32_t>(index));
        writeGzipFile(path, bytes);
        files.materialGz.push_back(path);
    }

    return files;
}

} // namespace hbg::dawn
