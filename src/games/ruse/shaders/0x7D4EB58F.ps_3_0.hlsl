sampler2D uniAuto_OARTexture0 : register(s0);

float2 uniHDRParam  : register(c0);
float3 uniHazeColor : register(c1);

struct PS_INPUT
{
    float2 texCoord   : TEXCOORD0;
    half4  color      : COLOR0;
    float  hazeAmount : TEXCOORD3;
};

float4 main(PS_INPUT input) : COLOR0
{
    float4 texColor = tex2D(uniAuto_OARTexture0, input.texCoord);

    half3 baseColor =
        (texColor.rgb - 0.05h) *
        input.color.rgb;

    float4 result;

    result.rgb =
        lerp(baseColor, uniHazeColor, input.hazeAmount) *
        uniHDRParam.y;

    result.a = texColor.a * input.color.a * 10.f;

    return result;
}