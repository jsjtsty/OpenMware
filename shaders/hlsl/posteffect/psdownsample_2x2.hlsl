// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float4 g_DownSampleOffsetTmp[8] : packoffset(c0.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float4 p0 = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_DownSampleOffsetTmp[0].xy);
    float4 p1 = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_DownSampleOffsetTmp[0].zw);
    float4 p2 = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_DownSampleOffsetTmp[1].xy);
    float4 p3 = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_DownSampleOffsetTmp[1].zw);
    float4 sum = p0 + p1 + p2 + p3;
    clip(sum.a);
    return sum * 0.25;
}
