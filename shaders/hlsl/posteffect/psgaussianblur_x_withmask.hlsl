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
    float radius=Texture1.Sample(Sampler_Tex1,input.input_texcoord_0).r;
    if(radius==0.0) return Texture0.Sample(Sampler_Tex0,input.input_texcoord_0);
    // Masked passes treat matrix rows as packed pairs; shifted taps add both matrices.
    float4x4 a=g_GaussianBlurOffsetMatrix*radius;
    float4x4 b=(g_GaussianBlurOffsetMatrix+g_GaussianBlurShiftMatrix)*radius;
    float4 sum=0.0;
    [unroll] for(int i=0;i<4;++i){
        float2 uv=input.input_texcoord_0;
        float4 even=Texture0.Sample(Sampler_Tex0,uv+a[i].xy)+Texture0.Sample(Sampler_Tex0,uv+b[3-i].zw);
        float4 odd=Texture0.Sample(Sampler_Tex0,uv+a[i].zw)+Texture0.Sample(Sampler_Tex0,uv+b[3-i].xy);
        sum+=even*g_GaussianBlurWeightTmp[i/2][(i%2)*2];
        sum+=odd*g_GaussianBlurWeightTmp[i/2][(i%2)*2+1];
    }
    return sum;
}
