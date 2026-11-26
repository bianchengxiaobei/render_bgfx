#include "compile_internal.h"

namespace sg::detail {
namespace {

Value f(float value) { return Value::makeFloat(value); }
Value v3(float x, float y, float z) { return Value::makeVector3({x, y, z}); }
Value v4(float x, float y, float z, float w) { return Value::makeVector4({x, y, z, w}); }

PassSpec pass(const char* id, const char* vs, const char* fs) {
    PassSpec result;
    result.id = id;
    result.vertexTemplate = vs;
    result.fragmentTemplate = fs;
    return result;
}

const TargetSpec kLit = {
    "lit",
    {
        pass("forward", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/forward_graph.fs.sc"),
        pass("gbuffer", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/gbuffer_graph.fs.sc"),
        pass("depth", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/depth_graph.fs.sc"),
        pass("shadow", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/shadow_graph.fs.sc"),
    },
    {{"baseColor", v4(1.0f, 1.0f, 1.0f, 1.0f)},
     {"metallic", f(0.0f)},
     {"smoothness", f(0.5f)},
     {"normalTangent", v3(0.0f, 0.0f, 1.0f)},
     {"normalObject", v3(0.0f, 0.0f, 1.0f)},
     {"normalWorld", v3(0.0f, 0.0f, 1.0f)},
     {"occlusion", f(1.0f)},
     {"emission", v4(0.0f, 0.0f, 0.0f, 1.0f)},
     {"alpha", f(1.0f)},
     {"alphaClipThreshold", f(0.5f)}},
    {"positionOS", "baseColor", "metallic", "smoothness", "normalTangent", "normalObject",
     "normalWorld", "occlusion", "emission", "alpha", "alphaClipThreshold"},
    5,
    32,
    4,
    true,
    true,
};

const TargetSpec kUnlit = {
    "unlit",
    {
        pass("forward", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/unlit_graph.fs.sc"),
        pass("depth", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/depth_graph.fs.sc"),
        pass("shadow", "shaders/shadergraph/forward_graph.vs.sc", "shaders/shadergraph/shadow_graph.fs.sc"),
    },
    {{"baseColor", v4(1.0f, 1.0f, 1.0f, 1.0f)}, {"alpha", f(1.0f)}, {"alphaClipThreshold", f(0.5f)}},
    {"positionOS", "baseColor", "alpha", "alphaClipThreshold"},
    5,
    32,
    4,
    true,
    true,
};

const TargetSpec kPreview = {
    "preview",
    {pass("forward", "shaders/shadergraph/forward_graph.vs.sc",
          "shaders/shadergraph/unlit_graph.fs.sc")},
    {{"baseColor", v4(1.0f, 1.0f, 1.0f, 1.0f)}, {"alpha", f(1.0f)}, {"alphaClipThreshold", f(0.5f)}},
    {"positionOS", "baseColor", "alpha"},
    5,
    32,
    4,
    true,
    true,
};

} // namespace

const TargetSpec* findTarget(std::string_view id) {
    if (id == "lit") {
        return &kLit;
    }
    if (id == "unlit") {
        return &kUnlit;
    }
    if (id == "preview") {
        return &kPreview;
    }
    return nullptr;
}

} // namespace sg::detail
