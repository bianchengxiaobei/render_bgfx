#include "vfx/editor.h"

#include "core/block_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace vfx::editor {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::uint32_t kLayoutVersion = 1;
constexpr std::size_t kMaxHistory = 128;
constexpr auto kHistoryCoalesce = std::chrono::milliseconds(500);

std::string indexedKey(std::string_view prefix, std::uint32_t value) {
    return std::string(prefix) + ":" + std::to_string(value);
}

std::uint64_t stableStringHash(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const char character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string blockParameterKey(std::uint32_t system, ContextKind context, std::uint32_t block,
                              std::string_view parameter, int component) {
    return "block.param:" + std::to_string(system) + ":" + std::to_string(static_cast<int>(context)) + ":" +
           std::to_string(block) + ":" + std::string(parameter) + ":" + std::to_string(component);
}

std::string outputKey(std::string_view prefix, std::uint32_t system, std::uint32_t output) {
    return std::string(prefix) + ":" + std::to_string(system) + ":" + std::to_string(output);
}

std::string propertyKey(std::string_view prefix, std::string_view property, int component = -1) {
    return std::string(prefix) + ":" + std::string(property) + ":" + std::to_string(component);
}

Json defaultPropertyJson(std::string_view name, PropertyType type) {
    Json property = Json::object();
    property["name"] = name;
    property["type"] = toString(type);
    switch (type) {
    case PropertyType::Float:
        property["default"] = 0.0;
        break;
    case PropertyType::Int:
        property["default"] = 0;
        break;
    case PropertyType::Bool:
        property["default"] = false;
        break;
    case PropertyType::Vector2:
        property["default"] = Json::array({0.0, 0.0});
        break;
    case PropertyType::Vector3:
        property["default"] = Json::array({0.0, 0.0, 0.0});
        break;
    case PropertyType::Vector4:
        property["default"] = Json::array({0.0, 0.0, 0.0, 0.0});
        break;
    case PropertyType::Color:
        property["default"] = Json::array({1.0, 1.0, 1.0, 1.0});
        break;
    case PropertyType::Texture:
        property["default"] = "";
        break;
    case PropertyType::Curve:
        property["keys"] = Json::array({Json::array({0.0, 1.0}), Json::array({1.0, 0.0})});
        break;
    case PropertyType::Gradient:
        property["keys"] = Json::array({
            Json{{"t", 0.0}, {"color", Json::array({1.0, 1.0, 1.0, 1.0})}},
            Json{{"t", 1.0}, {"color", Json::array({1.0, 1.0, 1.0, 1.0})}},
        });
        break;
    }
    return property;
}

double keyTime(const Json& key, bool gradient) {
    if (gradient) {
        if (!key.is_object()) {
            return -1.0;
        }
        const auto found = key.find("t");
        return found != key.end() && found->is_number() ? found->get<double>() : -1.0;
    }
    if (!key.is_array() || key.empty() || !key[0].is_number()) {
        return -1.0;
    }
    return key[0].get<double>();
}

double freeKeyTime(const Json& keys, bool gradient) {
    for (int step = 0; step <= 10; ++step) {
        const double candidate = static_cast<double>(step) * 0.1;
        const bool used = std::any_of(keys.begin(), keys.end(), [&](const Json& key) {
            return std::abs(keyTime(key, gradient) - candidate) < 1e-6;
        });
        if (!used) {
            return candidate;
        }
    }
    return keyTime(keys.back(), gradient) + 0.1;
}

std::string readFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        error = "failed to open '" + path.string() + "'";
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string firstError(const std::vector<ParseError>& errors) {
    if (errors.empty()) {
        return "unknown editor error";
    }
    std::string message = formatParseError(errors.front());
    if (errors.size() > 1) {
        message += " (and " + std::to_string(errors.size() - 1) + " more)";
    }
    return message;
}

std::string contextName(ContextKind kind) {
    switch (kind) {
    case ContextKind::Spawner:
        return "spawner";
    case ContextKind::Init:
        return "init";
    case ContextKind::Update:
        return "update";
    case ContextKind::Output:
        return "outputs";
    }
    return "update";
}

bool parseUint(std::string_view text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

Vec2 layoutPosition(std::uint32_t system, ContextKind kind, std::uint32_t output) {
    const float x = 48.0f + static_cast<float>(system) * 420.0f;
    float y = 40.0f;
    switch (kind) {
    case ContextKind::Spawner:
        y = 40.0f;
        break;
    case ContextKind::Init:
        y = 150.0f;
        break;
    case ContextKind::Update:
        y = 330.0f;
        break;
    case ContextKind::Output:
        y = 500.0f + static_cast<float>(output) * 190.0f;
        break;
    }
    return Vec2{x, y};
}

void collectContexts(const Graph& graph, std::vector<ContextRef>& contexts) {
    contexts.clear();
    for (std::uint32_t system = 0; system < graph.systems.size(); ++system) {
        contexts.push_back(ContextRef{system, ContextKind::Spawner, 0});
        contexts.push_back(ContextRef{system, ContextKind::Init, 0});
        contexts.push_back(ContextRef{system, ContextKind::Update, 0});
        for (std::uint32_t output = 0; output < graph.systems[system].outputs.size(); ++output) {
            contexts.push_back(ContextRef{system, ContextKind::Output, output});
        }
    }
}

Json* findSystem(Json& root, std::uint32_t system) {
    const auto systems = root.find("systems");
    if (systems == root.end() || !systems->is_array() || system >= systems->size()) {
        return nullptr;
    }
    Json& value = (*systems)[system];
    return value.is_object() ? &value : nullptr;
}

const Json* findSystem(const Json& root, std::uint32_t system) {
    const auto systems = root.find("systems");
    if (systems == root.end() || !systems->is_array() || system >= systems->size()) {
        return nullptr;
    }
    const Json& value = (*systems)[system];
    return value.is_object() ? &value : nullptr;
}

Json* findOutput(Json& system, std::uint32_t output) {
    const auto outputs = system.find("outputs");
    if (outputs == system.end() || !outputs->is_array() || output >= outputs->size()) {
        return nullptr;
    }
    Json& value = (*outputs)[output];
    return value.is_object() ? &value : nullptr;
}

Json* findBlock(Json& system, ContextKind context, std::uint32_t block) {
    const char* name = nullptr;
    switch (context) {
    case ContextKind::Init:
        name = "init";
        break;
    case ContextKind::Update:
        name = "update";
        break;
    case ContextKind::Spawner:
    case ContextKind::Output:
        return nullptr;
    }
    auto stage = system.find(name);
    if (stage == system.end() || !stage->is_object()) {
        return nullptr;
    }
    auto blocks = stage->find("blocks");
    if (blocks == stage->end() || !blocks->is_array() || block >= blocks->size()) {
        return nullptr;
    }
    Json& value = (*blocks)[block];
    return value.is_object() ? &value : nullptr;
}

Json* findProperty(Json& root, std::string_view name) {
    const auto properties = root.find("properties");
    if (properties == root.end() || !properties->is_array()) {
        return nullptr;
    }
    for (auto& property : *properties) {
        if (!property.is_object()) {
            continue;
        }
        const auto found = property.find("name");
        if (found != property.end() && found->is_string() && found->get<std::string>() == name) {
            return &property;
        }
    }
    return nullptr;
}

const Json* findBlock(const Json& system, ContextKind context, std::uint32_t block) {
    const char* name = nullptr;
    switch (context) {
    case ContextKind::Init:
        name = "init";
        break;
    case ContextKind::Update:
        name = "update";
        break;
    case ContextKind::Spawner:
    case ContextKind::Output:
        return nullptr;
    }
    const auto stage = system.find(name);
    if (stage == system.end() || !stage->is_object()) {
        return nullptr;
    }
    const auto blocks = stage->find("blocks");
    if (blocks == stage->end() || !blocks->is_array() || block >= blocks->size()) {
        return nullptr;
    }
    const Json& value = (*blocks)[block];
    return value.is_object() ? &value : nullptr;
}

Json* blockArray(Json& system, ContextKind context) {
    const char* name = nullptr;
    switch (context) {
    case ContextKind::Init:
        name = "init";
        break;
    case ContextKind::Update:
        name = "update";
        break;
    case ContextKind::Spawner:
    case ContextKind::Output:
        return nullptr;
    }
    auto stage = system.find(name);
    if (stage == system.end() || !stage->is_object()) {
        return nullptr;
    }
    auto blocks = stage->find("blocks");
    return blocks != stage->end() && blocks->is_array() ? &*blocks : nullptr;
}

const Json* findProperty(const Json& root, std::string_view name) {
    const auto properties = root.find("properties");
    if (properties == root.end() || !properties->is_array()) {
        return nullptr;
    }
    for (const auto& property : *properties) {
        if (!property.is_object()) {
            continue;
        }
        const auto found = property.find("name");
        if (found != property.end() && found->is_string() && found->get<std::string>() == name) {
            return &property;
        }
    }
    return nullptr;
}

const Json* findSubgraph(const Json& root, std::string_view name) {
    const auto subgraphs = root.find("subgraphs");
    if (subgraphs == root.end() || !subgraphs->is_array()) {
        return nullptr;
    }
    for (const auto& subgraph : *subgraphs) {
        if (!subgraph.is_object()) {
            continue;
        }
        const auto found = subgraph.find("name");
        if (found != subgraph.end() && found->is_string() && found->get<std::string>() == name) {
            return &subgraph;
        }
    }
    return nullptr;
}

std::string uniqueName(std::string base, const std::function<bool(const std::string&)>& exists,
                       std::string_view separator = " ") {
    if (base.empty()) {
        base = "Copy";
    }
    if (!exists(base)) {
        return base;
    }
    for (std::uint32_t suffix = 1;; ++suffix) {
        const std::string candidate = base + std::string(separator) + std::to_string(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
    }
}

bool hasPropertyName(const Json& root, const std::string& name) {
    return findProperty(root, name) != nullptr;
}

bool hasSystemName(const Json& root, const std::string& name) {
    const auto systems = root.find("systems");
    if (systems == root.end() || !systems->is_array()) {
        return false;
    }
    return std::any_of(systems->begin(), systems->end(), [&](const Json& system) {
        const auto found = system.find("name");
        return found != system.end() && found->is_string() && found->get<std::string>() == name;
    });
}

const Property* findGraphProperty(const Graph& graph, std::string_view name) {
    const auto found = std::find_if(graph.properties.begin(), graph.properties.end(),
                                    [&](const Property& property) { return property.name == name; });
    return found == graph.properties.end() ? nullptr : &*found;
}

} // namespace

std::vector<std::string> blockTypes(ContextKind kind) {
    if (kind != ContextKind::Init && kind != ContextKind::Update) {
        return {};
    }
    const auto specs = kind == ContextKind::Init ? detail::initBlockSpecs() : detail::updateBlockSpecs();
    std::vector<std::string> types;
    types.reserve(specs.size());
    for (const detail::BlockSpec& spec : specs) {
        types.emplace_back(spec.type);
    }
    return types;
}

std::string contextPath(const ContextRef& context) {
    if (context.kind == ContextKind::Output) {
        return "systems/" + std::to_string(context.system) + "/outputs/" + std::to_string(context.output);
    }
    return "systems/" + std::to_string(context.system) + "/" + contextName(context.kind);
}

std::optional<ContextRef> parseContextPath(std::string_view path) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        parts.push_back(path.substr(start, end - start));
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    if (parts.size() < 3 || parts[0] != "systems") {
        return std::nullopt;
    }
    ContextRef context;
    if (!parseUint(parts[1], context.system)) {
        return std::nullopt;
    }
    if (parts[2] == "spawner" && parts.size() == 3) {
        context.kind = ContextKind::Spawner;
        return context;
    }
    if (parts[2] == "init" && parts.size() == 3) {
        context.kind = ContextKind::Init;
        return context;
    }
    if (parts[2] == "update" && parts.size() == 3) {
        context.kind = ContextKind::Update;
        return context;
    }
    if (parts[2] == "outputs" && parts.size() == 4 && parseUint(parts[3], context.output)) {
        context.kind = ContextKind::Output;
        return context;
    }
    return std::nullopt;
}

std::filesystem::path defaultLayoutPath(const std::filesystem::path& assetPath) {
    std::filesystem::path layout = assetPath;
    if (layout.extension() == ".json") {
        layout.replace_extension();
    }
    return std::filesystem::path(layout.string() + ".editor.json");
}

std::filesystem::path defaultBinaryPath(const std::filesystem::path& assetPath) {
    const std::string filename = assetPath.filename().string();
    constexpr std::string_view suffix = ".vfx.json";
    if (filename.size() > suffix.size() &&
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return assetPath.parent_path() / (filename.substr(0, filename.size() - suffix.size()) + ".vfxb");
    }
    return assetPath.parent_path() / (assetPath.stem().string() + ".vfxb");
}

std::filesystem::path defaultRecoveryPath(const std::filesystem::path& assetPath) {
    std::error_code error;
    std::filesystem::path directory = std::filesystem::temp_directory_path(error);
    if (error) {
        directory = assetPath.parent_path();
    }
    const std::filesystem::path absolute = std::filesystem::absolute(assetPath, error).lexically_normal();
    const std::string normalized = error ? assetPath.lexically_normal().string() : absolute.string();
    return directory / ("hbg_vfx_recovery_" + std::to_string(stableStringHash(normalized)) + ".json");
}

std::optional<ErrorFocus> errorFocus(std::string_view path) {
    const auto parseIndex = [&](std::size_t open, std::size_t& next) -> std::optional<std::uint32_t> {
        if (open >= path.size() || path[open] != '[') {
            return std::nullopt;
        }
        const std::size_t close = path.find(']', open + 1);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        const auto parsed = std::from_chars(path.data() + open + 1, path.data() + close, value);
        if (parsed.ec != std::errc{} || parsed.ptr != path.data() + close) {
            return std::nullopt;
        }
        next = close + 1;
        return value;
    };

    if (path.rfind("properties[", 0) == 0) {
        std::size_t next = 0;
        const auto property = parseIndex(std::string_view("properties").size(), next);
        if (!property) {
            return std::nullopt;
        }
        ErrorFocus focus;
        focus.property = static_cast<int>(*property);
        return focus;
    }

    constexpr std::string_view prefix = "systems[";
    if (path.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    std::size_t next = 0;
    const auto system = parseIndex(prefix.size() - 1, next);
    if (!system) {
        return std::nullopt;
    }

    ErrorFocus focus;
    focus.context = ContextRef{*system, ContextKind::Update, 0};
    if (next >= path.size()) {
        return focus;
    }
    if (path[next] != '.') {
        return std::nullopt;
    }
    const std::string_view suffix = path.substr(next + 1);
    if (suffix.rfind("spawner", 0) == 0) {
        focus.context->kind = ContextKind::Spawner;
        return focus;
    }
    if (suffix.rfind("init", 0) == 0) {
        focus.context->kind = ContextKind::Init;
    } else if (suffix.rfind("update", 0) == 0) {
        focus.context->kind = ContextKind::Update;
    } else if (suffix.rfind("outputs[", 0) == 0) {
        std::size_t outputNext = 0;
        const std::size_t outputOpen = next + 1 + suffix.find('[');
        const auto output = parseIndex(outputOpen, outputNext);
        if (!output) {
            return std::nullopt;
        }
        focus.context->kind = ContextKind::Output;
        focus.context->output = *output;
        return focus;
    } else {
        return focus;
    }

    const std::size_t blocks = suffix.find(".blocks[");
    if (blocks != std::string_view::npos) {
        std::size_t blockNext = 0;
        const std::size_t blockOpen = next + 1 + blocks + std::string_view(".blocks").size();
        const auto block = parseIndex(blockOpen, blockNext);
        if (block) {
            focus.block = *block;
        }
    }
    return focus;
}

struct Document::Impl {
    struct Snapshot {
        Json root;
        bool dirty = false;
        bool layoutStale = false;
    };

    std::filesystem::path assetPath;
    std::filesystem::path layoutPath;
    std::filesystem::path recoveryPath;
    Json root;
    std::unique_ptr<Graph> graph;
    std::vector<ParseError> errors;
    std::vector<NodeLayout> nodes;
    ViewLayout view;
    std::uint64_t layoutHash = 0;
    bool layoutLoaded = false;
    bool layoutStale = false;
    bool dirty = false;
    bool layoutDirty = false;
    std::string lastError;
    std::string layoutError;
    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
    std::string lastHistoryKey;
    std::chrono::steady_clock::time_point lastHistoryTime{};

    void ensureLayout() {
        std::vector<ContextRef> contexts;
        collectContexts(*graph, contexts);
        std::vector<NodeLayout> next;
        next.reserve(contexts.size());
        for (const ContextRef& context : contexts) {
            const auto found = std::find_if(nodes.begin(), nodes.end(),
                                            [&](const NodeLayout& node) { return node.context == context; });
            if (found != nodes.end()) {
                next.push_back(*found);
            } else {
                next.push_back(
                    NodeLayout{context, layoutPosition(context.system, context.kind, context.output), false});
            }
        }
        nodes = std::move(next);
    }

    void resetLayout() {
        nodes.clear();
        std::vector<ContextRef> contexts;
        collectContexts(*graph, contexts);
        nodes.reserve(contexts.size());
        for (const ContextRef& context : contexts) {
            nodes.push_back(NodeLayout{context, layoutPosition(context.system, context.kind, context.output), false});
        }
    }

    bool mutate(const std::function<bool(Json&)>& operation, std::string_view historyKey = {}) {
        lastError.clear();
        errors.clear();
        Json backup = root;
        const bool wasDirty = dirty;
        const bool wasLayoutStale = layoutStale;
        if (!operation(root)) {
            root = std::move(backup);
            dirty = wasDirty;
            layoutStale = wasLayoutStale;
            lastHistoryKey.clear();
            return false;
        }
        ParseResult parsed = parseGraphJson(root.dump(), assetPath.string(), GraphParseMode::PreserveSubgraphs);
        if (!parsed.ok()) {
            root = std::move(backup);
            dirty = wasDirty;
            layoutStale = wasLayoutStale;
            errors = std::move(parsed.errors);
            lastError = firstError(errors);
            lastHistoryKey.clear();
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool coalesce = !historyKey.empty() && historyKey == lastHistoryKey && !undoStack.empty() &&
                              now - lastHistoryTime < kHistoryCoalesce;
        if (!coalesce) {
            undoStack.push_back(Snapshot{std::move(backup), wasDirty, wasLayoutStale});
            if (undoStack.size() > kMaxHistory) {
                undoStack.erase(undoStack.begin());
            }
            redoStack.clear();
            lastHistoryKey = std::string(historyKey);
        }
        lastHistoryTime = now;

        graph = std::move(parsed.graph);
        dirty = true;
        if (layoutLoaded) {
            layoutStale = layoutHash != graphHash(*graph);
        }
        ensureLayout();
        return true;
    }

    bool restore(Snapshot snapshot) {
        ParseResult parsed = parseGraphJson(snapshot.root.dump(), assetPath.string(), GraphParseMode::PreserveSubgraphs);
        if (!parsed.ok()) {
            errors = std::move(parsed.errors);
            lastError = firstError(errors);
            return false;
        }
        const std::vector<NodeLayout> previousLayout = nodes;
        root = std::move(snapshot.root);
        graph = std::move(parsed.graph);
        dirty = snapshot.dirty;
        if (layoutLoaded) {
            layoutStale = layoutHash != graphHash(*graph);
        } else {
            layoutStale = snapshot.layoutStale;
        }
        lastError.clear();
        errors.clear();
        lastHistoryKey.clear();
        ensureLayout();
        if (previousLayout.size() != nodes.size()) {
            layoutDirty = true;
        } else {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (!(previousLayout[i].context == nodes[i].context)) {
                    layoutDirty = true;
                    break;
                }
            }
        }
        return true;
    }

    void setEditError(std::string path, std::string message) {
        ParseError error;
        error.path = std::move(path);
        error.message = std::move(message);
        error.source = assetPath.string();
        lastError = formatParseError(error);
        errors = {std::move(error)};
    }
};

Document::Document() : m_impl(std::make_unique<Impl>()) {}

Document::~Document() = default;

std::unique_ptr<Document> Document::load(const std::filesystem::path& assetPath, std::string& error) {
    std::string contents = readFile(assetPath, error);
    if (contents.empty()) {
        if (error.empty()) {
            error = "asset is empty";
        }
        return nullptr;
    }
    ParseResult parsed = parseGraphJson(contents, assetPath.string(), GraphParseMode::PreserveSubgraphs);
    if (!parsed.ok()) {
        error = firstError(parsed.errors);
        return nullptr;
    }

    Json root;
    try {
        root = Json::parse(contents);
    } catch (const std::exception& exception) {
        error = exception.what();
        return nullptr;
    }
    if (!root.is_object()) {
        error = "asset root must be an object";
        return nullptr;
    }

    auto document = std::unique_ptr<Document>(new Document());
    document->m_impl->assetPath = assetPath;
    document->m_impl->layoutPath = defaultLayoutPath(assetPath);
    document->m_impl->recoveryPath = defaultRecoveryPath(assetPath);
    document->m_impl->root = std::move(root);
    document->m_impl->graph = std::move(parsed.graph);
    document->m_impl->resetLayout();

    if (std::filesystem::exists(document->m_impl->layoutPath)) {
        std::string layoutError;
        if (!document->loadLayout(layoutError)) {
            document->m_impl->layoutError = layoutError;
            document->m_impl->resetLayout();
        }
    }
    document->m_impl->dirty = false;
    document->m_impl->layoutDirty = false;
    return document;
}

const std::filesystem::path& Document::assetPath() const {
    return m_impl->assetPath;
}
const std::filesystem::path& Document::layoutPath() const {
    return m_impl->layoutPath;
}
const Graph& Document::graph() const {
    return *m_impl->graph;
}
const std::vector<ParseError>& Document::errors() const {
    return m_impl->errors;
}
const std::string& Document::lastError() const {
    return m_impl->lastError;
}
const std::string& Document::layoutError() const {
    return m_impl->layoutError;
}
bool Document::dirty() const {
    return m_impl->dirty;
}
bool Document::layoutDirty() const {
    return m_impl->layoutDirty;
}
bool Document::layoutStale() const {
    return m_impl->layoutStale;
}
const std::vector<NodeLayout>& Document::layout() const {
    return m_impl->nodes;
}
const ViewLayout& Document::view() const {
    return m_impl->view;
}

std::string Document::serialize() const {
    return m_impl->root.dump(2) + "\n";
}

bool Document::save(std::string& error) {
    std::ofstream file(m_impl->assetPath, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        error = "failed to write '" + m_impl->assetPath.string() + "'";
        return false;
    }
    file << serialize();
    if (!file.good()) {
        error = "failed to write '" + m_impl->assetPath.string() + "'";
        return false;
    }
    m_impl->dirty = false;
    m_impl->lastHistoryKey.clear();
    clearRecovery();
    return true;
}

const std::filesystem::path& Document::recoveryPath() const {
    return m_impl->recoveryPath;
}

bool Document::saveRecovery(std::string& error) const {
    if (m_impl->recoveryPath.empty()) {
        error = "recovery path is not configured";
        return false;
    }
    std::ofstream file(m_impl->recoveryPath, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        error = "failed to write recovery '" + m_impl->recoveryPath.string() + "'";
        return false;
    }
    const std::string contents = serialize();
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file.good()) {
        error = "failed to write recovery '" + m_impl->recoveryPath.string() + "'";
        return false;
    }
    return true;
}

bool Document::loadRecovery(std::string& error) {
    std::string contents = readFile(m_impl->recoveryPath, error);
    if (contents.empty()) {
        if (error.empty()) {
            error = "recovery file is empty";
        }
        return false;
    }
    ParseResult parsed = parseGraphJson(contents, m_impl->assetPath.string(), GraphParseMode::PreserveSubgraphs);
    if (!parsed.ok()) {
        error = parsed.errors.empty() ? "failed to parse recovery" : firstError(parsed.errors);
        return false;
    }
    Json root;
    try {
        root = Json::parse(contents);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "recovery root must be an object";
        return false;
    }

    m_impl->root = std::move(root);
    m_impl->graph = std::move(parsed.graph);
    m_impl->dirty = true;
    m_impl->layoutDirty = true;
    m_impl->errors.clear();
    m_impl->lastError.clear();
    m_impl->lastHistoryKey.clear();
    m_impl->undoStack.clear();
    m_impl->redoStack.clear();
    if (m_impl->layoutLoaded) {
        m_impl->layoutStale = m_impl->layoutHash != graphHash(*m_impl->graph);
    }
    m_impl->ensureLayout();
    return true;
}

void Document::clearRecovery() {
    if (m_impl->recoveryPath.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(m_impl->recoveryPath, error);
}

bool Document::addSystem(std::string_view name) {
    return m_impl->mutate([&](Json& root) {
        auto systems = root.find("systems");
        if (systems == root.end() || !systems->is_array()) {
            m_impl->setEditError("systems", "systems array is missing");
            return false;
        }
        std::string unique = std::string(name);
        if (unique.empty()) {
            for (std::uint32_t suffix = 1;; ++suffix) {
                unique = "System " + std::to_string(suffix);
                const bool exists = std::any_of(systems->begin(), systems->end(), [&](const Json& value) {
                    const auto found = value.find("name");
                    return found != value.end() && found->is_string() && found->get<std::string>() == unique;
                });
                if (!exists) {
                    break;
                }
            }
        }
        Json system = Json::object();
        system["name"] = unique;
        system["capacity"] = 64;
        system["space"] = "world";
        system["bounds"] = Json{
            {"mode", "manual"}, {"center", Json::array({0.0, 1.0, 0.0})}, {"extents", Json::array({4.0, 4.0, 4.0})}};
        system["spawner"] = Json{{"rate", 10.0}, {"bursts", Json::array()}};
        system["init"] = Json{{"blocks", Json::array({
                                             Json{{"type", "set_position_box"},
                                                  {"center", Json::array({0.0, 0.0, 0.0})},
                                                  {"size", Json::array({1.0, 1.0, 1.0})}},
                                             Json{{"type", "set_lifetime"}, {"lifetime", 1.0}},
                                         })}};
        system["update"] = Json{{"blocks", Json::array({
                                               Json{{"type", "integrate"}},
                                               Json{{"type", "age"}},
                                               Json{{"type", "reap"}},
                                           })}};
        system["outputs"] = Json::array({Json{{"type", "quad"}, {"lit", false}, {"blend", "alpha"}, {"texture", ""}}});
        systems->push_back(std::move(system));
        return true;
    });
}

bool Document::removeSystem(std::uint32_t system) {
    return m_impl->mutate([&](Json& root) {
        auto systems = root.find("systems");
        if (systems == root.end() || !systems->is_array() || system >= systems->size()) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        if (systems->size() <= 1) {
            m_impl->setEditError("systems", "at least one system is required");
            return false;
        }
        systems->erase(systems->begin() + static_cast<std::ptrdiff_t>(system));
        return true;
    });
}

bool Document::renameSystem(std::uint32_t system, std::string_view name) {
    return m_impl->mutate([&](Json& root) {
        Json* value = findSystem(root, system);
        if (value == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        if (name.empty()) {
            m_impl->setEditError("systems", "system name must not be empty");
            return false;
        }
        (*value)["name"] = name;
        return true;
    });
}

bool Document::duplicateSystem(std::uint32_t system, std::uint32_t& duplicated) {
    return m_impl->mutate([&](Json& root) {
        auto systems = root.find("systems");
        if (systems == root.end() || !systems->is_array() || system >= systems->size()) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        if (!(*systems)[system].is_object()) {
            m_impl->setEditError("systems", "system is malformed");
            return false;
        }
        Json copy = (*systems)[system];
        const auto name = copy.find("name");
        const std::string base = name != copy.end() && name->is_string() ? name->get<std::string>() : "System";
        copy["name"] = uniqueName(base, [&](const std::string& candidate) {
            return hasSystemName(root, candidate);
        });
        systems->push_back(std::move(copy));
        duplicated = static_cast<std::uint32_t>(systems->size() - 1);
        return true;
    });
}

bool Document::addBlock(std::uint32_t system, ContextKind context, std::string_view type, std::uint32_t index) {
    std::string defaultError;
    const std::string defaults =
        detail::defaultBlockJson(type, context == ContextKind::Update, *m_impl->graph, system, defaultError);
    if (!defaultError.empty()) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks", defaultError);
        return false;
    }
    Json block;
    try {
        block = Json::parse(defaults);
    } catch (const std::exception& exception) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks", exception.what());
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        if (context != ContextKind::Init && context != ContextKind::Update) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}),
                                 "blocks are only supported on init/update contexts");
            return false;
        }
        const char* stageName = context == ContextKind::Init ? "init" : "update";
        auto stage = systemValue->find(stageName);
        if (stage == systemValue->end() || !stage->is_object()) {
            (*systemValue)[stageName] = Json{{"blocks", Json::array()}};
            stage = systemValue->find(stageName);
        }
        auto blocks = stage->find("blocks");
        if (blocks == stage->end() || !blocks->is_array()) {
            (*stage)["blocks"] = Json::array();
            blocks = stage->find("blocks");
        }
        const std::size_t insert = index == kAppend ? blocks->size() : static_cast<std::size_t>(index);
        if (insert > blocks->size()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        blocks->insert(blocks->begin() + static_cast<std::ptrdiff_t>(insert), std::move(block));
        return true;
    });
}

