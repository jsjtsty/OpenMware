// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float4x4 g_World : packoffset(c0.x);
};
struct ShaderInput {
    float3 input_position_0 : POSITION0;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
    float3 input_normal_0 : NORMAL0;
    float3 input_tangent_0 : TANGENT0;
    float3 input_binormal_0 : BINORMAL0;
};
struct ShaderOutput {
    float4 output_position_0 : POSITION0;
    float4 output_color_0 : COLOR0;
    float4 output_texcoord_0 : TEXCOORD0;
    float4 output_texcoord_1 : TEXCOORD1;
    float4 output_texcoord_2 : TEXCOORD2;
    float4 output_texcoord_3 : TEXCOORD3;
};
ShaderOutput main(ShaderInput input) {
    ShaderOutput output; float4 p=float4(input.input_position_0,1.0);
    output.output_position_0=mul(p,g_World); output.output_color_0=input.input_color_0; output.output_texcoord_0=float4(input.input_texcoord_0,0,0);
    output.output_texcoord_1=normalize(mul(float4(input.input_normal_0,0),g_World));
    output.output_texcoord_2=normalize(mul(float4(input.input_tangent_0,0),g_World));
    output.output_texcoord_3=normalize(mul(float4(input.input_binormal_0,0),g_World)); return output;
}
