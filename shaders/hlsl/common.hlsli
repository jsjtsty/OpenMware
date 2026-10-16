// Equations recovered from the game shaders, including its nonstandard modes.
#ifndef OPENMWARE_COMMON_HLSLI
#define OPENMWARE_COMMON_HLSLI

#if !defined(BLEND_MODE_BLEND) && !defined(BLEND_MODE_MUL) && !defined(BLEND_MODE_SCREEN)
#define READ_DESTINATION
#endif

// Fixed-function RGB coverage after these shaders:
// blend: SRC_ALPHA / INV_SRC_ALPHA, ADD (straight colour).
// mul: DEST_COLOR / ZERO, ADD (shader emits 1-a+a*s); ZERO / SRC_COLOR is equivalent.
// screen: ONE / INV_SRC_COLOR, ADD (shader emits a*s).
// All t7 RGB modes below: SRC_ALPHA / INV_SRC_ALPHA, ADD over the computed blend colour.
// writealpha: alpha must replace (ONE / ZERO); shader already accumulates destination alpha.
// These are equivalent compositing states inferred from the shader outputs, not recovered D3D state objects.

float VividLightChannel(float source, float destination)
{
    if (source < 0.5)
        return (1.0 - 2.0 * source >= destination) ? 0.0 :
            (destination - (1.0 - 2.0 * source)) / (2.0 * source);
    return destination < 2.0 - 2.0 * source ? destination / (2.0 - 2.0 * source) : 1.0;
}

float3 BlendColor(float3 source, float3 destination)
{
#if defined(BLEND_MODE_MUL)
    // Fixed mul state; the actual premultiplication is in the family output path.
    return source * destination;
#elif defined(BLEND_MODE_ADD) || defined(BLEND_MODE_LINEARDODGE)
    // lineardodge reads t7, then straight-alpha over; particle add instead uses SRC_ALPHA / ONE.
    return source + destination;
#elif defined(BLEND_MODE_SUB) || defined(BLEND_MODE_LINEARBURN)
    // Reads t7; alpha-over the computed colour (SRC_ALPHA, INV_SRC_ALPHA).
    return destination + source - 1.0;
#elif defined(BLEND_MODE_DARKEN)
    // t7 minimum, then straight-alpha over.
    return min(source, destination);
#elif defined(BLEND_MODE_LIGHTEN)
    // t7 maximum, then straight-alpha over.
    return max(source, destination);
#elif defined(BLEND_MODE_SCREEN)
    // Fixed screen state; family output emits source.rgb * coverage instead of reading t7.
    return 1.0 - (1.0 - source) * (1.0 - destination);
#elif defined(BLEND_MODE_OVERLAY)
    // t7 overlay, then straight-alpha over.
    return lerp(2.0 * source * destination,
                1.0 - 2.0 * (1.0 - source) * (1.0 - destination),
                step(0.5, destination));
#elif defined(BLEND_MODE_HARDLIGHT)
    // t7 hardlight, then straight-alpha over.
    return lerp(2.0 * source * destination,
                1.0 - 2.0 * (1.0 - source) * (1.0 - destination),
                step(0.5, source));
#elif defined(BLEND_MODE_DIFFERENCE)
    // t7 difference, then straight-alpha over.
    return abs(destination - source);
#elif defined(BLEND_MODE_EXCLUSION)
    // t7 exclusion, then straight-alpha over.
    return source + destination - 2.0 * source * destination;
#elif defined(BLEND_MODE_COLORDODGE)
    // t7 colordodge, then straight-alpha over; no shader saturation.
    return destination / (1.0 - source);
#elif defined(BLEND_MODE_COLORBURN)
    // t7 colorburn, then straight-alpha over; no shader saturation.
    return 1.0 - (1.0 - destination) / source;
#elif defined(BLEND_MODE_HARDMIX)
    // t7 hardmix, then straight-alpha over.
    // The original keeps the sum above the threshold; it does not quantize to 1.
    float3 sum = source + destination;
    return float3(sum.x < 1.0 ? 0.0 : sum.x, sum.y < 1.0 ? 0.0 : sum.y, sum.z < 1.0 ? 0.0 : sum.z);
#elif defined(BLEND_MODE_VIVIDLIGHT)
    // t7 vividlight, then straight-alpha over.
    return float3(VividLightChannel(source.x, destination.x), VividLightChannel(source.y, destination.y), VividLightChannel(source.z, destination.z));
#elif defined(BLEND_MODE_PINLIGHT)
    // t7 pinlight, then straight-alpha over.
    return lerp(min(destination, 2.0 * source), max(destination, 2.0 * source - 1.0), step(0.5, source));
#elif defined(BLEND_MODE_LINEARLIGHT)
    // t7 linearlight, then straight-alpha over.
    return saturate(destination + 2.0 * source - 1.0);
#elif defined(BLEND_MODE_SOFTLIGHT)
    // t7 power-curve softlight, then straight-alpha over.
    float3 low = exp2(log2(abs(destination)) * (2.0 * (1.0 - source)));
    float3 high = exp2(log2(abs(destination)) * (0.5 / source));
    return float3(source.x < 0.5 ? low.x : high.x, source.y < 0.5 ? low.y : high.y, source.z < 0.5 ? low.z : high.z);
#else
    return source;
#endif
}

float TransitionAlpha(float maskValue, float ruleValue)
{
    return step(ruleValue, maskValue);
}
#endif