bool Document::addSubgraphBlock(std::uint32_t system, ContextKind context, std::string_view subgraph,
                                std::uint32_t index, std::uint32_t& inserted) {
    if (context != ContextKind::Init && context != ContextKind::Update) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}),
                             "subgraph blocks are only supported on init/update contexts");
        return false;
    }
    const Json* definition = findSubgraph(m_impl->root, subgraph);
    if (definition == nullptr) {
        m_impl->setEditError("subgraphs", "subgraph '" + std::string(subgraph) + "' was not found");
        return false;
    }
    const char* stageName = context == ContextKind::Init ? "init" : "update";
    const auto stage = definition->find(stageName);
    if (stage == definition->end() || !stage->is_object() || stage->find("blocks") == stage->end() ||
        !stage->find("blocks")->is_array() || stage->find("blocks")->empty()) {
        m_impl->setEditError("subgraphs", "subgraph '" + std::string(subgraph) + "' has no " + stageName +
                                              " blocks");
        return false;
    }

    Json block = Json{{"type", "subgraph"}, {"name", subgraph}};
    const auto inputs = definition->find("inputs");
    Json args = Json::object();
    if (inputs != definition->end() && inputs->is_array()) {
        for (const Json& input : *inputs) {
            if (!input.is_object()) {
                continue;
            }
            const auto name = input.find("name");
            if (name == input.end() || !name->is_string()) {
                continue;
            }
            const auto defaultValue = input.find("default");
            if (defaultValue == input.end()) {
                m_impl->setEditError("subgraphs", "subgraph '" + std::string(subgraph) + "' input '" +
                                                      name->get<std::string>() + "' has no default");
                return false;
            }
            args[name->get<std::string>()] = *defaultValue;
        }
    }
    if (!args.empty()) {
        block["args"] = std::move(args);
    }

    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        Json* blocks = blockArray(*systemValue, context);
        if (blocks == nullptr) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}), "stage is missing");
            return false;
        }
        const std::size_t insert = index == kAppend ? blocks->size() : static_cast<std::size_t>(index);
        if (insert > blocks->size()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        blocks->insert(blocks->begin() + static_cast<std::ptrdiff_t>(insert), std::move(block));
        inserted = static_cast<std::uint32_t>(insert);
        return true;
    });
}

