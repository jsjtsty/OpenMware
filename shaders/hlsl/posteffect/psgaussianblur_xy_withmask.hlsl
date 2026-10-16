// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex1 : register(s1);
Texture2D Texture0 : register(t0);
Texture2D Texture1 : register(t1);
cbuffer ConstantBuffer : register(b0) {
    float4 g_GaussianBlurWeightTmp[2] : packoffset(c0.x);
    float4 g_GaussianBlurOffsetArrayXTmp[2] : packoffset(c2.x);
    float4 g_GaussianBlurOffsetArrayYTmp[2] : packoffset(c4.x);
    float4x4 g_GaussianBlurOffsetMatrix : packoffset(c6.x);
    float4x4 g_GaussianBlurShiftMatrix : packoffset(c10.x);
    float4 g_GaussianBlurOffset : packoffset(c14.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float2 radius=Texture1.Sample(Sampler_Tex1,input.input_texcoord_0).rg;
    float4 x=g_GaussianBlurOffsetArrayXTmp[0]*radius.x;
    float4 y=g_GaussianBlurOffsetArrayYTmp[0]*radius.y;
    float2 shift=g_GaussianBlurOffset.xy*radius;
    float2 uv=input.input_texcoord_0;
    float4 sum=0.0;
    [unroll] for(int i=0;i<4;++i){
        float4 horizontal=Texture0.Sample(Sampler_Tex0,uv+float2(x[i],0))+Texture0.Sample(Sampler_Tex0,uv+float2(x[3-i]+shift.x,0));
        float4 vertical=Texture0.Sample(Sampler_Tex0,uv+float2(0,y[i]))+Texture0.Sample(Sampler_Tex0,uv+float2(0,y[3-i]+shift.y));
        sum+=horizontal*g_GaussianBlurWeightTmp[0][i];
        sum+=vertical*g_GaussianBlurWeightTmp[1][i];
    }
    return sum;
}
