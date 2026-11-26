#include "test_helpers.h"

#include "sg/concretize.h"
#include "sg/validate.h"

#include <algorithm>
#include <cmath>

using namespace sg;

namespace {

ConcreteValueType dynamicResult(int left, int right) {
    const int widths[] = {1, 2, 3, 4, 1};
    const ConcreteValueType types[] = {ConcreteValueType::Float, ConcreteValueType::Vector2,
                                       ConcreteValueType::Vector3, ConcreteValueType::Vector4,
                                       ConcreteValueType::Float};
    if (left < 0 && right < 0) {
        return ConcreteValueType::Float;
    }
    if (left < 0) {
        return types[right];
    }
    if (right < 0) {
        return types[left];
    }
    if (widths[left] == 1 && widths[right] == 1) {
        return ConcreteValueType::Float;
    }
    if (widths[left] == 1) {
        return types[right];
    }
    if (widths[right] == 1) {
        return types[left];
    }
    const int width = std::min(widths[left], widths[right]);
    return width == 2 ? ConcreteValueType::Vector2
                      : width == 3 ? ConcreteValueType::Vector3 : ConcreteValueType::Vector4;
}

ConcreteValueType connectDynamic(sgt::NodeRegistry& registry, int left, int right) {
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"Static", 1}, {"Dynamic", 2}});
    if (left >= 0) {
        REQUIRE(graph.addEdge({1, left, 2, 0}));
    }
    if (right >= 0) {
        REQUIRE(graph.addEdge({1, right, 2, 1}));
    }
    const sg::ConcretizeResult result = sg::concretize(graph, registry);
    REQUIRE(result.ok);
    CHECK(result.activeNodes.size() == 2);
    return graph.findSlot(2, 2)->concreteType;
}

ConcreteValueType connectMatrix(sgt::NodeRegistry& registry, int left, int right) {
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"Static", 1}, {"DynamicMatrix", 2}});
    if (left >= 0) {
        REQUIRE(graph.addEdge({1, left, 2, 0}));
    }
    if (right >= 0) {
        REQUIRE(graph.addEdge({1, right, 2, 1}));
    }
    const sg::ConcretizeResult result = sg::concretize(graph, registry);
    REQUIRE(result.ok);
    CHECK(result.activeNodes.size() == 2);
    return graph.findSlot(2, 2)->concreteType;
}

} // namespace

TEST_CASE("types cover values and compatibility") {
    CHECK(sg::Value::makeVector3({1.0f, 2.0f, 3.0f}) ==
          sg::Value::makeVector3({1.0f, 2.0f, 3.0f}));
    CHECK(sg::approxEqual(sg::Value::makeFloat(1.0f), sg::Value::makeFloat(1.0000001f)));
    CHECK(sg::Mat4::identity().m[0] == 1.0f);
    CHECK(sg::isCompatible(sg::ValueType::Vector4, sg::ConcreteValueType::Float));
    CHECK(sg::isCompatible(sg::ValueType::Boolean, sg::ConcreteValueType::Vector4));
    CHECK_FALSE(sg::isCompatible(sg::ValueType::Texture2D, sg::ConcreteValueType::Vector4));
    CHECK(sg::isCompatible(sg::ValueType::DynamicVector, sg::ConcreteValueType::Vector3));
    CHECK(sg::isCompatible(sg::ValueType::DynamicMatrix, sg::ConcreteValueType::Matrix4));
    CHECK(sg::valueTypeFromString("vector3").has_value());
    CHECK(static_cast<int>(*sg::valueTypeFromString("vector3")) == static_cast<int>(sg::ValueType::Vector3));
    CHECK(sg::concreteTypeFromString("matrix4").has_value());
    CHECK(static_cast<int>(*sg::concreteTypeFromString("matrix4")) == static_cast<int>(sg::ConcreteValueType::Matrix4));
    CHECK(sg::isIdentifier("_BaseColor"));
    CHECK_FALSE(sg::isIdentifier("1BaseColor"));
}