std::vector<std::string> Document::subgraphNames(ContextKind context) const {
    if (context != ContextKind::Init && context != ContextKind::Update) {
        return {};
    }
    std::vector<std::string> names;
    const auto subgraphs = m_impl->root.find("subgraphs");
    if (subgraphs == m_impl->root.end() || !subgraphs->is_array()) {
        return names;
    }
    const char* stageName = context == ContextKind::Init ? "init" : "update";
    for (const Json& subgraph : *subgraphs) {
        if (!subgraph.is_object()) {
            continue;
        }
        const auto name = subgraph.find("name");
        const Json* blocks = nullptr;
        const auto stage = subgraph.find(stageName);
        if (stage != subgraph.end() && stage->is_object()) {
            const auto found = stage->find("blocks");
            if (found != stage->end() && found->is_array()) {
                blocks = &*found;
            }
        }
        if (name != subgraph.end() && name->is_string() && blocks != nullptr && !blocks->empty()) {
            names.push_back(name->get<std::string>());
        }
    }
    return names;
}

bool Document::copyBlock(std::uint32_t system, ContextKind context, std::uint32_t block,
                         std::string& json) const {
    const Json* systemValue = findSystem(m_impl->root, system);
    if (systemValue == nullptr) {
        return false;
    }
    const Json* blockValue = findBlock(*systemValue, context, block);
    if (blockValue == nullptr) {
        return false;
    }
    json = blockValue->dump();
    return true;
}

