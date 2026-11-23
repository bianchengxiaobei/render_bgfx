#pragma once

#include "vfx/asset.h"
#include "vfx/graph.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vfx::editor {

enum class ContextKind : std::uint8_t {
    Spawner,
    Init,
    Update,
    Output,
};

struct ContextRef {
    std::uint32_t system = 0;
    ContextKind kind = ContextKind::Update;
    std::uint32_t output = 0;

    bool operator==(const ContextRef& other) const noexcept {
        return system == other.system && kind == other.kind && output == other.output;
    }
};

struct NodeLayout {
    ContextRef context;
    Vec2 position;
    bool collapsed = false;
};

struct ViewLayout {
    Vec2 pan;
    float zoom = 1.0f;
};

inline constexpr std::uint32_t kAppend = 0xffffffffu;

struct ErrorFocus {
    std::optional<ContextRef> context;
    std::uint32_t block = kAppend;
    int property = -1;
};

std::vector<std::string> blockTypes(ContextKind kind);
std::string contextPath(const ContextRef& context);
std::optional<ContextRef> parseContextPath(std::string_view path);
std::optional<ErrorFocus> errorFocus(std::string_view jsonPath);
std::filesystem::path defaultLayoutPath(const std::filesystem::path& assetPath);
std::filesystem::path defaultBinaryPath(const std::filesystem::path& assetPath);

class Document {
  public:
    static std::unique_ptr<Document> load(const std::filesystem::path& assetPath, std::string& error);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const std::filesystem::path& assetPath() const;
    const std::filesystem::path& layoutPath() const;
    const Graph& graph() const;
    const std::vector<ParseError>& errors() const;
    const std::string& lastError() const;
    const std::string& layoutError() const;

    bool dirty() const;
    bool layoutDirty() const;
    bool layoutStale() const;

    std::string serialize() const;
    bool save(std::string& error);

    bool addSystem(std::string_view name = {});
    bool removeSystem(std::uint32_t system);
    bool renameSystem(std::uint32_t system, std::string_view name);
    bool addBlock(std::uint32_t system, ContextKind context, std::string_view type, std::uint32_t index = kAppend);
    bool removeBlock(std::uint32_t system, ContextKind context, std::uint32_t block);
    bool moveBlock(std::uint32_t system, ContextKind context, std::uint32_t from, std::uint32_t to);
    bool addOutput(std::uint32_t system);
    bool removeOutput(std::uint32_t system, std::uint32_t output);
    bool addProperty(std::string_view name, PropertyType type);
    bool removeProperty(std::string_view name);

    bool setSystemCapacity(std::uint32_t system, std::uint32_t capacity);
    bool setSystemSpace(std::uint32_t system, SystemSpace space);
    bool setSystemSort(std::uint32_t system, SortMode mode, SortOrder order);
    bool setSystemBounds(std::uint32_t system, Vec3 center, Vec3 extents);
    bool setSpawnerRate(std::uint32_t system, float rate);
    bool setBlockEnabled(std::uint32_t system, ContextKind context, std::uint32_t block, bool enabled);
    bool setBlockParameterNumber(std::uint32_t system, ContextKind context, std::uint32_t block,
                                 std::string_view parameter, int component, double value);
    bool setBlockParameterBool(std::uint32_t system, ContextKind context, std::uint32_t block,
                               std::string_view parameter, bool value);
    bool setBlockParameterString(std::uint32_t system, ContextKind context, std::uint32_t block,
                                 std::string_view parameter, std::string_view value);
    bool setOutputLit(std::uint32_t system, std::uint32_t output, bool lit);
    bool setOutputBlend(std::uint32_t system, std::uint32_t output, BlendMode blend);
    bool setOutputTexture(std::uint32_t system, std::uint32_t output, std::string_view texture);
    bool setOutputSoftParticles(std::uint32_t system, std::uint32_t output, bool enabled);
    bool setOutputSoftDistance(std::uint32_t system, std::uint32_t output, float distance);
    bool setOutputMetallic(std::uint32_t system, std::uint32_t output, float metallic);
    bool setOutputRoughness(std::uint32_t system, std::uint32_t output, float roughness);
    bool setPropertyNumber(std::string_view property, std::uint32_t component, double value);
    bool setPropertyTexture(std::string_view property, std::string_view path);
    bool setPropertyCurveKey(std::string_view property, std::uint32_t key, float time, float value);
    bool addPropertyCurveKey(std::string_view property);
    bool removePropertyCurveKey(std::string_view property, std::uint32_t key);
    bool setPropertyGradientKey(std::string_view property, std::uint32_t key, float time, Vec4 color);
    bool addPropertyGradientKey(std::string_view property);
    bool removePropertyGradientKey(std::string_view property, std::uint32_t key);

    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

    const std::vector<NodeLayout>& layout() const;
    const ViewLayout& view() const;

    void setNodePosition(ContextRef context, Vec2 position);
    void setNodeCollapsed(ContextRef context, bool collapsed);
    void setViewLayout(ViewLayout view);
    void resetLayout();

    bool loadLayout(std::string& error);
    bool saveLayout(std::string& error);

  private:
    Document();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vfx::editor
