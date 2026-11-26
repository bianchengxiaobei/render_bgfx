#include "app/sg_editor_host.h"

#include "core/log.h"
#include "sg/compile.h"
#include "sg/editor.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "cpp-httplib/httplib.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifndef HBG_SG_ROOT
#define HBG_SG_ROOT "."
#endif
#ifndef HBG_SG_SHADERC_DEFAULT
#define HBG_SG_SHADERC_DEFAULT ""
#endif
#ifndef HBG_SG_BGFX_SOURCE_DEFAULT
#define HBG_SG_BGFX_SOURCE_DEFAULT ""
#endif

namespace hbg {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxBodyBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxCommands = 512;
constexpr auto kCommandTimeout = std::chrono::seconds(2);

struct HttpReply {
    int status = 200;
    std::string body;
    std::string contentType = "application/json; charset=utf-8";
    std::vector<std::pair<std::string, std::string>> headers;
};

struct Event {
    std::uint64_t id = 0;
    std::string name;
    std::string data;
};

struct Task {
    std::function<HttpReply()> function;
    HttpReply reply;
    bool done = false;
    bool failed = false;
    std::string error;
    std::mutex mutex;
    std::condition_variable condition;
};

struct NodePreviewJob {
    std::string key;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    sg::NodeId node = sg::kInvalidNode;
    sg::SlotId slot = sg::kInvalidSlot;
    std::string state = "queued";
    std::vector<std::uint8_t> binary;
    std::string error;
    std::thread worker;
};

struct Job {
    std::string id;
    std::string mode;
    std::uint64_t revision = 0;
    std::string state = "queued";
    sg::Graph graph;
    sg::NodeRegistry registry;
    sg::CompileOptions compile;
    bool writeArtifact = false;
    std::filesystem::path artifactPath;
    std::vector<std::uint8_t> binary;
    std::string diagnostics = "[]";
    std::string error;
    sg::CompileStats stats;
    bool canceled = false;
    bool terminalNotified = false;
    std::thread worker;
};

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

std::string genericPath(const std::filesystem::path& path) {
    std::string text = path.lexically_normal().generic_string();
    while (text.find("\\") != std::string::npos) {
        text.replace(text.find("\\"), 1, "/");
    }
    return text;
}

bool pathInside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::string rootText = genericPath(root);
    std::string candidateText = genericPath(candidate);
#ifdef _WIN32
    rootText = lower(rootText);
    candidateText = lower(candidateText);
#endif
    if (!rootText.empty() && rootText.back() != '/') {
        rootText.push_back('/');
    }
    return candidateText == rootText.substr(0, rootText.size() - 1) ||
           candidateText.starts_with(rootText);
}

std::string relativePath(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return relative.empty() ? path.filename().generic_string() : relative.generic_string();
}

std::string makeToken() {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 15);
    constexpr char kHex[] = "0123456789abcdef";
    std::string token(32, '0');
    for (char& character : token) {
        character = kHex[distribution(generator)];
    }
    return token;
}

HttpReply jsonReply(int status, const Json& body) {
    return {status, body.dump(), "application/json; charset=utf-8"};
}

HttpReply errorReply(int status, std::string code, std::string message) {
    Json body;
    body["ok"] = false;
    body["error"] = {{"code", std::move(code)}, {"message", std::move(message)}};
    return jsonReply(status, body);
}

HttpReply errorReplyAtRevision(int status, std::string code, std::string message,
                               std::uint64_t revision) {
    Json body;
    body["ok"] = false;
    body["revision"] = revision;
    body["error"] = {{"code", std::move(code)}, {"message", std::move(message)}};
    return jsonReply(status, body);
}

HttpReply okReply(std::uint64_t revision, Json data) {
    Json body;
    body["ok"] = true;
    body["revision"] = revision;
    body["data"] = std::move(data);
    return jsonReply(200, body);
}

int statusForCode(const std::string& code) {
    if (code == "SG_BAD_REQUEST" || code == "SG_INVALID_JSON" || code == "SG_INVALID_VALUE") {
        return 400;
    }
    if (code == "SG_NOT_FOUND" || code == "SG_UNKNOWN_NODE") {
        return 404;
    }
    if (code == "SG_STALE_REVISION") {
        return 409;
    }
    if (code == "SG_TYPE_MISMATCH" || code == "SG_CYCLE" || code == "SG_TARGET_UNSUPPORTED" ||
        code == "SG_VARIANT_LIMIT" || code == "SG_BUDGET_EXCEEDED" ||
        code == "SG_COMPILE_FAILED" || code == "SG_INVALID_EDGE" ||
        code == "SG_NAME_CONFLICT") {
        return 422;
    }
    if (code == "SG_BUSY" || code == "SG_TIMEOUT") {
        return 503;
    }
    return 500;
}

std::string contentTypeForPath(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".html" || extension == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js" || extension == ".mjs") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".json") {
        return "application/json; charset=utf-8";
    }
    return "application/octet-stream";
}

bool isGraphPath(const std::filesystem::path& path) {
    const std::string name = lower(path.filename().string());
    return endsWith(name, ".sgraph.json") || endsWith(name, ".ssubgraph.json");
}

bool isTexturePath(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".hdr" || extension == ".exr" || extension == ".tga" ||
           extension == ".bmp";
}

bool isBakePath(const std::filesystem::path& path) {
    return endsWith(lower(path.filename().string()), ".sgb");
}

std::string makeArtifactHash(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char text[32];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(hash));
    return text;
}

bool parseFloatText(const std::string& text, float& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseUint64Text(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

bool parsePreviewOptions(const httplib::Request& request, SgPreviewOptions& options,
                         std::uint64_t& revision, std::string& error) {
    revision = 0;
    const std::string mesh = lower(request.get_param_value("mesh"));
    if (!mesh.empty()) {
        if (mesh != "sphere" && mesh != "plane" && mesh != "cube" && mesh != "quad") {
            error = "mesh must be sphere, plane, cube or quad";
            return false;
        }
        options.mesh = mesh;
    }

    const std::string sizeText = request.get_param_value("size");
    if (!sizeText.empty()) {
        int size = 0;
        const auto result = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), size);
        if (result.ec != std::errc{} || result.ptr != sizeText.data() + sizeText.size()) {
            error = "size must be an integer";
            return false;
        }
        options.size = std::clamp(size, 128, 1024);
    }

    const auto parseAngle = [&](const char* name, float& target, float minimum, float maximum) {
        const std::string text = request.get_param_value(name);
        if (text.empty()) {
            return true;
        }
        float value = 0.0f;
        if (!parseFloatText(text, value)) {
            error = std::string(name) + " must be a finite number";
            return false;
        }
        target = std::clamp(value, minimum, maximum);
        return true;
    };
    if (!parseAngle("yaw", options.yaw, -180.0f, 180.0f) ||
        !parseAngle("pitch", options.pitch, -85.0f, 85.0f)) {
        return false;
    }

    const std::string distanceText = request.get_param_value("distance");
    if (!distanceText.empty()) {
        float distance = 0.0f;
        if (!parseFloatText(distanceText, distance)) {
            error = "distance must be a finite number";
            return false;
        }
        options.distance = distance <= 0.0f ? 0.0f : std::clamp(distance, 0.5f, 10.0f);
    }

    const std::string background = lower(request.get_param_value("background"));
    if (!background.empty()) {
        if (background != "checker" && background != "black" && background != "gray" &&
            background != "sky") {
            error = "background must be checker, black, gray or sky";
            return false;
        }
        options.background = background;
    }

    const std::string channel = lower(request.get_param_value("channel"));
    if (!channel.empty()) {
        if (channel != "rgb" && channel != "r" && channel != "g" && channel != "b" &&
            channel != "a") {
            error = "channel must be rgb, r, g, b or a";
            return false;
        }
        options.channel = channel;
    }

    const std::string fpsText = request.get_param_value("fps");
    if (!fpsText.empty()) {
        int fps = 10;
        const auto result = std::from_chars(fpsText.data(), fpsText.data() + fpsText.size(), fps);
        if (result.ec != std::errc{} || result.ptr != fpsText.data() + fpsText.size()) {
            error = "fps must be an integer";
            return false;
        }
        options.fps = std::clamp(fps, 1, 30);
    }

    const std::string revisionText = request.get_param_value("rev");
    if (!revisionText.empty() && !parseUint64Text(revisionText, revision)) {
        error = "rev must be an unsigned integer";
        return false;
    }

    error.clear();
    return true;
}

std::string previewKey(const SgPreviewOptions& options, std::uint64_t revision) {
    std::ostringstream stream;
    stream << revision << '|' << options.mesh << '|' << options.size << '|' << std::fixed
           << std::setprecision(4) << options.yaw << '|' << options.pitch << '|' << options.distance
           << '|' << options.background << '|' << options.channel << '|' << options.format;
    return stream.str();
}

bool isPreviewableNodeType(const sg::NodeDef& definition) {
    return definition.hasPreview && !definition.blockNode;
}

