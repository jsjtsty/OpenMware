#include "common.hlsli"

Texture2D Texture_Tex0 : register(t0);
SamplerState Sampler_Tex0 : register(s0);
#if defined(USE_MASK)
Texture2D Texture_Mask : register(t2);
SamplerState Sampler_Mask : register(s2);
#endif
#if defined(USE_TRANSITION)
Texture2D Texture_TransitionMask : register(t3);
SamplerState Sampler_TransitionMask : register(s3);
Texture2D Texture_TransitionRule : register(t4);
SamplerState Sampler_TransitionRule : register(s4);
#endif
#if defined(USE_DISPLACEMENT)
Texture2D Texture_DisplacementMap : register(t5);
SamplerState Sampler_DisplacementMap : register(s5);
cbuffer ConstantBuffer : register(b0) { float2 g_DisplacementMapSamplingScale; };
#endif
#if defined(READ_DESTINATION)
Texture2D Texture_Destination : register(t7);
SamplerState Sampler_Destination : register(s7);
#endif

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 projective : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target
{
    float2 projectiveUV = input.projective.xy / input.projective.w;
    float2 sampleUV = input.uv;
#if defined(USE_DISPLACEMENT)
    float2 displacement = Texture_DisplacementMap.Sample(Sampler_DisplacementMap, projectiveUV).xz * 2.0 - 1.0;
    sampleUV += displacement * g_DisplacementMapSamplingScale;
#endif
    float4 source = Texture_Tex0.Sample(Sampler_Tex0, sampleUV) * input.color;
#if defined(BLEND_MODE_BLEND) && defined(USE_DISPLACEMENT) && defined(USE_TRANSITION) && !defined(USE_MASK)
    // This one original permutation discards before applying the transition rule.
    clip(source.a - (1.0 / 255.0));
#define DISCARDED_BEFORE_TRANSITION
#endif
#if defined(USE_MASK)
    source *= Texture_Mask.Sample(Sampler_Mask, projectiveUV);
#endif
#if defined(USE_TRANSITION)
    float transition = Texture_TransitionMask.Sample(Sampler_TransitionMask, projectiveUV).r;
    // The DXBC sample swizzle xywz places the blue rule channel into alpha.
    float transitionAlpha = Texture_TransitionRule.Sample(Sampler_TransitionRule, float2(transition, 0.0)).b;
    source.a *= transitionAlpha;
#endif
#if defined(BLEND_MODE_BLEND)
#if !defined(DISCARDED_BEFORE_TRANSITION)
    clip(source.a - (1.0 / 255.0));
#endif
    return source;
#elif defined(BLEND_MODE_MUL)
    // Fixed-function RGB: src=DEST_COLOR, dst=ZERO, ADD.
    return float4(source.rgb * source.a + (1.0 - source.a), source.a);
#elif defined(BLEND_MODE_SCREEN)
    // Fixed-function RGB: src=ONE, dst=INV_SRC_COLOR, ADD.
    return float4(source.rgb * source.a, source.a);
#elif defined(BLEND_MODE_WRITEALPHA)
    // Replacement output: RGB is the untextured vertex colour, alpha accumulates t7.
    float4 destination = Texture_Destination.Sample(Sampler_Destination, projectiveUV);
    return float4(input.color.rgb, source.a + destination.a);
#else
    float4 destination = Texture_Destination.Sample(Sampler_Destination, projectiveUV);
    return float4(BlendColor(source.rgb, destination.rgb), source.a);
#endif
}
