// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
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
    float mn=1.0, mx=0.0, avg=0.0;
    [unroll] for(int i=0;i<4;++i){ float3 c=Texture0.Sample(Sampler_Tex0,input.input_texcoord_0+g_DownSampleOffset[i]).rgb; float v=dot(c,float3(0.2125,0.7154,0.0721)); mn=min(mn,v); mx=max(mx,v); avg+=v; } return float4(mn,mx,avg/4.0,1.0);
}