bool isPreviewableConcreteType(sg::ConcreteValueType type) {
    return type == sg::ConcreteValueType::Float || type == sg::ConcreteValueType::Integer ||
           type == sg::ConcreteValueType::Vector2 || type == sg::ConcreteValueType::Vector3 ||
           type == sg::ConcreteValueType::Vector4;
}

bool nodeUses3D(const sg::Graph& graph, sg::NodeId start) {
    static const std::set<std::string> k3DNodeTypes = {
        "Position", "Normal Vector", "Tangent Vector", "Bitangent Vector", "View Direction",
        "View Vector", "Vertex Color", "Fresnel Effect", "Triplanar", "Sample Cubemap",
        "Sample Raw Cubemap"};
    std::vector<sg::NodeId> stack{start};
    std::set<sg::NodeId> visited;
    while (!stack.empty()) {
        const sg::NodeId id = stack.back();
        stack.pop_back();
        if (!visited.insert(id).second) {
            continue;
        }
        const sg::NodeInstance* node = graph.findNode(id);
        if (node == nullptr) {
            continue;
        }
        if (k3DNodeTypes.contains(node->type)) {
            return true;
        }
        for (const sg::Edge& edge : graph.incomingEdges(id)) {
            stack.push_back(edge.fromNode);
        }
    }
    return false;
}

bool buildNodePreviewGraph(const sg::Graph& source, const sg::NodeRegistry& registry,
                           sg::NodeId selectedId, sg::Graph& preview, sg::SlotId& outputSlot,
                           bool& uses3D, std::string& error) {
    const sg::NodeInstance* selected = source.findNode(selectedId);
    if (selected == nullptr) {
        error = "node not found";
        return false;
    }
    const sg::NodeDef* definition = registry.find(selected->type);
    if (definition == nullptr || !isPreviewableNodeType(*definition)) {
        error = "node does not support preview";
        return false;
    }
    const std::vector<const sg::SlotInstance*> outputs = selected->outputSlots();
    if (outputs.empty() || !isPreviewableConcreteType(outputs.front()->concreteType)) {
        error = "node has no previewable output";
        return false;
    }
    outputSlot = outputs.front()->spec.id;

    std::set<sg::NodeId> closure;
    std::vector<sg::NodeId> stack{selectedId};
    while (!stack.empty()) {
        const sg::NodeId id = stack.back();
        stack.pop_back();
        if (!closure.insert(id).second) {
            continue;
        }
        for (const sg::Edge& edge : source.incomingEdges(id)) {
            stack.push_back(edge.fromNode);
        }
    }

    preview = {};
    preview.schemaVersion = source.schemaVersion;
    preview.kind = sg::GraphKind::Graph;
    preview.name = source.name;
    preview.generatorVersion = source.generatorVersion;
    preview.target = source.target;
    preview.target.id = "preview";
    preview.target.subTarget = "preview";
    preview.defaultGraphPrecision = source.defaultGraphPrecision;
    preview.defaultPrecision = source.defaultPrecision;
    preview.properties = source.properties;
    preview.keywords = source.keywords;
    preview.categories = source.categories;

    for (const sg::NodeInstance& node : source.nodes) {
        if (closure.contains(node.id)) {
            preview.nodes.push_back(node);
        }
    }
    for (const sg::Edge& edge : source.edges) {
        if (closure.contains(edge.fromNode) && closure.contains(edge.toNode)) {
            preview.edges.push_back(edge);
        }
    }

    const sg::NodeId blockId = preview.nextNodeId();
    sg::NodeInstance block = registry.instantiate("BlockBaseColor", blockId);
    if (!preview.addNode(std::move(block), &error)) {
        return false;
    }
    const sg::NodeInstance* blockNode = preview.findNode(blockId);
    if (blockNode == nullptr || blockNode->slots.empty()) {
        error = "failed to build node preview graph";
        return false;
    }
    if (!preview.addEdge({selectedId, outputSlot, blockNode->id, blockNode->slots.front().spec.id},
                         &error)) {
        return false;
    }
    uses3D = nodeUses3D(source, selectedId);
    return true;
}

std::string nodePreviewKey(std::uint64_t generation, std::uint64_t revision, sg::NodeId node,
                           sg::SlotId slot) {
    std::ostringstream stream;
    stream << generation << '|' << revision << '|' << node << '|' << slot;
    return stream.str();
}

struct PreviewFrame {
    std::string key;
    std::uint64_t revision = 0;
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point time{};
    std::vector<std::uint8_t> jpeg;
};

std::string diagnosticsForJob(const sg::CompileResult& result) {
    return sg::editor::diagnosticsJson(result.diagnostics);
}

void writeEventJson(Json& destination, const Job& job) {
    destination["jobId"] = job.id;
    destination["state"] = job.state;
    destination["revision"] = job.revision;
}

} // namespace

struct SgEditorHost::Impl {
    Options options;
    sg::NodeRegistry registry;
    std::unique_ptr<sg::editor::Document> document;
    httplib::Server server;
    std::thread serverThread;
    std::uint16_t port = 0;
    std::string token;
    std::atomic<bool> stopping{false};

    std::mutex taskMutex;
    std::condition_variable taskCondition;
    std::deque<std::shared_ptr<Task>> tasks;

    std::mutex jobMutex;
    std::map<std::string, std::shared_ptr<Job>> jobs;
    std::shared_ptr<Job> latestPreviewJob;
    std::uint64_t nextJob = 1;

    std::mutex nodePreviewMutex;
    std::map<std::string, std::shared_ptr<NodePreviewJob>> nodePreviewJobs;
    std::map<std::string, std::vector<std::uint8_t>> nodePreviewImages;
    int nodePreviewRunning = 0;
    std::atomic<std::uint64_t> previewGeneration{0};

    SgPreviewCallback previewCallback;
    std::atomic<bool> previewEnabled{false};
    std::mutex previewMutex;
    std::condition_variable previewCondition;
    PreviewFrame previewFrame;
    std::uint64_t previewSequence = 0;
    SgPreviewOptions streamOptions;
    std::uint64_t streamRevision = 0;
    int previewStreamClients = 0;

    struct StreamLease {
        Impl* owner = nullptr;
        ~StreamLease();
    };

    std::mutex eventMutex;
    std::condition_variable eventCondition;
    std::deque<Event> events;
    std::uint64_t eventSequence = 0;

    bool webMounted = false;
    bool openDocument(std::string& error);
    bool resolvePath(const std::string& requestPath, bool allowAbsolute,
                     const std::vector<std::string>& extensions, std::filesystem::path& resolved,
                     std::string& error) const;
    std::optional<HttpReply> submit(std::function<HttpReply()> function);
    void respond(httplib::Response& response, std::function<HttpReply()> function);
    void processTasks();
    void processJobs();
    void publishEvent(std::string name, Json data);
    bool waitEvent(std::uint64_t lastId, Event& event);
    void setPreviewCallback(SgPreviewCallback callback);
    std::shared_ptr<Job> previewJobFor(std::uint64_t revision);
    bool renderPreview(const SgPreviewOptions& options, std::vector<std::uint8_t>& jpeg,
                       std::string& error);
    bool renderPreviewBinary(const std::vector<std::uint8_t>& binary,
                             const SgPreviewOptions& options, std::vector<std::uint8_t>& image,
                             std::string& error);
    bool renderNodePreview(const std::string& key, const SgPreviewOptions& options,
                           std::vector<std::uint8_t>& image, std::string& error);
    std::shared_ptr<NodePreviewJob> nodePreviewJobFor(const std::string& key);
    std::shared_ptr<NodePreviewJob> ensureNodePreviewJob(const std::string& key,
                                                         sg::Graph graph,
                                                         std::uint64_t generation,
                                                         std::uint64_t revision, sg::NodeId node,
                                                         sg::SlotId slot, std::string& error);
    bool cachedPreview(const std::string& key, std::uint64_t revision,
                       std::vector<std::uint8_t>& jpeg);
    void publishPreview(const std::string& key, std::uint64_t revision,
                        std::vector<std::uint8_t> jpeg);
    bool waitPreviewFrame(std::uint64_t lastSequence, const std::string& key, PreviewFrame& frame);
    void beginPreviewStream(const SgPreviewOptions& options, std::uint64_t revision);
    void endPreviewStream();
    void maybeRenderPreview();
    void resetPreviewState();
    void registerRoutes();
    std::shared_ptr<Job> createCompileJob(const Json& request, std::string& error);
    Json documentData() const;
    Json sessionData() const;
    Json catalogNodes(const std::string& query, int limit) const;
    Json catalogAssets(const std::string& kind, const std::string& query, int limit) const;
    Json catalogTargets() const;
};

SgEditorHost::Impl::StreamLease::~StreamLease() {
    if (owner != nullptr) {
        owner->endPreviewStream();
    }
}

void SgEditorHost::Impl::setPreviewCallback(SgPreviewCallback callback) {
    const bool enabled = static_cast<bool>(callback);
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewCallback = std::move(callback);
    }
    previewEnabled.store(enabled);
    previewCondition.notify_all();
}

std::shared_ptr<Job> SgEditorHost::Impl::previewJobFor(std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(jobMutex);
    if (latestPreviewJob == nullptr || latestPreviewJob->state != "succeeded") {
        return nullptr;
    }
    if (revision != 0 && latestPreviewJob->revision != revision) {
        return nullptr;
    }
    return latestPreviewJob;
}

