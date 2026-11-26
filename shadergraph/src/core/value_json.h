#pragma once

#include "sg/types.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <optional>
#include <string>

namespace sg {
namespace detail {

using Json = nlohmann::ordered_json;

inline std::optional<Value> parseValue(const Json& json, std::optional<ValueType> declared,
                                       std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<Value> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };

    if (json.is_null()) {
        return std::nullopt;
    }

    if (declared) {
        switch (*declared) {
        case ValueType::Boolean:
            if (!json.is_boolean()) {
                return fail("expected boolean");
            }
            return Value::makeBool(json.get<bool>());
        case ValueType::Integer:
            if (json.is_number_integer()) {
                return Value::makeInteger(json.get<std::int64_t>());
            }
            if (json.is_number_float()) {
                const double value = json.get<double>();
                if (std::floor(value) != value) {
                    return fail("expected integer");
                }
                return Value::makeInteger(static_cast<std::int64_t>(value));
            }
            return fail("expected integer");
        case ValueType::Float:
            if (!json.is_number()) {
                return fail("expected number");
            }
            return Value::makeFloat(json.get<float>());
        case ValueType::Vector2:
            if (!json.is_array() || json.size() != 2) {
                return fail("expected vector2 array");
            }
            return Value::makeVector2({json[0].get<float>(), json[1].get<float>()});
        case ValueType::Vector3:
            if (!json.is_array() || json.size() != 3) {
                return fail("expected vector3 array");
            }
            return Value::makeVector3(
                {json[0].get<float>(), json[1].get<float>(), json[2].get<float>()});
        case ValueType::Vector4:
            if (!json.is_array() || json.size() != 4) {
                return fail("expected vector4 array");
            }
            return Value::makeVector4({json[0].get<float>(), json[1].get<float>(),
                                       json[2].get<float>(), json[3].get<float>()});
        case ValueType::Color:
            if (!json.is_array() || json.size() != 4) {
                return fail("expected color array");
            }
            return Value::makeColor({json[0].get<float>(), json[1].get<float>(),
                                     json[2].get<float>(), json[3].get<float>()});
        case ValueType::Matrix2:
            if (!json.is_array() || json.size() != 4) {
                return fail("expected matrix2 array");
            }
            {
                Mat2 matrix;
                for (std::size_t i = 0; i < 4; ++i) {
                    matrix.m[i] = json[i].get<float>();
                }
                return Value::makeMatrix2(matrix);
            }
        case ValueType::Matrix3:
            if (!json.is_array() || json.size() != 9) {
                return fail("expected matrix3 array");
            }
            {
                Mat3 matrix;
                for (std::size_t i = 0; i < 9; ++i) {
                    matrix.m[i] = json[i].get<float>();
                }
                return Value::makeMatrix3(matrix);
            }
        case ValueType::Matrix4:
            if (!json.is_array() || json.size() != 16) {
                return fail("expected matrix4 array");
            }
            {
                Mat4 matrix;
                for (std::size_t i = 0; i < 16; ++i) {
                    matrix.m[i] = json[i].get<float>();
                }
                return Value::makeMatrix4(matrix);
            }
        case ValueType::Gradient: {
            if (!json.is_object() || !json.contains("keys") || !json["keys"].is_array()) {
                return fail("expected gradient object");
            }
            Gradient gradient;
            for (const Json& key : json["keys"]) {
                if (!key.is_object() || !key.contains("t") || !key.contains("color") ||
                    !key["color"].is_array() || key["color"].size() != 4) {
                    return fail("invalid gradient key");
                }
                gradient.keys.push_back(
                    {key["t"].get<float>(),
                     {key["color"][0].get<float>(), key["color"][1].get<float>(),
                      key["color"][2].get<float>(), key["color"][3].get<float>()}});
            }
            return Value::makeGradient(std::move(gradient));
        }
        case ValueType::Texture2D:
        case ValueType::Texture2DArray:
        case ValueType::Texture3D:
        case ValueType::Cubemap:
        case ValueType::VirtualTexture:
        case ValueType::SamplerState:
        case ValueType::String:
            if (!json.is_string()) {
                return fail("expected string");
            }
            return Value::makeString(*declared, json.get<std::string>());
        case ValueType::DynamicMatrix:
            if (json.is_array()) {
                if (json.size() == 4) {
                    Mat2 matrix;
                    for (std::size_t i = 0; i < 4; ++i) {
                        matrix.m[i] = json[i].get<float>();
                    }
                    return Value::makeMatrix2(matrix);
                }
                if (json.size() == 9) {
                    Mat3 matrix;
                    for (std::size_t i = 0; i < 9; ++i) {
                        matrix.m[i] = json[i].get<float>();
                    }
                    return Value::makeMatrix3(matrix);
                }
                if (json.size() == 16) {
                    Mat4 matrix;
                    for (std::size_t i = 0; i < 16; ++i) {
                        matrix.m[i] = json[i].get<float>();
                    }
                    return Value::makeMatrix4(matrix);
                }
            }
            break;
        case ValueType::Dynamic:
        case ValueType::DynamicVector:
        case ValueType::External:
        case ValueType::PropertyConnectionState:
            break;
        }
    }

