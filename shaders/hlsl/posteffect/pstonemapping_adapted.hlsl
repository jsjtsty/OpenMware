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
    float amount=1.0-exp2(-g_ToneMappingBias*0.029146345659516);
    float4 a=Texture0.Sample(Sampler_Tex1,float2(0.5,0.5)); float4 b=Texture0.Sample(Sampler_Tex0,float2(0.5,0.5)); return lerp(b,a,amount);
}
