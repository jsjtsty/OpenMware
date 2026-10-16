// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_DirectionalBlurDirection : packoffset(c0.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float4 sum = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_DirectionalBlurDirection) * 0.17;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0) * 0.19;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 2.0 * g_DirectionalBlurDirection) * 0.15;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 3.0 * g_DirectionalBlurDirection) * 0.13;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 4.0 * g_DirectionalBlurDirection) * 0.11;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 5.0 * g_DirectionalBlurDirection) * 0.09;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 6.0 * g_DirectionalBlurDirection) * 0.07;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 7.0 * g_DirectionalBlurDirection) * 0.05;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 8.0 * g_DirectionalBlurDirection) * 0.03;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + 9.0 * g_DirectionalBlurDirection) * 0.01;
    return sum;
}
