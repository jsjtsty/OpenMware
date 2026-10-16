#include "common.hlsli"

#if defined(PARTICLE_DEPTH_ONLY)
Texture2D Texture_DiffuseMap : register(t0);
SamplerState Sampler_DiffuseMap : register(s0);
cbuffer ConstantBuffer : register(b0)
{
    bool g_IsSoftParticle;
    float g_SoftParticleLength;
    float g_FarClip;
};
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 viewDepth : TEXCOORD1;
};
float4 main(PSInput input) : SV_Target
{
    float4 texel = Texture_DiffuseMap.Sample(Sampler_DiffuseMap, input.uv);
    float alpha = saturate((texel.a * input.color.a - 0.2) * 1.25);
    float smooth = alpha * alpha * (3.0 - 2.0 * alpha);
    float depthValue = input.viewDepth.z * smooth + (1.0 - smooth) * g_FarClip;
    return float4(depthValue, depthValue, depthValue, 1.0);
}
#else
Texture2D Texture_DiffuseMap : register(t0);
SamplerState Sampler_DiffuseMap : register(s0);
Texture2D Texture_DepthBuffer : register(t6);
SamplerState Sampler_DepthBuffer : register(s6);
#if !defined(PARTICLE_ALPHA_BLEND) && !defined(PARTICLE_ALPHA_ADD) && !defined(PARTICLE_ALPHA_MUL) && !defined(BLEND_MODE_SCREEN)
Texture2D Texture_Destination : register(t7);
SamplerState Sampler_Destination : register(s7);
#endif
cbuffer ConstantBuffer : register(b0)
{
    bool g_IsSoftParticle;
    float g_SoftParticleLength;
    float g_FarClip;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 viewDepth : TEXCOORD1;
    float4 projective : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target
{
    float4 texel = Texture_DiffuseMap.Sample(Sampler_DiffuseMap, input.uv);
#if defined(PARTICLE_COLOR_MUL)
    float3 sourceColor = texel.rgb * input.color.rgb;
#elif defined(PARTICLE_COLOR_SUB)
    float3 sourceColor = texel.rgb - input.color.rgb;
#else
    float3 sourceColor = texel.rgb + input.color.rgb;
#endif
    float alpha = texel.a * input.color.a;
    if (g_IsSoftParticle)
    {
        float2 screenUV = input.projective.xy / input.projective.w;
        float sceneDepth = Texture_DepthBuffer.Sample(Sampler_DepthBuffer, screenUV).r;
        float fade = saturate((sceneDepth - input.viewDepth.z) / g_SoftParticleLength);
        alpha *= fade * fade * (3.0 - 2.0 * fade);
    }
#if defined(PARTICLE_ALPHA_MUL)
    float coverage = texel.a * input.color.a;
    return float4(sourceColor * coverage + (1.0 - coverage), alpha);
#elif defined(PARTICLE_ALPHA_BLEND) || defined(PARTICLE_ALPHA_ADD)
    return float4(sourceColor, alpha);
#elif defined(BLEND_MODE_SCREEN)
    // RGB premultiplication uses coverage before the soft-depth alpha fade.
    return float4(sourceColor * (texel.a * input.color.a), alpha);
#else
    float2 screenUV = input.projective.xy / input.projective.w;
    float3 destination = Texture_Destination.Sample(Sampler_Destination, screenUV).rgb;
    return float4(BlendColor(sourceColor, destination), alpha);
#endif
}
#endif
