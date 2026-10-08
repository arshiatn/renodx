// Ryse: Son of Rome sun shafts mask. From 0x4DFAE399.ps_5_0.cso (3Dmigoto).
// Stock: sky depth (t0.x) x 1.32 x (r + g + b) of the scene (t1), alpha 1 - depth, into an RGBA8 target that clamped
// it to 0-1. The addon upgrades that target to FP16 (it has the back buffer's aspect), so the mask passed 1 far
// (scene x 3.96) and the shafts grew about 4x and blinding, also in SDR. Saturated here, as the RGBA8 target did.

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyz = _tex1.Sample(_tex1_s, v1.xy).xyz;
  r0.x = dot(r0.xyz, float3(1.32000005,1.32000005,1.32000005));
  r0.y = _tex0.Sample(_tex0_s, v1.xy).x;
  o0.xyz = saturate(r0.yyy * r0.xxx);
  o0.w = saturate(1 + -r0.y);
}
