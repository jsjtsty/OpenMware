// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float4x4 g_World : packoffset(c0.x);
    float4x4 g_View : packoffset(c4.x);
    float4x4 g_Proj : packoffset(c8.x);
    float4x4 g_T : packoffset(c12.x);
};
struct ShaderInput {
    float3 input_position_0 : POSITION0;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
struct ShaderOutput {
    float4 output_sv_position_0 : SV_Position;
    float4 output_color_0 : COLOR0;
    float2 output_texcoord_0 : TEXCOORD0;
    float4 output_texcoord_1 : TEXCOORD1;
};
ShaderOutput main(ShaderInput input) {
    ShaderOutput output;
    float4 p=mul(float4(input.input_position_0,1.0),g_World); p=mul(p,g_View); p=mul(p,g_Proj);
    output.output_sv_position_0=p; output.output_color_0=input.input_color_0; output.output_texcoord_0=input.input_texcoord_0; output.output_texcoord_1=mul(p,g_T); return output;
}
