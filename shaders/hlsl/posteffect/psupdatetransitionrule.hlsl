// Clean-room reconstruction from the shader behavior and declared interface.
cbuffer ConstantBuffer : register(b0) {
    float g_TransitionNow : packoffset(c0.x);
    float g_TransitionDst : packoffset(c0.y);
    float g_TransitionSrc : packoffset(c0.z);
    float g_TransitionRate : packoffset(c0.w);
};
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
};
float4 main(ShaderInput input) : SV_Target0
{
    float slope=2.0/g_TransitionRate;
    float intercept=slope*g_TransitionNow;
    float edge=1.0-g_TransitionNow*(1.0/slope+1.0);
    float value=saturate(input.input_texcoord_0.x*(-slope*edge-intercept)+intercept);
    return float4(value,value,value,1.0);
}
