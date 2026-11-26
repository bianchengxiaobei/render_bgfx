#include "sg/compile.h"

#include "compile_internal.h"
#include "sg/node_library.h"
#include "sg/serialize.h"
#include "sg/validate.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string_view>

namespace sg {
namespace {

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool readText(const std::filesystem::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();
    return true;
}

std::string backendName(Backend backend) {
    return backend == Backend::Dxbc ? "dxbc" : "spirv";
}

std::string variantDefines(const Graph& graph, const detail::VariantInfo& variant,
                           const detail::TargetSpec& target) {
    std::vector<std::string> defines = variant.defines;
    if (target.supportsAlphaClip) {
        const auto found = graph.target.settings.find("alphaClip");
        if (found != graph.target.settings.end() && found->second.asBool() != nullptr &&
            *found->second.asBool()) {
            defines.push_back("ALPHA_CLIP=1");
        }
    }
    if (target.supportsDoubleSided) {
        const auto found = graph.target.settings.find("doubleSided");
        if (found != graph.target.settings.end() && found->second.asBool() != nullptr &&
            *found->second.asBool()) {
            defines.push_back("DOUBLE_SIDED=1");
        }
    }
    const auto transparent = graph.target.settings.find("transparent");
    if (transparent != graph.target.settings.end() && transparent->second.asBool() != nullptr &&
        *transparent->second.asBool()) {
        defines.push_back("TRANSPARENT=1");
    }
    std::sort(defines.begin(), defines.end());
    return defines.empty() ? std::string() : [&]() {
        std::string joined;
        for (const std::string& define : defines) {
            if (!joined.empty()) {
                joined.push_back(';');
            }
            joined += define;
        }
        return joined;
    }();
}

std::vector<std::string> splitDefines(const std::string& defines) {
    std::vector<std::string> result;
    if (defines.empty()) {
        return result;
    }
    std::size_t begin = 0;
    while (begin <= defines.size()) {
        const std::size_t end = defines.find(';', begin);
        result.push_back(defines.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

} // namespace

std::string generatorVersion() { return "sg-2.0.0"; }

CompileResult compile(const Graph& source, const CompileOptions& options) {
    NodeRegistry registry = makeBuiltinNodeRegistry();
    return compile(source, registry, options);
}

CompileResult compile(const Graph& source, const NodeRegistry& registry, const CompileOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    CompileResult result;
    Graph graph = source;
    if (!options.subgraphs.graphs.empty()) {
        SubGraphExpansion expansion = expandSubgraphs(graph, registry, options.subgraphs);
        if (!expansion.ok) {
            result.diagnostics.insert(result.diagnostics.end(), expansion.diagnostics.begin(),
                                      expansion.diagnostics.end());
            result.error = expansion.error;
            return result;
        }
        graph = std::move(expansion.graph);
    }

    const detail::TargetSpec* target = detail::findTarget(graph.target.id);
    if (target == nullptr) {
        result.error = "unsupported target: " + graph.target.id;
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorTargetUnsupported;
        diagnostic.message = result.error;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    std::string planError;
    if (!detail::validatePassPlan(*target, planError)) {
        result.error = planError;
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorTemplate;
        diagnostic.message = planError;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    if (!detail::preparePropertyNodes(graph, result.diagnostics)) {
        result.error = "invalid Property node";
        return result;
    }

    ValidationResult validation = validate(graph, registry, true);
    if (!validation.ok) {
        result.diagnostics.insert(result.diagnostics.end(), validation.diagnostics.begin(),
                                  validation.diagnostics.end());
        result.error = "graph validation failed";
        return result;
    }

    detail::ActiveGraph active = detail::collectActiveNodes(graph, registry, *target);
    detail::PropertySet properties = detail::collectProperties(graph, active, *target);
    if (!properties.ok) {
        result.diagnostics.insert(result.diagnostics.end(), properties.diagnostics.begin(),
                                  properties.diagnostics.end());
        result.error = "property collection failed";
        return result;
    }
    detail::KeywordSet keywords = detail::collectKeywords(graph, *target, options.variants,
                                                          options.maxVariants);
    if (!keywords.ok) {
        result.diagnostics.insert(result.diagnostics.end(), keywords.diagnostics.begin(),
                                  keywords.diagnostics.end());
        result.error = "keyword collection failed";
        return result;
    }

    if (options.rootDirectory.empty() || options.shaderc.empty() || options.bgfxSource.empty()) {
        result.error = "compile requires rootDirectory, shaderc and bgfxSource";
        return result;
    }
    for (const detail::PassSpec& pass : target->passes) {
        for (const std::string& templatePath : {pass.vertexTemplate, pass.fragmentTemplate}) {
            if (!std::filesystem::exists(options.rootDirectory / templatePath)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTemplate;
                diagnostic.message = "missing shader template: " + templatePath;
                diagnostic.pass = pass.id;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (!result.diagnostics.empty()) {
        result.error = "template validation failed";
        return result;
    }

    std::string templateFingerprint;
    for (const detail::PassSpec& pass : target->passes) {
        for (const std::string& path : {pass.vertexTemplate, pass.fragmentTemplate}) {
            std::string text;
            if (readText(options.rootDirectory / path, text)) {
                templateFingerprint += path;
                templateFingerprint += text;
            }
        }
    }
    std::string varyingText;
    if (readText(options.rootDirectory / "shaders/shadergraph/varying_graph.def.sc", varyingText)) {
        templateFingerprint += "varying";
        templateFingerprint += varyingText;
    }

    const std::uint64_t content = sg::contentHash(graph);
    const std::string graphDirName = [&]() {
        std::ostringstream stream;
        stream << std::hex << content;
        return stream.str();
    }();
    const std::filesystem::path cache = options.cacheDirectory.empty()
                                            ? options.rootDirectory / "build/shadergraph" / graphDirName
                                            : options.cacheDirectory / graphDirName;
    std::error_code filesystemError;
    std::filesystem::create_directories(cache, filesystemError);
    if (filesystemError) {
        result.error = "failed to create shader cache";
        return result;
    }

    const std::string templateHash = std::to_string(fnv1a(templateFingerprint));
    detail::ShadercOptions shaderc;
    shaderc.root = options.rootDirectory;
    shaderc.shaderc = options.shaderc;
    shaderc.bgfxSource = options.bgfxSource;
    shaderc.cache = cache;
    shaderc.timeoutSeconds = options.shaderTimeoutSeconds;

    std::vector<detail::PackedBlob> blobs;
    std::string debugHlsl;
    std::uint32_t requiredFeatureMask = 0;
    for (const detail::VariantInfo& variant : keywords.variants) {
        detail::EmittedGraph emitted =
            detail::emitSurface(graph, registry, active, *target, properties, keywords, variant);
        if (!emitted.ok) {
            result.diagnostics.insert(result.diagnostics.end(), emitted.diagnostics.begin(),
                                      emitted.diagnostics.end());
            result.error = "HLSL emission failed";
            return result;
        }
        requiredFeatureMask |= emitted.requiredFeatureMask;
        const std::string surfacePath = (cache / "surface.sc").string();
        {
            std::ofstream surface(surfacePath, std::ios::binary | std::ios::trunc);
            if (!surface) {
                result.error = "failed to write generated surface";
                return result;
            }
            surface << emitted.source;
        }
        const std::string defineText = variantDefines(graph, variant, *target);
        const std::vector<std::string> defines = splitDefines(defineText);
        debugHlsl += "// variant " + std::to_string(variant.key) + "\n" + emitted.source + "\n";
        for (std::size_t passIndex = 0; passIndex < target->passes.size(); ++passIndex) {
            const detail::PassSpec& pass = target->passes[passIndex];
            for (int stage = 0; stage < 2; ++stage) {
                const std::string stageName = stage == 0 ? "vertex" : "fragment";
                const std::string stageSuffix = stage == 0 ? "vs" : "fs";
                const std::string wrapperName = pass.id + "." + std::to_string(variant.key) + "." +
                                                stageSuffix + ".hlsl";
                const std::filesystem::path wrapper = cache / wrapperName;
                {
                    std::ofstream file(wrapper, std::ios::binary | std::ios::trunc);
                    if (!file) {
                        result.error = "failed to write shader wrapper";
                        return result;
                    }
                    file << "#include \"" << (stage == 0 ? pass.vertexTemplate : pass.fragmentTemplate)
                         << "\"\n";
                }
                for (Backend backend : options.backends.empty()
                                           ? std::vector<Backend>{Backend::Dxbc, Backend::Spirv}
                                           : options.backends) {
                    const std::string outputName = pass.id + "." + std::to_string(variant.key) + "." +
                                                   backendName(backend) + "." + stageSuffix + ".bin";
                    const std::string hashKey = templateHash + "|" + std::to_string(content) + "|" +
                                                generatorVersion() + "|" + defineText + "|" +
                                                std::to_string(fnv1a(emitted.source)) + "|" +
                                                std::to_string(passIndex) + "|" +
                                                std::to_string(variant.key);
                    detail::ShadercResult compiled = detail::compileShader(
                        shaderc, wrapper.string(), (cache / outputName).string(), stageName, backend,
                        defines, hashKey);
                    if (!compiled.ok) {
                        Diagnostic diagnostic;
                        diagnostic.severity = Severity::Error;
                        diagnostic.code = kErrorShaderCompile;
                        diagnostic.message = pass.id + " " + stageName + " (" + backendName(backend) +
                                             "): " + compiled.error;
                        if (!compiled.compilerOutput.empty()) {
                            diagnostic.message += "\n" + compiled.compilerOutput;
                        }
                        diagnostic.pass = pass.id;
                        diagnostic.variant = std::to_string(variant.key);
                        result.diagnostics.push_back(std::move(diagnostic));
                        result.error = "shaderc compilation failed";
                        return result;
                    }
                    detail::PackedBlob packed;
                    packed.pass = pass.id;
                    packed.passId = static_cast<std::uint16_t>(passIndex);
                    packed.variantKey = variant.key;
                    packed.backend = backend;
                    packed.stage = stage;
                    packed.data = std::move(compiled.data);
                    blobs.push_back(std::move(packed));
                    if (compiled.cached) {
                        ++result.stats.cacheHits;
                    }
                }
            }
        }
    }

    detail::PackInput packed;
    packed.graph = &graph;
    packed.target = target;
    packed.properties = properties.properties;
    packed.keywords = keywords.keywords;
    packed.variants = keywords.variants;
    packed.blobs = std::move(blobs);
    packed.hlsl = std::move(debugHlsl);
    packed.keepHlsl = options.keepHlsl;
    packed.requiredFeatureMask = requiredFeatureMask;
    packed.contentHash = content ^ fnv1a(generatorVersion()) ^ fnv1a(templateHash);
    SerializeOptions sourceOptions;
    sourceOptions.pretty = false;
    sourceOptions.includePositions = false;
    sourceOptions.includeGeneratorVersion = false;
    packed.sourceHash = fnv1a(sg::serialize(graph, sourceOptions));
    if (!detail::packSgb(packed, result.binary, result.error)) {
        return result;
    }

    result.ok = true;
    result.stats.activeNodes = static_cast<int>(active.order.size());
    result.stats.passes = static_cast<int>(target->passes.size());
    result.stats.variants = static_cast<int>(keywords.variants.size());
    result.stats.shaders = static_cast<int>(packed.blobs.size());
    (void)started;
    const auto finished = std::chrono::steady_clock::now();
    result.stats.elapsedSeconds =
        std::chrono::duration<double>(finished - started).count();
    return result;
}

} // namespace sg
