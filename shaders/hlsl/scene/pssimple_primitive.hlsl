// Clean-room reconstruction from the shader behavior and declared interface.
struct ShaderInput {
    float4 input_sv_position_0 : SV_Position;
    float4 input_color_0 : COLOR0;
    float4 input_texcoord_1 : TEXCOORD1;
};
float4 main(ShaderInput input) : SV_Target0 {
    return input.input_color_0;
}
