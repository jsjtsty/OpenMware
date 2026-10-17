// Sprite post effects of OpenMware (own implementation; the original's post-effect shaders are not used).
// Input is a premultiplied-alpha picture; the output is premultiplied as well.
//   FX_BLUR  : one axis of a gaussian blur   (g_Step = UV step of one texel along the axis, g_Sigma in texels)
//   FX_COLOR : hue/saturation/brightness/glow, sepia, negative/positive, monochrome with Cb/Cr tint

Texture2D Texture_Tex0 : register(t0);
SamplerState Sampler_Tex0 : register(s0);
// Effect parameters: a 4x1 float texture (fragment uniform buffers do not work through SDL_gpu's D3D12 path here).
Texture2D<float4> Texture_Params : register(t1);
SamplerState Sampler_Params : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 projective : TEXCOORD2;
};

// D3D12 only links a pixel shader whose input signature packs like the vertex shader's output, so every input has to
// be read (a dropped TEXCOORD2 shifts the registers of the others). Always true, and not foldable by the compiler.
void UseAllInputs(PSInput input)
{
    clip(input.color.a + 1000.0);
    clip(input.projective.w + 1000.0);
}

#if defined(FX_BLUR)

float4 main(PSInput input) : SV_Target
{
    UseAllInputs(input);
    float4 params = Texture_Params.Load(int3(0, 0, 0));
    float2 g_Step = params.xy;
    float g_Sigma = params.z;
    int radius = (int)min(ceil(g_Sigma * 3.0), 40.0);
    float4 sum = 0.0;
    float weights = 0.0;
    [loop] for (int i = -radius; i <= radius; i++)
    {
        float w = exp(-0.5 * (float)(i * i) / (g_Sigma * g_Sigma));
        sum += Texture_Tex0.Sample(Sampler_Tex0, input.uv + g_Step * (float)i) * w;
        weights += w;
    }
    return sum / weights;
}
#elif defined(FX_WARP)

// Ripple (0), lens (1), raster (2): the picture is resampled at a displaced position. All distances are in sprite pixels.
float4 main(PSInput input) : SV_Target
{
    UseAllInputs(input);
    float4 p0 = Texture_Params.Load(int3(0, 0, 0)); // kind, centre uv (x, y), unused
    float4 p1 = Texture_Params.Load(int3(1, 0, 0)); // power, frequency, phase, radius (lens) / angle (raster)
    float4 p2 = Texture_Params.Load(int3(2, 0, 0)); // sprite size in px
    float4 p3 = Texture_Params.Load(int3(3, 0, 0)); // uv bounds of the sprite: min x, min y, max x, max y
    float2 ext = p3.zw - p3.xy;
    float2 local = (input.uv - p3.xy) / ext * p2.xy;
    float2 centre = (p0.yz - p3.xy) / ext * p2.xy;
    const float kTau = 6.2831853;
    float2 offset = 0.0;
    if (p0.x < 0.5)
    {
        float2 delta = local - centre;
        float d = length(delta);
        float2 dir = d > 0.0 ? delta / d : float2(0.0, 0.0);
        offset = dir * (p1.x * 0.1) * sin(d * p1.y * kTau / 100000.0 - p1.z);
    }
    else if (p0.x < 1.5)
    {
        float2 delta = local - centre;
        float r = length(delta) / max(p1.w, 1.0);
        if (r < 1.0)
            offset = -delta * (p1.x / 1000.0) * (1.0 - r) * (1.0 - r);
    }
    else
    {
        float2 axis = float2(cos(p1.w), sin(p1.w));
        float s = dot(local, float2(-axis.y, axis.x));
        offset = axis * (p1.x * 0.1) * sin(s * p1.y * kTau / 100000.0 - p1.z);
    }
    return Texture_Tex0.Sample(Sampler_Tex0, input.uv + offset / p2.xy * ext);
}
#else

float4 main(PSInput input) : SV_Target
{
    UseAllInputs(input);
    float4 g_Hsv = Texture_Params.Load(int3(0, 0, 0));  // hue shift, saturation added, value added, glow added to the value
    float4 g_Mix = Texture_Params.Load(int3(1, 0, 0));  // sepia, nega, mono, unused
    float4 g_Mono = Texture_Params.Load(int3(2, 0, 0)); // Cb, Cr
    float4 c = Texture_Tex0.Sample(Sampler_Tex0, input.uv);
    float a = c.a;
    if (a <= 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);
    float3 rgb = saturate(c.rgb / a);

    if (g_Hsv.x != 0.0 || g_Hsv.y != 0.0 || g_Hsv.w != 0.0)
    {
        float hi = max(rgb.r, max(rgb.g, rgb.b)), lo = min(rgb.r, min(rgb.g, rgb.b));
        float delta = hi - lo;
        float hue = 0.0;
        if (delta > 0.0)
        {
            hue = rgb.r == hi ? (rgb.g - rgb.b) / delta : (rgb.g == hi ? 2.0 + (rgb.b - rgb.r) / delta : 4.0 + (rgb.r - rgb.g) / delta);
            hue = hue < 0.0 ? hue / 6.0 + 1.0 : hue / 6.0;
        }
        float sat = saturate((hi != 0.0 ? delta / hi : 0.0) + g_Hsv.y);
        float value = saturate(hi + g_Hsv.w);
        hue = frac(hue + g_Hsv.x);
        float sector = floor(hue * 6.0), f = hue * 6.0 - sector;
        float p = value * (1.0 - sat), q = value * (1.0 - sat * f), t = value * (1.0 - sat * (1.0 - f));
        if (sector < 1.0) rgb = float3(value, t, p);
        else if (sector < 2.0) rgb = float3(q, value, p);
        else if (sector < 3.0) rgb = float3(p, value, t);
        else if (sector < 4.0) rgb = float3(p, q, value);
        else if (sector < 5.0) rgb = float3(t, p, value);
        else rgb = float3(value, p, q);
    }
    rgb += g_Hsv.z;

    float gray = dot(rgb, float3(0.299, 0.587, 0.114));
    rgb = lerp(rgb, gray.xxx + float3(0.117647, 0.058824, -0.117647), g_Mix.x); // sepia
    float3 mono = gray.xxx + float3(1.402 * g_Mono.y, -0.344 * g_Mono.x - 0.714 * g_Mono.y, 1.772 * g_Mono.x);
    rgb = lerp(rgb, mono, g_Mix.z);
    rgb = rgb + g_Mix.y * (1.0 - 2.0 * rgb); // negative <-> positive
    return float4(saturate(rgb) * a, a);
}
#endif
