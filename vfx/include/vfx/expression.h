#pragma once

#include "vfx/graph.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

enum class ExprType : std::uint8_t {
    Invalid,
    Float,
    Float2,
    Float3,
    Float4,
    UInt,
    Int,
    Bool,
};

enum class ExpressionOp : std::uint8_t {
    Literal,
    Attribute,
    Property,
    Time,
    DeltaTime,
    LifetimeRatio,
    Add,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
    Clamp,
    Lerp,
    Remap,
    Sin,
    Cos,
    Abs,
    Saturate,
    Random,
    Noise,
    SampleCurve,
    SampleGradient,
    SampleTexture,
    Select,
    Swizzle,
    Cast,
    Splat,
};

struct ExpressionValue {
    ExprType type = ExprType::Invalid;
    std::array<double, 4> components{};
};

struct ExpressionNode {
    ExpressionOp op = ExpressionOp::Literal;
    ExprType type = ExprType::Invalid;
    bool constant = false;
    ExpressionValue value;
    std::vector<std::shared_ptr<const ExpressionNode>> inputs;
    std::string name;
    std::string mask;
    std::string hlsl;
};

struct ExpressionContext {
    explicit ExpressionContext(const Graph& graph) noexcept : m_graph(&graph) {}

    const Property* findProperty(std::string_view name) const noexcept;
    const AttributeDesc* findAttribute(std::string_view name) const noexcept;
    const Graph& graph() const noexcept { return *m_graph; }

private:
    const Graph* m_graph;
};

struct ExpressionError {
    std::string path;
    std::string message;
};

struct CompiledExpression {
    std::shared_ptr<const ExpressionNode> root;
    std::vector<ExpressionError> errors;

    bool ok() const noexcept { return root != nullptr && errors.empty(); }
};

const char* toString(ExprType type) noexcept;
const char* toString(ExpressionOp op) noexcept;
std::uint8_t expressionComponentCount(ExprType type) noexcept;
bool isFloatExpressionType(ExprType type) noexcept;
bool isNumericExpressionType(ExprType type) noexcept;
ExprType expressionScalarType(ExprType type) noexcept;
ExprType expressionVectorType(ExprType scalar, std::uint8_t components) noexcept;

bool isExpressionDagAcyclic(const ExpressionNode& root);

CompiledExpression compileExpression(const Value& value, const ExpressionContext& context,
                                     std::string_view path,
                                     ExprType expected = ExprType::Invalid);

} // namespace vfx
