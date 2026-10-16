// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex1 : register(s1);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_ToneMappingDarkClamp : packoffset(c0.x);
    float g_ToneMappingLightClamp : packoffset(c0.y);
    float g_ToneMappingBias : packoffset(c0.z);
    float2 g_DownSampleOffset[16] : packoffset(c1.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float3 bounds=Texture0.Sample(Sampler_Tex1,float2(0.5,0.5)).rgb;
    float4 color=Texture0.Sample(Sampler_Tex0,input.input_texcoord_0);
    float luminance=dot(color.rgb,float3(0.2125,0.7154,0.0721));
    float value=max((luminance-bounds.x)/(bounds.y-bounds.x+0.001),0.0);
    float scale=exp2(log2(value)*bounds.z)/(luminance+0.001);
    return color*scale;
}