TEST_CASE("dynamic vector slots match Unity width rules") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    for (int left = -1; left < 5; ++left) {
        for (int right = -1; right < 5; ++right) {
            CHECK(static_cast<int>(connectDynamic(registry, left, right)) == static_cast<int>(dynamicResult(left, right)));
        }
    }
}

TEST_CASE("dynamic matrix slots select largest dimension") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    CHECK(static_cast<int>(connectMatrix(registry, -1, -1)) == static_cast<int>(sg::ConcreteValueType::Matrix2));
    CHECK(static_cast<int>(connectMatrix(registry, 6, 6)) == static_cast<int>(sg::ConcreteValueType::Matrix2));
    CHECK(static_cast<int>(connectMatrix(registry, 6, 7)) == static_cast<int>(sg::ConcreteValueType::Matrix3));
    CHECK(static_cast<int>(connectMatrix(registry, 6, 8)) == static_cast<int>(sg::ConcreteValueType::Matrix4));
    CHECK(static_cast<int>(connectMatrix(registry, 7, 8)) == static_cast<int>(sg::ConcreteValueType::Matrix4));
}

TEST_CASE("fixed slot connections are validated") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"Static", 1}, {"FloatIn", 2}, {"TextureToFloat", 3}});
    REQUIRE(graph.addEdge({1, 3, 2, 0}));
    sg::ValidationResult result = sg::validate(graph, registry);
    CHECK(result.ok);

    sgt::Graph bad = sgt::makeGraph();
    sgt::addNodes(bad, registry, {{"TextureToFloat", 1}, {"FloatIn", 2}});
    REQUIRE(bad.addEdge({1, 0, 2, 0}));
    result = sg::validate(bad, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorTypeMismatch));
}

TEST_CASE("precision follows inherit graph and forced rules") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    graph.defaultPrecision = sg::ConcretePrecision::Half;
    sg::NodeInstance source = registry.instantiate("Constant", 1);
    source.precision = sg::Precision::Half;
    sg::NodeInstance consumer = registry.instantiate("FloatIn", 2);
    REQUIRE(graph.addNode(std::move(source)));
    REQUIRE(graph.addNode(std::move(consumer)));
    REQUIRE(graph.addEdge({1, 0, 2, 0}));
    REQUIRE(sg::concretize(graph, registry).ok);
    CHECK(static_cast<int>(graph.findNode(1)->graphPrecision) == static_cast<int>(sg::GraphPrecision::Half));
    CHECK(static_cast<int>(graph.findNode(2)->graphPrecision) == static_cast<int>(sg::GraphPrecision::Half));
    CHECK(static_cast<int>(graph.findNode(2)->concretePrecision) == static_cast<int>(sg::ConcretePrecision::Half));

    sgt::Graph noInput = sgt::makeGraph();
    noInput.defaultPrecision = sg::ConcretePrecision::Half;
    sg::NodeInstance lone = registry.instantiate("RequiredIn", 1);
    REQUIRE(noInput.addNode(std::move(lone)));
    REQUIRE(sg::concretize(noInput, registry).ok);
    CHECK(static_cast<int>(noInput.findNode(1)->graphPrecision) == static_cast<int>(sg::GraphPrecision::Graph));
    CHECK(static_cast<int>(noInput.findNode(1)->concretePrecision) == static_cast<int>(sg::ConcretePrecision::Half));

    sgt::Graph forced = sgt::makeGraph();
    forced.defaultPrecision = sg::ConcretePrecision::Half;
    sg::NodeInstance single = registry.instantiate("Constant", 1);
    single.precision = sg::Precision::Single;
    sg::NodeInstance inherited = registry.instantiate("FloatIn", 2);
    REQUIRE(forced.addNode(std::move(single)));
    REQUIRE(forced.addNode(std::move(inherited)));
    REQUIRE(forced.addEdge({1, 0, 2, 0}));
    REQUIRE(sg::concretize(forced, registry).ok);
    CHECK(static_cast<int>(forced.findNode(2)->graphPrecision) == static_cast<int>(sg::GraphPrecision::Single));
    CHECK(static_cast<int>(forced.findNode(2)->concretePrecision) == static_cast<int>(sg::ConcretePrecision::Single));
}