bool Document::pasteBlock(std::uint32_t system, ContextKind context, std::uint32_t index,
                          std::string_view json, std::uint32_t& inserted) {
    Json block;
    try {
        block = Json::parse(json);
    } catch (const std::exception& exception) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks", exception.what());
        return false;
    }
    if (!block.is_object()) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                             "clipboard block must be a JSON object");
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        Json* blocks = blockArray(*systemValue, context);
        if (blocks == nullptr) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}), "stage is missing");
            return false;
        }
        const std::size_t insert = index == kAppend ? blocks->size() : static_cast<std::size_t>(index);
        if (insert > blocks->size()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        blocks->insert(blocks->begin() + static_cast<std::ptrdiff_t>(insert), std::move(block));
        inserted = static_cast<std::uint32_t>(insert);
        return true;
    });
}

bool Document::duplicateBlock(std::uint32_t system, ContextKind context, std::uint32_t block,
                              std::uint32_t& duplicated) {
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        Json* blocks = blockArray(*systemValue, context);
        if (blocks == nullptr || block >= blocks->size()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        if (!(*blocks)[block].is_object()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block must be an object");
            return false;
        }
        Json copy = (*blocks)[block];
        const std::size_t insert = static_cast<std::size_t>(block) + 1u;
        blocks->insert(blocks->begin() + static_cast<std::ptrdiff_t>(insert), std::move(copy));
        duplicated = static_cast<std::uint32_t>(insert);
        return true;
    });
}

bool Document::removeBlock(std::uint32_t system, ContextKind context, std::uint32_t block) {
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        Json* blockValue = findBlock(*systemValue, context, block);
        if (blockValue == nullptr) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        const char* stageName = context == ContextKind::Init ? "init" : "update";
        auto blocks = systemValue->find(stageName)->find("blocks");
        blocks->erase(blocks->begin() + static_cast<std::ptrdiff_t>(block));
        return true;
    });
}

