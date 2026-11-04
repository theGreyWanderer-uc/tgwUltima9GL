sampler2D input : register(s0);

float4 main(float2 uv : TEXCOORD) : COLOR
{
    float4 c = tex2D(input, uv);
    c.rgb = min(c.rgb * (255.0 / 250.0), c.a);
    float mn = min(c.r, min(c.g, c.b));
    return float4(c.rgb - mn, c.a - mn);
}
