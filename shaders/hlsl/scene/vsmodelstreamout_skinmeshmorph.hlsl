// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer1 : register(b1) {
    float4x4 g_Matrix[256] : packoffset(c0.x);
    int g_BoneNum : packoffset(c1024.x);
};
cbuffer ConstantBuffer2 : register(b2) {
    int4 g_MorphID : packoffset(c0.x);
    float g_MorphingWeight : packoffset(c1.x);
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
    float3 input_position_1 : POSITION1;
    float3 input_normal_1 : NORMAL1;
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
    // The two morph IDs select original/target/zero, using the first two columns of the identity table.
    float3 sources[4]={input.input_position_0,input.input_position_1,float3(0,0,0),float3(0,0,0)};
    position=lerp(sources[g_MorphID.x],sources[g_MorphID.y],g_MorphingWeight);
    // NORMAL1 is declared in the original input signature but is not read by the shader.
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