bool Document::moveBlock(std::uint32_t system, ContextKind context, std::uint32_t from, std::uint32_t to) {
    if (context != ContextKind::Init && context != ContextKind::Update) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}),
                             "blocks are only supported on init/update contexts");
        return false;
    }
    if (from == to) {
        return true;
    }
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        const char* stageName = context == ContextKind::Init ? "init" : "update";
        auto stage = systemValue->find(stageName);
        if (stage == systemValue->end() || !stage->is_object()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}), "stage is missing");
            return false;
        }
        auto blocks = stage->find("blocks");
        if (blocks == stage->end() || !blocks->is_array() || from >= blocks->size() || to >= blocks->size()) {
            m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                 "block index is out of range");
            return false;
        }
        Json block = std::move((*blocks)[from]);
        blocks->erase(blocks->begin() + static_cast<std::ptrdiff_t>(from));
        blocks->insert(blocks->begin() + static_cast<std::ptrdiff_t>(to), std::move(block));
        return true;
    });
}

bool Document::addOutput(std::uint32_t system) {
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        auto outputs = systemValue->find("outputs");
        if (outputs == systemValue->end() || !outputs->is_array()) {
            (*systemValue)["outputs"] = Json::array();
            outputs = systemValue->find("outputs");
        }
        Json output = Json{{"type", "quad"}, {"lit", false}, {"blend", "alpha"}, {"texture", ""}};
        if (system >= m_impl->graph->systems.size()) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        const vfx::SortMode sort = m_impl->graph->systems[system].sort.mode;
        if (sort != SortMode::None) {
            output["sort"] = toString(sort);
        }
        outputs->push_back(std::move(output));
        return true;
    });
}

bool Document::removeOutput(std::uint32_t system, std::uint32_t output) {
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        auto outputs = systemValue->find("outputs");
        if (outputs == systemValue->end() || !outputs->is_array() || output >= outputs->size()) {
            m_impl->setEditError("systems", "output index is out of range");
            return false;
        }
        if (outputs->size() <= 1) {
            m_impl->setEditError(contextPath(ContextRef{system, ContextKind::Output, output}),
                                 "at least one output is required");
            return false;
        }
        outputs->erase(outputs->begin() + static_cast<std::ptrdiff_t>(output));
        return true;
    });
}

bool Document::duplicateOutput(std::uint32_t system, std::uint32_t output, std::uint32_t& duplicated) {
    return m_impl->mutate([&](Json& root) {
        Json* systemValue = findSystem(root, system);
        if (systemValue == nullptr) {
            m_impl->setEditError("systems", "system index is out of range");
            return false;
        }
        auto outputs = systemValue->find("outputs");
        if (outputs == systemValue->end() || !outputs->is_array() || output >= outputs->size() ||
            !(*outputs)[output].is_object()) {
            m_impl->setEditError(contextPath(ContextRef{system, ContextKind::Output, output}),
                                 "output index is out of range");
            return false;
        }
        Json copy = (*outputs)[output];
        outputs->push_back(std::move(copy));
        duplicated = static_cast<std::uint32_t>(outputs->size() - 1);
        return true;
    });
}

bool Document::addProperty(std::string_view name, PropertyType type) {
    return m_impl->mutate([&](Json& root) {
        auto properties = root.find("properties");
        if (properties == root.end() || !properties->is_array()) {
            root["properties"] = Json::array();
            properties = root.find("properties");
        }
        std::string unique = std::string(name);
        if (unique.empty()) {
            for (std::uint32_t suffix = 1;; ++suffix) {
                unique = "Property" + std::to_string(suffix);
                const bool exists = std::any_of(properties->begin(), properties->end(), [&](const Json& value) {
                    const auto found = value.find("name");
                    return found != value.end() && found->is_string() && found->get<std::string>() == unique;
                });
                if (!exists) {
                    break;
                }
            }
        }
        properties->push_back(defaultPropertyJson(unique, type));
        return true;
    });
}

bool Document::removeProperty(std::string_view name) {
    return m_impl->mutate([&](Json& root) {
        auto properties = root.find("properties");
        if (properties == root.end() || !properties->is_array()) {
            m_impl->setEditError("properties", "properties array is missing");
            return false;
        }
        for (auto it = properties->begin(); it != properties->end(); ++it) {
            const auto found = it->find("name");
            if (found != it->end() && found->is_string() && found->get<std::string>() == name) {
                properties->erase(it);
                return true;
            }
        }
        m_impl->setEditError("properties", "property '" + std::string(name) + "' was not found");
        return false;
    });
}

bool Document::copyProperty(std::string_view property, std::string& json) const {
    const Json* value = findProperty(m_impl->root, property);
    if (value == nullptr) {
        return false;
    }
    json = value->dump();
    return true;
}

bool Document::pasteProperty(std::string_view json, std::string& insertedName) {
    Json copy;
    try {
        copy = Json::parse(json);
    } catch (const std::exception& exception) {
        m_impl->setEditError("properties", exception.what());
        return false;
    }
    if (!copy.is_object()) {
        m_impl->setEditError("properties", "clipboard property must be a JSON object");
        return false;
    }
    const auto name = copy.find("name");
    if (name == copy.end() || !name->is_string() || name->get<std::string>().empty()) {
        m_impl->setEditError("properties", "clipboard property must have a non-empty name");
        return false;
    }
    const std::string inserted = uniqueName(name->get<std::string>(), [&](const std::string& candidate) {
        return hasPropertyName(m_impl->root, candidate);
    }, "");
    copy["name"] = inserted;
    if (!m_impl->mutate([&](Json& root) {
            auto properties = root.find("properties");
            if (properties == root.end() || !properties->is_array()) {
                root["properties"] = Json::array();
                properties = root.find("properties");
            }
            properties->push_back(std::move(copy));
            return true;
        })) {
        return false;
    }
    insertedName = inserted;
    return true;
}

bool Document::duplicateProperty(std::string_view property, std::string& duplicatedName) {
    const Json* source = findProperty(m_impl->root, property);
    if (source == nullptr) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
        return false;
    }
    const std::string inserted = uniqueName(source->value("name", "Property"), [&](const std::string& candidate) {
        return hasPropertyName(m_impl->root, candidate);
    }, "");
    Json copy = *source;
    copy["name"] = inserted;
    if (!m_impl->mutate([&](Json& root) {
            auto properties = root.find("properties");
            if (properties == root.end() || !properties->is_array()) {
                root["properties"] = Json::array();
                properties = root.find("properties");
            }
            properties->push_back(std::move(copy));
            return true;
        })) {
        return false;
    }
    duplicatedName = inserted;
    return true;
}

bool Document::setCullingFlags(std::uint32_t flags) {
    if (flags != 0u && flags != kCullSimulation && flags != (kCullSimulation | kCullBoundsUpdate)) {
        m_impl->setEditError("culling", "unsupported culling flags");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            if (flags != 0u) {
                for (const Json& system : root["systems"]) {
                    if (system.is_object()) {
                        const auto bounds = system.find("bounds");
                        if (bounds != system.end() && bounds->is_object()) {
                            const auto mode = bounds->find("mode");
                            if (mode != bounds->end() && mode->is_string() &&
                                mode->get<std::string>() == "automatic") {
                                m_impl->setEditError("culling",
                                                     "automatic bounds require culling to be none");
                                return false;
                            }
                        }
                    }
                }
            }
            if (flags == 0u) {
                root.erase("culling");
            } else {
                root["culling"] = flags == kCullSimulation ? "simulation" : "visible";
            }
            return true;
        },
        "asset.culling");
}

bool Document::setSystemCapacity(std::uint32_t system, std::uint32_t capacity) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            (*value)["capacity"] = capacity;
            return true;
        },
        indexedKey("system.capacity", system));
}

bool Document::setSystemSpace(std::uint32_t system, SystemSpace space) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            (*value)["space"] = toString(space);
            return true;
        },
        indexedKey("system.space", system));
}

bool Document::setSystemSort(std::uint32_t system, SortMode mode, SortOrder order) {
    const SortMode previousMode =
        system < m_impl->graph->systems.size() ? m_impl->graph->systems[system].sort.mode
                                               : SortMode::None;
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            if (mode == SortMode::None) {
                value->erase("sort");
                if (const auto outputs = value->find("outputs"); outputs != value->end() && outputs->is_array()) {
                    for (auto& output : *outputs) {
                        if (output.is_object()) {
                            output.erase("sort");
                            output.erase("sortOrder");
                        }
                    }
                }
                return true;
            }
            Json sort = Json::object();
            sort["mode"] = toString(mode);
            sort["order"] = toString(order);
            (*value)["sort"] = std::move(sort);
            if (const auto outputs = value->find("outputs"); outputs != value->end() && outputs->is_array()) {
                for (auto& output : *outputs) {
                    if (!output.is_object()) {
                        continue;
                    }
                    const auto outputSort = output.find("sort");
                    if (previousMode != SortMode::None && outputSort != output.end() &&
                        outputSort->is_string() &&
                        outputSort->get<std::string>() != toString(previousMode)) {
                        continue;
                    }
                    output["sort"] = toString(mode);
                }
            }
            return true;
        },
        indexedKey("system.sort", system));
}

