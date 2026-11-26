#include "render/shadergraph/sgb_asset.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace hbg::render::graph {
namespace {

constexpr uint32_t kMetaSection = 1;
constexpr uint32_t kStringsSection = 2;
constexpr uint32_t kPropertiesSection = 3;
constexpr uint32_t kKeywordsSection = 4;
constexpr uint32_t kVariantsSection = 5;
constexpr uint32_t kPassesSection = 6;
constexpr uint32_t kBlobsSection = 7;
constexpr int kMaxGraphTextureStage = 4;

struct Section {
    uint64_t offset = 0;
    uint64_t size = 0;
};

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8u));
}

uint32_t readU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
}

uint64_t readU64(const uint8_t* data) {
    uint64_t value = 0;
    for (int byte = 0; byte < 8; ++byte) {
        value |= static_cast<uint64_t>(data[byte]) << (8 * byte);
    }
    return value;
}

float readF32(const uint8_t* data) {
    const uint32_t bits = readU32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool rangeValid(uint64_t offset, uint64_t size, uint64_t total) {
    return offset <= total && size <= total - offset;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

PassKind passKindFromId(std::string_view id) {
    const std::string normalized = lower(std::string(id));
    if (normalized == "forward") {
        return PassKind::Forward;
    }
    if (normalized == "gbuffer") {
        return PassKind::GBuffer;
    }
    if (normalized == "depth") {
        return PassKind::Depth;
    }
    if (normalized == "shadow") {
        return PassKind::Shadow;
    }
    if (normalized == "motion") {
        return PassKind::Motion;
    }
    return PassKind::Unknown;
}

bool findMetaString(std::string_view meta, std::string_view key, std::string& value) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const std::size_t begin = meta.find(needle);
    if (begin == std::string_view::npos) {
        return false;
    }
    const std::size_t start = begin + needle.size();
    const std::size_t end = meta.find('"', start);
    if (end == std::string_view::npos) {
        return false;
    }
    value.assign(meta.substr(start, end - start));
    return true;
}

bool findMetaInteger(std::string_view meta, std::string_view key, uint64_t& value) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const std::size_t begin = meta.find(needle);
    if (begin == std::string_view::npos) {
        return false;
    }
    std::size_t cursor = begin + needle.size();
    while (cursor < meta.size() && meta[cursor] == ' ') {
        ++cursor;
    }
    if (cursor >= meta.size() || !std::isdigit(static_cast<unsigned char>(meta[cursor]))) {
        return false;
    }
    uint64_t result = 0;
    while (cursor < meta.size() && std::isdigit(static_cast<unsigned char>(meta[cursor]))) {
        result = result * 10u + static_cast<uint64_t>(meta[cursor] - '0');
        ++cursor;
    }
    value = result;
    return true;
}

struct StringTable {
    const uint8_t* data = nullptr;
    uint32_t size = 0;

    bool get(uint32_t offset, std::string& value) const {
        if (offset >= size) {
            return false;
        }
        const void* terminator = std::memchr(data + offset, 0, size - offset);
        if (terminator == nullptr) {
            return false;
        }
        const std::size_t length = static_cast<const uint8_t*>(terminator) - (data + offset);
        value.assign(reinterpret_cast<const char*>(data + offset), length);
        return true;
    }
};

struct Reader {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t cursor = 0;

    bool available(uint32_t count) const { return cursor <= size && count <= size - cursor; }

    bool u8(uint8_t& value) {
        if (!available(1)) {
            return false;
        }
        value = data[cursor++];
        return true;
    }

    bool u16(uint16_t& value) {
        if (!available(2)) {
            return false;
        }
        value = readU16(data + cursor);
        cursor += 2;
        return true;
    }

    bool u32(uint32_t& value) {
        if (!available(4)) {
            return false;
        }
        value = readU32(data + cursor);
        cursor += 4;
        return true;
    }

