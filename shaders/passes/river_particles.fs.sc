$input v_screen

#include "common/common.sc"

SAMPLER2D(s_riverWaveBase, 0);
SAMPLER2D(s_riverParticleParams, 1);
SAMPLER2D(s_riverParticleSpriteTable, 2);
SAMPLER2D(s_riverParticleDepth, 3);

uniform mat4 u_riverParticleInvViewProj;
uniform vec4 u_riverParticleCamera;
uniform vec4 u_riverParticleParams;
uniform vec4 u_riverParticleGrid;

float riverParticleSmooth(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

void accumulateRiverSprite(float id, vec2 screen, vec2 waterWorldXZ, vec2 waveGradX, vec2 waveGradY,
                           inout vec2 sumSlopes, inout float sumWeight)
{
    if (id < 0.0 || id >= u_riverParticleGrid.w)
    {
        return;
    }
    const float row = (id + 0.5) / max(u_riverParticleGrid.w, 1.0);
    const vec4 p0 = texture2DLod(s_riverParticleParams, vec2(0.25, row), 0.0);
    const vec4 p1 = texture2DLod(s_riverParticleParams, vec2(0.75, row), 0.0);
    const vec2 spriteScreen = p0.xy;
    const vec2 spriteWorld = p0.zw;
    const vec2 spriteOffset = p1.xy;
    const float intensity = p1.z;
    const float radius = max(p1.w, 1.0e-3);
    const float s = distance(screen, spriteScreen) / radius;
    if (s > 1.0)
    {
        return;
    }
    const vec2 waveCoord =
        (waterWorldXZ - spriteWorld + spriteOffset) / max(u_riverParticleParams.z, 1.0e-3);
    const vec2 slopes = texture2DGrad(s_riverWaveBase, waveCoord, waveGradX, waveGradY).xy;
    const float weight = riverParticleSmooth(1.0 - s) * riverParticleSmooth(intensity);
    sumSlopes += slopes * weight;
    sumWeight += weight * weight;
}

void main()
{
    const vec2 screenSize = max(u_riverParticleParams.xy, vec2(1.0, 1.0));
    const vec2 screenUV = v_screen / screenSize;
    if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0)
    {
        discard;
    }

    const vec2 ndc = vec2(screenUV.x * 2.0 - 1.0, 1.0 - screenUV.y * 2.0);
    const vec3 camera = u_riverParticleCamera.xyz;
    const vec4 farClip = mul(u_riverParticleInvViewProj, vec4(ndc, 1.0, 1.0));
    const vec3 farPosition = farClip.xyz / max(farClip.w, 1.0e-6);
    const vec3 ray = farPosition - camera;
    if (abs(ray.y) <= 1.0e-5)
    {
        discard;
    }
    const float distanceToPlane = (u_riverParticleCamera.w - camera.y) / ray.y;
    if (distanceToPlane <= 0.0)
    {
        discard;
    }
    const vec3 waterWorld = camera + ray * distanceToPlane;
    const float waterDistance = distance(camera, waterWorld);
    const float sceneDepth = texture2D(s_riverParticleDepth, screenUV).x;
    float sceneDistance = 1.0e9;
    if (sceneDepth < 0.999999)
    {
        const vec4 worldClip = mul(u_riverParticleInvViewProj, vec4(ndc, sceneDepth, 1.0));
        const vec3 sceneWorld = worldClip.xyz / max(worldClip.w, 1.0e-6);
        sceneDistance = distance(camera, sceneWorld);
    }
    if (waterDistance > sceneDistance + 0.25)
    {
        discard;
    }

    const vec2 gridSize = max(u_riverParticleGrid.xy, vec2(1.0, 1.0));
    const vec2 gridCell =
        clamp(floor(v_screen / screenSize * gridSize), vec2(0.0, 0.0), gridSize - vec2(1.0, 1.0));
    const float spriteWidth = gridSize.x * 4.0;
    const vec2 waveGradX = dFdx(waterWorld.xz) / max(u_riverParticleParams.z, 1.0e-3);
    const vec2 waveGradY = dFdy(waterWorld.xz) / max(u_riverParticleParams.z, 1.0e-3);

    vec2 sumSlopes = vec2(0.0, 0.0);
    float sumWeight = 0.0;
    bool done = false;
    for (int layer = 0; layer < 4 && !done; ++layer)
    {
        const vec2 uv = vec2((gridCell.x * 4.0 + float(layer) + 0.5) / spriteWidth,
                             (gridCell.y + 0.5) / gridSize.y);
        const vec4 ids = texture2DLod(s_riverParticleSpriteTable, uv, 0.0);
        if (ids.x < 0.0)
        {
            done = true;
            break;
        }
        accumulateRiverSprite(ids.x, v_screen, waterWorld.xz, waveGradX, waveGradY, sumSlopes, sumWeight);
        if (ids.y < 0.0)
        {
            done = true;
            break;
        }
        accumulateRiverSprite(ids.y, v_screen, waterWorld.xz, waveGradX, waveGradY, sumSlopes, sumWeight);
        if (ids.z < 0.0)
        {
            done = true;
            break;
        }
        accumulateRiverSprite(ids.z, v_screen, waterWorld.xz, waveGradX, waveGradY, sumSlopes, sumWeight);
        if (ids.w < 0.0)
        {
            done = true;
            break;
        }
        accumulateRiverSprite(ids.w, v_screen, waterWorld.xz, waveGradX, waveGradY, sumSlopes, sumWeight);
    }

    gl_FragColor = vec4(sumSlopes, sumWeight, 1.0);
}