bool SgEditorHost::Impl::renderPreviewBinary(const std::vector<std::uint8_t>& binary,
                                             const SgPreviewOptions& previewOptions,
                                             std::vector<std::uint8_t>& image,
                                             std::string& error) {
    SgPreviewCallback callback;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        callback = previewCallback;
    }
    if (!callback) {
        error = "preview rendering is not available";
        return false;
    }
    return callback(binary, previewOptions, image, error);
}

bool SgEditorHost::Impl::renderPreview(const SgPreviewOptions& previewOptions,
                                       std::vector<std::uint8_t>& jpeg, std::string& error) {
    std::vector<std::uint8_t> binary;
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (latestPreviewJob == nullptr || latestPreviewJob->state != "succeeded" ||
            latestPreviewJob->binary.empty()) {
            error = "preview has not been compiled";
            return false;
        }
        binary = latestPreviewJob->binary;
    }
    return renderPreviewBinary(binary, previewOptions, jpeg, error);
}

std::shared_ptr<NodePreviewJob> SgEditorHost::Impl::nodePreviewJobFor(const std::string& key) {
    std::lock_guard<std::mutex> lock(nodePreviewMutex);
    const auto found = nodePreviewJobs.find(key);
    return found == nodePreviewJobs.end() ? nullptr : found->second;
}

bool SgEditorHost::Impl::renderNodePreview(const std::string& key,
                                           const SgPreviewOptions& options,
                                           std::vector<std::uint8_t>& image,
                                           std::string& error) {
    std::shared_ptr<NodePreviewJob> job = nodePreviewJobFor(key);
    std::vector<std::uint8_t> binary;
    {
        std::lock_guard<std::mutex> lock(nodePreviewMutex);
        if (job == nullptr || job->state != "succeeded" || job->binary.empty()) {
            error = "node preview has not been compiled";
            return false;
        }
        binary = job->binary;
    }
    return renderPreviewBinary(binary, options, image, error);
}

std::shared_ptr<NodePreviewJob> SgEditorHost::Impl::ensureNodePreviewJob(
    const std::string& key, sg::Graph graph, std::uint64_t generation, std::uint64_t revision,
    sg::NodeId node, sg::SlotId slot, std::string& error) {
    std::lock_guard<std::mutex> lock(nodePreviewMutex);
    const auto existing = nodePreviewJobs.find(key);
    if (existing != nodePreviewJobs.end()) {
        return existing->second;
    }
    if (nodePreviewRunning >= 2) {
        return nullptr;
    }

    auto job = std::make_shared<NodePreviewJob>();
    job->key = key;
    job->generation = generation;
    job->revision = revision;
    job->node = node;
    job->slot = slot;
    job->state = "running";
    ++nodePreviewRunning;
    nodePreviewJobs[key] = job;

    sg::CompileOptions compile;
    compile.rootDirectory = options.projectRoot;
    compile.shaderc = options.shaderc;
    compile.bgfxSource = options.bgfxSource;
    compile.backends = {options.previewBackend == "spirv" ? sg::Backend::Spirv
                                                          : sg::Backend::Dxbc};
    compile.variants = sg::VariantPolicy::Used;
    compile.maxVariants = 64;
    compile.shaderTimeoutSeconds = 600;

    const sg::NodeRegistry registryCopy = registry;
    job->worker = std::thread([this, job, graph = std::move(graph),
                               registry = registryCopy, compile = std::move(compile)]() mutable {
        sg::CompileResult result;
        try {
            result = sg::compile(graph, registry, compile);
        } catch (const std::exception& exception) {
            result.ok = false;
            result.error = exception.what();
        }
        std::lock_guard<std::mutex> lock(nodePreviewMutex);
        if (job->generation != previewGeneration.load()) {
            job->state = "stale";
            job->error = "node preview revision is stale";
        } else if (!result.ok) {
            job->state = "failed";
            if (!result.error.empty()) {
                job->error = result.error;
            } else if (!result.diagnostics.empty()) {
                job->error = result.diagnostics.front().message;
            } else {
                job->error = "node preview compilation failed";
            }
        } else if (result.binary.empty()) {
            job->state = "failed";
            job->error = "node preview compilation produced an empty shader";
        } else {
            job->binary = std::move(result.binary);
            job->state = "succeeded";
        }
        --nodePreviewRunning;
    });
    error.clear();
    return job;
}

bool SgEditorHost::Impl::cachedPreview(const std::string& key, std::uint64_t revision,
                                       std::vector<std::uint8_t>& jpeg) {
    std::lock_guard<std::mutex> lock(previewMutex);
    if (previewFrame.key != key || previewFrame.revision != revision || previewFrame.jpeg.empty()) {
        return false;
    }
    jpeg = previewFrame.jpeg;
    return true;
}

void SgEditorHost::Impl::publishPreview(const std::string& key, std::uint64_t revision,
                                        std::vector<std::uint8_t> jpeg) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewFrame.key = key;
        previewFrame.revision = revision;
        previewFrame.sequence = ++previewSequence;
        previewFrame.time = std::chrono::steady_clock::now();
        previewFrame.jpeg = std::move(jpeg);
    }
    previewCondition.notify_all();
}

bool SgEditorHost::Impl::waitPreviewFrame(std::uint64_t lastSequence, const std::string& key,
                                           PreviewFrame& frame) {
    std::unique_lock<std::mutex> lock(previewMutex);
    previewCondition.wait_for(lock, std::chrono::seconds(1), [&] {
        return stopping.load() ||
               (previewSequence != lastSequence && (key.empty() || previewFrame.key == key));
    });
    if (stopping.load() || previewSequence == lastSequence ||
        (!key.empty() && previewFrame.key != key)) {
        return false;
    }
    frame = previewFrame;
    return !frame.jpeg.empty();
}

void SgEditorHost::Impl::beginPreviewStream(const SgPreviewOptions& previewOptions,
                                            std::uint64_t revision) {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        ++previewStreamClients;
        streamOptions = previewOptions;
        streamRevision = revision;
    }
    previewCondition.notify_all();
}

void SgEditorHost::Impl::endPreviewStream() {
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewStreamClients = std::max(previewStreamClients - 1, 0);
        if (previewStreamClients == 0) {
            streamOptions = {};
            streamRevision = 0;
        }
    }
    previewCondition.notify_all();
}

void SgEditorHost::Impl::resetPreviewState() {
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        latestPreviewJob.reset();
        for (auto& [id, job] : jobs) {
            (void)id;
            if (job->mode == "preview") {
                job->canceled = true;
                if (job->state == "queued" || job->state == "running") {
                    job->state = "cancelled";
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewFrame = {};
        ++previewSequence;
    }
    ++previewGeneration;
    {
        std::lock_guard<std::mutex> lock(nodePreviewMutex);
        nodePreviewImages.clear();
        for (auto iterator = nodePreviewJobs.begin(); iterator != nodePreviewJobs.end();) {
            const std::shared_ptr<NodePreviewJob>& job = iterator->second;
            if (job->state == "running" || job->state == "queued") {
                ++iterator;
                continue;
            }
            if (job->worker.joinable()) {
                job->worker.join();
            }
            iterator = nodePreviewJobs.erase(iterator);
        }
    }
    previewCondition.notify_all();
}

void SgEditorHost::Impl::maybeRenderPreview() {
    if (!previewEnabled.load()) {
        return;
    }
    SgPreviewOptions previewOptions;
    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        if (previewStreamClients <= 0) {
            return;
        }
        previewOptions = streamOptions;
        revision = streamRevision;
    }

    const std::shared_ptr<Job> job = previewJobFor(revision);
    if (job == nullptr) {
        return;
    }
    if (document != nullptr && job->revision != document->revision()) {
        return;
    }
    const std::string key = previewKey(previewOptions, job->revision);
    std::vector<std::uint8_t> cachedJpeg;
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        const auto now = std::chrono::steady_clock::now();
        const auto interval =
            std::chrono::milliseconds(1000 / std::clamp(previewOptions.fps, 1, 30));
        if (previewFrame.key == key && !previewFrame.jpeg.empty()) {
            if (now - previewFrame.time < interval) {
                return;
            }
            cachedJpeg = previewFrame.jpeg;
        }
    }
    if (!cachedJpeg.empty()) {
        publishPreview(key, job->revision, std::move(cachedJpeg));
        return;
    }
    std::vector<std::uint8_t> jpeg;
    std::string error;
    if (!renderPreview(previewOptions, jpeg, error)) {
        return;
    }
    publishPreview(key, job->revision, std::move(jpeg));
}

bool SgEditorHost::Impl::openDocument(std::string& error) {
    registry = sg::makeBuiltinNodeRegistry();
    document = sg::editor::Document::open(options.documentPath, registry, error);
    if (document != nullptr) {
        resetPreviewState();
    }
    return document != nullptr;
}