    bool u64(uint64_t& value) {
        if (!available(8)) {
            return false;
        }
        value = readU64(data + cursor);
        cursor += 8;
        return true;
    }

    bool skip(uint32_t count) {
        if (!available(count)) {
            return false;
        }
        cursor += count;
        return true;
    }

    bool string(const StringTable& strings, std::string& value) {
        uint32_t offset = 0;
        return u32(offset) && strings.get(offset, value);
    }
};

} // namespace

PropertyKind propertyKindFromPacked(uint8_t value) {
    switch (value) {
    case 2:
        return PropertyKind::Matrix4;
    case 3:
        return PropertyKind::Matrix3;
    case 4:
        return PropertyKind::Matrix2;
    case 5:
        return PropertyKind::Texture2D;
    case 6:
        return PropertyKind::Texture2DArray;
    case 7:
        return PropertyKind::Texture3D;
    case 8:
        return PropertyKind::Cubemap;
    case 9:
        return PropertyKind::Gradient;
    case 11:
        return PropertyKind::Vector4;
    case 12:
        return PropertyKind::Vector3;
    case 13:
        return PropertyKind::Vector2;
    case 14:
        return PropertyKind::Float;
    case 16:
        return PropertyKind::Boolean;
    case 17:
        return PropertyKind::Integer;
    case 0:
        return PropertyKind::SamplerState;
    case 21:
        return PropertyKind::Color;
    default:
        return PropertyKind::Other;
    }
}

bool isTextureKind(PropertyKind kind) {
    return kind == PropertyKind::Texture2D || kind == PropertyKind::Texture2DArray ||
           kind == PropertyKind::Texture3D || kind == PropertyKind::Cubemap ||
           kind == PropertyKind::Gradient;
}

bool isMatrixKind(PropertyKind kind) {
    return kind == PropertyKind::Matrix2 || kind == PropertyKind::Matrix3 || kind == PropertyKind::Matrix4;
}

void Asset::reset() {
    m_bytes.clear();
    m_path.clear();
    m_lastWriteTime = {};
    m_properties.clear();
    m_keywords.clear();
    m_passes.clear();
    m_variants.clear();
    m_groups.clear();
    m_target.clear();
    m_contentHash = 0;
    m_sourceHash = 0;
    m_requiredFeatureMask = 0;
    m_stageBudget = 5;
}

bool Asset::load(const std::filesystem::path& path, std::string* error) {
    reset();
    m_path = path;

    std::error_code timeError;
    m_lastWriteTime = std::filesystem::last_write_time(path, timeError);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        if (error != nullptr) {
            *error = "failed to open '" + path.string() + "'";
        }
        return false;
    }
    const std::streamsize fileSize = file.tellg();
    if (fileSize < 28) {
        if (error != nullptr) {
            *error = "file is too small to be SGB1";
        }
        return false;
    }
    file.seekg(0, std::ios::beg);
    m_bytes.resize(static_cast<std::size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(m_bytes.data()), fileSize)) {
        if (error != nullptr) {
            *error = "failed to read '" + path.string() + "'";
        }
        return false;
    }
    return parseBytes(error);
}

bool Asset::loadFromMemory(const std::vector<uint8_t>& bytes, std::string* error) {
    reset();
    m_bytes = bytes;
    return parseBytes(error);
}

