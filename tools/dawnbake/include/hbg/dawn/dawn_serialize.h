#pragma once

#include "hbg/dawn/dawn_protocol.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace hbg::dawn {

class Writer {
public:
    void u8(uint8_t value) { append(&value, sizeof(value)); }
    void u16(uint16_t value) { append(&value, sizeof(value)); }
    void u32(uint32_t value) { append(&value, sizeof(value)); }
    void i32(int32_t value) { append(&value, sizeof(value)); }
    void f32(float value) { append(&value, sizeof(value)); }
    void f64(double value) { append(&value, sizeof(value)); }

    template <typename T>
    void pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "pod() requires a trivially copyable type");
        append(&value, sizeof(T));
    }

    template <typename T>
    void podArray(const std::vector<T>& values) {
        static_assert(std::is_trivially_copyable_v<T>, "podArray() requires trivially copyable elements");
        if (!values.empty()) {
            append(values.data(), values.size() * sizeof(T));
        }
    }

    template <typename T>
    void array(const std::vector<T>& values) {
        i32(static_cast<int32_t>(values.size()));
        podArray(values);
    }

    void version(int32_t value) { i32(value); }

    const std::vector<uint8_t>& bytes() const { return m_bytes; }
    std::vector<uint8_t> take() { return std::move(m_bytes); }
    size_t size() const { return m_bytes.size(); }

private:
    void append(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_bytes.insert(m_bytes.end(), bytes, bytes + size);
    }

    std::vector<uint8_t> m_bytes;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : m_data(data) {}

    uint8_t u8() { return pod<uint8_t>(); }
    uint16_t u16() { return pod<uint16_t>(); }
    uint32_t u32() { return pod<uint32_t>(); }
    int32_t i32() { return pod<int32_t>(); }
    float f32() { return pod<float>(); }
    double f64() { return pod<double>(); }

    template <typename T>
    T pod() {
        static_assert(std::is_trivially_copyable_v<T>, "pod() requires a trivially copyable type");
        require(sizeof(T));
        T value{};
        std::memcpy(&value, m_data.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return value;
    }

    template <typename T>
    std::vector<T> array() {
        static_assert(std::is_trivially_copyable_v<T>, "array() requires trivially copyable elements");
        const int32_t count = i32();
        if (count < 0) {
            throw std::runtime_error("negative array count");
        }
        const size_t bytes = static_cast<size_t>(count) * sizeof(T);
        require(bytes);
        std::vector<T> values(static_cast<size_t>(count));
        if (bytes != 0) {
            std::memcpy(values.data(), m_data.data() + m_offset, bytes);
        }
        m_offset += bytes;
        return values;
    }

    int32_t version() { return i32(); }

    void skip(size_t size) {
        require(size);
        m_offset += size;
    }

    size_t offset() const { return m_offset; }
    size_t remaining() const { return m_data.size() - m_offset; }
    size_t size() const { return m_data.size(); }

    void require(size_t size) const {
        if (size > remaining()) {
            throw std::runtime_error("truncated file at offset " + std::to_string(m_offset) +
                                     " (need " + std::to_string(size) + " bytes, have " +
                                     std::to_string(remaining()) + ")");
        }
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_offset = 0;
};

std::vector<uint8_t> readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);

void writeScene(Writer& writer, const SceneInfo& scene, int32_t version);
void readScene(Reader& reader, SceneInfo& scene, int32_t version);

void writeMesh(Writer& writer, const MeshInfo& mesh, int32_t version);
void readMesh(Reader& reader, MeshInfo& mesh, int32_t version);

void writeMaterial(Writer& writer, const MaterialInfo& material, int32_t version);
void readMaterial(Reader& reader, MaterialInfo& material, int32_t version);

void writeBakingJobInputs(Writer& writer, const BakingJobInputs& jobs, int32_t version);
void readBakingJobInputs(Reader& reader, BakingJobInputs& jobs, int32_t version);
SceneInfo readSceneFile(const std::filesystem::path& path);
BakingJobInputs readBakingJobInputsFile(const std::filesystem::path& path);

void writeLightmap2DOutput(Writer& writer, const Lightmap2DOutput& output);
void readLightmap2DOutput(Reader& reader, Lightmap2DOutput& output);
Lightmap2DOutput readLightmap2DOutputFile(const std::filesystem::path& path);

void writeSdfShadowOutput(Writer& writer, const SdfShadowOutput& output);
void readSdfShadowOutput(Reader& reader, SdfShadowOutput& output);
SdfShadowOutput readSdfShadowOutputFile(const std::filesystem::path& path);

}  // namespace hbg::dawn