    if (json.is_object() && json.contains("keys")) {
        return parseValue(json, ValueType::Gradient, error);
    }
    if (json.is_boolean()) {
        return Value::makeBool(json.get<bool>());
    }
    if (json.is_number_integer()) {
        return Value::makeInteger(json.get<std::int64_t>());
    }
    if (json.is_number_float()) {
        return Value::makeFloat(json.get<float>());
    }
    if (json.is_string()) {
        return Value::makeString(ValueType::String, json.get<std::string>());
    }
    if (json.is_array()) {
        if (json.size() == 2) {
            return Value::makeVector2({json[0].get<float>(), json[1].get<float>()});
        }
        if (json.size() == 3) {
            return Value::makeVector3(
                {json[0].get<float>(), json[1].get<float>(), json[2].get<float>()});
        }
        if (json.size() == 4) {
            return Value::makeVector4({json[0].get<float>(), json[1].get<float>(),
                                       json[2].get<float>(), json[3].get<float>()});
        }
    }
    return fail("unsupported value");
}

inline Json encodeValue(const Value& value) {
    switch (value.type) {
    case ValueType::Boolean: {
        const auto* data = value.asBool();
        return data == nullptr ? Json(nullptr) : Json(*data);
    }
    case ValueType::Integer: {
        const auto* data = value.asInteger();
        return data == nullptr ? Json(nullptr) : Json(*data);
    }
    case ValueType::Float: {
        const auto* data = value.asFloat();
        return data == nullptr ? Json(nullptr) : Json(*data);
    }
    case ValueType::Vector2: {
        const auto* data = value.asVector2();
        return data == nullptr ? Json(nullptr) : Json::array({data->x, data->y});
    }
    case ValueType::Vector3: {
        const auto* data = value.asVector3();
        return data == nullptr ? Json(nullptr) : Json::array({data->x, data->y, data->z});
    }
    case ValueType::Vector4:
    case ValueType::Color: {
        const auto* data = value.asVector4();
        return data == nullptr ? Json(nullptr) : Json::array({data->x, data->y, data->z, data->w});
    }
    case ValueType::Matrix2: {
        const auto* data = value.asMatrix2();
        if (data == nullptr) {
            return Json(nullptr);
        }
        Json result = Json::array();
        for (float item : data->m) {
            result.push_back(item);
        }
        return result;
    }
    case ValueType::Matrix3: {
        const auto* data = value.asMatrix3();
        if (data == nullptr) {
            return Json(nullptr);
        }
        Json result = Json::array();
        for (float item : data->m) {
            result.push_back(item);
        }
        return result;
    }
    case ValueType::Matrix4: {
        const auto* data = value.asMatrix4();
        if (data == nullptr) {
            return Json(nullptr);
        }
        Json result = Json::array();
        for (float item : data->m) {
            result.push_back(item);
        }
        return result;
    }
    case ValueType::Gradient: {
        const auto* data = value.asGradient();
        if (data == nullptr) {
            return Json(nullptr);
        }
        Json result;
        result["keys"] = Json::array();
        for (const GradientKey& key : data->keys) {
            result["keys"].push_back({{"t", key.time},
                                      {"color", Json::array({key.color.x, key.color.y, key.color.z,
                                                             key.color.w})}});
        }
        return result;
    }
    case ValueType::Texture2D:
    case ValueType::Texture2DArray:
    case ValueType::Texture3D:
    case ValueType::Cubemap:
    case ValueType::VirtualTexture:
    case ValueType::SamplerState:
    case ValueType::String: {
        const auto* data = value.asString();
        return data == nullptr ? Json(nullptr) : Json(*data);
    }
    default:
        return Json(nullptr);
    }
}

} // namespace detail
} // namespace sg