bool Asset::parseBytes(std::string* error) {
    const uint8_t* data = m_bytes.data();
    const uint64_t total = m_bytes.size();
    if (total < 28) {
        if (error != nullptr) {
            *error = "SGB1 data is too small";
        }
        return false;
    }
    if (std::memcmp(data, "SGB1", 4) != 0) {
        if (error != nullptr) {
            *error = "bad magic (expected SGB1)";
        }
        return false;
    }
    if (readU16(data + 4) != 1) {
        if (error != nullptr) {
            *error = "unsupported SGB format version";
        }
        return false;
    }
    m_contentHash = readU64(data + 8);
    m_sourceHash = readU64(data + 16);
    const uint32_t sectionCount = readU32(data + 24);
    if (sectionCount == 0 || sectionCount > 16) {
        if (error != nullptr) {
            *error = "invalid section count";
        }
        return false;
    }
    if (!rangeValid(28, static_cast<uint64_t>(sectionCount) * 20u, total)) {
        if (error != nullptr) {
            *error = "section table is out of range";
        }
        return false;
    }

    std::array<Section, 8> sections{};
    std::array<bool, 8> present{};
    for (uint32_t index = 0; index < sectionCount; ++index) {
        const uint8_t* entry = data + 28 + static_cast<std::size_t>(index) * 20;
        const uint32_t kind = readU32(entry);
        const uint64_t offset = readU64(entry + 4);
        const uint64_t size = readU64(entry + 12);
        if (kind == 0 || kind >= sections.size() || present[kind]) {
            if (error != nullptr) {
                *error = "invalid or duplicate section " + std::to_string(kind);
            }
            return false;
        }
        if (!rangeValid(offset, size, total)) {
            if (error != nullptr) {
                *error = "section " + std::to_string(kind) + " is out of range";
            }
            return false;
        }
        sections[kind] = {offset, size};
        present[kind] = true;
    }
    for (uint32_t kind = 1; kind <= 7; ++kind) {
        if (!present[kind]) {
            if (error != nullptr) {
                *error = "missing required section " + std::to_string(kind);
            }
            return false;
        }
    }

    {
        const Section& section = sections[kMetaSection];
        const std::string_view meta(reinterpret_cast<const char*>(data + section.offset),
                                    static_cast<std::size_t>(section.size));
        findMetaString(meta, "target", m_target);
        uint64_t value = 0;
        if (findMetaInteger(meta, "stageBudget", value)) {
            m_stageBudget = static_cast<int>(std::min<uint64_t>(value, 1024u));
        }
        if (findMetaInteger(meta, "requiredFeatureMask", value)) {
            m_requiredFeatureMask = value;
        }
    }

    const Section& stringsSection = sections[kStringsSection];
    const StringTable strings{data + stringsSection.offset, static_cast<uint32_t>(stringsSection.size)};

    {
        const Section& section = sections[kPropertiesSection];
        Reader reader{data + section.offset, static_cast<uint32_t>(section.size)};
        uint32_t count = 0;
        if (!reader.u32(count) || count > 4096) {
            if (error != nullptr) {
                *error = "invalid property count";
            }
            return false;
        }
        int samplerStage = 0;
        m_properties.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            PropertyDesc property;
            uint8_t packedType = 0;
            uint8_t flags = 0;
            uint8_t packedDefaultType = 0;
            if (!reader.string(strings, property.name) || !reader.string(strings, property.refName) ||
                !reader.u8(packedType) || !reader.u8(property.precision) || !reader.u8(flags) ||
                !reader.u8(packedDefaultType)) {
                if (error != nullptr) {
                    *error = "truncated property table";
                }
                return false;
            }
            property.kind = propertyKindFromPacked(packedType);
            property.useTilingAndOffset = (flags & 2u) != 0;
            property.isHDR = (flags & 4u) != 0;
            if (packedDefaultType >
                static_cast<uint8_t>(TextureDefaultType::Red)) {
                if (error != nullptr) {
                    *error = "invalid texture default type";
                }
                return false;
            }
            property.defaultType =
                static_cast<TextureDefaultType>(packedDefaultType);
            if (property.useTilingAndOffset && !isTextureKind(property.kind)) {
                if (error != nullptr) {
                    *error = "texture transform flag on a non-texture property";
                }
                return false;
            }
            if (property.isHDR && property.kind != PropertyKind::Texture2D &&
                property.kind != PropertyKind::Texture2DArray &&
                property.kind != PropertyKind::Texture3D &&
                property.kind != PropertyKind::Cubemap) {
                if (error != nullptr) {
                    *error = "HDR decode flag on an unsupported property";
                }
                return false;
            }
            if (!reader.available(4 * 4 + 16 * 4 + 8)) {
                if (error != nullptr) {
                    *error = "truncated property values";
                }
                return false;
            }
            for (float& value : property.vector) {
                value = readF32(reader.data + reader.cursor);
                reader.cursor += 4;
            }
            for (float& value : property.matrix) {
                value = readF32(reader.data + reader.cursor);
                reader.cursor += 4;
            }
            if (property.useTilingAndOffset) {
                property.tiling = {property.matrix[0], property.matrix[1]};
                property.offset = {property.matrix[2], property.matrix[3]};
                property.matrix = {};
            }
            uint32_t defaultTextureIndex = 0;
            if (!reader.u32(defaultTextureIndex) || !reader.u8(property.samplerFilter) ||
                !reader.u8(property.samplerWrap) ||
                !reader.u8(property.samplerAnisotropy) || !reader.skip(1)) {
                if (error != nullptr) {
                    *error = "truncated property sampler";
                }
                return false;
            }
            if (property.samplerAnisotropy != 0 && property.samplerAnisotropy != 2 &&
                property.samplerAnisotropy != 4 && property.samplerAnisotropy != 8 &&
                property.samplerAnisotropy != 16) {
                if (error != nullptr) {
                    *error = "invalid sampler anisotropy level";
                }
                return false;
            }
            (void)defaultTextureIndex;
            if (isTextureKind(property.kind)) {
                property.samplerStage = samplerStage++;
            }
            m_properties.push_back(std::move(property));
        }
    }

    {
        const Section& section = sections[kKeywordsSection];
        Reader reader{data + section.offset, static_cast<uint32_t>(section.size)};
        uint32_t count = 0;
        if (!reader.u32(count) || count > 1024) {
            if (error != nullptr) {
                *error = "invalid keyword count";
            }
            return false;
        }
        m_keywords.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            KeywordDesc keyword;
            uint8_t reserved = 0;
            uint32_t entryCount = 0;
            if (!reader.string(strings, keyword.name) || !reader.string(strings, keyword.refName) ||
                !reader.u8(keyword.type) || !reader.u8(keyword.definition) || !reader.u8(keyword.scope) ||
                !reader.u8(reserved) || !reader.u32(keyword.defaultValue) || !reader.u32(entryCount) ||
                entryCount > 1024) {
                if (error != nullptr) {
                    *error = "truncated keyword table";
                }
                return false;
            }
            keyword.entries.reserve(entryCount);
            for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
                KeywordEntryDesc entry;
                if (!reader.u32(entry.id) || !reader.string(strings, entry.displayName) ||
                    !reader.string(strings, entry.referenceName)) {
                    if (error != nullptr) {
                        *error = "truncated keyword entry table";
                    }
                    return false;
                }
                keyword.entries.push_back(std::move(entry));
            }
            m_keywords.push_back(std::move(keyword));
        }
    }

    {
        const Section& section = sections[kPassesSection];
        Reader reader{data + section.offset, static_cast<uint32_t>(section.size)};
        uint32_t count = 0;
        if (!reader.u32(count) || count > 64) {
            if (error != nullptr) {
                *error = "invalid pass count";
            }
            return false;
        }
        m_passes.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            PassDesc pass;
            uint32_t stageCount = 0;
            if (!reader.u32(pass.index) || !reader.string(strings, pass.id) || !reader.u32(stageCount) ||
                stageCount > 64) {
                if (error != nullptr) {
                    *error = "truncated pass table";
                }
                return false;
            }
            pass.kind = passKindFromId(pass.id);
            pass.stages.reserve(stageCount);
            for (uint32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
                StageBinding binding;
                uint8_t reserved0 = 0;
                uint16_t reserved1 = 0;
                if (!reader.string(strings, binding.name) || !reader.u8(binding.stage) ||
                    !reader.u8(reserved0) || !reader.u16(reserved1)) {
                    if (error != nullptr) {
                        *error = "truncated pass stage table";
                    }
                    return false;
                }
                pass.stages.push_back(std::move(binding));
            }
            m_passes.push_back(std::move(pass));
        }
    }

    {
        const Section& section = sections[kBlobsSection];
        Reader reader{data + section.offset, static_cast<uint32_t>(section.size)};
        uint32_t groupCount = 0;
        if (!reader.u32(groupCount) || groupCount > 4096) {
            if (error != nullptr) {
                *error = "invalid blob group count";
            }
            return false;
        }
        m_groups.reserve(groupCount);
        for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
            uint32_t leading = 0;
            uint32_t blobCount = 0;
            if (!reader.u32(leading) || !reader.u32(blobCount) || blobCount > 64) {
                if (error != nullptr) {
                    *error = "truncated blob group";
                }
                return false;
            }
            Group group;
            group.blobs.reserve(blobCount);
            for (uint32_t blobIndex = 0; blobIndex < blobCount; ++blobIndex) {
                BlobDesc blob;
                uint8_t backend = 0;
                uint16_t reserved = 0;
                uint32_t relativeOffset = 0;
                if (!reader.u8(blob.stage) || !reader.u8(backend) || !reader.u16(reserved) ||
                    !reader.u32(relativeOffset) || !reader.u32(blob.size)) {
                    if (error != nullptr) {
                        *error = "truncated blob table";
                    }
                    return false;
                }
                if (blob.stage > 2 || backend > 1 || blob.size == 0 ||
                    !rangeValid(section.offset + relativeOffset, blob.size, total)) {
                    if (error != nullptr) {
                        *error = "invalid blob entry";
                    }
                    return false;
                }
                blob.offset = section.offset + relativeOffset;
                blob.backend = backend == 0 ? Backend::Dxbc : Backend::Spirv;
                group.blobs.push_back(blob);
            }
            m_groups.push_back(std::move(group));
        }
    }

    {
        const Section& section = sections[kVariantsSection];
        Reader reader{data + section.offset, static_cast<uint32_t>(section.size)};
        uint32_t count = 0;
        if (!reader.u32(count) || count == 0 || count > 4096) {
            if (error != nullptr) {
                *error = "invalid variant count";
            }
            return false;
        }
        m_variants.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            VariantDesc variant;
            uint32_t passCount = 0;
            if (!reader.u64(variant.key) || !reader.u32(passCount) || passCount > m_passes.size()) {
                if (error != nullptr) {
                    *error = "truncated variant table";
                }
                return false;
            }
            variant.groups.assign(m_passes.size(), std::numeric_limits<uint16_t>::max());
            for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
                uint16_t pass = 0;
                uint16_t group = 0;
                if (!reader.u16(pass) || !reader.u16(group) || pass >= m_passes.size() ||
                    group >= m_groups.size()) {
                    if (error != nullptr) {
                        *error = "invalid variant pass/group";
                    }
                    return false;
                }
                variant.groups[pass] = group;
            }
            m_variants.push_back(std::move(variant));
        }
    }

    bool hasVertex = false;
    bool hasFragment = false;
    for (const Group& group : m_groups) {
        for (const BlobDesc& blob : group.blobs) {
            hasVertex = hasVertex || blob.stage == 0;
            hasFragment = hasFragment || blob.stage == 1;
        }
    }
    if (!hasVertex || !hasFragment) {
        if (error != nullptr) {
            *error = "asset has no vertex/fragment blobs";
        }
        return false;
    }
    return true;
}

