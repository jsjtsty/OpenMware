// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex1 : register(s1);
SamplerState Sampler_Tex3 : register(s3);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_DOF_Middle : packoffset(c0.x);
    float g_DOF_Far : packoffset(c0.y);
    float g_DOF_Lange : packoffset(c0.z);
    float g_GaussianBlurWeight[8] : packoffset(c1.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float depth = Texture0.Sample(Sampler_Tex3, input.input_texcoord_0).r;
    float4 nearColor = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0);
    float4 farColor = Texture0.Sample(Sampler_Tex1, input.input_texcoord_0);
    if (depth >= g_DOF_Far) return farColor;
    if (depth <= g_DOF_Middle) return nearColor;
    return lerp(nearColor, farColor, ((depth-g_DOF_Middle)/g_DOF_Lange));
}