bool SgEditorHost::Impl::resolvePath(const std::string& requestPath, bool allowAbsolute,
                                     const std::vector<std::string>& extensions,
                                     std::filesystem::path& resolved, std::string& error) const {
    if (requestPath.empty()) {
        error = "path must not be empty";
        return false;
    }
    const std::filesystem::path supplied(requestPath);
    if (!allowAbsolute && supplied.is_absolute()) {
        error = "absolute paths are not allowed";
        return false;
    }
    for (const std::filesystem::path& component : supplied) {
        if (component == "..") {
            error = "parent path components are not allowed";
            return false;
        }
    }
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::weakly_canonical(options.root, filesystemError);
    if (filesystemError) {
        error = "cannot resolve sandbox root";
        return false;
    }
    const std::filesystem::path candidate =
        supplied.is_absolute() ? supplied : (root / supplied);
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(candidate, filesystemError);
    if (filesystemError) {
        error = "cannot resolve path";
        return false;
    }
    if (!pathInside(root, canonical)) {
        error = "path escapes the sandbox root";
        return false;
    }
    if (!extensions.empty()) {
        const std::string filename = lower(canonical.filename().string());
        bool allowed = false;
        for (const std::string& extension : extensions) {
            if (endsWith(filename, lower(extension))) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            error = "file extension is not allowed";
            return false;
        }
    }
    resolved = canonical;
    return true;
}

std::optional<HttpReply> SgEditorHost::Impl::submit(std::function<HttpReply()> function) {
    if (stopping.load()) {
        return std::nullopt;
    }
    auto task = std::make_shared<Task>();
    task->function = std::move(function);
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        if (stopping.load()) {
            return std::nullopt;
        }
        tasks.push_back(task);
    }
    taskCondition.notify_one();

    std::unique_lock<std::mutex> lock(task->mutex);
    task->condition.wait_for(lock, kCommandTimeout, [&] {
        return task->done || stopping.load();
    });
    if (!task->done) {
        return std::nullopt;
    }
    if (task->failed) {
        return errorReply(500, "SG_INTERNAL", task->error);
    }
    return task->reply;
}

void SgEditorHost::Impl::respond(httplib::Response& response,
                                 std::function<HttpReply()> function) {
    const auto reply = submit(std::move(function));
    if (!reply.has_value()) {
        response.status = 503;
        response.set_content(errorReply(503, "SG_BUSY", "main thread did not answer in time").body,
                             "application/json; charset=utf-8");
        return;
    }
    response.status = reply->status;
    for (const auto& [name, value] : reply->headers) {
        response.set_header(name, value);
    }
    response.set_content(reply->body, reply->contentType);
}

void SgEditorHost::Impl::processTasks() {
    for (;;) {
        std::shared_ptr<Task> task;
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            if (tasks.empty()) {
                break;
            }
            task = std::move(tasks.front());
            tasks.pop_front();
        }
        HttpReply reply;
        std::string error;
        bool failed = false;
        try {
            reply = task->function();
        } catch (const std::exception& exception) {
            failed = true;
            error = exception.what();
        } catch (...) {
            failed = true;
            error = "unknown main-thread exception";
        }
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            task->failed = failed;
            task->error = std::move(error);
            task->reply = std::move(reply);
            task->done = true;
        }
        task->condition.notify_all();
    }
}

void SgEditorHost::Impl::processJobs() {
    std::vector<std::pair<std::string, Json>> notifications;
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        for (const auto& [id, job] : jobs) {
            const bool terminal = job->state == "succeeded" || job->state == "failed" ||
                                  job->state == "cancelled" || job->state == "stale";
            if (!terminal && document != nullptr && job->revision != document->revision()) {
                job->state = "stale";
                job->canceled = true;
            }
            const bool nowTerminal = job->state == "succeeded" || job->state == "failed" ||
                                     job->state == "cancelled" || job->state == "stale";
            if (nowTerminal && !job->terminalNotified) {
                job->terminalNotified = true;
                Json event;
                writeEventJson(event, *job);
                notifications.emplace_back("compile", std::move(event));
            }
        }
    }
    for (auto& [name, data] : notifications) {
        publishEvent(name, std::move(data));
    }
}

void SgEditorHost::Impl::publishEvent(std::string name, Json data) {
    Event event;
    event.name = std::move(name);
    event.data = data.dump();
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        event.id = ++eventSequence;
        events.push_back(std::move(event));
        while (events.size() > 256) {
            events.pop_front();
        }
    }
    eventCondition.notify_all();
}

bool SgEditorHost::Impl::waitEvent(std::uint64_t lastId, Event& event) {
    std::unique_lock<std::mutex> lock(eventMutex);
    eventCondition.wait_for(lock, std::chrono::seconds(15), [&] {
        if (stopping.load()) {
            return true;
        }
        for (const Event& item : events) {
            if (item.id > lastId) {
                return true;
            }
        }
        return false;
    });
    if (stopping.load()) {
        return false;
    }
    for (const Event& item : events) {
        if (item.id > lastId) {
            event = item;
            return true;
        }
    }
    return false;
}

Json SgEditorHost::Impl::documentData() const {
    Json data;
    data["path"] = relativePath(options.root, document->assetPath());
    data["kind"] = document->graph().kind == sg::GraphKind::SubGraph ? "subgraph" : "graph";
    data["dirty"] = document->dirty();
    data["graph"] = Json::parse(document->graphJson());
    data["layout"] = Json::parse(document->layoutJson());
    data["targets"] = catalogTargets();
    data["capabilities"] = {{"compile", true}, {"preview", previewEnabled.load()}, {"nodePreview", previewEnabled.load()}};
    data["diagnostics"] = Json::parse(document->diagnosticsJson());
    return data;
}

Json SgEditorHost::Impl::sessionData() const {
    Json data;
    data["protocolVersion"] = 1;
    data["server"] = "hbg-sg-editor";
    data["document"] = {{"path", relativePath(options.root, document->assetPath())},
                        {"kind", document->graph().kind == sg::GraphKind::SubGraph ? "subgraph" : "graph"},
                        {"dirty", document->dirty()},
                        {"revision", document->revision()}};
    data["port"] = port;
    data["capabilities"] = {{"compile", true}, {"preview", previewEnabled.load()}, {"nodePreview", previewEnabled.load()},
                            {"undo", document->canUndo()},
                            {"redo", document->canRedo()}};
    data["targets"] = catalogTargets();
    return data;
}

Json SgEditorHost::Impl::catalogTargets() const {
    Json targets = Json::array();
    targets.push_back({{"id", "lit"}, {"available", true}, {"reason", ""}});
    targets.push_back({{"id", "unlit"}, {"available", true}, {"reason", ""}});
    targets.push_back({{"id", "fullscreen"}, {"available", false},
                        {"reason", "not implemented in SG-4"}});
    return targets;
}

Json SgEditorHost::Impl::catalogNodes(const std::string& query, int limit) const {
    const std::string needle = lower(query);
    struct Entry {
        const sg::NodeDef* definition;
        int priority;
        std::string title;
        std::string category;
    };
    std::vector<Entry> matches;
    for (const auto& [type, definition] : registry.definitions()) {
        const std::string title = lower(definition.title.empty() ? type : definition.title);
        const std::string category = lower(definition.category);
        int priority = -1;
        if (needle.empty()) {
            priority = 4;
        } else if (title == needle) {
            priority = 0;
        } else if (title.starts_with(needle)) {
            priority = 1;
        } else {
            for (const std::string& synonym : definition.synonyms) {
                if (lower(synonym).find(needle) != std::string::npos) {
                    priority = 2;
                    break;
                }
            }
            if (priority < 0 && (title.find(needle) != std::string::npos ||
                                 category.find(needle) != std::string::npos)) {
                priority = 3;
            }
        }
        if (priority >= 0) {
            matches.push_back({&definition, priority, definition.title.empty() ? type : definition.title,
                               definition.category});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        if (lhs.title != rhs.title) {
            return lhs.title < rhs.title;
        }
        return lhs.definition->type < rhs.definition->type;
    });
    Json nodes = Json::array();
    const int effectiveLimit = std::clamp(limit, 1, 500);
    for (std::size_t index = 0; index < matches.size() && static_cast<int>(index) < effectiveLimit;
         ++index) {
        const Entry& entry = matches[index];
        Json item;
        item["type"] = entry.definition->type;
        item["title"] = entry.title;
        item["category"] = entry.category;
        item["synonyms"] = entry.definition->synonyms;
        item["targets"] = entry.definition->allowedTargets;
        item["settings"] = entry.definition->settings;
        item["hasPreview"] = entry.definition->hasPreview;
        item["blockNode"] = entry.definition->blockNode;
        item["allowInMainGraph"] = entry.definition->allowInMainGraph;
        item["allowInSubGraph"] = entry.definition->allowInSubGraph;
        item["isTargetSpecific"] = entry.definition->isTargetSpecific;
        item["defaultPrecision"] = sg::toString(entry.definition->defaultPrecision);
        Json slots = Json::array();
        for (const sg::SlotSpec& slot : entry.definition->slots) {
            Json slotJson;
            slotJson["id"] = slot.id;
            slotJson["name"] = slot.name;
            slotJson["displayName"] = slot.label();
            slotJson["direction"] = sg::toString(slot.direction);
            slotJson["type"] = sg::toString(slot.type);
            slotJson["required"] = slot.required;
            slotJson["hidden"] = slot.hidden;
            slotJson["allowConnection"] = slot.allowConnection;
            slotJson["supportsLiteral"] = slot.supportsLiteral;
            slotJson["requiresLiteral"] = slot.requiresLiteral;
            slotJson["canPropagateLiteral"] = slot.canPropagateLiteral;
            slotJson["allowedConcreteTypes"] = Json::array();
            for (sg::ConcreteValueType type : slot.allowedConcreteTypes) {
                slotJson["allowedConcreteTypes"].push_back(sg::toString(type));
            }
            if (slot.defaultValue) {
                slotJson["default"] = Json::parse(sg::serializeValueJson(*slot.defaultValue));
            }
            slots.push_back(std::move(slotJson));
        }
        item["slots"] = std::move(slots);
        nodes.push_back(std::move(item));
    }
    return nodes;
}

