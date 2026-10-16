// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
SamplerState Sampler_Tex3 : register(s3);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_DOF_Middle : packoffset(c0.x);
    float g_DOF_Far : packoffset(c0.y);
    float g_DOF_Lange : packoffset(c0.z);
    float g_GaussianBlurWeight[8] : packoffset(c1.x);
    float4x4 g_GaussianBlurOffsetMatrix : packoffset(c9.x);
    float4x4 g_GaussianBlurShiftMatrix : packoffset(c13.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
// Depth at each original tap scales only the current blur axis inside the far focus range.
float4 DofTap(float2 uv,float2 offset,bool adjust)
{
    if(adjust){
        float depth=Texture0.Sample(Sampler_Tex3,uv+offset).r;
        float amount=saturate((depth-g_DOF_Middle)/g_DOF_Lange);
        offset *= float2(amount,0.0);
    }
    return Texture0.Sample(Sampler_Tex0,uv+offset);
}
float4 main(ShaderInput input) : SV_Target0
{
    float2 uv=input.input_texcoord_0;
    float depth=Texture0.Sample(Sampler_Tex3,uv).r;
    if(depth<=g_DOF_Middle) return Texture0.Sample(Sampler_Tex0,uv);
    bool adjust=depth<=g_DOF_Far;
    float4x4 a=g_GaussianBlurOffsetMatrix;
    float4x4 b=g_GaussianBlurOffsetMatrix+g_GaussianBlurShiftMatrix;
    float4 sum=0.0;
    [unroll] for(int i=0;i<4;++i){
        float4 even=DofTap(uv,a[i].xy,adjust)+DofTap(uv,b[3-i].zw,adjust);
        float4 odd=DofTap(uv,a[i].zw,adjust)+DofTap(uv,b[3-i].xy,adjust);
        sum+=even*g_GaussianBlurWeight[i*2]; sum+=odd*g_GaussianBlurWeight[i*2+1];
    }
    return sum;
}
