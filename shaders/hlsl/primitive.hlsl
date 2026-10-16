#include "common.hlsli"

#if defined(READ_DESTINATION)
Texture2D Texture_Destination : register(t7);
SamplerState Sampler_Destination : register(s7);
#endif
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

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 projective : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.projective.xy / input.projective.w;
    float4 source = input.color;
#if defined(USE_MASK)
    source *= Texture_Mask.Sample(Sampler_Mask, uv);
#endif
#if defined(USE_TRANSITION)
    float transition = Texture_TransitionMask.Sample(Sampler_TransitionMask, uv).r;
    float transitionAlpha = Texture_TransitionRule.Sample(Sampler_TransitionRule, float2(transition, 0.0)).b;
    source.a *= transitionAlpha;
#endif
#if defined(BLEND_MODE_BLEND)
    return source;
#elif defined(BLEND_MODE_MUL)
    return float4(source.rgb * source.a + (1.0 - source.a), source.a);
#elif defined(BLEND_MODE_SCREEN)
    return float4(source.rgb * source.a, source.a);
#elif defined(BLEND_MODE_WRITEALPHA)
    float4 destination = Texture_Destination.Sample(Sampler_Destination, uv);
    return float4(input.color.rgb, source.a + destination.a);
#else
    float4 destination = Texture_Destination.Sample(Sampler_Destination, uv);
    return float4(BlendColor(source.rgb, destination.rgb), source.a);
#endif
}