bool Document::setSystemBounds(std::uint32_t system, Vec3 center, Vec3 extents) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            Json bounds = Json::object();
            if (const auto existing = value->find("bounds"); existing != value->end() && existing->is_object()) {
                if (const auto padding = existing->find("padding"); padding != existing->end()) {
                    bounds["padding"] = *padding;
                }
            }
            bounds["mode"] = "manual";
            bounds["center"] = Json::array({center.x, center.y, center.z});
            bounds["extents"] = Json::array({extents.x, extents.y, extents.z});
            (*value)["bounds"] = std::move(bounds);
            return true;
        },
        indexedKey("system.bounds", system));
}

bool Document::setSystemBoundsPadding(std::uint32_t system, Vec3 padding) {
    if (!std::isfinite(padding.x) || !std::isfinite(padding.y) || !std::isfinite(padding.z)) {
        m_impl->setEditError("system.bounds.padding", "components must be finite");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            auto existing = value->find("bounds");
            if (existing == value->end() || !existing->is_object()) {
                (*value)["bounds"] = Json::object();
                existing = value->find("bounds");
            }
            (*existing)["padding"] = Json::array({padding.x, padding.y, padding.z});
            return true;
        },
        indexedKey("system.bounds.padding", system));
}

bool Document::setSystemBoundsMode(std::uint32_t system, BoundsMode mode) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            Json bounds = Json::object();
            if (const auto existing = value->find("bounds"); existing != value->end() && existing->is_object()) {
                bounds = *existing;
            }
            bounds["mode"] = mode == BoundsMode::Automatic ? "automatic" : "manual";
            if (mode == BoundsMode::Manual) {
                if (bounds.find("center") == bounds.end()) {
                    bounds["center"] = Json::array({0.0, 0.0, 0.0});
                }
                if (bounds.find("extents") == bounds.end()) {
                    bounds["extents"] = Json::array({1.0, 1.0, 1.0});
                }
            }
            (*value)["bounds"] = std::move(bounds);
            if (mode == BoundsMode::Automatic) {
                root.erase("culling");
            }
            return true;
        },
        indexedKey("system.bounds", system));
}

bool Document::setSpawnerRate(std::uint32_t system, float rate) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* value = findSystem(root, system);
            if (value == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            auto spawner = value->find("spawner");
            if (spawner == value->end() || !spawner->is_object()) {
                (*value)["spawner"] = Json::object();
                spawner = value->find("spawner");
            }
            (*spawner)["rate"] = rate;
            return true;
        },
        indexedKey("system.rate", system));
}

bool Document::setBlockEnabled(std::uint32_t system, ContextKind context, std::uint32_t block, bool enabled) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            if (systemValue == nullptr) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            Json* blockValue = findBlock(*systemValue, context, block);
            if (blockValue == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            (*blockValue)["enabled"] = enabled;
            return true;
        },
        blockParameterKey(system, context, block, "enabled", -1));
}

bool Document::setOutputLit(std::uint32_t system, std::uint32_t output, bool lit) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["lit"] = lit;
            return true;
        },
        outputKey("output.lit", system, output));
}

bool Document::setOutputBlend(std::uint32_t system, std::uint32_t output, BlendMode blend) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["blend"] = toString(blend);
            return true;
        },
        outputKey("output.blend", system, output));
}

bool Document::setOutputSoftParticles(std::uint32_t system, std::uint32_t output, bool enabled) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["softParticles"] = enabled;
            return true;
        },
        outputKey("output.soft", system, output));
}

bool Document::setOutputCastShadows(std::uint32_t system, std::uint32_t output, bool enabled) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["castShadows"] = enabled;
            return true;
        },
        outputKey("output.castShadows", system, output));
}

bool Document::setOutputGenerateMotionVector(std::uint32_t system, std::uint32_t output, bool enabled) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["generateMotionVector"] = enabled;
            return true;
        },
        outputKey("output.generateMotionVector", system, output));
}

bool Document::setOutputSortPriority(std::uint32_t system, std::uint32_t output,
                                     std::int32_t priority) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            if (priority == 0) {
                outputValue->erase("sortPriority");
            } else {
                (*outputValue)["sortPriority"] = priority;
            }
            return true;
        },
        outputKey("output.sortPriority", system, output));
}

bool Document::setOutputSortMode(std::uint32_t system, std::uint32_t output, SortMode mode) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            if (system >= m_impl->graph->systems.size()) {
                m_impl->setEditError("systems", "system index is out of range");
                return false;
            }
            const SortMode systemMode = m_impl->graph->systems[system].sort.mode;
            if (systemMode == SortMode::None) {
                if (mode != SortMode::None) {
                    m_impl->setEditError("output.sort", "requires the system sort mode to be enabled");
                    return false;
                }
                outputValue->erase("sort");
                outputValue->erase("sortOrder");
                return true;
            }
            if (mode == SortMode::None) {
                m_impl->setEditError("output.sort", "must select a criterion when the system sort is enabled");
                return false;
            }
            (*outputValue)["sort"] = toString(mode);
            return true;
        },
        outputKey("output.sort", system, output));
}

bool Document::setOutputSortOrder(std::uint32_t system, std::uint32_t output,
                                  std::optional<SortOrder> order) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            if (!order.has_value()) {
                outputValue->erase("sortOrder");
            } else {
                (*outputValue)["sortOrder"] = toString(*order);
            }
            return true;
        },
        outputKey("output.sortOrder", system, output));
}

bool Document::setOutputSoftDistance(std::uint32_t system, std::uint32_t output, float distance) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["softDistance"] = distance;
            return true;
        },
        outputKey("output.softDistance", system, output));
}

bool Document::setOutputMetallic(std::uint32_t system, std::uint32_t output, float metallic) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["metallic"] = metallic;
            return true;
        },
        outputKey("output.metallic", system, output));
}

bool Document::setOutputRoughness(std::uint32_t system, std::uint32_t output, float roughness) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["roughness"] = roughness;
            return true;
        },
        outputKey("output.roughness", system, output));
}

bool Document::setPropertyNumber(std::string_view property, std::uint32_t component, double value) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* propertyValue = findProperty(root, property);
            if (propertyValue == nullptr) {
                m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
                return false;
            }
            switch (description->type) {
            case PropertyType::Float:
                (*propertyValue)["default"] = value;
                return true;
            case PropertyType::Int: {
                const std::int64_t rounded = static_cast<std::int64_t>(std::llround(value));
                const std::int64_t clamped = std::clamp<std::int64_t>(rounded, std::numeric_limits<std::int32_t>::min(),
                                                                      std::numeric_limits<std::int32_t>::max());
                (*propertyValue)["default"] = static_cast<std::int32_t>(clamped);
                return true;
            }
            case PropertyType::Bool:
                (*propertyValue)["default"] = value >= 0.5;
                return true;
            case PropertyType::Vector2:
            case PropertyType::Vector3:
            case PropertyType::Vector4:
            case PropertyType::Color: {
                const std::size_t count = description->type == PropertyType::Vector2   ? 2u
                                          : description->type == PropertyType::Vector3 ? 3u
                                                                                       : 4u;
                if (component >= count) {
                    m_impl->setEditError("properties", "property component is out of range");
                    return false;
                }
                auto& array = (*propertyValue)["default"];
                if (!array.is_array() || array.size() != count) {
                    m_impl->setEditError("properties", "property default is not a vector");
                    return false;
                }
                array[component] = value;
                return true;
            }
            case PropertyType::Texture:
            case PropertyType::Curve:
            case PropertyType::Gradient:
                m_impl->setEditError("properties", "property type is not numerically editable");
                return false;
            }
            return false;
        },
        propertyKey("property.number", property, static_cast<int>(component)));
}

bool Document::setBlockParameterNumber(std::uint32_t system, ContextKind context, std::uint32_t block,
                                       std::string_view parameter, int component, double value) {
    const std::string parameterName(parameter);
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* blockValue = systemValue != nullptr ? findBlock(*systemValue, context, block) : nullptr;
            if (blockValue == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            auto it = blockValue->find(parameterName);
            if (it == blockValue->end()) {
                m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                     "parameter was not found");
                return false;
            }
            Json* target = nullptr;
            if (component < 0) {
                target = &*it;
            } else {
                if (!it->is_array() || static_cast<std::size_t>(component) >= it->size()) {
                    m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                         "parameter component is out of range");
                    return false;
                }
                target = &(*it)[static_cast<std::size_t>(component)];
            }
            if (target->is_number_integer()) {
                const std::int64_t rounded = static_cast<std::int64_t>(std::llround(value));
                *target = rounded;
                return true;
            }
            if (target->is_number_float()) {
                *target = value;
                return true;
            }
            m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                 "parameter is not a number");
            return false;
        },
        blockParameterKey(system, context, block, parameter, -1));
}