Json SgEditorHost::Impl::catalogAssets(const std::string& kind, const std::string& query,
                                       int limit) const {
    const std::string needle = lower(query);
    Json assets = Json::array();
    std::error_code error;
    if (!std::filesystem::is_directory(options.root, error)) {
        return assets;
    }
    const int effectiveLimit = std::clamp(limit, 1, 1000);
    for (auto iterator = std::filesystem::recursive_directory_iterator(
             options.root, std::filesystem::directory_options::skip_permission_denied, error);
         iterator != std::filesystem::recursive_directory_iterator(); ++iterator) {
        if (error) {
            error.clear();
            continue;
        }
        if (iterator->is_symlink() || !iterator->is_regular_file()) {
            continue;
        }
        const std::filesystem::path path = iterator->path();
        if (!isTexturePath(path)) {
            continue;
        }
        const std::string relative = relativePath(options.root, path);
        if (!needle.empty() && lower(relative).find(needle) == std::string::npos) {
            continue;
        }
        Json item;
        item["path"] = relative;
        item["kind"] = kind.empty() ? "texture2d" : kind;
        item["width"] = 0;
        item["height"] = 0;
        assets.push_back(std::move(item));
        if (static_cast<int>(assets.size()) >= effectiveLimit) {
            break;
        }
    }
    return assets;
}


std::shared_ptr<Job> SgEditorHost::Impl::createCompileJob(const Json& request, std::string& error) {
    if (!request.is_object()) {
        error = "compile request must be an object";
        return nullptr;
    }
    std::string mode = "preview";
    if (request.contains("mode")) {
        if (!request["mode"].is_string()) {
            error = "mode must be a string";
            return nullptr;
        }
        mode = request["mode"].get<std::string>();
    }
    if (mode != "preview" && mode != "bake") {
        error = "mode must be preview or bake";
        return nullptr;
    }

    auto job = std::make_shared<Job>();
    job->mode = mode;
    job->revision = document->revision();
    job->graph = document->graph();
    if (mode == "preview") {
        job->graph.target.id = "preview";
        job->graph.target.subTarget = "preview";
    }
    job->registry = registry;
    job->compile.rootDirectory = options.projectRoot;
    job->compile.shaderc = options.shaderc;
    job->compile.bgfxSource = options.bgfxSource;
    job->compile.backends.clear();
    if (request.contains("backends")) {
        if (!request["backends"].is_array()) {
            error = "backends must be an array";
            return nullptr;
        }
        for (const Json& backend : request["backends"]) {
            if (!backend.is_string()) {
                error = "backends must contain strings";
                return nullptr;
            }
            const std::string name = lower(backend.get<std::string>());
            if (name == "dxbc") {
                job->compile.backends.push_back(sg::Backend::Dxbc);
            } else if (name == "spirv") {
                job->compile.backends.push_back(sg::Backend::Spirv);
            } else {
                error = "unsupported compile backend: " + name;
                return nullptr;
            }
        }
    }
    if (job->compile.backends.empty() && mode == "preview" &&
        !options.previewBackend.empty()) {
        if (options.previewBackend == "spirv") {
            job->compile.backends.push_back(sg::Backend::Spirv);
        } else {
            job->compile.backends.push_back(sg::Backend::Dxbc);
        }
    }
    if (job->compile.backends.empty()) {
        job->compile.backends = {sg::Backend::Dxbc, sg::Backend::Spirv};
    }
    if (mode == "preview") {
        job->compile.backends.resize(1);
    }
    if (request.contains("variants")) {
        if (!request["variants"].is_string()) {
            error = "variants must be a string";
            return nullptr;
        }
        const std::string variants = lower(request["variants"].get<std::string>());
        if (variants == "all") {
            job->compile.variants = sg::VariantPolicy::All;
        } else if (variants == "used") {
            job->compile.variants = sg::VariantPolicy::Used;
        } else {
            error = "variants must be used or all";
            return nullptr;
        }
    }
    if (request.contains("includeHlsl")) {
        if (!request["includeHlsl"].is_boolean()) {
            error = "includeHlsl must be a boolean";
            return nullptr;
        }
        job->compile.keepHlsl = request["includeHlsl"].get<bool>();
    }
    if (request.contains("maxVariants")) {
        if (!request["maxVariants"].is_number_integer()) {
            error = "maxVariants must be an integer";
            return nullptr;
        }
        job->compile.maxVariants = std::clamp(request["maxVariants"].get<int>(), 1, 1024);
    }
    if (request.contains("timeoutSeconds")) {
        if (!request["timeoutSeconds"].is_number_integer()) {
            error = "timeoutSeconds must be an integer";
            return nullptr;
        }
        job->compile.shaderTimeoutSeconds =
            std::clamp(request["timeoutSeconds"].get<int>(), 1, 3600);
    }

    if (mode == "bake") {
        std::string output;
        if (request.contains("output")) {
            if (!request["output"].is_string()) {
                error = "output must be a string";
                return nullptr;
            }
            output = request["output"].get<std::string>();
        } else {
            output = sg::editor::defaultBakePath(document->assetPath()).generic_string();
        }
        if (!resolvePath(output, true, {".sgb"}, job->artifactPath, error)) {
            return nullptr;
        }
        job->writeArtifact = true;
    }

    job->id = "c-" + std::to_string(nextJob++);
    return job;
}


