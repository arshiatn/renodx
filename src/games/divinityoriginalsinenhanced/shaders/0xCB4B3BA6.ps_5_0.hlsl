// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 29 14:36:24 2026

cbuffer _Globals : register(b0)
{
  float4 color : packoffset(c0);
}



// 3Dmigoto declarations
#define cmp -


void main(
  out float4 o0 : SV_Target0)
{
  o0.xyzw = color.xyzw;
  return;
}