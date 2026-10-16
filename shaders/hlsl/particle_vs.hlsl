cbuffer ConstantBuffer : register(b0)
{
    float4x4 g_mWVP;
    float4x4 g_mWVP_Linear;
    float4x4 g_mT;
};
struct VSInput
{
    float3 position : POSITION0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 viewDepth : TEXCOORD1;
    float4 projective : TEXCOORD2;
};
VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPosition = float4(input.position, 1.0);
    output.position = mul(localPosition, g_mWVP);
    output.color = input.color;
    output.uv = input.uv;
    output.viewDepth = mul(localPosition, g_mWVP_Linear);
    output.projective = mul(output.position, g_mT);
    return output;
}
