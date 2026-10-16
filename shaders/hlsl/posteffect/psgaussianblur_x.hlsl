// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
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
    // Unmasked passes use the packed cbuffer columns directly, not logical matrix rows.
    float4x4 a=transpose(g_GaussianBlurOffsetMatrix);
    float4x4 b=transpose(g_GaussianBlurShiftMatrix);
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
