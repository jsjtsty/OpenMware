// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_Aspect : packoffset(c0.x);
    float g_Phase : packoffset(c0.z);
    float g_Frequency : packoffset(c0.w);
    float g_Power : packoffset(c1.x);
    float4 g_Dir : packoffset(c2.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float phase = dot(g_Dir.zw, input.input_texcoord_0) * g_Frequency + g_Phase;
    float wave = sin(phase) * g_Power;
    float2 offset = wave * g_Dir.xy / g_Aspect;
    return Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + offset);
}
