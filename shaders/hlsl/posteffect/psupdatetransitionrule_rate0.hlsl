// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float g_TransitionNow : packoffset(c0.x);
    float g_TransitionDst : packoffset(c0.y);
    float g_TransitionSrc : packoffset(c0.z);
    float g_TransitionRate : packoffset(c0.w);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
float value = g_TransitionNow >= input.input_texcoord_0.x ? 1.0 : 0.0;
    return float4(value, value, value, 1.0);
}
