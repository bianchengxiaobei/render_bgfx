$input v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#include "material/surface_data.sc"
#define HBG_OCEAN_SLOPE_VARIANCE_TEXTURE 1
#define HBG_OCEAN_SLOPE_VARIANCE_STAGE 11
#include "lighting/ocean_brdf.sc"
#include "lighting/shadows.sc"

void main()
{
    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    const vec3 geometricNormalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, geometricNormalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);

    const vec3 N = normalize(surfaceData.normalWS);
    const vec3 V = normalize(viewWS);
    const vec3 L = normalize(u_pass_lightDirection.xyz);
    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, N);
    const vec3 sunRadiance = u_pass_lightColor.rgb * max(u_pass_lightDirection.w, 0.0) *
                             shadowVisibility;
    const vec3 skyIrradiance =
        hbgOceanSkyIrradiance(v_worldPos, V, N, surfaceData.perceptualRoughness);

    const vec2 oceanCoord = v_worldPos.xz;
    const float seaRoughness =
        max(hbgOceanSlopeVariance(dFdx(oceanCoord), dFdy(oceanCoord)), 2.0e-5);
    const vec3 ocean = hbgEvaluateOceanBrdf(V, N, L, sunRadiance, skyIrradiance,
                                            surfaceData.baseColor, seaRoughness);
    gl_FragColor = vec4(max(ocean, vec3(0.0, 0.0, 0.0)), 1.0);
}
