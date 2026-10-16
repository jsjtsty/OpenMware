// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float g_Alpha : packoffset(c0.x);
    float4x4 g_Transform : packoffset(c1.x);
    float4 g_StencilRect : packoffset(c5.x);
};
struct ShaderInput {
    float4 input_position_0 : POSITION0;
    float2 input_texcoord_0 : TEXCOORD0;
};
struct ShaderOutput {
    float4 output_sv_position_0 : SV_Position;
    float4 output_color_0 : COLOR0;
    float2 output_texcoord_0 : TEXCOORD0;
};
ShaderOutput main(ShaderInput input) {
    ShaderOutput output;
    float4 transformed = mul(input.input_position_0, g_Transform);
    float2 clipPosition = transformed.xy;
    float2 rectPosition = (clipPosition + float2(1.0, -1.0)) * g_StencilRect.zw;
    rectPosition = rectPosition * float2(0.5, -0.5) + g_StencilRect.xy;
    output.output_sv_position_0.xy = rectPosition * float2(2.0, -2.0) + float2(-1.0, 1.0);
    output.output_sv_position_0.z = transformed.z;
    output.output_sv_position_0.w = transformed.w;
    output.output_color_0 = float4(1.0, 1.0, 1.0, g_Alpha);
    output.output_texcoord_0 = input.input_texcoord_0 * g_StencilRect.zw + g_StencilRect.xy;
    return output;
}
