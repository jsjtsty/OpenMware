// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Albedo : register(s0);
SamplerState Sampler_Fresnel : register(s3);
Texture2D Texture_Albedo : register(t0);
Texture2D Texture_Fresnel : register(t3);
cbuffer ConstantBuffer : register(b0) {
    float4 g_Albedo : packoffset(c0.x);
    float4 g_SSS : packoffset(c1.x);
    float4 g_Material_1 : packoffset(c2.x);
    float4 g_Material_2 : packoffset(c3.x);
    float4 g_LightDirection : packoffset(c4.x);
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
// Material x is the original roughness parameter; do not renormalize the supplied light.
float4 main(ShaderInput input) : SV_Target0
{
    float3 position=input.input_texcoord_4.xyz;
    float3 view=normalize(-position);
    float3 light=g_LightDirection.xyz;
    float3 halfVector=normalize(view+light);
    float3 normal=normalize(input.input_texcoord_1.xyz);
    float vh=saturate(dot(view,halfVector));
    float reflectance=Texture_Fresnel.Sample(Sampler_Fresnel,input.input_texcoord_0).b*g_Material_1.z;
    float fresnel=(1.0-reflectance)*exp2(-8.35*vh)+reflectance;
    float nh=dot(normal,halfVector), lh=dot(halfVector,light);
    float rough=g_Material_1.x, rough2=rough*rough;
    float distributionDenominator=nh*nh*(rough2*rough2-1.0)+1.0;
    float distribution=(rough2*rough2)/(distributionDenominator*distributionDenominator*3.141592653589793);
    float nv=dot(normal,view), nl=dot(normal,light);
    float nvSat=saturate(nv), nlSat=saturate(nl);
    float k=(rough+1.0)*(rough+1.0)*0.125;
    float geometry=(nvSat*nlSat)/((nlSat*(1.0-k)+k)*(nvSat*(1.0-k)+k));
    float specular=geometry*(distribution*fresnel)*0.25;
    float grazingV=saturate(1.0-nv), grazingL=saturate(1.0-nl);
    float v2=grazingV*grazingV, l2=grazingL*grazingL;
    float v5=grazingV*(v2*v2), l5=grazingL*(l2*l2);
    float fd=lh*lh*rough;
    float diffuse=(fd*((fd-1.0)*(v5*l5)+v5+l5)+(1.0-0.5*l5)*(1.0-0.5*v5))*0.318309886183791;
    float4 base=Texture_Albedo.Sample(Sampler_Albedo,input.input_texcoord_0)*input.input_color_0;
    base.rgb*=g_Albedo.rgb;
    float3 result=base.rgb*diffuse+specular;
    // Two exponential lobes approximate subsurface scattering along the light direction.
    if(g_Material_1.w>0.0){
        float distance=abs(dot(-light,position-0.1*normal)-dot(-light,position))*(0.0825/g_SSS.w);
        float distance2=distance*distance;
        float3 lobes=exp2((-distance2/(3.0*g_SSS.rgb))*1.442695040888963)+exp2((-distance2/g_SSS.rgb)*1.442695040888963);
        float3 subsurface=base.rgb*lobes;
        subsurface*=0.039788735772974;
        subsurface*=saturate(dot(light,-normal)+0.3);
        subsurface*=g_Material_1.w;
        result+=subsurface;
    }
    return float4(result*3.141592653589793,base.a);
}
