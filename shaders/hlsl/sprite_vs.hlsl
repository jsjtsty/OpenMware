// Primitive and textured-sprite vertex paths share colour overrides but differ in input z handling.
cbuffer ConstantBuffer : register(b0)
{
    bool g_UseColor_Override : packoffset(c0.x);
    float4 g_Color_Override : packoffset(c1.x);
    float g_Color_OverrideAlphaExponent : packoffset(c2.x);
    float4 g_ColorBalance : packoffset(c3.x);
    float4 g_Offset : packoffset(c4.x);
    float4x4 g_mT : packoffset(c5.x);
};
struct VSInput
{
    float4 position : POSITION0;
    float4 color : COLOR0;
#if !defined(VERTEX_PRIMITIVE)
    float2 uv : TEXCOORD0;
#endif
};
struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
#if !defined(VERTEX_PRIMITIVE)
    float2 uv : TEXCOORD0;
#endif
    float4 projective : TEXCOORD2;
};
VSOutput main(VSInput input)
{
    VSOutput output;
    float4 projectedInput=float4(input.position.xy+g_Offset.xy,input.position.zw);
    output.position=float4(projectedInput.xy,0.0,input.position.w);
    output.color=input.color*g_ColorBalance;
    if(g_UseColor_Override){
        output.color.rgb=g_Color_Override.rgb;
        output.color.a=exp2(log2(abs(input.color.a))*g_Color_OverrideAlphaExponent)*g_Color_Override.a;
    }
#if !defined(VERTEX_PRIMITIVE)
    projectedInput.z=0.0;
    output.uv=input.uv+g_Offset.zw;
#endif
    output.projective=mul(projectedInput,g_mT);
    return output;
}