bool Document::setBlockParameterBool(std::uint32_t system, ContextKind context, std::uint32_t block,
                                     std::string_view parameter, bool value) {
    const std::string parameterName(parameter);
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* blockValue = systemValue != nullptr ? findBlock(*systemValue, context, block) : nullptr;
            if (blockValue == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            auto it = blockValue->find(parameterName);
            if (it == blockValue->end() || !it->is_boolean()) {
                m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                     "parameter is not a bool");
                return false;
            }
            *it = value;
            return true;
        },
        blockParameterKey(system, context, block, parameter, -1));
}

bool Document::setBlockParameterString(std::uint32_t system, ContextKind context, std::uint32_t block,
                                       std::string_view parameter, std::string_view value) {
    const std::string parameterName(parameter);
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* blockValue = systemValue != nullptr ? findBlock(*systemValue, context, block) : nullptr;
            if (blockValue == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            auto it = blockValue->find(parameterName);
            if (it == blockValue->end() || !it->is_string()) {
                m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                     "parameter is not a string");
                return false;
            }
            *it = value;
            return true;
        },
        blockParameterKey(system, context, block, parameter, -1));
}

bool Document::blockParameterJson(std::uint32_t system, ContextKind context, std::uint32_t block,
                                  std::string_view parameter, std::string& json) const {
    const Json* systemValue = findSystem(m_impl->root, system);
    if (systemValue == nullptr) {
        return false;
    }
    const Json* blockValue = findBlock(*systemValue, context, block);
    if (blockValue == nullptr) {
        return false;
    }
    const auto found = blockValue->find(parameter);
    if (found == blockValue->end()) {
        return false;
    }
    json = found->dump(2);
    return true;
}

bool Document::setBlockParameterJson(std::uint32_t system, ContextKind context, std::uint32_t block,
                                     std::string_view parameter, std::string_view json) {
    const std::string parameterName(parameter);
    if (parameterName == "type") {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + ".type",
                             "block type is not a parameter");
        return false;
    }
    Json value;
    try {
        value = Json::parse(json);
    } catch (const std::exception& exception) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                             exception.what());
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* blockValue = systemValue != nullptr ? findBlock(*systemValue, context, block) : nullptr;
            if (blockValue == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            auto it = blockValue->find(parameterName);
            if (it == blockValue->end()) {
                m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                     "parameter was not found");
                return false;
            }
            *it = std::move(value);
            return true;
        },
        blockParameterKey(system, context, block, parameter, -2));
}

std::vector<std::string> Document::optionalBlockParameters(std::uint32_t system, ContextKind context,
                                                            std::uint32_t block) const {
    const Json* systemValue = findSystem(m_impl->root, system);
    if (systemValue == nullptr) {
        return {};
    }
    const Json* blockValue = findBlock(*systemValue, context, block);
    if (blockValue == nullptr) {
        return {};
    }
    const auto type = blockValue->find("type");
    if (type == blockValue->end() || !type->is_string()) {
        return {};
    }
    const detail::BlockSpec* spec =
        detail::findBlockSpec(type->get<std::string>(), context == ContextKind::Update);
    if (spec == nullptr) {
        return {};
    }
    std::vector<std::string> parameters;
    for (const detail::FieldSpec& field : spec->fields) {
        if (field.required || field.name == nullptr || field.name[0] == '\0' || field.defaultJson == nullptr) {
            continue;
        }
        if (blockValue->find(field.name) == blockValue->end()) {
            parameters.emplace_back(field.name);
        }
    }
    return parameters;
}

bool Document::addOptionalBlockParameter(std::uint32_t system, ContextKind context, std::uint32_t block,
                                         std::string_view parameter) {
    const std::string parameterName(parameter);
    const Json* systemValue = findSystem(m_impl->root, system);
    const Json* blockValue = systemValue != nullptr ? findBlock(*systemValue, context, block) : nullptr;
    if (blockValue == nullptr) {
        m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                             "block index is out of range");
        return false;
    }
    const auto type = blockValue->find("type");
    if (type == blockValue->end() || !type->is_string()) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}), "block type is missing");
        return false;
    }
    const detail::BlockSpec* spec =
        detail::findBlockSpec(type->get<std::string>(), context == ContextKind::Update);
    if (spec == nullptr) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}), "block type is not editable");
        return false;
    }
    const auto field = std::find_if(spec->fields.begin(), spec->fields.end(),
                                    [&](const detail::FieldSpec& candidate) {
                                        return candidate.name != nullptr && parameterName == candidate.name;
                                    });
    if (field == spec->fields.end()) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                             "parameter is not defined for block '" + type->get<std::string>() + "'");
        return false;
    }
    if (field->required || field->defaultJson == nullptr) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                             "parameter is not an optional parameter with a default value");
        return false;
    }
    if (blockValue->find(parameterName) != blockValue->end()) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                             "parameter already exists");
        return false;
    }
    Json value;
    try {
        value = Json::parse(field->defaultJson);
    } catch (const std::exception& exception) {
        m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                             exception.what());
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* rootSystem = findSystem(root, system);
            Json* rootBlock = rootSystem != nullptr ? findBlock(*rootSystem, context, block) : nullptr;
            if (rootBlock == nullptr) {
                m_impl->setEditError(contextPath(ContextRef{system, context, 0}) + "/blocks",
                                     "block index is out of range");
                return false;
            }
            if (rootBlock->find(parameterName) != rootBlock->end()) {
                m_impl->setEditError(contextPath(ContextRef{system, context, block}) + "." + parameterName,
                                     "parameter already exists");
                return false;
            }
            (*rootBlock)[parameterName] = std::move(value);
            return true;
        },
        blockParameterKey(system, context, block, parameter, -3));
}

bool Document::setOutputTexture(std::uint32_t system, std::uint32_t output, std::string_view texture) {
    return m_impl->mutate(
        [&](Json& root) {
            Json* systemValue = findSystem(root, system);
            Json* outputValue = systemValue != nullptr ? findOutput(*systemValue, output) : nullptr;
            if (outputValue == nullptr) {
                m_impl->setEditError("systems", "output index is out of range");
                return false;
            }
            (*outputValue)["texture"] = texture;
            return true;
        },
        outputKey("output.texture", system, output));
}

bool Document::setPropertyTexture(std::string_view property, std::string_view path) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Texture) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a texture");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* propertyValue = findProperty(root, property);
            if (propertyValue == nullptr) {
                m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
                return false;
            }
            (*propertyValue)["default"] = path;
            return true;
        },
        propertyKey("property.texture", property));
}

bool Document::setPropertyCurveKey(std::string_view property, std::uint32_t key, float time, float value) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Curve) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a curve");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* propertyValue = findProperty(root, property);
            if (propertyValue == nullptr) {
                m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
                return false;
            }
            auto keys = propertyValue->find("keys");
            if (keys == propertyValue->end() || !keys->is_array() || key >= keys->size()) {
                m_impl->setEditError("properties", "curve key index is out of range");
                return false;
            }
            Json& item = (*keys)[key];
            if (!item.is_array() || item.size() != 2) {
                m_impl->setEditError("properties", "curve key is malformed");
                return false;
            }
            item[0] = time;
            item[1] = value;
            return true;
        },
        propertyKey("property.curve", property, static_cast<int>(key)));
}

bool Document::addPropertyCurveKey(std::string_view property) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Curve) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a curve");
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* propertyValue = findProperty(root, property);
        if (propertyValue == nullptr) {
            m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
            return false;
        }
        auto keys = propertyValue->find("keys");
        if (keys == propertyValue->end() || !keys->is_array()) {
            m_impl->setEditError("properties", "curve keys are missing");
            return false;
        }
        const double time = keys->empty() ? 0.0 : freeKeyTime(*keys, false);
        keys->push_back(Json::array({time, 1.0}));
        return true;
    });
}

bool Document::removePropertyCurveKey(std::string_view property, std::uint32_t key) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Curve) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a curve");
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* propertyValue = findProperty(root, property);
        if (propertyValue == nullptr) {
            m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
            return false;
        }
        auto keys = propertyValue->find("keys");
        if (keys == propertyValue->end() || !keys->is_array() || key >= keys->size()) {
            m_impl->setEditError("properties", "curve key index is out of range");
            return false;
        }
        if (keys->size() <= 1) {
            m_impl->setEditError("properties", "a curve requires at least one key");
            return false;
        }
        keys->erase(keys->begin() + static_cast<std::ptrdiff_t>(key));
        return true;
    });
}

