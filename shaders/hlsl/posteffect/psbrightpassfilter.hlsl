// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_BrightLowLimit : packoffset(c0.x);
    float g_BrightPower : packoffset(c0.y);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0 {
    float4 color = Texture0.Sample(Sampler_Tex0, input.input_texcoord_0);
    return float4(max(color.rgb - g_BrightLowLimit, 0.0) * g_BrightPower, 1.0);
}
