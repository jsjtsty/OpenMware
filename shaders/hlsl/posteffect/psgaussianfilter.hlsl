// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float2 g_SampleOffsets[16] : packoffset(c0.x);
    float4 g_SampleWeights[16] : packoffset(c16.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float4 sum = 0.0;
    [unroll] for (int i=0; i<13; ++i) sum += Texture0.Sample(Sampler_Tex0, input.input_texcoord_0 + g_SampleOffsets[i]) * g_SampleWeights[i];
    return sum;
}
