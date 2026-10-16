// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_CenterUV : packoffset(c0.x);
    float g_RadialBlurPower : packoffset(c0.z);
    float2 g_InvTexSize : packoffset(c1.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float2 delta = g_CenterUV - input.input_texcoord_0;
    float distanceToCenter = length(delta) * g_RadialBlurPower;
    float2 direction = normalize(delta) * g_InvTexSize;
    float4 sum = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + direction * distanceToCenter) * 0.17;
    sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0) * 0.19;
    [unroll] for (int i = 2; i <= 9; ++i) {
        float a = i * distanceToCenter;
        sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + direction * a) * (0.19 - 0.02 * i);
    }
    return sum;
}
