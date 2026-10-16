// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Albedo : register(s0);
Texture2D Texture_Albedo : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float4 g_Albedo : packoffset(c0.x);
    float4 g_SSS : packoffset(c1.x);
    float4 g_Material_1 : packoffset(c2.x);
    float4 g_Material_2 : packoffset(c3.x);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
    float4 input_texcoord_1 : TEXCOORD1;
    float4 input_texcoord_2 : TEXCOORD2;
    float4 input_texcoord_3 : TEXCOORD3;
    float4 input_texcoord_4 : TEXCOORD4;
};
struct ShaderOutput {
    float4 target_0 : SV_Target0;
    float4 target_1 : SV_Target1;
    float4 target_2 : SV_Target2;
    float4 target_3 : SV_Target3;
    float4 target_4 : SV_Target4;
    float4 target_5 : SV_Target5;
};
ShaderOutput main(ShaderInput input)
{
    ShaderOutput output;
    float4 albedo=Texture_Albedo.Sample(Sampler_Albedo,input.input_texcoord_0)*input.input_color_0;
    clip(albedo.a-0.996078431372549);
    output.target_0=albedo*g_Albedo;
    // The original normalizes all four basis components, including w.
    float4 n=normalize(input.input_texcoord_1);
    float4 b=normalize(input.input_texcoord_3);
    output.target_1=n*0.5+0.5;
    output.target_2=(input.input_texcoord_4.z/input.input_texcoord_4.w).xxxx;
    output.target_3=g_Material_1;
    output.target_4=g_SSS;
    output.target_5=float4(normalize(cross(n.xyz,b.xyz))*0.5+0.5,g_Material_2.w);
    return output;
}
