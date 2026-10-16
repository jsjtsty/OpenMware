// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float4x4 g_World : packoffset(c0.x);
    float4x4 g_View : packoffset(c4.x);
    float4x4 g_Proj : packoffset(c8.x);
    float4x4 g_LinearProj : packoffset(c12.x);
};
struct ShaderInput {
    float4 input_position_0 : POSITION0;
    float4 input_color_0 : COLOR0;
    float4 input_texcoord_0 : TEXCOORD0;
    float4 input_normal_0 : NORMAL0;
    float4 input_tangent_0 : TANGENT0;
    float4 input_binormal_0 : BINORMAL0;
};
struct ShaderOutput {
    float4 output_sv_position_0 : SV_Position;
    float4 output_color_0 : COLOR0;
    float4 output_texcoord_0 : TEXCOORD0;
    float4 output_texcoord_1 : TEXCOORD1;
    float4 output_texcoord_2 : TEXCOORD2;
    float4 output_texcoord_3 : TEXCOORD3;
    float4 output_texcoord_4 : TEXCOORD4;
};
ShaderOutput main(ShaderInput input) {
    ShaderOutput output;
    float4 viewPos=mul(input.input_position_0,g_View); output.output_sv_position_0=mul(viewPos,g_Proj);
    output.output_color_0=input.input_color_0; output.output_texcoord_0=input.input_texcoord_0;
    output.output_texcoord_1=normalize(mul(float4(input.input_normal_0.xyz,0),g_View));
    output.output_texcoord_2=normalize(mul(float4(input.input_tangent_0.xyz,0),g_View));
    output.output_texcoord_3=normalize(mul(float4(input.input_binormal_0.xyz,0),g_View));
    float4 linearPos=mul(viewPos,g_LinearProj); output.output_texcoord_4=float4(linearPos.xy,linearPos.z*linearPos.w,linearPos.w); return output;
}