const PropertyDesc* Asset::findProperty(std::string_view refName) const {
    for (const PropertyDesc& property : m_properties) {
        if (property.refName == refName) {
            return &property;
        }
    }
    return nullptr;
}

const KeywordDesc* Asset::findKeyword(std::string_view refName) const {
    for (const KeywordDesc& keyword : m_keywords) {
        if (keyword.refName == refName) {
            return &keyword;
        }
    }
    return nullptr;
}

const PassDesc* Asset::findPass(PassKind kind) const {
    for (const PassDesc& pass : m_passes) {
        if (pass.kind == kind) {
            return &pass;
        }
    }
    return nullptr;
}

const VariantDesc* Asset::findVariant(uint64_t key) const {
    for (const VariantDesc& variant : m_variants) {
        if (variant.key == key) {
            return &variant;
        }
    }
    return nullptr;
}

const VariantDesc* Asset::fallbackVariant(uint64_t key) const {
    if (const VariantDesc* exact = findVariant(key)) {
        return exact;
    }
    if (m_variants.empty()) {
        return nullptr;
    }
    const uint64_t engineBits = key & 0x7u;
    for (const VariantDesc& variant : m_variants) {
        if ((variant.key & 0x7u) == engineBits) {
            return &variant;
        }
    }
    return &m_variants.front();
}

