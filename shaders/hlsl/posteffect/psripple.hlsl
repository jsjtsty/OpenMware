// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_Aspect : packoffset(c0.x);
    float g_Phase : packoffset(c0.z);
    float g_Frequency : packoffset(c0.w);
    float g_Power : packoffset(c1.x);
    float2 g_Center : packoffset(c1.y);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float2 delta = (g_Center - input.input_texcoord_0) * g_Aspect;
    float distanceToCenter = length(delta);
    float2 direction = normalize(delta);
    float phase = distanceToCenter * g_Frequency - g_Phase;
    float ripple = sin(phase) * g_Power;
    float envelope = max(step(1.5707963, abs(distanceToCenter * g_Frequency)), sin(distanceToCenter * g_Frequency) * sin(distanceToCenter * g_Frequency));
    float2 uv = input.input_texcoord_0 + (direction * ripple * envelope) / g_Aspect;
    return Texture0.Sample(Sampler_Tex0, uv);
}
