// Clean-room reconstruction from the shader behavior and declared interface.
struct ShaderInput {
    float4 input_position_0 : POSITION0;
    float4 input_color_0 : COLOR0;
    float4 input_texcoord_0 : TEXCOORD0;
    float4 input_texcoord_1 : TEXCOORD1;
    float4 input_texcoord_2 : TEXCOORD2;
    float4 input_texcoord_3 : TEXCOORD3;
};
struct ShaderOutput {
    float4 output_position_0 : POSITION0;
    float4 output_color_0 : COLOR0;
    float4 output_texcoord_0 : TEXCOORD0;
    float4 output_texcoord_1 : TEXCOORD1;
    float4 output_texcoord_2 : TEXCOORD2;
    float4 output_texcoord_3 : TEXCOORD3;
};
[maxvertexcount(1)] void main(point ShaderInput input[1], inout PointStream<ShaderOutput> stream) {
    ShaderOutput output;
    output.output_position_0 = input[0].input_position_0;
    output.output_color_0 = input[0].input_color_0;
    output.output_texcoord_0 = input[0].input_texcoord_0;
    output.output_texcoord_1 = input[0].input_texcoord_1;
    output.output_texcoord_2 = input[0].input_texcoord_2;
    output.output_texcoord_3 = input[0].input_texcoord_3;
    stream.Append(output);
}
