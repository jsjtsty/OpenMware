// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Tex0 : register(s0);
Texture2D Texture0 : register(t0);
cbuffer ConstantBuffer : register(b0) {
    float g_Shift_Hue : packoffset(c0.x);
    float g_Shift_Saturation : packoffset(c0.y);
    float g_Shift_Value : packoffset(c0.z);
    float g_Shift_Glow : packoffset(c0.w);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float4 color=Texture0.Sample(Sampler_Tex0,input.input_texcoord_0);
    float hi=max(color.r,max(color.g,color.b)), lo=min(color.r,min(color.g,color.b));
    float delta=hi-lo;
    float hue= color.r==hi ? (color.g-color.b)/delta : (color.g==hi ? 2.0+(color.b-color.r)/delta : 4.0+(color.r-color.g)/delta);
    hue=hue<0.0 ? hue/6.0+1.0 : hue/6.0;
    hue=saturate(hue); float saturation=saturate(hi!=0.0 ? delta/hi : 0.0);
    saturation=saturate(saturation+g_Shift_Saturation);
    float value=saturate(hi)+color.a*g_Shift_Glow;
    float3 rgb;
    if(saturation==0.0) rgb=value.xxx;
    else {
        hue+=g_Shift_Hue;
        if(hue>=1.0) hue-=1.0; else if(hue<=0.0) hue+=1.0;
        if(hue>=1.0) hue-=1.0;
        float sector=floor(hue*6.0), f=hue*6.0-sector;
        float p=value*(1.0-saturation), q=value*(1.0-saturation*f), t=value*(1.0-saturation*(1.0-f));
        if(sector<1.0) rgb=float3(value,t,p);
        else if(sector<2.0) rgb=float3(q,value,p);
        else if(sector<3.0) rgb=float3(p,value,t);
        else if(sector<4.0) rgb=float3(p,q,value);
        else if(sector<5.0) rgb=float3(t,p,value);
        else rgb=float3(value,p,q);
    }
    return float4(rgb+g_Shift_Value,color.a);
}