ShaderBlob Asset::blob(uint32_t passIndex, uint64_t variantKey, Backend backend, uint8_t stage) const {
    if (passIndex >= m_passes.size()) {
        return {};
    }
    const VariantDesc* variant = fallbackVariant(variantKey);
    if (variant == nullptr || passIndex >= variant->groups.size()) {
        return {};
    }
    const uint16_t groupIndex = variant->groups[passIndex];
    if (groupIndex == std::numeric_limits<uint16_t>::max() || groupIndex >= m_groups.size()) {
        return {};
    }
    const Group& group = m_groups[groupIndex];
    for (const BlobDesc& entry : group.blobs) {
        if (entry.stage == stage && entry.backend == backend) {
            ShaderBlob result;
            result.data = m_bytes.data() + entry.offset;
            result.size = entry.size;
            result.backend = backend;
            result.stage = stage;
            return result;
        }
    }
    return {};
}

bool validateStageContract(const Asset& asset, std::string* error) {
    const auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (asset.stageBudget() < 0 || asset.stageBudget() > kMaxGraphTextureStage + 1) {
        return fail("graph stage budget is outside the v1 0-4 range");
    }
    if ((asset.requiredFeatureMask() & ~kSupportedGraphFeatures) != 0) {
        return fail("graph requires unsupported engine features");
    }
    int nextStage = 0;
    for (const PropertyDesc& property : asset.properties()) {
        if (property.samplerStage >= 0) {
            nextStage = std::max(nextStage, property.samplerStage + 1);
        }
    }
    if ((asset.requiredFeatureMask() & kGraphFeatureSceneColor) != 0) {
        ++nextStage;
    }
    if ((asset.requiredFeatureMask() & kGraphFeatureSceneDepth) != 0) {
        ++nextStage;
    }
    if (nextStage > asset.stageBudget()) {
        return fail("graph scene textures exceed the graph texture stage budget");
    }
    for (const PassDesc& pass : asset.passes()) {
        std::array<bool, 256> used{};
        for (const StageBinding& binding : pass.stages) {
            if (binding.stage < asset.stageBudget()) {
                if (binding.name.rfind("graph", 0) != 0) {
                    return fail("graph texture stage uses a non-graph semantic name");
                }
            } else if (binding.stage >= 5 && binding.name.rfind("graph", 0) == 0) {
                return fail("graph semantic is bound outside the graph stage budget");
            }
            if (used[binding.stage]) {
                return fail("pass declares duplicate texture stage " + std::to_string(binding.stage));
            }
            used[binding.stage] = true;
        }
        for (int stage = 0; stage < asset.stageBudget(); ++stage) {
            bool found = false;
            for (const StageBinding& binding : pass.stages) {
                found = found || binding.stage == stage;
            }
            if (!found) {
                return fail("pass '" + pass.id + "' is missing graph stage " + std::to_string(stage));
            }
        }
    }
    return true;
}

} // namespace hbg::render::graph
