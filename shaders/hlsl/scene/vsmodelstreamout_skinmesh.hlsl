// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer1 : register(b1) {
    float4x4 g_Matrix[256] : packoffset(c0.x);
    int g_BoneNum : packoffset(c1024.x);
};
struct ShaderInput {
    float3 input_position_0 : POSITION0;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
    float3 input_normal_0 : NORMAL0;
    float3 input_tangent_0 : TANGENT0;
    float3 input_binormal_0 : BINORMAL0;
    float4 input_blendweight_0 : BLENDWEIGHT0;
    uint4 input_blendindices_0 : BLENDINDICES0;
};
struct ShaderOutput {
    float4 output_position_0 : POSITION0;
    float4 output_color_0 : COLOR0;
    float4 output_texcoord_0 : TEXCOORD0;
    float4 output_texcoord_1 : TEXCOORD1;
    float4 output_texcoord_2 : TEXCOORD2;
    float4 output_texcoord_3 : TEXCOORD3;
};
ShaderOutput main(ShaderInput input)
{
    ShaderOutput output;
    float3 position=input.input_position_0;
    // g_BoneNum is an influence count (1..4). The final influence takes the residual weight.
    float4x4 skin=0.0; float used=0.0;
    [loop] for(int i=0;i<g_BoneNum-1;++i){
        float weight=input.input_blendweight_0[i];
        skin+=g_Matrix[input.input_blendindices_0[i]]*weight;
        used+=weight;
    }
    skin+=g_Matrix[input.input_blendindices_0[g_BoneNum-1]]*(1.0-used);
    output.output_position_0=mul(float4(position,1.0),skin);
    output.output_color_0=input.input_color_0;
    output.output_texcoord_0=float4(input.input_texcoord_0,0.0,0.0);
    output.output_texcoord_1=normalize(mul(float4(input.input_normal_0,0.0),skin));
    output.output_texcoord_2=normalize(mul(float4(input.input_tangent_0,0.0),skin));
    output.output_texcoord_3=normalize(mul(float4(input.input_binormal_0,0.0),skin));
    return output;
}
