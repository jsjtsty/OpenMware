// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex1 : register(s1);
SamplerState Sampler_Tex2 : register(s2);
SamplerState Sampler_Tex3 : register(s3);
SamplerState Sampler_Tex4 : register(s4);
SamplerState Sampler_Tex5 : register(s5);
Texture2D Texture0 : register(t0);
Texture2D Texture1 : register(t1);
Texture2D Texture2 : register(t2);
Texture2D Texture3 : register(t3);
Texture2D Texture4 : register(t4);
Texture2D Texture5 : register(t5);
cbuffer ConstantBuffer : register(b0) {
    float4 m_Tu : packoffset(c0.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float4 original = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0);
    float4 mask = Texture1.Sample(Sampler_Tex1, input.input_texcoord_0);
    float4 weights = max(mask.x, mask.y).xxxx * m_Tu;
    float4 result = lerp(original, Texture2.Sample(Sampler_Tex2, input.input_texcoord_0), weights.x);
    result = lerp(result, Texture3.Sample(Sampler_Tex3, input.input_texcoord_0), weights.y);
    result = lerp(result, Texture4.Sample(Sampler_Tex4, input.input_texcoord_0), weights.z);
    result = lerp(result, Texture5.Sample(Sampler_Tex5, input.input_texcoord_0), weights.w);
    return result;
}
