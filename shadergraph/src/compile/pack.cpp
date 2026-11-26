#include "compile_internal.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

namespace sg::detail {
namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU8(Bytes& out, std::uint8_t value) { out.push_back(value); }

void appendU16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(Bytes& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void appendU64(Bytes& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void appendF32(Bytes& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

std::uint32_t align8(std::uint32_t value) { return (value + 7u) & ~7u; }

std::uint64_t fnv1a(const Bytes& bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint8_t samplerFilterCode(std::string_view value) {
    if (value == "point") {
        return 1;
    }
    if (value == "trilinear") {
        return 2;
    }
    return 0;
}

std::uint8_t samplerWrapCode(std::string_view value) {
    if (value == "clamp") {
        return 1;
    }
    if (value == "mirror") {
        return 2;
    }
    if (value == "mirroronce") {
        return 3;
    }
    return 0;
}

std::vector<float> valueFloats(const Value& value) {
    std::vector<float> result(16, 0.0f);
    if (const float* scalar = value.asFloat()) {
        result[0] = *scalar;
    } else if (const bool* boolean = value.asBool()) {
        result[0] = *boolean ? 1.0f : 0.0f;
    } else if (const std::int64_t* integer = value.asInteger()) {
        result[0] = static_cast<float>(*integer);
    } else if (const Vec2* vec2Value = value.asVector2()) {
        result[0] = vec2Value->x;
        result[1] = vec2Value->y;
    } else if (const Vec3* vec3Value = value.asVector3()) {
        result[0] = vec3Value->x;
        result[1] = vec3Value->y;
        result[2] = vec3Value->z;
    } else if (const Vec4* vec4Value = value.asVector4()) {
        result[0] = vec4Value->x;
        result[1] = vec4Value->y;
        result[2] = vec4Value->z;
        result[3] = vec4Value->w;
    } else if (const Mat2* mat2Value = value.asMatrix2()) {
        std::copy(mat2Value->m.begin(), mat2Value->m.end(), result.begin());
    } else if (const Mat3* mat3Value = value.asMatrix3()) {
        std::copy(mat3Value->m.begin(), mat3Value->m.end(), result.begin());
    } else if (const Mat4* mat4Value = value.asMatrix4()) {
        std::copy(mat4Value->m.begin(), mat4Value->m.end(), result.begin());
    }
    return result;
}

struct StringTable {
    Bytes data;
    std::map<std::string, std::uint32_t> indices;

    std::uint32_t add(const std::string& value) {
        const auto found = indices.find(value);
        if (found != indices.end()) {
            return found->second;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(data.size());
        indices.emplace(value, index);
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(0);
        return index;
    }
};

const std::vector<std::pair<std::string, int>> kStages = {
    {"graph0", 0}, {"graph1", 1}, {"graph2", 2}, {"graph3", 3}, {"graph4", 4},
    {"lightData", 5}, {"directionalShadow", 6}, {"lightList", 7}, {"shadowContext", 8},
    {"punctualShadow", 9}, {"environment", 10}, {"contactShadow", 11},
    {"pointImsShadow", 12}, {"pointAreaDepth", 13}, {"probeMaps", 14}, {"cookieAtlas", 15},
};

struct Group {
    std::uint16_t pass = 0;
    std::uint64_t variant = 0;
    std::vector<const PackedBlob*> blobs;
};

bool sortBlob(const PackedBlob* lhs, const PackedBlob* rhs) {
    if (lhs->stage != rhs->stage) {
        return lhs->stage < rhs->stage;
    }
    return static_cast<int>(lhs->backend) < static_cast<int>(rhs->backend);
}

} // namespace

bool packSgb(const PackInput& input, std::vector<std::uint8_t>& output, std::string& error) {
    output.clear();
    error.clear();
    if (input.graph == nullptr || input.target == nullptr) {
        error = "pack input is missing graph or target";
        return false;
    }

    StringTable strings;
    strings.add("");
    strings.add(input.graph->name);
    strings.add(input.target->id);
    strings.add(generatorVersion());
    for (const PropertyInfo& property : input.properties) {
        strings.add(property.name);
        strings.add(property.refName);
    }
    for (const Keyword& keyword : input.keywords) {
        strings.add(keyword.name);
        strings.add(keyword.refName);
        for (const KeywordEntry& entry : keyword.entries) {
            strings.add(entry.displayName);
            strings.add(entry.referenceName);
        }
    }
    for (const auto& pass : input.target->passes) {
        strings.add(pass.id);
    }
    for (const auto& stage : kStages) {
        strings.add(stage.first);
    }

    Bytes properties;
    appendU32(properties, static_cast<std::uint32_t>(input.properties.size()));
    for (const PropertyInfo& property : input.properties) {
        appendU32(properties, strings.indices.at(property.name));
        appendU32(properties, strings.indices.at(property.refName));
        appendU8(properties, static_cast<std::uint8_t>(property.type));
        appendU8(properties, static_cast<std::uint8_t>(property.precision));
        std::uint8_t propertyFlags = property.samplerStage >= 0 ? 1u : 0u;
        if (property.useTilingAndOffset) {
            propertyFlags |= 2u;
        }
        if (property.isHDR) {
            propertyFlags |= 4u;
        }
        appendU8(properties, propertyFlags);
        appendU8(properties, property.defaultType);
        const std::vector<float> values = valueFloats(property.defaultValue);
        for (int index = 0; index < 4; ++index) {
            appendF32(properties, values[static_cast<std::size_t>(index)]);
        }
        for (int index = 0; index < 16; ++index) {
            float value = values[static_cast<std::size_t>(index)];
            if (property.useTilingAndOffset) {
                switch (index) {
                case 0:
                    value = property.tiling.x;
                    break;
                case 1:
                    value = property.tiling.y;
                    break;
                case 2:
                    value = property.offset.x;
                    break;
                case 3:
                    value = property.offset.y;
                    break;
                default:
                    break;
                }
            }
            appendF32(properties, value);
        }
        appendU32(properties, 0);
        appendU8(properties, samplerFilterCode(property.samplerFilter));
        appendU8(properties, samplerWrapCode(property.samplerWrap));
        appendU8(properties, samplerAnisotropyLevel(property.samplerAnisotropic));
        appendU8(properties, 0);
    }

    Bytes keywords;
    appendU32(keywords, static_cast<std::uint32_t>(input.keywords.size()));
    for (const Keyword& keyword : input.keywords) {
        appendU32(keywords, strings.indices.at(keyword.name));
        appendU32(keywords, strings.indices.at(keyword.refName));
        appendU8(keywords, static_cast<std::uint8_t>(keyword.type));
        appendU8(keywords, static_cast<std::uint8_t>(keyword.definition));
        appendU8(keywords, static_cast<std::uint8_t>(keyword.scope));
        appendU8(keywords, 0);
        appendU32(keywords, static_cast<std::uint32_t>(keyword.defaultValue));
        appendU32(keywords, static_cast<std::uint32_t>(keyword.entries.size()));
        for (const KeywordEntry& entry : keyword.entries) {
            appendU32(keywords, static_cast<std::uint32_t>(entry.id));
            appendU32(keywords, strings.indices.at(entry.displayName));
            appendU32(keywords, strings.indices.at(entry.referenceName));
        }
    }

    std::vector<Group> groups;
    for (const VariantInfo& variant : input.variants) {
        for (std::size_t passIndex = 0; passIndex < input.target->passes.size(); ++passIndex) {
            Group group;
            group.pass = static_cast<std::uint16_t>(passIndex);
            group.variant = variant.key;
            for (const PackedBlob& blob : input.blobs) {
                if (blob.variantKey == variant.key && blob.passId == passIndex) {
                    group.blobs.push_back(&blob);
                }
            }
            std::sort(group.blobs.begin(), group.blobs.end(), sortBlob);
            groups.push_back(std::move(group));
        }
    }

    Bytes passes;
    appendU32(passes, static_cast<std::uint32_t>(input.target->passes.size()));
    for (std::size_t index = 0; index < input.target->passes.size(); ++index) {
        const PassSpec& pass = input.target->passes[index];
        appendU32(passes, static_cast<std::uint32_t>(index));
        appendU32(passes, strings.indices.at(pass.id));
        appendU32(passes, static_cast<std::uint32_t>(kStages.size()));
        for (const auto& stage : kStages) {
            appendU32(passes, strings.indices.at(stage.first));
            appendU8(passes, static_cast<std::uint8_t>(stage.second));
            appendU8(passes, 0);
            appendU16(passes, 0);
        }
    }

    Bytes variants;
    appendU32(variants, static_cast<std::uint32_t>(input.variants.size()));
    for (const VariantInfo& variant : input.variants) {
        appendU64(variants, variant.key);
        appendU32(variants, static_cast<std::uint32_t>(input.target->passes.size()));
        for (std::size_t passIndex = 0; passIndex < input.target->passes.size(); ++passIndex) {
            appendU16(variants, static_cast<std::uint16_t>(passIndex));
            const auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& group) {
                return group.pass == passIndex && group.variant == variant.key;
            });
            appendU16(variants, static_cast<std::uint16_t>(found - groups.begin()));
        }
    }

    Bytes blobs;
    appendU32(blobs, static_cast<std::uint32_t>(groups.size()));
    std::vector<std::uint32_t> blobTableOffsets;
    for (const Group& group : groups) {
        appendU32(blobs, 1);
        appendU32(blobs, static_cast<std::uint32_t>(group.blobs.size()));
        for (const PackedBlob* blob : group.blobs) {
            blobTableOffsets.push_back(static_cast<std::uint32_t>(blobs.size()));
            appendU8(blobs, static_cast<std::uint8_t>(blob->stage));
            appendU8(blobs, static_cast<std::uint8_t>(blob->backend));
            appendU16(blobs, 0);
            appendU32(blobs, 0);
            appendU32(blobs, static_cast<std::uint32_t>(blob->data.size()));
        }
    }
    while (blobs.size() % 8u != 0u) {
        blobs.push_back(0);
    }
    std::size_t blobIndex = 0;
    for (const Group& group : groups) {
        for (const PackedBlob* blob : group.blobs) {
            const std::uint32_t offset = static_cast<std::uint32_t>(blobs.size());
            const std::uint32_t tableOffset = blobTableOffsets[blobIndex++];
            for (int shift = 0; shift < 32; shift += 8) {
                blobs[tableOffset + 4 + static_cast<std::size_t>(shift / 8)] =
                    static_cast<std::uint8_t>((offset >> shift) & 0xffu);
            }
            blobs.insert(blobs.end(), blob->data.begin(), blob->data.end());
            while (blobs.size() % 8u != 0u) {
                blobs.push_back(0);
            }
        }
    }

    Bytes meta;
    nlohmann::ordered_json metaJson;
    metaJson["name"] = input.graph->name;
    metaJson["target"] = input.target->id;
    metaJson["generatorVersion"] = generatorVersion();
    metaJson["schemaVersion"] = input.graph->schemaVersion;
    metaJson["nodeSpecVersion"] = 1;
    metaJson["stageBudget"] = input.target->textureBudget;
    metaJson["requiredFeatureMask"] = input.requiredFeatureMask;
    const std::string metaText = metaJson.dump();
    meta.insert(meta.end(), metaText.begin(), metaText.end());

    struct Section {
        std::uint32_t kind;
        Bytes data;
    };
    std::vector<Section> sections;
    sections.push_back({1, std::move(meta)});
    sections.push_back({2, Bytes(strings.data.begin(), strings.data.end())});
    sections.push_back({3, std::move(properties)});
    sections.push_back({4, std::move(keywords)});
    sections.push_back({5, std::move(variants)});
    sections.push_back({6, std::move(passes)});
    sections.push_back({7, std::move(blobs)});
    if (input.keepHlsl && !input.hlsl.empty()) {
        Bytes hlsl;
        hlsl.insert(hlsl.end(), input.hlsl.begin(), input.hlsl.end());
        sections.push_back({8, std::move(hlsl)});
    }
    const std::uint32_t headerSize = 28u;
    std::uint32_t tableSize = static_cast<std::uint32_t>(sections.size()) * 20u;
    std::uint32_t offset = headerSize + tableSize;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(sections.size());
    for (const Section& section : sections) {
        offset = align8(offset);
        offsets.push_back(offset);
        offset += static_cast<std::uint32_t>(section.data.size());
    }

    Bytes table;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        appendU32(table, sections[index].kind);
        appendU64(table, offsets[index]);
        appendU64(table, sections[index].data.size());
    }

    Bytes result;
    const std::uint32_t flags = input.keepHlsl ? 1u : 0u;
    result.push_back('S');
    result.push_back('G');
    result.push_back('B');
    result.push_back('1');
    appendU16(result, 1);
    appendU16(result, static_cast<std::uint16_t>(flags));
    appendU64(result, input.contentHash);
    appendU64(result, input.sourceHash);
    appendU32(result, static_cast<std::uint32_t>(sections.size()));
    result.insert(result.end(), table.begin(), table.end());
    for (std::size_t index = 0; index < sections.size(); ++index) {
        while (result.size() < offsets[index]) {
            result.push_back(0);
        }
        result.insert(result.end(), sections[index].data.begin(), sections[index].data.end());
    }
    while (result.size() % 8u != 0u) {
        result.push_back(0);
    }
    if (!validateSgb(result, error)) {
        return false;
    }
    output = std::move(result);
    return true;
}

bool validateSgb(const std::vector<std::uint8_t>& bytes, std::string& error) {
    error.clear();
    if (bytes.size() < 28 || std::memcmp(bytes.data(), "SGB1", 4) != 0) {
        error = "invalid SGB header";
        return false;
    }
    const std::uint16_t version = static_cast<std::uint16_t>(bytes[4] | (bytes[5] << 8u));
    if (version != 1) {
        error = "unsupported SGB version";
        return false;
    }
    const std::uint32_t sectionCount =
        static_cast<std::uint32_t>(bytes[24]) | (static_cast<std::uint32_t>(bytes[25]) << 8u) |
        (static_cast<std::uint32_t>(bytes[26]) << 16u) |
        (static_cast<std::uint32_t>(bytes[27]) << 24u);
    if (sectionCount == 0 || sectionCount > 16 || bytes.size() < 28u + sectionCount * 20u) {
        error = "invalid SGB section count";
        return false;
    }
    std::set<std::uint32_t> kinds;
    std::uint64_t blobsOffset = 0;
    std::uint64_t blobsSize = 0;
    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        const std::size_t base = 28 + static_cast<std::size_t>(index) * 20;
        const std::uint32_t kind = static_cast<std::uint32_t>(bytes[base]) |
                                   (static_cast<std::uint32_t>(bytes[base + 1]) << 8u) |
                                   (static_cast<std::uint32_t>(bytes[base + 2]) << 16u) |
                                   (static_cast<std::uint32_t>(bytes[base + 3]) << 24u);
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        for (int byte = 0; byte < 8; ++byte) {
            offset |= static_cast<std::uint64_t>(bytes[base + 4 + byte]) << (8 * byte);
            size |= static_cast<std::uint64_t>(bytes[base + 12 + byte]) << (8 * byte);
        }
        if (!kinds.insert(kind).second || offset > bytes.size() || size > bytes.size() - offset) {
            error = "invalid SGB section";
            return false;
        }
        if (kind == 7) {
            blobsOffset = offset;
            blobsSize = size;
        }
        if (kind == 1 || kind == 2 || kind == 3 || kind == 4 || kind == 5 || kind == 6) {
            if (size == 0 && kind != 4) {
                error = "empty required SGB section";
                return false;
            }
        }
    }
    for (std::uint32_t kind = 1; kind <= 7; ++kind) {
        if (!kinds.contains(kind)) {
            error = "missing required SGB section";
            return false;
        }
    }
    if (blobsSize < 4) {
        error = "empty SGB blobs section";
        return false;
    }
    const std::uint8_t* blobs = bytes.data() + blobsOffset;
    const std::uint32_t groupCount = static_cast<std::uint32_t>(blobs[0]) |
                                     (static_cast<std::uint32_t>(blobs[1]) << 8u) |
                                     (static_cast<std::uint32_t>(blobs[2]) << 16u) |
                                     (static_cast<std::uint32_t>(blobs[3]) << 24u);
    std::uint64_t cursor = 4;
    for (std::uint32_t group = 0; group < groupCount; ++group) {
        if (cursor + 8 > blobsSize) {
            error = "truncated SGB blob group";
            return false;
        }
        const std::uint32_t count = static_cast<std::uint32_t>(blobs[cursor + 4]) |
                                    (static_cast<std::uint32_t>(blobs[cursor + 5]) << 8u) |
                                    (static_cast<std::uint32_t>(blobs[cursor + 6]) << 16u) |
                                    (static_cast<std::uint32_t>(blobs[cursor + 7]) << 24u);
        cursor += 8;
        for (std::uint32_t blob = 0; blob < count; ++blob) {
            if (cursor + 12 > blobsSize) {
                error = "truncated SGB blob table";
                return false;
            }
            const std::uint8_t stage = blobs[cursor];
            const std::uint8_t backend = blobs[cursor + 1];
            std::uint32_t offset = 0;
            std::uint32_t size = 0;
            for (int byte = 0; byte < 4; ++byte) {
                offset |= static_cast<std::uint32_t>(blobs[cursor + 4 + byte]) << (8 * byte);
                size |= static_cast<std::uint32_t>(blobs[cursor + 8 + byte]) << (8 * byte);
            }
            if (stage > 2 || backend > 1 || size == 0 || offset < cursor + 12 ||
                offset > blobsSize || size > blobsSize - offset) {
                error = "invalid SGB blob";
                return false;
            }
            cursor += 12;
        }
    }
    return true;
}

} // namespace sg::detail