TEST_CASE("literal values and required inputs are validated") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"FloatIn", 1}});
    REQUIRE(graph.setSlotValue(1, 0, sg::SlotMode::Literal, sg::Value::makeFloat(2.0f)));
    sg::ValidationResult result = sg::validate(graph, registry);
    CHECK(result.ok);

    sgt::Graph wrong = sgt::makeGraph();
    sgt::addNodes(wrong, registry, {{"FloatIn", 1}});
    REQUIRE(wrong.setSlotValue(1, 0, sg::SlotMode::Literal,
                               sg::Value::makeMatrix4(sg::Mat4::identity())));
    result = sg::validate(wrong, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorTypeMismatch));

    sgt::NodeDef required =
        sgt::definition("Required", {sgt::slot(0, "In", sg::SlotDirection::Input, sg::ValueType::Float)});
    required.slots[0].required = true;
    CHECK(registry.add(required));
    sgt::Graph missing = sgt::makeGraph();
    sgt::addNodes(missing, registry, {{"Required", 1}});
    result = sg::validate(missing, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorMissingInput));
}

TEST_CASE("literal source requirements are checked on edges") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sg::NodeDef literalIn =
        sgt::definition("LiteralInput", {sgt::slot(0, "In", sg::SlotDirection::Input, sg::ValueType::Float)});
    literalIn.slots[0].requiresLiteral = true;
    CHECK(registry.add(literalIn));
    sg::NodeDef literalSource =
        sgt::definition("LiteralSource", {sgt::slot(0, "Out", sg::SlotDirection::Output, sg::ValueType::Float)});
    literalSource.slots[0].supportsLiteral = true;
    CHECK(registry.add(literalSource));
    sg::NodeDef runtimeSource =
        sgt::definition("RuntimeSource", {sgt::slot(0, "Out", sg::SlotDirection::Output, sg::ValueType::Float)});
    CHECK(registry.add(runtimeSource));

    sgt::Graph good = sgt::makeGraph();
    sgt::addNodes(good, registry, {{"LiteralSource", 1}, {"LiteralInput", 2}});
    REQUIRE(good.addEdge({1, 0, 2, 0}));
    CHECK(sg::validate(good, registry).ok);

    sgt::Graph bad = sgt::makeGraph();
    sgt::addNodes(bad, registry, {{"RuntimeSource", 1}, {"LiteralInput", 2}});
    REQUIRE(bad.addEdge({1, 0, 2, 0}));
    const sg::ValidationResult result = sg::validate(bad, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorTypeMismatch));
}

TEST_CASE("graph model enforces ids and cleans edges") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"Constant", 1}, {"FloatIn", 2}});
    CHECK_FALSE(graph.addNode(registry.instantiate("Constant", 1)));
    CHECK_FALSE(graph.addEdge({1, 0, 1, 0}));
    REQUIRE(graph.addEdge({1, 0, 2, 0}));
    CHECK(graph.incomingEdge(2, 0) != nullptr);
    REQUIRE(graph.removeNode(1));
    CHECK(graph.incomingEdge(2, 0) == nullptr);
    CHECK(graph.nextNodeId() == 3);

    sg::Property property;
    property.name = "Color";
    property.refName = "_Color";
    property.type = sg::ValueType::Vector4;
    REQUIRE(graph.addProperty(property));
    CHECK_FALSE(graph.addProperty(property));

    sgt::Graph duplicate = graph;
    duplicate.properties.push_back(property);
    const sg::ValidationResult result = sg::validate(duplicate, registry);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorNameConflict));
}
