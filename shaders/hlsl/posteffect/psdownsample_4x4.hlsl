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
float4 sum = 0.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        float4 offsets = g_DownSampleOffsetTmp[i];
        sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + offsets.xy);
        sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + offsets.zw);
    }
    clip(sum.a);
    return sum * (1.0 / 16.0);
}