void SgEditorHost::Impl::registerRoutes() {
    server.set_payload_max_length(kMaxBodyBytes);

    if (!options.webRoot.empty()) {
        std::error_code webError;
        if (std::filesystem::is_directory(options.webRoot, webError) &&
            std::filesystem::is_regular_file(options.webRoot / "index.html", webError) &&
            server.set_mount_point("/", options.webRoot.string())) {
            webMounted = true;
        } else {
            HBG_LOGW("sg editor: web root is unavailable: '{}'", options.webRoot.string());
        }
    }

    auto authorized = [this](const httplib::Request& request) {
        if (token.empty()) {
            return true;
        }
        if (request.get_header_value("X-SG-Token") == token) {
            return true;
        }
        return request.get_param_value("token") == token;
    };
    auto originAllowed = [this](const httplib::Request& request) {
        const std::string origin = request.get_header_value("Origin");
        if (origin.empty()) {
            return true;
        }
        const std::string portText = std::to_string(port);
        if (origin == "http://127.0.0.1:" + portText ||
            origin == "http://localhost:" + portText) {
            return true;
        }
        for (const char* prefix : {"http://127.0.0.1", "http://localhost"}) {
            const std::string_view view(origin);
            if (!view.starts_with(prefix)) {
                continue;
            }
            const std::string_view suffix = view.substr(std::char_traits<char>::length(prefix));
            if (suffix.empty()) {
                return true;
            }
            if (suffix.front() != ':' || suffix.size() == 1) {
                continue;
            }
            const bool digits = std::all_of(suffix.begin() + 1, suffix.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            });
            if (digits) {
                return true;
            }
        }
        return false;
    };
    auto setError = [](httplib::Response& response, int status, const std::string& code,
                       const std::string& message) {
        const HttpReply reply = errorReply(status, code, message);
        response.status = status;
        response.set_content(reply.body, reply.contentType);
    };
    auto parseBody = [setError](const httplib::Request& request, httplib::Response& response, Json& body) {
        try {
            body = Json::parse(request.body);
        } catch (const std::exception& exception) {
            setError(response, 400, "SG_INVALID_JSON", exception.what());
            return false;
        }
        if (!body.is_object()) {
            setError(response, 400, "SG_INVALID_JSON", "request body must be an object");
            return false;
        }
        return true;
    };
    auto limitFrom = [](const httplib::Request& request, int fallback) {
        const std::string text = request.get_param_value("limit");
        if (text.empty()) {
            return fallback;
        }
        int value = fallback;
        const auto [pointer, error] =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || pointer != text.data() + text.size()) {
            return fallback;
        }
        return std::clamp(value, 1, 1000);
    };
    auto revisionQuery = [](const httplib::Request& request, std::uint64_t& revision) {
        const std::string text = request.get_param_value("rev");
        if (text.empty()) {
            return true;
        }
        unsigned long long value = 0;
        const auto [pointer, error] =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || pointer != text.data() + text.size()) {
            return false;
        }
        revision = static_cast<std::uint64_t>(value);
        return true;
    };

    server.set_pre_routing_handler([this, authorized, originAllowed, setError](
                                       const httplib::Request& request,
                                       httplib::Response& response) {
        if (request.path == "/api/health" || !request.path.starts_with("/api/")) {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        if (!authorized(request)) {
            setError(response, 401, "SG_UNAUTHORIZED", "missing or invalid editor token");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (!originAllowed(request)) {
            setError(response, 403, "SG_FORBIDDEN", "origin is not allowed");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& response) {
        Json data;
        data["status"] = "ok";
        data["protocolVersion"] = 1;
        const HttpReply reply = jsonReply(200, data);
        response.status = 200;
        response.set_content(reply.body, reply.contentType);
    });

    server.Get("/api/session", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            return okReply(document->revision(), sessionData());
        });
    });

    server.Get("/api/document", [this, revisionQuery](const httplib::Request& request,
                                                       httplib::Response& response) {
        std::uint64_t requested = 0;
        const bool hasRevision = !request.get_param_value("rev").empty();
        if (hasRevision && !revisionQuery(request, requested)) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", "invalid rev query");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        respond(response, [this, hasRevision, requested] {
            if (hasRevision && requested != document->revision()) {
                return errorReplyAtRevision(409, "SG_STALE_REVISION", "document revision is stale",
                                            document->revision());
            }
            return okReply(document->revision(), documentData());
        });
    });

    server.Post("/api/commands", [this, parseBody](const httplib::Request& request,
                                                   httplib::Response& response) {
        Json body;
        if (!parseBody(request, response, body)) {
            return;
        }
        if (body.contains("commands") && body["commands"].is_array() &&
            body["commands"].size() > kMaxCommands) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", "command batch exceeds 512 commands");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        respond(response, [this, body = std::move(body)] {
            const sg::editor::CommandResult result = document->applyCommands(body.dump());
            if (!result.ok) {
                return errorReplyAtRevision(statusForCode(result.code), result.code, result.message,
                                            document->revision());
            }
            Json data = Json::parse(result.dataJson);
            publishEvent("revision", Json{{"revision", document->revision()}});
            if (data.contains("diagnostics") && !data["diagnostics"].empty()) {
                publishEvent("diagnostics", Json{{"revision", document->revision()},
                                                 {"count", data["diagnostics"].size()}});
            }
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post("/api/undo", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            std::string error;
            if (!document->undo(error)) {
                return errorReply(404, "SG_NOT_FOUND", error);
            }
            publishEvent("revision", Json{{"revision", document->revision()}});
            Json data;
            data["affected"] = Json::array();
            data["undo"] = document->canUndo();
            data["redo"] = document->canRedo();
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post("/api/redo", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            std::string error;
            if (!document->redo(error)) {
                return errorReply(404, "SG_NOT_FOUND", error);
            }
            publishEvent("revision", Json{{"revision", document->revision()}});
            Json data;
            data["affected"] = Json::array();
            data["undo"] = document->canUndo();
            data["redo"] = document->canRedo();
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post("/api/open", [this, parseBody](const httplib::Request& request,
                                                httplib::Response& response) {
        Json body;
        if (!parseBody(request, response, body)) {
            return;
        }
        if (!body.contains("path") || !body["path"].is_string()) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", "open requires a path");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        const std::string requested = body["path"].get<std::string>();
        respond(response, [this, requested] {
            std::filesystem::path path;
            std::string error;
            if (!resolvePath(requested, false, {".sgraph.json", ".ssubgraph.json"}, path, error)) {
                return errorReply(400, "SG_BAD_REQUEST", error);
            }
            const bool exists = std::filesystem::is_regular_file(path);
            auto opened = sg::editor::Document::open(path, registry, error);
            if (opened == nullptr) {
                return errorReply(exists ? 400 : 404,
                                  exists ? std::string("SG_INVALID_JSON") : std::string("SG_NOT_FOUND"),
                                  error);
            }
            document = std::move(opened);
            resetPreviewState();
            publishEvent("revision", Json{{"revision", document->revision()}});
            publishEvent("file", Json{{"path", relativePath(options.root, document->assetPath())},
                                      {"kind", "opened"}});
            return okReply(document->revision(), sessionData());
        });
    });

    server.Post("/api/save", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            std::string error;
            if (!document->save(error)) {
                return errorReply(500, "SG_IO", error);
            }
            publishEvent("file", Json{{"path", relativePath(options.root, document->assetPath())},
                                      {"kind", "modified"}});
            Json data;
            data["path"] = relativePath(options.root, document->assetPath());
            data["dirty"] = document->dirty();
            data["layoutError"] = document->layoutError();
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post("/api/save-as", [this, parseBody](const httplib::Request& request,
                                                   httplib::Response& response) {
        Json body;
        if (!parseBody(request, response, body)) {
            return;
        }
        if (!body.contains("path") || !body["path"].is_string()) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", "save-as requires a path");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        const std::string requested = body["path"].get<std::string>();
        respond(response, [this, requested] {
            std::filesystem::path path;
            std::string error;
            if (!resolvePath(requested, false, {".sgraph.json", ".ssubgraph.json"}, path, error)) {
                return errorReply(400, "SG_BAD_REQUEST", error);
            }
            if (!document->saveAs(path, error)) {
                return errorReply(500, "SG_IO", error);
            }
            resetPreviewState();
            publishEvent("file", Json{{"path", relativePath(options.root, document->assetPath())},
                                      {"kind", "created"}});
            Json data;
            data["path"] = relativePath(options.root, document->assetPath());
            data["dirty"] = document->dirty();
            data["layoutError"] = document->layoutError();
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Get("/api/diagnostics", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            Json data;
            data["diagnostics"] = Json::parse(document->diagnosticsJson());
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post("/api/compile", [this, parseBody](const httplib::Request& request,
                                                   httplib::Response& response) {
        Json body;
        if (!parseBody(request, response, body)) {
            return;
        }
        respond(response, [this, body = std::move(body)] {
            std::string error;
            const std::shared_ptr<Job> job = createCompileJob(body, error);
            if (job == nullptr) {
                return errorReply(400, "SG_BAD_REQUEST", error);
            }
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                jobs[job->id] = job;
            }
            job->worker = std::thread([this, job] {
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    if (job->canceled) {
                        job->state = "cancelled";
                        return;
                    }
                    job->state = "running";
                }
                sg::CompileResult result;
                try {
                    result = sg::compile(job->graph, job->registry, job->compile);
                } catch (const std::exception& exception) {
                    result.ok = false;
                    result.error = exception.what();
                }
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    job->stats = result.stats;
                    job->diagnostics = diagnosticsForJob(result);
                    if (job->canceled || job->state == "stale") {
                        if (job->state != "stale") {
                            job->state = "cancelled";
                        }
                        return;
                    }
                    if (!result.ok) {
                        job->state = "failed";
                        job->error = result.error;
                        return;
                    }
                    if (job->writeArtifact) {
                        std::error_code filesystemError;
                        if (!job->artifactPath.parent_path().empty()) {
                            std::filesystem::create_directories(job->artifactPath.parent_path(),
                                                                filesystemError);
                        }
                        std::ofstream output(job->artifactPath,
                                             std::ios::binary | std::ios::trunc);
                        if (!output) {
                            job->state = "failed";
                            job->error = "cannot write baked artifact";
                            return;
                        }
                        output.write(reinterpret_cast<const char*>(result.binary.data()),
                                     static_cast<std::streamsize>(result.binary.size()));
                        if (!output) {
                            job->state = "failed";
                            job->error = "cannot write baked artifact";
                            return;
                        }
                    }
                    job->binary = std::move(result.binary);
                    job->state = "succeeded";
                    if (job->mode == "preview") {
                        latestPreviewJob = job;
                    }
                }
            });
            Json data;
            data["jobId"] = job->id;
            data["revision"] = job->revision;
            data["state"] = job->state;
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Post(R"(/api/compile/([A-Za-z0-9_\-]+)/cancel)",
                [this](const httplib::Request& request, httplib::Response& response) {
                    const std::string id = request.matches[1].str();
                    std::lock_guard<std::mutex> lock(jobMutex);
                    const auto found = jobs.find(id);
                    if (found == jobs.end()) {
                        const HttpReply reply = errorReply(404, "SG_NOT_FOUND", "compile job not found");
                        response.status = reply.status;
                        response.set_content(reply.body, reply.contentType);
                        return;
                    }
                    found->second->canceled = true;
                    if (found->second->state == "queued" || found->second->state == "running") {
                        found->second->state = "cancelled";
                    }
                    Json data;
                    data["jobId"] = id;
                    data["state"] = found->second->state;
                    const HttpReply reply = okReply(found->second->revision, std::move(data));
                    response.status = reply.status;
                    response.set_content(reply.body, reply.contentType);
                });

    server.Get(R"(/api/compile/([A-Za-z0-9_\-]+))",
               [this](const httplib::Request& request, httplib::Response& response) {
                   const std::string id = request.matches[1].str();
                   std::lock_guard<std::mutex> lock(jobMutex);
                   const auto found = jobs.find(id);
                   if (found == jobs.end()) {
                       const HttpReply reply = errorReply(404, "SG_NOT_FOUND", "compile job not found");
                       response.status = reply.status;
                       response.set_content(reply.body, reply.contentType);
                       return;
                   }
                   const Job& job = *found->second;
                   Json data;
                   data["state"] = job.state;
                   data["revision"] = job.revision;
                   data["diagnostics"] = Json::parse(job.diagnostics);
                   data["stats"] = {{"passes", job.stats.passes},
                                    {"variants", job.stats.variants},
                                    {"backends", job.compile.backends.size()},
                                    {"milliseconds", static_cast<int>(job.stats.elapsedSeconds * 1000.0)},
                                    {"cacheHits", job.stats.cacheHits},
                                    {"shaders", job.stats.shaders}};
                   data["artifacts"] = Json::array();
                   if (job.state == "succeeded" && job.writeArtifact) {
                       Json artifact;
                       artifact["path"] = relativePath(options.root, job.artifactPath);
                       artifact["hash"] = makeArtifactHash(job.binary);
                       data["artifacts"].push_back(std::move(artifact));
                   }
                   if (!job.error.empty()) {
                       data["error"] = job.error;
                   }
                   const HttpReply reply = okReply(job.revision, std::move(data));
                   response.status = reply.status;
                   response.set_content(reply.body, reply.contentType);
               });

    server.Get("/api/catalog/nodes", [this, limitFrom](const httplib::Request& request,
                                                        httplib::Response& response) {
        const std::string query = request.get_param_value("q");
        const int limit = limitFrom(request, 50);
        respond(response, [this, query, limit] {
            Json data;
            data["nodes"] = catalogNodes(query, limit);
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Get("/api/catalog/targets", [this](const httplib::Request&, httplib::Response& response) {
        respond(response, [this] {
            Json data;
            data["targets"] = catalogTargets();
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Get("/api/assets", [this, limitFrom](const httplib::Request& request,
                                                 httplib::Response& response) {
        const std::string kind = request.get_param_value("kind");
        const std::string query = request.get_param_value("q");
        const int limit = limitFrom(request, 100);
        respond(response, [this, kind, query, limit] {
            Json data;
            data["assets"] = catalogAssets(kind, query, limit);
            return okReply(document->revision(), std::move(data));
        });
    });

    server.Get("/api/preview.jpg", [this](const httplib::Request& request,
                                          httplib::Response& response) {
        SgPreviewOptions options;
        std::uint64_t revision = 0;
        std::string parseError;
        if (!parsePreviewOptions(request, options, revision, parseError)) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", parseError);
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        respond(response, [this, options, revision] {
            if (!previewEnabled.load()) {
                return errorReply(503, "SG_BUSY", "preview rendering is not available");
            }
            if (document == nullptr) {
                return errorReply(500, "SG_INTERNAL", "no document is open");
            }
            if (revision != 0 && revision != document->revision()) {
                return errorReplyAtRevision(409, "SG_STALE_REVISION",
                                            "the requested preview revision is stale",
                                            document->revision());
            }
            const std::uint64_t currentRevision = document->revision();
            if (previewJobFor(currentRevision) == nullptr) {
                return errorReply(503, "SG_BUSY",
                                  "preview has not been compiled; request mode=preview first");
            }
            const std::string key = previewKey(options, currentRevision);
            std::vector<std::uint8_t> jpeg;
            const bool cacheHit = cachedPreview(key, currentRevision, jpeg);
            if (!cacheHit) {
                std::string error;
                if (!renderPreview(options, jpeg, error)) {
                    return errorReply(503, "SG_BUSY", error);
                }
                publishPreview(key, currentRevision, jpeg);
            }
            HttpReply reply{200, std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()),
                            "image/jpeg",
                            {{"Cache-Control", "no-store"},
                             {"X-SG-Preview-Cache", cacheHit ? "hit" : "miss"}}};
            return reply;
        });
    });

    server.Get("/api/preview/stream", [this](const httplib::Request& request,
                                              httplib::Response& response) {
        SgPreviewOptions options;
        std::uint64_t revision = 0;
        std::string parseError;
        if (!parsePreviewOptions(request, options, revision, parseError)) {
            const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", parseError);
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        if (!previewEnabled.load()) {
            const HttpReply reply = errorReply(503, "SG_BUSY", "preview rendering is not available");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        const std::shared_ptr<Job> job = previewJobFor(0);
        if (job == nullptr) {
            const HttpReply reply =
                errorReply(503, "SG_BUSY", "preview has not been compiled; request mode=preview first");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }
        if (revision != 0 && job->revision != revision) {
            const HttpReply reply = errorReply(409, "SG_STALE_REVISION",
                                               "the requested preview revision is stale");
            response.status = reply.status;
            response.set_content(reply.body, reply.contentType);
            return;
        }

        response.set_header("Cache-Control", "no-store");
        auto lease = std::make_shared<StreamLease>();
        lease->owner = this;
        beginPreviewStream(options, revision);
        response.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=sgframe",
            [this, lease = std::move(lease),
             lastSequence = std::uint64_t{0}, key = previewKey(options, job->revision)](
                std::size_t, httplib::DataSink& sink) mutable {
                (void)lease;
                if (!sink.is_writable()) {
                    return false;
                }
                PreviewFrame frame;
                if (!waitPreviewFrame(lastSequence, key, frame)) {
                    return !stopping.load();
                }
                lastSequence = frame.sequence;
                if (frame.jpeg.empty()) {
                    return true;
                }
                char header[160];
                const int headerLength = std::snprintf(
                    header, sizeof(header),
                    "--sgframe\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                    frame.jpeg.size());
                if (headerLength <= 0) {
                    return false;
                }
                if (!sink.write(header, static_cast<std::size_t>(headerLength))) {
                    return false;
                }
                if (!sink.write(reinterpret_cast<const char*>(frame.jpeg.data()),
                                frame.jpeg.size())) {
                    return false;
                }
                return sink.write("\r\n", 2);
            },
            nullptr);
    });

    server.Get(R"(/api/preview/node/(\d+)\.png)",
               [this](const httplib::Request& request, httplib::Response& response) {
                   if (!previewEnabled.load()) {
                       const HttpReply reply =
                           errorReply(503, "SG_BUSY", "preview rendering is not available");
                       response.status = reply.status;
                       response.set_content(reply.body, reply.contentType);
                       return;
                   }
                   int nodeValue = 0;
                   const std::string nodeText = request.matches[1].str();
                   const auto parsed = std::from_chars(nodeText.data(),
                                                       nodeText.data() + nodeText.size(), nodeValue);
                   if (parsed.ec != std::errc{} ||
                       parsed.ptr != nodeText.data() + nodeText.size() || nodeValue < 0) {
                       const HttpReply reply =
                           errorReply(400, "SG_BAD_REQUEST", "invalid node id");
                       response.status = reply.status;
                       response.set_content(reply.body, reply.contentType);
                       return;
                   }
                   SgPreviewOptions requested;
                   requested.size = 128;
                   requested.format = "png";
                   std::uint64_t revision = 0;
                   std::string parseError;
                   if (!parsePreviewOptions(request, requested, revision, parseError)) {
                       const HttpReply reply = errorReply(400, "SG_BAD_REQUEST", parseError);
                       response.status = reply.status;
                       response.set_content(reply.body, reply.contentType);
                       return;
                   }
                   if (request.get_param_value("size").empty()) {
                       requested.size = 128;
                   }
                   requested.format = "png";
                   const bool explicitMesh = !request.get_param_value("mesh").empty();
                   const sg::NodeId nodeId = static_cast<sg::NodeId>(nodeValue);

                   respond(response, [this, requested, revision, nodeId, explicitMesh] {
                       if (document == nullptr) {
                           return errorReply(500, "SG_INTERNAL", "no document is open");
                       }
                       const std::uint64_t currentRevision = document->revision();
                       if (revision != 0 && revision != currentRevision) {
                           return errorReplyAtRevision(409, "SG_STALE_REVISION",
                                                       "the requested preview revision is stale",
                                                       currentRevision);
                       }
                       const sg::Graph& graph = document->graph();
                       const sg::NodeInstance* selected = graph.findNode(nodeId);
                       if (selected == nullptr) {
                           return errorReply(404, "SG_NOT_FOUND", "node not found");
                       }
                       sg::Graph nodeGraph;
                       sg::SlotId outputSlot = sg::kInvalidSlot;
                       bool uses3D = false;
                       std::string error;
                       if (!buildNodePreviewGraph(graph, registry, nodeId, nodeGraph, outputSlot,
                                                  uses3D, error)) {
                           return errorReply(404, "SG_NOT_FOUND", error);
                       }
                       SgPreviewOptions options = requested;
                       if (!explicitMesh) {
                           options.mesh = uses3D ? "sphere" : "quad";
                       }
                       const std::uint64_t generation = previewGeneration.load();
                       const std::string key =
                           nodePreviewKey(generation, currentRevision, nodeId, outputSlot);
                       std::shared_ptr<NodePreviewJob> job = nodePreviewJobFor(key);
                       const std::string imageKey = key + "|" + previewKey(options, 0);
                       if (job != nullptr) {
                           std::string state;
                           std::string jobError;
                           {
                               std::lock_guard<std::mutex> lock(nodePreviewMutex);
                               state = job->state;
                               jobError = job->error;
                           }
                           if (state == "succeeded") {
                               {
                                   std::lock_guard<std::mutex> lock(nodePreviewMutex);
                                   const auto cached = nodePreviewImages.find(imageKey);
                                   if (cached != nodePreviewImages.end()) {
                                       return HttpReply{
                                           200,
                                           std::string(reinterpret_cast<const char*>(
                                                           cached->second.data()),
                                                       cached->second.size()),
                                           "image/png",
                                           {{"Cache-Control", "no-store"},
                                            {"X-SG-Node-Preview", "ready"},
                                            {"X-SG-Node-Preview-Cache", "hit"}}};
                                   }
                               }
                               std::vector<std::uint8_t> image;
                               std::string renderError;
                               if (!renderNodePreview(key, options, image, renderError) ||
                                   image.empty()) {
                                   return errorReply(503, "SG_BUSY",
                                                     renderError.empty()
                                                         ? "node preview rendering failed"
                                                         : renderError);
                               }
                               {
                                   std::lock_guard<std::mutex> lock(nodePreviewMutex);
                                   nodePreviewImages[imageKey] = image;
                               }
                               return HttpReply{
                                   200,
                                   std::string(reinterpret_cast<const char*>(image.data()),
                                               image.size()),
                                   "image/png",
                                   {{"Cache-Control", "no-store"},
                                    {"X-SG-Node-Preview", "ready"},
                                    {"X-SG-Node-Preview-Cache", "miss"}}};
                           }
                           if (state == "failed" || state == "stale") {
                               return errorReplyAtRevision(422, "SG_COMPILE_FAILED", jobError,
                                                           currentRevision);
                           }
                       } else {
                           std::string startError;
                           job = ensureNodePreviewJob(key, std::move(nodeGraph), generation,
                                                      currentRevision, nodeId, outputSlot,
                                                      startError);
                       }
                       Json pending;
                       pending["node"] = nodeId;
                       pending["slot"] = outputSlot;
                       pending["state"] = job == nullptr ? "queued" : "running";
                       HttpReply reply = okReply(currentRevision, std::move(pending));
                       reply.status = 202;
                       reply.headers.push_back({"Retry-After", "1"});
                       reply.headers.push_back({"X-SG-Node-Preview", "compiling"});
                       return reply;
                   });
               });

    server.Get("/api/events", [this](const httplib::Request&, httplib::Response& response) {
        response.set_header("Cache-Control", "no-cache");
        response.set_header("Connection", "keep-alive");
        response.set_chunked_content_provider(
            "text/event-stream",
            [this, lastId = std::uint64_t{0}](std::size_t, httplib::DataSink& sink) mutable {
                if (!sink.is_writable()) {
                    return false;
                }
                Event event;
                if (!waitEvent(lastId, event)) {
                    if (stopping.load()) {
                        return false;
                    }
                    return sink.write(": ping\n\n", 8);
                }
                lastId = event.id;
                const std::string message = "id: " + std::to_string(event.id) + "\nevent: " +
                                            event.name + "\ndata: " + event.data + "\n\n";
                return sink.write(message.data(), message.size());
            },
            nullptr);
    });

    if (!webMounted) {
        server.Get("/", [this](const httplib::Request&, httplib::Response& response) {
            const std::string html =
                "<!doctype html><html><head><meta charset=\"utf-8\"><title>Shader Graph editor</title></head>"
                "<body style=\"font-family:sans-serif;background:#111;color:#ddd;padding:24px\">"
                "<h1>Shader Graph editor host</h1><p>SG-4 HTTP core is running. "
                "Use <code>/api/session</code> and <code>/api/document</code> with the editor token.</p>"
                "</body></html>";
            response.status = 200;
            response.set_content(html, "text/html; charset=utf-8");
        });
    }
}


SgEditorHost::SgEditorHost() = default;
SgEditorHost::~SgEditorHost() { stop(); }

bool SgEditorHost::start(const Options& requested, std::string& error) {
    stop();
    auto impl = std::make_unique<Impl>();
    impl->options = requested;
    if (impl->options.root.empty()) {
        impl->options.root = std::filesystem::current_path();
    }
    std::error_code filesystemError;
    impl->options.root = std::filesystem::weakly_canonical(impl->options.root, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(impl->options.root)) {
        error = "invalid --sg-root directory";
        return false;
    }
    if (impl->options.projectRoot.empty()) {
        impl->options.projectRoot = HBG_SG_ROOT;
    }
    impl->options.projectRoot = std::filesystem::weakly_canonical(impl->options.projectRoot,
                                                                  filesystemError);
    if (filesystemError) {
        impl->options.projectRoot = std::filesystem::path(HBG_SG_ROOT);
    }
    if (impl->options.shaderc.empty()) {
        impl->options.shaderc = HBG_SG_SHADERC_DEFAULT;
    }
    if (impl->options.bgfxSource.empty()) {
        impl->options.bgfxSource = HBG_SG_BGFX_SOURCE_DEFAULT;
    }
    if (impl->options.documentPath.empty()) {
        error = "--sg-editor requires a graph path";
        return false;
    }
    std::filesystem::path documentPath;
    if (!impl->resolvePath(impl->options.documentPath.generic_string(), true,
                           {".sgraph.json", ".ssubgraph.json"}, documentPath, error)) {
        return false;
    }
    impl->options.documentPath = documentPath;
    if (!impl->openDocument(error)) {
        return false;
    }
    impl->token = impl->options.token.empty() ? makeToken() : impl->options.token;

    impl->registerRoutes();
    int bound = -1;
    if (impl->options.port == 0) {
        bound = impl->server.bind_to_any_port("127.0.0.1");
    } else if (impl->server.bind_to_port("127.0.0.1", impl->options.port)) {
        bound = impl->options.port;
    }
    if (bound < 0) {
        error = "failed to bind 127.0.0.1:" + std::to_string(impl->options.port);
        return false;
    }
    impl->port = static_cast<std::uint16_t>(bound);
    impl->serverThread = std::thread([server = &impl->server] { server->listen_after_bind(); });
    for (int attempt = 0; attempt < 100 && !impl->server.is_running(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!impl->server.is_running()) {
        impl->stopping = true;
        impl->server.stop();
        if (impl->serverThread.joinable()) {
            impl->serverThread.join();
        }
        error = "editor server did not start";
        return false;
    }

    m_impl = std::move(impl);
    HBG_LOGI("SG editor: http://127.0.0.1:{}?token={}", m_impl->port, m_impl->token);
    error.clear();
    return true;
}

void SgEditorHost::stop() {
    if (!m_impl) {
        return;
    }
    m_impl->stopping = true;
    {
        std::lock_guard<std::mutex> lock(m_impl->taskMutex);
        for (const std::shared_ptr<Task>& task : m_impl->tasks) {
            {
                std::lock_guard<std::mutex> taskLock(task->mutex);
                task->failed = true;
                task->error = "editor host is stopping";
                task->done = true;
            }
            task->condition.notify_all();
        }
        m_impl->tasks.clear();
    }
    m_impl->taskCondition.notify_all();
    m_impl->eventCondition.notify_all();
    m_impl->previewCondition.notify_all();
    m_impl->server.stop();
    if (m_impl->serverThread.joinable()) {
        m_impl->serverThread.join();
    }
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(m_impl->jobMutex);
        for (const auto& [id, job] : m_impl->jobs) {
            job->canceled = true;
            if (job->state == "queued") {
                job->state = "cancelled";
            }
            jobs.push_back(job);
        }
    }
    for (const std::shared_ptr<Job>& job : jobs) {
        if (job->worker.joinable()) {
            job->worker.join();
        }
    }
    std::vector<std::shared_ptr<NodePreviewJob>> nodeJobs;
    {
        std::lock_guard<std::mutex> lock(m_impl->nodePreviewMutex);
        for (const auto& [key, job] : m_impl->nodePreviewJobs) {
            (void)key;
            nodeJobs.push_back(job);
        }
    }
    for (const std::shared_ptr<NodePreviewJob>& job : nodeJobs) {
        if (job->worker.joinable()) {
            job->worker.join();
        }
    }
    m_impl.reset();
}

void SgEditorHost::poll() {
    if (!m_impl) {
        return;
    }
    m_impl->processTasks();
    m_impl->processJobs();
    m_impl->maybeRenderPreview();
}

bool SgEditorHost::running() const {
    return m_impl != nullptr && m_impl->server.is_running() && !m_impl->stopping.load();
}

std::uint16_t SgEditorHost::port() const {
    return m_impl != nullptr ? m_impl->port : 0;
}

const std::string& SgEditorHost::token() const {
    static const std::string empty;
    return m_impl != nullptr ? m_impl->token : empty;
}

void SgEditorHost::setPreviewCallback(SgPreviewCallback callback) {
    if (m_impl != nullptr) {
        m_impl->setPreviewCallback(std::move(callback));
    }
}

} // namespace hbg
