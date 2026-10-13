// Found in Gears of War E-Day; native HDR scene/UI composite.
#include "./composite.hlsli"

Texture2D<float4> t0 : register(t0);

Texture2D<float4> t1 : register(t1);

cbuffer cb0 : register(b0) {
  float cb0_001z : packoffset(c001.z);
  float cb0_001w : packoffset(c001.w);
  float cb0_003x : packoffset(c003.x);
  float cb0_003y : packoffset(c003.y);
  float cb0_003z : packoffset(c003.z);
  float cb0_003w : packoffset(c003.w);
};

SamplerState s0 : register(s0);

SamplerState s1 : register(s1);

// DXIL FirstbitHi: returns bit position counting from MSB (leading zeros count)
uint firstbithigh_msb(int value) { return (value == 0) ? 0xFFFFFFFF : (31u - firstbithigh(value)); }
uint firstbithigh_msb(uint value) { return (value == 0) ? 0xFFFFFFFF : (31u - firstbithigh(value)); }

float4 main(
  noperspective float2 TEXCOORD : TEXCOORD,
  precise noperspective float4 SV_Position : SV_Position
) : SV_Target {
  float4 SV_Target;
  float4 _11;
  float _40;
  float _41;
  float _42;
  float _54;
  float _55;
  float _56;
  float _114;
  float _115;
  float _116;
  float _179;
  float _180;
  float _181;
  float _63;
  float _64;
  float _65;
  float _70;
  float _78;
  float _79;
  float _80;
  float4 _119;
  float _129;
  float _130;
  float _131;
  float _156;
  float _157;
  float _158;
  float _164;
  float _165;
  float _166;
  float _174;
  float _182;
  float _201;
  float _202;
  float _203;
  _11 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y));
  _119 = t1.Sample(s1, float2(TEXCOORD.x, TEXCOORD.y));

  // Pass normalized encoded BT.709 UI before the native HDR brightness scale.
  if (HandleUICompositing(_11, _119, SV_Target, TEXCOORD.xy, t1, s1)) {
    return SV_Target;
  }

  // Original native composite retained below the shared helper return.
  _40 = select((_11.x > 0.040449999272823334f), exp2(log2((abs(_11.x) * 0.9478672742843628f) + 0.05213269963860512f) * 2.4000000953674316f), (_11.x * 0.07739938050508499f));
  _41 = select((_11.y > 0.040449999272823334f), exp2(log2((abs(_11.y) * 0.9478672742843628f) + 0.05213269963860512f) * 2.4000000953674316f), (_11.y * 0.07739938050508499f));
  _42 = select((_11.z > 0.040449999272823334f), exp2(log2((abs(_11.z) * 0.9478672742843628f) + 0.05213269963860512f) * 2.4000000953674316f), (_11.z * 0.07739938050508499f));
  _54 = cb0_001w * mad(0.043313056230545044f, _42, mad(0.3292830288410187f, _41, (_40 * 0.6274039149284363f)));
  _55 = cb0_001w * mad(0.011362319812178612f, _42, mad(0.919540286064148f, _41, (_40 * 0.06909731030464172f)));
  _56 = cb0_001w * mad(0.8955953121185303f, _42, mad(0.08801331371068954f, _41, (_40 * 0.016391439363360405f)));
  if (((_54 * _55) * _56) > 0.0f) {
    _63 = cb0_003z * _54;
    _64 = cb0_003z * _55;
    _65 = cb0_003z * _56;
    _70 = ((_63 * 0.2125999927520752f) + (_64 * 0.7152000069618225f)) + (_65 * 0.0722000002861023f);
    _78 = ((_63 - _70) * cb0_003y) + _70;
    _79 = ((_64 - _70) * cb0_003y) + _70;
    _80 = ((_65 - _70) * cb0_003y) + _70;
    _114 = ((cb0_003x * max((((((_78 * _78) * (3.0f - (_78 * 2.0f))) - _78) * cb0_003w) + _78), 0.0f)) * cb0_001w);
    _115 = ((cb0_003x * max((((((_79 * _79) * (3.0f - (_79 * 2.0f))) - _79) * cb0_003w) + _79), 0.0f)) * cb0_001w);
    _116 = ((cb0_003x * max((((((_80 * _80) * (3.0f - (_80 * 2.0f))) - _80) * cb0_003w) + _80), 0.0f)) * cb0_001w);
  } else {
    _114 = _54;
    _115 = _55;
    _116 = _56;
  }
  _129 = (pow(_119.x, 0.012683313339948654f));
  _130 = (pow(_119.y, 0.012683313339948654f));
  _131 = (pow(_119.z, 0.012683313339948654f));
  _156 = exp2(log2(max(0.0f, (_129 + -0.8359375f)) / (18.8515625f - (_129 * 18.6875f))) * 6.277394771575928f) * 10000.0f;
  _157 = exp2(log2(max(0.0f, (_130 + -0.8359375f)) / (18.8515625f - (_130 * 18.6875f))) * 6.277394771575928f) * 10000.0f;
  _158 = exp2(log2(max(0.0f, (_131 + -0.8359375f)) / (18.8515625f - (_131 * 18.6875f))) * 6.277394771575928f) * 10000.0f;
  if ((_11.w > 0.0f) && (_11.w < 1.0f)) {
    _164 = max(_156, 0.0f);
    _165 = max(_157, 0.0f);
    _166 = max(_158, 0.0f);
    _174 = ((((1.0f / ((dot(float3(_164, _165, _166), float3(0.26269999146461487f, 0.6779999732971191f, 0.059300001710653305f)) / cb0_001z) + 1.0f)) * cb0_001z) + -1.0f) * _11.w) + 1.0f;
    _179 = (_174 * _164);
    _180 = (_174 * _165);
    _181 = (_174 * _166);
  } else {
    _179 = _156;
    _180 = _157;
    _181 = _158;
  }
  _182 = 1.0f - _11.w;
  _201 = exp2(log2(((_179 * _182) + (cb0_001z * _114)) * 9.999999747378752e-05f) * 0.1593017578125f);
  _202 = exp2(log2(((_180 * _182) + (cb0_001z * _115)) * 9.999999747378752e-05f) * 0.1593017578125f);
  _203 = exp2(log2(((_181 * _182) + (cb0_001z * _116)) * 9.999999747378752e-05f) * 0.1593017578125f);
  SV_Target.x = exp2(log2((1.0f / ((_201 * 18.6875f) + 1.0f)) * ((_201 * 18.8515625f) + 0.8359375f)) * 78.84375f);
  SV_Target.y = exp2(log2((1.0f / ((_202 * 18.6875f) + 1.0f)) * ((_202 * 18.8515625f) + 0.8359375f)) * 78.84375f);
  SV_Target.z = exp2(log2((1.0f / ((_203 * 18.6875f) + 1.0f)) * ((_203 * 18.8515625f) + 0.8359375f)) * 78.84375f);
  SV_Target.w = _11.w;
  return SV_Target;
}