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
    float low=10.0, high=0.0, sum=0.0;
    [loop] for(int i=0;i<16;++i){
        float3 range=Texture0.Sample(Sampler_Tex0,input.input_texcoord_0+g_DownSampleOffset[i]).rgb;
        low=min(low,range.r); high=max(high,range.g); sum+=range.b;
    }
    low=min(max(low,0.0),g_ToneMappingLightClamp)*g_ToneMappingBias;
    high=(min(max(high,g_ToneMappingDarkClamp),1.0)-1.0)*g_ToneMappingBias+1.0;
    float average=min(max(sum/16.0,low),high);
    float exponent=g_ToneMappingBias*(2.0*(average-low)/(high-low+0.0001)-0.5)+1.0;
    return float4(low,high,exponent,1.0);
}
