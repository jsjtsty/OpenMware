// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_Aspect : packoffset(c0.x);
    float g_Distance : packoffset(c0.z);
    float g_Radius : packoffset(c0.w);
    float2 g_Center : packoffset(c1.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float2 delta = (input.input_texcoord_0 - g_Center) * g_Aspect;
    float radiusZ = sqrt(max(g_Radius * g_Radius - dot(delta, delta), 0.0));
    float3 sphere = float3(delta, radiusZ);
    float lengthInv = rsqrt(dot(sphere, sphere));
    float2 offset = (-sphere.xy * lengthInv * max(radiusZ * g_Distance, 0.0) / g_Aspect) + input.input_texcoord_0;
    return Texture0.Sample(Sampler_Tex0, offset);
}
