// Clean-room reconstruction from the shader behavior and declared interface.
SamplerState Sampler_Albedo : register(s0);
Texture2D Texture_Albedo : register(t0);
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float2 input_texcoord_0 : TEXCOORD0;
    float4 input_texcoord_1 : TEXCOORD1;
};
float4 main(ShaderInput input) : SV_Target0 {
    float4 color=Texture_Albedo.Sample(Sampler_Albedo,input.input_texcoord_0)*input.input_color_0;
    clip(color.a-0.00390625); return color;
}