bool Document::setPropertyGradientKey(std::string_view property, std::uint32_t key, float time, Vec4 color) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Gradient) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a gradient");
        return false;
    }
    return m_impl->mutate(
        [&](Json& root) {
            Json* propertyValue = findProperty(root, property);
            if (propertyValue == nullptr) {
                m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
                return false;
            }
            auto keys = propertyValue->find("keys");
            if (keys == propertyValue->end() || !keys->is_array() || key >= keys->size()) {
                m_impl->setEditError("properties", "gradient key index is out of range");
                return false;
            }
            Json& item = (*keys)[key];
            if (!item.is_object()) {
                m_impl->setEditError("properties", "gradient key is malformed");
                return false;
            }
            item["t"] = time;
            item["color"] = Json::array({color.x, color.y, color.z, color.w});
            return true;
        },
        propertyKey("property.gradient", property, static_cast<int>(key)));
}

bool Document::addPropertyGradientKey(std::string_view property) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Gradient) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a gradient");
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* propertyValue = findProperty(root, property);
        if (propertyValue == nullptr) {
            m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
            return false;
        }
        auto keys = propertyValue->find("keys");
        if (keys == propertyValue->end() || !keys->is_array()) {
            m_impl->setEditError("properties", "gradient keys are missing");
            return false;
        }
        const double time = keys->empty() ? 0.0 : freeKeyTime(*keys, true);
        keys->push_back(Json{{"t", time}, {"color", Json::array({1.0, 1.0, 1.0, 1.0})}});
        return true;
    });
}

bool Document::removePropertyGradientKey(std::string_view property, std::uint32_t key) {
    const Property* description = findGraphProperty(*m_impl->graph, property);
    if (description == nullptr || description->type != PropertyType::Gradient) {
        m_impl->setEditError("properties", "property '" + std::string(property) + "' is not a gradient");
        return false;
    }
    return m_impl->mutate([&](Json& root) {
        Json* propertyValue = findProperty(root, property);
        if (propertyValue == nullptr) {
            m_impl->setEditError("properties", "property '" + std::string(property) + "' was not found");
            return false;
        }
        auto keys = propertyValue->find("keys");
        if (keys == propertyValue->end() || !keys->is_array() || key >= keys->size()) {
            m_impl->setEditError("properties", "gradient key index is out of range");
            return false;
        }
        if (keys->size() <= 1) {
            m_impl->setEditError("properties", "a gradient requires at least one key");
            return false;
        }
        keys->erase(keys->begin() + static_cast<std::ptrdiff_t>(key));
        return true;
    });
}

bool Document::canUndo() const {
    return !m_impl->undoStack.empty();
}

bool Document::canRedo() const {
    return !m_impl->redoStack.empty();
}

bool Document::undo() {
    if (m_impl->undoStack.empty()) {
        return false;
    }
    const Impl::Snapshot current{m_impl->root, m_impl->dirty, m_impl->layoutStale};
    const Impl::Snapshot target = m_impl->undoStack.back();
    if (!m_impl->restore(target)) {
        return false;
    }
    m_impl->undoStack.pop_back();
    m_impl->redoStack.push_back(current);
    if (m_impl->redoStack.size() > kMaxHistory) {
        m_impl->redoStack.erase(m_impl->redoStack.begin());
    }
    return true;
}

bool Document::redo() {
    if (m_impl->redoStack.empty()) {
        return false;
    }
    const Impl::Snapshot current{m_impl->root, m_impl->dirty, m_impl->layoutStale};
    const Impl::Snapshot target = m_impl->redoStack.back();
    if (!m_impl->restore(target)) {
        return false;
    }
    m_impl->redoStack.pop_back();
    m_impl->undoStack.push_back(current);
    if (m_impl->undoStack.size() > kMaxHistory) {
        m_impl->undoStack.erase(m_impl->undoStack.begin());
    }
    return true;
}

void Document::setNodePosition(ContextRef context, Vec2 position) {
    auto found = std::find_if(m_impl->nodes.begin(), m_impl->nodes.end(),
                              [&](const NodeLayout& node) { return node.context == context; });
    if (found == m_impl->nodes.end()) {
        return;
    }
    if (found->position.x == position.x && found->position.y == position.y) {
        return;
    }
    found->position = position;
    m_impl->layoutDirty = true;
}

void Document::setNodeCollapsed(ContextRef context, bool collapsed) {
    auto found = std::find_if(m_impl->nodes.begin(), m_impl->nodes.end(),
                              [&](const NodeLayout& node) { return node.context == context; });
    if (found == m_impl->nodes.end() || found->collapsed == collapsed) {
        return;
    }
    found->collapsed = collapsed;
    m_impl->layoutDirty = true;
}

void Document::setViewLayout(ViewLayout view) {
    if (view.zoom <= 0.0f) {
        view.zoom = 0.01f;
    }
    if (m_impl->view.pan.x == view.pan.x && m_impl->view.pan.y == view.pan.y && m_impl->view.zoom == view.zoom) {
        return;
    }
    m_impl->view = view;
    m_impl->layoutDirty = true;
}

void Document::resetLayout() {
    m_impl->resetLayout();
    m_impl->view = ViewLayout{};
    m_impl->layoutDirty = true;
    m_impl->layoutStale = false;
}

bool Document::loadLayout(std::string& error) {
    std::string contents = readFile(m_impl->layoutPath, error);
    if (contents.empty()) {
        if (error.empty()) {
            error = "editor layout is empty";
        }
        return false;
    }
    Json root;
    try {
        root = Json::parse(contents);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "editor layout root must be an object";
        return false;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_unsigned() || version->get<std::uint32_t>() != kLayoutVersion) {
        error = "unsupported editor layout version";
        return false;
    }

    ViewLayout view;
    if (const auto viewIt = root.find("view"); viewIt != root.end() && viewIt->is_object()) {
        if (const auto pan = viewIt->find("pan"); pan != viewIt->end() && pan->is_array() && pan->size() == 2) {
            view.pan.x = (*pan)[0].get<float>();
            view.pan.y = (*pan)[1].get<float>();
        }
        if (const auto zoom = viewIt->find("zoom"); zoom != viewIt->end() && zoom->is_number()) {
            view.zoom = zoom->get<float>();
        }
    }
    if (view.zoom <= 0.0f) {
        view.zoom = 1.0f;
    }

    std::vector<NodeLayout> loadedNodes;
    if (const auto nodes = root.find("nodes"); nodes != root.end() && nodes->is_object()) {
        for (auto it = nodes->begin(); it != nodes->end(); ++it) {
            const std::optional<ContextRef> context = parseContextPath(it.key());
            if (!context.has_value()) {
                continue;
            }
            if (!it.value().is_object()) {
                continue;
            }
            const auto x = it.value().find("x");
            const auto y = it.value().find("y");
            if (x == it.value().end() || y == it.value().end() || !x->is_number() || !y->is_number()) {
                continue;
            }
            NodeLayout node;
            node.context = *context;
            node.position.x = x->get<float>();
            node.position.y = y->get<float>();
            if (const auto collapsed = it.value().find("collapsed"); collapsed != it.value().end()) {
                node.collapsed = collapsed->is_boolean() && collapsed->get<bool>();
            }
            loadedNodes.push_back(node);
        }
    }

    std::uint64_t hash = 0;
    if (const auto stored = root.find("assetHash"); stored != root.end() && stored->is_string()) {
        try {
            hash = std::stoull(stored->get<std::string>());
        } catch (const std::exception&) {
            hash = 0;
        }
    }

    m_impl->nodes = std::move(loadedNodes);
    m_impl->view = view;
    m_impl->ensureLayout();
    m_impl->layoutLoaded = true;
    m_impl->layoutHash = hash;
    m_impl->layoutStale = hash != graphHash(*m_impl->graph);
    m_impl->layoutDirty = false;
    m_impl->layoutError.clear();
    return true;
}

bool Document::saveLayout(std::string& error) {
    Json root = Json::object();
    root["version"] = kLayoutVersion;
    root["assetHash"] = std::to_string(graphHash(*m_impl->graph));
    root["view"] = Json{{"pan", Json::array({m_impl->view.pan.x, m_impl->view.pan.y})}, {"zoom", m_impl->view.zoom}};
    Json nodes = Json::object();
    for (const NodeLayout& node : m_impl->nodes) {
        Json value = Json::object();
        value["x"] = node.position.x;
        value["y"] = node.position.y;
        value["collapsed"] = node.collapsed;
        nodes[contextPath(node.context)] = std::move(value);
    }
    root["nodes"] = std::move(nodes);

    std::ofstream file(m_impl->layoutPath, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        error = "failed to write '" + m_impl->layoutPath.string() + "'";
        return false;
    }
    file << root.dump(2) << "\n";
    if (!file.good()) {
        error = "failed to write '" + m_impl->layoutPath.string() + "'";
        return false;
    }
    m_impl->layoutDirty = false;
    m_impl->layoutLoaded = true;
    m_impl->layoutHash = graphHash(*m_impl->graph);
    m_impl->layoutStale = false;
    m_impl->layoutError.clear();
    return true;
}

} // namespace vfx::editor
