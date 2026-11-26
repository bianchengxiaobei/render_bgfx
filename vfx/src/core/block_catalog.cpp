#include "block_catalog.h"

#include "vfx/graph.h"

#include <algorithm>

namespace vfx::detail {
namespace {

constexpr FieldSpec kSetPositionSphere[] = {
    {"center", true},
    {"radius", true},
    {"surface", false, "false"},
};
constexpr FieldSpec kSetPositionBox[] = {
    {"center", true},
    {"size", true},
};
constexpr FieldSpec kSetVelocityRandom[] = {
    {"minSpeed", true},
    {"maxSpeed", true},
    {"direction", false, "[0.0, 1.0, 0.0]"},
    {"coneAngle", false, "0.0"},
};
constexpr FieldSpec kSetVelocityDirection[] = {
    {"direction", true},
    {"speed", true},
};
constexpr FieldSpec kSetColor[] = {
    {"color", true},
};
constexpr FieldSpec kSetAlpha[] = {
    {"alpha", true},
};
constexpr FieldSpec kSetSize[] = {
    {"size", true},
};
constexpr FieldSpec kSetLifetime[] = {
    {"lifetime", true},
};
constexpr FieldSpec kSetAngle[] = {
    {"angle", true},
};
constexpr FieldSpec kSetScale[] = {
    {"scale", true},
};
constexpr FieldSpec kSetAttribute[] = {
    {"attribute", true},
    {"value", true},
};
constexpr FieldSpec kSetRandom[] = {
    {"attribute", true},
    {"min", true},
    {"max", true},
};
constexpr FieldSpec kGravity[] = {
    {"gravity", true},
};
constexpr FieldSpec kDrag[] = {
    {"drag", false, "0.5"},
    {"useSize", false, "false"},
};
constexpr FieldSpec kForce[] = {
    {"force", true},
    {"mode", false, "\"absolute\""},
    {"drag", false, "1.0"},
    {"space", false, "\"world\""},
};
constexpr FieldSpec kAttractor[] = {
    {"position", true},
    {"strength", true},
    {"falloff", false, "1.0"},
};
constexpr FieldSpec kTurbulence[] = {
    {"intensity", false, "1.0"},     {"frequency", false, "1.0"},  {"octaves", false, "3"},
    {"roughness", false, "0.5"},     {"lacunarity", false, "2.0"}, {"scroll", false, "[0.0, 0.0, 0.0]"},
    {"mode", false, "\"relative\""}, {"drag", false, "1.0"},
};
constexpr FieldSpec kEmpty[] = {{"", false}};
constexpr FieldSpec kReap[] = {
    {"condition", false},
};
constexpr FieldSpec kCollisionPlane[] = {
    {"normal", true},           {"distance", true}, {"mode", false, "\"bounce\""}, {"restitution", false, "0.5"},
    {"friction", false, "0.2"},
};
constexpr FieldSpec kCollisionSphere[] = {
    {"center", true},           {"radius", true}, {"mode", false, "\"bounce\""}, {"restitution", false, "0.5"},
    {"friction", false, "0.2"},
};
constexpr FieldSpec kOverLife[] = {
    {"gradient", false},
    {"curve", false},
};
constexpr FieldSpec kCustomHlsl[] = {
    {"code", true},
    {"reads", false, "[]"},
    {"writes", false, "[]"},
};
constexpr FieldSpec kTriggerEvent[] = {
    {"event", true},
    {"count", false, "1.0"},
    {"condition", false, "true"},
};
constexpr FieldSpec kSubgraphInvocation[] = {
    {"name", true},
    {"args", false},
    {"output", false},
};

constexpr auto kSetPositionSphereDefaults = R"({"type":"set_position_sphere","center":[0.0,0.0,0.0],"radius":1.0})";
constexpr auto kSetPositionBoxDefaults = R"({"type":"set_position_box","center":[0.0,0.0,0.0],"size":[1.0,1.0,1.0]})";
constexpr auto kSetVelocityRandomDefaults = R"({"type":"set_velocity_random","minSpeed":1.0,"maxSpeed":2.0})";
constexpr auto kSetVelocityDirectionDefaults =
    R"({"type":"set_velocity_direction","direction":[0.0,1.0,0.0],"speed":1.0})";
constexpr auto kSetColorDefaults = R"({"type":"set_color","color":[1.0,1.0,1.0,1.0]})";
constexpr auto kSetAlphaDefaults = R"({"type":"set_alpha","alpha":1.0})";
constexpr auto kSetSizeDefaults = R"({"type":"set_size","size":0.1})";
constexpr auto kSetLifetimeDefaults = R"({"type":"set_lifetime","lifetime":1.0})";
constexpr auto kSetAngleDefaults = R"({"type":"set_angle","angle":0.0})";
constexpr auto kSetScaleDefaults = R"({"type":"set_scale","scale":[1.0,1.0,1.0]})";
constexpr auto kSetAttributeDefaults = R"({"type":"set_attribute","attribute":"size","value":0.1})";
constexpr auto kSetRandomDefaults = R"({"type":"set_random","attribute":"size","min":0.1,"max":0.2})";
constexpr auto kCustomHlslDefaults = R"({"type":"custom_hlsl","code":"// TODO: custom HLSL\n"})";
constexpr auto kGravityDefaults = R"({"type":"gravity","gravity":[0.0,-9.81,0.0]})";
constexpr auto kDragDefaults = R"({"type":"drag","drag":0.5,"useSize":false})";
constexpr auto kForceDefaults = R"({"type":"force","force":[0.0,0.0,0.0],"mode":"absolute","drag":1.0})";
constexpr auto kAttractorDefaults = R"({"type":"attractor","position":[0.0,0.0,0.0],"strength":1.0,"falloff":1.0})";
constexpr auto kTurbulenceDefaults =
    R"({"type":"turbulence","intensity":1.0,"frequency":1.0,"octaves":3,"roughness":0.5,"lacunarity":2.0,"scroll":[0.0,0.0,0.0],"mode":"relative","drag":1.0})";
constexpr auto kIntegrateDefaults = R"({"type":"integrate"})";
constexpr auto kAgeDefaults = R"({"type":"age"})";
constexpr auto kReapDefaults = R"({"type":"reap"})";
constexpr auto kCollisionPlaneDefaults =
    R"({"type":"collision_plane","normal":[0.0,1.0,0.0],"distance":0.0,"mode":"bounce","restitution":0.5,"friction":0.2})";
constexpr auto kCollisionSphereDefaults =
    R"({"type":"collision_sphere","center":[0.0,0.0,0.0],"radius":1.0,"mode":"bounce","restitution":0.5,"friction":0.2})";

constexpr BlockSpec kInitBlockSpecs[] = {
    {"set_position_sphere", std::span<const FieldSpec>(kSetPositionSphere), kSetPositionSphereDefaults},
    {"set_position_box", std::span<const FieldSpec>(kSetPositionBox), kSetPositionBoxDefaults},
    {"set_velocity_random", std::span<const FieldSpec>(kSetVelocityRandom), kSetVelocityRandomDefaults},
    {"set_velocity_direction", std::span<const FieldSpec>(kSetVelocityDirection), kSetVelocityDirectionDefaults},
    {"set_color", std::span<const FieldSpec>(kSetColor), kSetColorDefaults},
    {"set_alpha", std::span<const FieldSpec>(kSetAlpha), kSetAlphaDefaults},
    {"set_size", std::span<const FieldSpec>(kSetSize), kSetSizeDefaults},
    {"set_lifetime", std::span<const FieldSpec>(kSetLifetime), kSetLifetimeDefaults},
    {"set_angle", std::span<const FieldSpec>(kSetAngle), kSetAngleDefaults},
    {"set_scale", std::span<const FieldSpec>(kSetScale), kSetScaleDefaults},
    {"set_attribute", std::span<const FieldSpec>(kSetAttribute), kSetAttributeDefaults},
    {"set_random", std::span<const FieldSpec>(kSetRandom), kSetRandomDefaults},
    {"custom_hlsl", std::span<const FieldSpec>(kCustomHlsl), kCustomHlslDefaults},
    {"subgraph", std::span<const FieldSpec>(kSubgraphInvocation), {}},
};

constexpr BlockSpec kUpdateBlockSpecs[] = {
    {"gravity", std::span<const FieldSpec>(kGravity), kGravityDefaults},
    {"drag", std::span<const FieldSpec>(kDrag), kDragDefaults},
    {"force", std::span<const FieldSpec>(kForce), kForceDefaults},
    {"attractor", std::span<const FieldSpec>(kAttractor), kAttractorDefaults},
    {"turbulence", std::span<const FieldSpec>(kTurbulence), kTurbulenceDefaults},
    {"integrate", std::span<const FieldSpec>(kEmpty), kIntegrateDefaults},
    {"age", std::span<const FieldSpec>(kEmpty), kAgeDefaults},
    {"reap", std::span<const FieldSpec>(kReap), kReapDefaults},
    {"collision_plane", std::span<const FieldSpec>(kCollisionPlane), kCollisionPlaneDefaults},
    {"collision_sphere", std::span<const FieldSpec>(kCollisionSphere), kCollisionSphereDefaults},
    {"color_over_life", std::span<const FieldSpec>(kOverLife), {}},
    {"alpha_over_life", std::span<const FieldSpec>(kOverLife), {}},
    {"size_over_life", std::span<const FieldSpec>(kOverLife), {}},
    {"custom_hlsl", std::span<const FieldSpec>(kCustomHlsl), kCustomHlslDefaults},
    {"trigger_event", std::span<const FieldSpec>(kTriggerEvent), {}},
    {"subgraph", std::span<const FieldSpec>(kSubgraphInvocation), {}},
};

const Property* findProperty(const Graph& graph, PropertyType type) {
    const auto found = std::find_if(graph.properties.begin(), graph.properties.end(),
                                    [type](const Property& property) { return property.type == type; });
    return found == graph.properties.end() ? nullptr : &*found;
}

} // namespace

std::span<const BlockSpec> initBlockSpecs() noexcept {
    return kInitBlockSpecs;
}

std::span<const BlockSpec> updateBlockSpecs() noexcept {
    return kUpdateBlockSpecs;
}

const BlockSpec* findBlockSpec(std::string_view type, bool update) noexcept {
    const auto specs = update ? updateBlockSpecs() : initBlockSpecs();
    const auto found =
        std::find_if(specs.begin(), specs.end(), [type](const BlockSpec& spec) { return spec.type == type; });
    return found == specs.end() ? nullptr : &*found;
}

std::string defaultBlockJson(std::string_view type, bool update, const Graph& graph, std::uint32_t system,
                             std::string& error) {
    error.clear();
    const BlockSpec* spec = findBlockSpec(type, update);
    if (spec == nullptr) {
        error = "unknown " + std::string(update ? "update" : "init") + " block type '" + std::string(type) + "'";
        return {};
    }
    if (system >= graph.systems.size()) {
        error = "system index is out of range";
        return {};
    }
    if (type == "subgraph") {
        error = "subgraph insertion requires a named subgraph; add the invocation in JSON for now";
        return {};
    }
    if (type == "trigger_event") {
        const SystemDesc& desc = graph.systems[system];
        if (desc.events.emit.empty()) {
            error = "trigger_event requires an emit event on the system";
            return {};
        }
        return R"({"type":"trigger_event","event":")" + desc.events.emit.front().name + R"(","count":1.0})";
    }
    if (type == "color_over_life") {
        const Property* property = findProperty(graph, PropertyType::Gradient);
        if (property == nullptr) {
            error = "color_over_life requires a gradient property";
            return {};
        }
        return R"({"type":"color_over_life","gradient":")" + property->name + R"("})";
    }
    if (type == "alpha_over_life" || type == "size_over_life") {
        const Property* property = findProperty(graph, PropertyType::Curve);
        if (property == nullptr) {
            error = std::string(type) + " requires a curve property";
            return {};
        }
        return R"({"type":")" + std::string(type) + R"(","curve":")" + property->name + R"("})";
    }
    if (spec->defaults.empty()) {
        error = "block type '" + std::string(type) + "' has no editor default";
        return {};
    }
    return std::string(spec->defaults);
}

} // namespace vfx::detail
