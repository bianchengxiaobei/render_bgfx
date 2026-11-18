$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_decalAlbedo, 0);
SAMPLER2D(s_decalNormal, 1);
SAMPLER2D(s_decalMask, 2);
SAMPLER2D(s_depthSource, 3);

vec3 hbgDecalReceiverNormal(vec3 worldPosition)
{
    vec3 normalWS = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
    const vec3 viewWS = normalize(u_global_cameraPos.xyz - worldPosition);
    if (dot(normalWS, viewWS) < 0.0)
    {
        normalWS = -normalWS;
    }
    return normalWS;
}

void main()
{
    const vec2 uv = v_texcoord0;
    const float rawDepth = texture2D(s_depthSource, uv).r;
    if (rawDepth >= 0.999999)
    {
        discard;
    }

    const vec4 clip = vec4(uv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 world = mul(u_global_invViewProj, clip);
    if (abs(world.w) < 1.0e-6)
    {
        discard;
    }
    world /= world.w;

    const vec3 local = mul(u_decal_worldToDecal, vec4(world.xyz, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5, 0.5, 0.5))))
    {
        discard;
    }

    const vec2 decalUV = local.xz + vec2(0.5, 0.5);
    const int flags = int(u_decal_params.w + 0.5);
    const vec3 receiverNormalWS = hbgDecalReceiverNormal(world.xyz);
    const vec3 decalForwardWS = normalize(u_decal_decalToWorld[1].xyz);
    float fade = 1.0;
    if (u_decal_params.y > 0.0)
    {
        const float fadeStart = cos(radians(min(u_decal_params.y, 180.0)));
        fade = smoothstep(fadeStart, 1.0, abs(dot(receiverNormalWS, decalForwardWS)));
    }

    vec4 albedo = u_decal_baseColor;
    if ((flags & 1) != 0)
    {
        albedo *= texture2D(s_decalAlbedo, decalUV);
    }
    const float coverage = u_decal_params.x * fade * albedo.a;
    if (coverage <= 0.0001)
    {
        discard;
    }

    vec3 normalWS = receiverNormalWS;
    if ((flags & 2) != 0)
    {
        const vec2 normalRG = texture2D(s_decalNormal, decalUV).xy * 2.0 - 1.0;
        const float normalZ = sqrt(max(1.0 - dot(normalRG, normalRG), 0.0));
        const vec3 normalTS = vec3(normalRG, normalZ);
        const mat3 tangentToWorld = mtxFromCols(normalize(u_decal_decalToWorld[0].xyz),
                                                normalize(u_decal_decalToWorld[2].xyz),
                                                normalize(u_decal_decalToWorld[1].xyz));
        normalWS = normalize(mul(normalTS, tangentToWorld));
    }
    normalWS *= u_decal_params.z;

    vec3 maos = u_decal_params2.xyz;
    if ((flags & 4) != 0)
    {
        maos = texture2D(s_decalMask, decalUV).rgb;
    }

    const int channelMask = int(u_decal_params3.y + 0.5);
    const float albedoCoverage = (channelMask & 1) != 0 ? coverage : 0.0;
    const float normalCoverage = (channelMask & 2) != 0 ? coverage : 0.0;
    const float maosCoverage = (channelMask & 4) != 0 ? coverage : 0.0;
    const float aoCoverage = (channelMask & 8) != 0 ? coverage : 0.0;
    const float roughnessCoverage = (channelMask & 16) != 0 ? coverage : 0.0;

    gl_FragData[0] = vec4((channelMask & 1) != 0 ? albedo.rgb : vec3(0.0, 0.0, 0.0), albedoCoverage);
    gl_FragData[1] = vec4(vec3(0.5, 0.5, 0.5) + normalWS * 0.5, normalCoverage);
    gl_FragData[2] = vec4(hbgSaturate(maos), aoCoverage);
    gl_FragData[3] = vec4(maosCoverage, roughnessCoverage, u_decal_params3.x / 255.0, 0.0);
}
