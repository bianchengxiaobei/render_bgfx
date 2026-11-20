void vfxTestExpressions(VfxAttributes attributes, inout float result)
{
    const float vfxExpr0 = (VFX_PROP(Size) + (VFX_TIME * 2.0));
    result += vfxExpr0;
    const float vfxExpr2 = ((VFX_TIME - 1.0) / 2.0);
    result += vfxExpr2;
    const float vfxExpr4 = saturate((sin(VFX_TIME) + cos(VFX_TIME)));
    result += vfxExpr4;
    const float vfxExpr6 = abs(VFX_PROP(Size));
    result += vfxExpr6;
    const float vfxExpr8 = (VFX_DT + VFX_PROP(Size));
    result += vfxExpr8;
    const float vfxExpr10 = lerp(0.0, VFX_PROP(Size), VFX_LIFETIME_RATIO);
    result += vfxExpr10;
    const float3 vfxExpr12 = (VFX_ATTR(color)).zyx;
    result += dot(vfxExpr12, vfxExpr12);
    const float3 vfxExpr14 = clamp(VFX_ATTR(position), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
    result += dot(vfxExpr14, vfxExpr14);
    const float3 vfxExpr16 = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0), VFX_RAND4(VFX_ATTR(seed)).xyz);
    result += dot(vfxExpr16, vfxExpr16);
    const float vfxExpr18 = lerp(0.0, 6.28, VFX_RAND(VFX_ATTR(seed)));
    result += vfxExpr18;
    const float vfxExpr20 = ((VFX_ATTR(alive)) ? (1.0) : (VFX_PROP(Size)));
    result += vfxExpr20;
    const float4 vfxExpr22 = VFX_SAMPLE_GRADIENT(ColorRamp, VFX_LIFETIME_RATIO);
    result += dot(vfxExpr22, vfxExpr22);
    const float vfxExpr24 = (0.0 + (VFX_LIFETIME_RATIO - 0.0) * (2.0 - 0.0) / (1.0 - 0.0));
    result += vfxExpr24;
    const float vfxExpr26 = max(0.0, min(1.0, VFX_SAMPLE_CURVE(SizeCurve, VFX_LIFETIME_RATIO)));
    result += vfxExpr26;
    const float vfxExpr28 = VFX_NOISE(VFX_ATTR(position), 2.0, VFX_ATTR(seed));
    result += vfxExpr28;
    const bool vfxExpr30 = ((VFX_ATTR(alive)) ? (true) : (false));
    result += vfxExpr30 ? 1.0 : 0.0;
}
