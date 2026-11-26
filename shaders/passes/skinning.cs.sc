#include "bgfx_compute.sh"

uniform vec4 u_skin_params;

BUFFER_RAW_RO(s_skinBaseVertices, 0);
BUFFER_RAW_RO(s_skinVertexInfo, 1);
BUFFER_RAW_RO(s_skinInfluences, 2);
BUFFER_RAW_RO(s_skinJointMatrices, 3);
BUFFER_RAW_WO(s_skinOutputVertices, 4);

#define HBG_SKIN_VERTEX_FLOATS 21u
#define HBG_SKIN_INFLUENCE_WORDS 2u
#define HBG_SKIN_MATRIX_FLOATS 16u

mat4 hbgLoadSkinMatrix(uint base)
{
    return mtxFromCols(
        vec4(rawLoadFloat(s_skinJointMatrices, base + 0u), rawLoadFloat(s_skinJointMatrices, base + 1u),
             rawLoadFloat(s_skinJointMatrices, base + 2u), rawLoadFloat(s_skinJointMatrices, base + 3u)),
        vec4(rawLoadFloat(s_skinJointMatrices, base + 4u), rawLoadFloat(s_skinJointMatrices, base + 5u),
             rawLoadFloat(s_skinJointMatrices, base + 6u), rawLoadFloat(s_skinJointMatrices, base + 7u)),
        vec4(rawLoadFloat(s_skinJointMatrices, base + 8u), rawLoadFloat(s_skinJointMatrices, base + 9u),
             rawLoadFloat(s_skinJointMatrices, base + 10u), rawLoadFloat(s_skinJointMatrices, base + 11u)),
        vec4(rawLoadFloat(s_skinJointMatrices, base + 12u), rawLoadFloat(s_skinJointMatrices, base + 13u),
             rawLoadFloat(s_skinJointMatrices, base + 14u), rawLoadFloat(s_skinJointMatrices, base + 15u)));
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint vertex = gl_GlobalInvocationID.x;
    const uint vertexCount = uint(max(u_skin_params.x, 0.0));
    const uint jointCount = uint(max(u_skin_params.y, 0.0));
    if (vertex >= vertexCount)
    {
        return;
    }

    const uint base = vertex * HBG_SKIN_VERTEX_FLOATS;
    const vec3 sourcePosition =
        vec3(rawLoadFloat(s_skinBaseVertices, base + 0u), rawLoadFloat(s_skinBaseVertices, base + 1u),
             rawLoadFloat(s_skinBaseVertices, base + 2u));
    const vec3 sourceNormal =
        vec3(rawLoadFloat(s_skinBaseVertices, base + 3u), rawLoadFloat(s_skinBaseVertices, base + 4u),
             rawLoadFloat(s_skinBaseVertices, base + 5u));
    const vec4 sourceTangent =
        vec4(rawLoadFloat(s_skinBaseVertices, base + 6u), rawLoadFloat(s_skinBaseVertices, base + 7u),
             rawLoadFloat(s_skinBaseVertices, base + 8u), rawLoadFloat(s_skinBaseVertices, base + 9u));

    const uint infoBase = vertex * 2u;
    const uint influenceOffset = rawLoadUint(s_skinVertexInfo, infoBase + 0u);
    const uint influenceCount = rawLoadUint(s_skinVertexInfo, infoBase + 1u);

    vec3 position = vec3(0.0, 0.0, 0.0);
    vec3 normal = vec3(0.0, 0.0, 0.0);
    vec3 tangent = vec3(0.0, 0.0, 0.0);
    vec3 previousPosition = vec3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    for (uint influence = 0u; influence < influenceCount; ++influence)
    {
        const uint word = (influenceOffset + influence) * HBG_SKIN_INFLUENCE_WORDS;
        const uint joint = rawLoadUint(s_skinInfluences, word + 0u) & 0xffffu;
        const float weight = rawLoadFloat(s_skinInfluences, word + 1u);
        if (abs(weight) <= 1.0e-8 || joint >= jointCount)
        {
            continue;
        }

        const mat4 jointMatrix = hbgLoadSkinMatrix(joint * HBG_SKIN_MATRIX_FLOATS);
        const mat4 previousJointMatrix =
            hbgLoadSkinMatrix((jointCount + joint) * HBG_SKIN_MATRIX_FLOATS);
        position += mul(jointMatrix, vec4(sourcePosition, 1.0)).xyz * weight;
        normal += mul(jointMatrix, vec4(sourceNormal, 0.0)).xyz * weight;
        tangent += mul(jointMatrix, vec4(sourceTangent.xyz, 0.0)).xyz * weight;
        previousPosition += mul(previousJointMatrix, vec4(sourcePosition, 1.0)).xyz * weight;
        totalWeight += weight;
    }

    vec3 outputPosition = sourcePosition;
    vec3 outputNormal = sourceNormal;
    vec4 outputTangent = sourceTangent;
    vec3 outputPreviousPosition = vec3(rawLoadFloat(s_skinBaseVertices, base + 18u),
                                       rawLoadFloat(s_skinBaseVertices, base + 19u),
                                       rawLoadFloat(s_skinBaseVertices, base + 20u));

    if (totalWeight > 1.0e-8)
    {
        const float inverseWeight = abs(totalWeight - 1.0) > 1.0e-5 ? 1.0 / totalWeight : 1.0;
        outputPosition = position * inverseWeight;
        outputPreviousPosition = previousPosition * inverseWeight;
        if (length(normal) > 1.0e-8)
        {
            outputNormal = normalize(normal);
        }
        if (length(tangent) > 1.0e-8)
        {
            outputTangent = vec4(normalize(tangent), sourceTangent.w);
        }
    }

    rawStoreFloat(s_skinOutputVertices, base + 0u, outputPosition.x);
    rawStoreFloat(s_skinOutputVertices, base + 1u, outputPosition.y);
    rawStoreFloat(s_skinOutputVertices, base + 2u, outputPosition.z);
    rawStoreFloat(s_skinOutputVertices, base + 3u, outputNormal.x);
    rawStoreFloat(s_skinOutputVertices, base + 4u, outputNormal.y);
    rawStoreFloat(s_skinOutputVertices, base + 5u, outputNormal.z);
    rawStoreFloat(s_skinOutputVertices, base + 6u, outputTangent.x);
    rawStoreFloat(s_skinOutputVertices, base + 7u, outputTangent.y);
    rawStoreFloat(s_skinOutputVertices, base + 8u, outputTangent.z);
    rawStoreFloat(s_skinOutputVertices, base + 9u, outputTangent.w);
    rawStoreFloat(s_skinOutputVertices, base + 10u, rawLoadFloat(s_skinBaseVertices, base + 10u));
    rawStoreFloat(s_skinOutputVertices, base + 11u, rawLoadFloat(s_skinBaseVertices, base + 11u));
    rawStoreFloat(s_skinOutputVertices, base + 12u, rawLoadFloat(s_skinBaseVertices, base + 12u));
    rawStoreFloat(s_skinOutputVertices, base + 13u, rawLoadFloat(s_skinBaseVertices, base + 13u));
    rawStoreFloat(s_skinOutputVertices, base + 14u, rawLoadFloat(s_skinBaseVertices, base + 14u));
    rawStoreFloat(s_skinOutputVertices, base + 15u, rawLoadFloat(s_skinBaseVertices, base + 15u));
    rawStoreFloat(s_skinOutputVertices, base + 16u, rawLoadFloat(s_skinBaseVertices, base + 16u));
    rawStoreFloat(s_skinOutputVertices, base + 17u, rawLoadFloat(s_skinBaseVertices, base + 17u));
    rawStoreFloat(s_skinOutputVertices, base + 18u, outputPreviousPosition.x);
    rawStoreFloat(s_skinOutputVertices, base + 19u, outputPreviousPosition.y);
    rawStoreFloat(s_skinOutputVertices, base + 20u, outputPreviousPosition.z);
}
