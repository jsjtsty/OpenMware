// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex1 : register(s1);
SamplerState Sampler_Tex2 : register(s2);
SamplerState Sampler_Tex3 : register(s3);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_BloomPower : packoffset(c0.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float4 base = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0);
    float3 bloom = Texture0.Sample(Sampler_Tex1, input.input_texcoord_0).rgb * g_BloomPower;
    bloom += Texture0.Sample(Sampler_Tex2, input.input_texcoord_0).rgb * g_BloomPower;
    bloom += Texture0.Sample(Sampler_Tex3, input.input_texcoord_0).rgb * g_BloomPower;
    return float4(base.rgb + bloom, base.a);
}
