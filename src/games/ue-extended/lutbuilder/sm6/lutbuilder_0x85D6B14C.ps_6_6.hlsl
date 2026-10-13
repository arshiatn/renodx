// Found in Gears of War E-Day; fixed sRGB output pixel LUT builder.
#include "../lutbuilderoutput.hlsli"

cbuffer cb0 : register(b0) {
  float cb0_015x : packoffset(c015.x);
  float cb0_015y : packoffset(c015.y);
  float cb0_015z : packoffset(c015.z);
  float cb0_015w : packoffset(c015.w);
  float cb0_016x : packoffset(c016.x);
  float cb0_016y : packoffset(c016.y);
  float cb0_016z : packoffset(c016.z);
  float cb0_017x : packoffset(c017.x);
  float cb0_017y : packoffset(c017.y);
  float cb0_017z : packoffset(c017.z);
  float cb0_017w : packoffset(c017.w);
  float cb0_018x : packoffset(c018.x);
  float cb0_018y : packoffset(c018.y);
  float cb0_018z : packoffset(c018.z);
  float cb0_018w : packoffset(c018.w);
  float cb0_019x : packoffset(c019.x);
  float cb0_019y : packoffset(c019.y);
  float cb0_019z : packoffset(c019.z);
  float cb0_019w : packoffset(c019.w);
  float cb0_020x : packoffset(c020.x);
  float cb0_020y : packoffset(c020.y);
  float cb0_020z : packoffset(c020.z);
  float cb0_020w : packoffset(c020.w);
  float cb0_021x : packoffset(c021.x);
  float cb0_021y : packoffset(c021.y);
  float cb0_021z : packoffset(c021.z);
  float cb0_021w : packoffset(c021.w);
  float cb0_022x : packoffset(c022.x);
  float cb0_022y : packoffset(c022.y);
  float cb0_022z : packoffset(c022.z);
  float cb0_022w : packoffset(c022.w);
  float cb0_023x : packoffset(c023.x);
  float cb0_023y : packoffset(c023.y);
  float cb0_023z : packoffset(c023.z);
  float cb0_023w : packoffset(c023.w);
  float cb0_024x : packoffset(c024.x);
  float cb0_024y : packoffset(c024.y);
  float cb0_024z : packoffset(c024.z);
  float cb0_024w : packoffset(c024.w);
  float cb0_025x : packoffset(c025.x);
  float cb0_025y : packoffset(c025.y);
  float cb0_025z : packoffset(c025.z);
  float cb0_025w : packoffset(c025.w);
  float cb0_026x : packoffset(c026.x);
  float cb0_026y : packoffset(c026.y);
  float cb0_026z : packoffset(c026.z);
  float cb0_026w : packoffset(c026.w);
  float cb0_027x : packoffset(c027.x);
  float cb0_027y : packoffset(c027.y);
  float cb0_027z : packoffset(c027.z);
  float cb0_027w : packoffset(c027.w);
  float cb0_028x : packoffset(c028.x);
  float cb0_028y : packoffset(c028.y);
  float cb0_028z : packoffset(c028.z);
  float cb0_028w : packoffset(c028.w);
  float cb0_029x : packoffset(c029.x);
  float cb0_029y : packoffset(c029.y);
  float cb0_029z : packoffset(c029.z);
  float cb0_029w : packoffset(c029.w);
  float cb0_030x : packoffset(c030.x);
  float cb0_030y : packoffset(c030.y);
  float cb0_030z : packoffset(c030.z);
  float cb0_030w : packoffset(c030.w);
  float cb0_031x : packoffset(c031.x);
  float cb0_031y : packoffset(c031.y);
  float cb0_031z : packoffset(c031.z);
  float cb0_031w : packoffset(c031.w);
  float cb0_032x : packoffset(c032.x);
  float cb0_032y : packoffset(c032.y);
  float cb0_032z : packoffset(c032.z);
  float cb0_032w : packoffset(c032.w);
  float cb0_033x : packoffset(c033.x);
  float cb0_033y : packoffset(c033.y);
  float cb0_033z : packoffset(c033.z);
  float cb0_033w : packoffset(c033.w);
  float cb0_034x : packoffset(c034.x);
  float cb0_034y : packoffset(c034.y);
  float cb0_034z : packoffset(c034.z);
  float cb0_034w : packoffset(c034.w);
  float cb0_035x : packoffset(c035.x);
  float cb0_035y : packoffset(c035.y);
  float cb0_035z : packoffset(c035.z);
  float cb0_035w : packoffset(c035.w);
  float cb0_036x : packoffset(c036.x);
  float cb0_036y : packoffset(c036.y);
  float cb0_036z : packoffset(c036.z);
  float cb0_036w : packoffset(c036.w);
  float cb0_037x : packoffset(c037.x);
  float cb0_037y : packoffset(c037.y);
  float cb0_037z : packoffset(c037.z);
  float cb0_037w : packoffset(c037.w);
  float cb0_038x : packoffset(c038.x);
  float cb0_038y : packoffset(c038.y);
  float cb0_038z : packoffset(c038.z);
  float cb0_038w : packoffset(c038.w);
  float cb0_039x : packoffset(c039.x);
  float cb0_039y : packoffset(c039.y);
  float cb0_039z : packoffset(c039.z);
  float cb0_039w : packoffset(c039.w);
  float cb0_040x : packoffset(c040.x);
  float cb0_040y : packoffset(c040.y);
  int cb0_040w : packoffset(c040.w);
  float cb0_042x : packoffset(c042.x);
  float cb0_042y : packoffset(c042.y);
  float cb0_042z : packoffset(c042.z);
  float cb0_043x : packoffset(c043.x);
  float cb0_043y : packoffset(c043.y);
  float cb0_043z : packoffset(c043.z);
  float cb0_044x : packoffset(c044.x);
  float cb0_044y : packoffset(c044.y);
  float cb0_044z : packoffset(c044.z);
  float cb0_050x : packoffset(c050.x);
  float cb0_050y : packoffset(c050.y);
  float cb0_050z : packoffset(c050.z);
  float cb0_052x : packoffset(c052.x);
  float cb0_052y : packoffset(c052.y);
  float cb0_052z : packoffset(c052.z);
  float cb0_053y : packoffset(c053.y);
  int cb0_054x : packoffset(c054.x);
};

cbuffer cb1 : register(b1) {
  float4 WorkingColorSpace_000[4] : packoffset(c000.x);
  float4 WorkingColorSpace_064[4] : packoffset(c004.x);
  float4 WorkingColorSpace_128[4] : packoffset(c008.x);
  float4 WorkingColorSpace_192[4] : packoffset(c012.x);
  float4 WorkingColorSpace_256[4] : packoffset(c016.x);
  float4 WorkingColorSpace_320[4] : packoffset(c020.x);
  int WorkingColorSpace_384 : packoffset(c024.x);
};

// DXIL FirstbitHi: returns bit position counting from MSB (leading zeros count)
uint firstbithigh_msb(int value) { return (value == 0) ? 0xFFFFFFFF : (31u - firstbithigh(value)); }
uint firstbithigh_msb(uint value) { return (value == 0) ? 0xFFFFFFFF : (31u - firstbithigh(value)); }

float4 main(
  noperspective float2 TEXCOORD : TEXCOORD,
  precise noperspective float4 SV_Position : SV_Position,
  nointerpolation uint SV_RenderTargetArrayIndex : SV_RenderTargetArrayIndex
) : SV_Target {
  float4 SV_Target;
  float _10;
  float _15;
  float _39;
  float _40;
  float _41;
  float _42;
  float _43;
  float _44;
  float _45;
  float _46;
  float _47;
  float _117;
  float _324;
  float _325;
  float _326;
  float _840;
  float _873;
  float _887;
  float _951;
  float _1211;
  float _1212;
  float _1213;
  float _1269;
  float _1280;
  float _1291;
  bool _28;
  float _60;
  float _61;
  float _62;
  bool _98;
  float _100;
  float _131;
  float _138;
  float _141;
  float _146;
  float _147;
  float _149;
  bool _150;
  float _159;
  float _161;
  float _168;
  float _170;
  float _172;
  float _173;
  float _176;
  float _179;
  float _184;
  float _190;
  float _191;
  float _192;
  float _193;
  float _194;
  float _195;
  float _196;
  float _197;
  float _200;
  float _201;
  float _202;
  float _205;
  float _224;
  float _225;
  float _226;
  float _227;
  float _228;
  float _229;
  float _230;
  float _231;
  float _232;
  float _235;
  float _238;
  float _241;
  float _244;
  float _247;
  float _250;
  float _253;
  float _256;
  float _259;
  float _262;
  float _265;
  float _268;
  float _271;
  float _274;
  float _277;
  float _280;
  float _283;
  float _286;
  float _341;
  float _344;
  float _347;
  float _348;
  float _352;
  float _353;
  float _354;
  float _366;
  float _394;
  float _395;
  float _396;
  float _397;
  float _411;
  float _425;
  float _439;
  float _453;
  float _467;
  float _471;
  float _472;
  float _473;
  float _530;
  float _534;
  float _535;
  float _544;
  float _553;
  float _562;
  float _571;
  float _580;
  float _643;
  float _647;
  float _656;
  float _665;
  float _674;
  float _683;
  float _692;
  float _750;
  float _761;
  float _763;
  float _765;
  float _780;
  float _781;
  float _782;
  float _785;
  float _788;
  float _791;
  float _795;
  float _800;
  float _813;
  float _814;
  float _815;
  float _816;
  float _820;
  float _831;
  float _841;
  float _842;
  float _843;
  float _844;
  float _851;
  float _854;
  float _856;
  bool _859;
  bool _860;
  bool _861;
  bool _862;
  float _878;
  float _891;
  float _895;
  float _901;
  float _911;
  float _912;
  float _913;
  float _914;
  float _929;
  float _931;
  float _933;
  float _942;
  float _954;
  float _956;
  float _960;
  float _961;
  float _962;
  float _966;
  float _967;
  float _968;
  float _969;
  float _971;
  float _972;
  float _973;
  float _974;
  float _993;
  float _995;
  float _1020;
  float _1021;
  float _1022;
  float _1029;
  float _1033;
  float _1034;
  float _1035;
  bool _1036;
  float _1040;
  float _1041;
  float _1042;
  float _1061;
  float _1062;
  float _1063;
  float _1064;
  float _1084;
  float _1085;
  float _1086;
  float _1102;
  float _1103;
  float _1104;
  float _1126;
  float _1127;
  float _1128;
  float _1154;
  float _1155;
  float _1156;
  float _1177;
  float _1178;
  float _1179;
  float _1194;
  float _1197;
  float _1200;
  float _1218;
  float _1227;
  float _1228;
  float _1229;
  float _1256;
  float _1257;
  float _1258;
  _10 = 0.5f / cb0_037x;
  _15 = cb0_037x + -1.0f;
  if (!(cb0_054x == 1)) {
    if (!(cb0_054x == 2)) {
      if (!(cb0_054x == 3)) {
        _28 = (cb0_054x == 4);
        _39 = select(_28, 1.0f, 1.705051064491272f);
        _40 = select(_28, 0.0f, -0.6217921376228333f);
        _41 = select(_28, 0.0f, -0.0832589864730835f);
        _42 = select(_28, 0.0f, -0.13025647401809692f);
        _43 = select(_28, 1.0f, 1.140804648399353f);
        _44 = select(_28, 0.0f, -0.010548308491706848f);
        _45 = select(_28, 0.0f, -0.024003351107239723f);
        _46 = select(_28, 0.0f, -0.1289689838886261f);
        _47 = select(_28, 1.0f, 1.1529725790023804f);
      } else {
        _39 = 0.6954522132873535f;
        _40 = 0.14067870378494263f;
        _41 = 0.16386906802654266f;
        _42 = 0.044794563204050064f;
        _43 = 0.8596711158752441f;
        _44 = 0.0955343171954155f;
        _45 = -0.005525882821530104f;
        _46 = 0.004025210160762072f;
        _47 = 1.0015007257461548f;
      }
    } else {
      _39 = 1.0258246660232544f;
      _40 = -0.020053181797266006f;
      _41 = -0.005771636962890625f;
      _42 = -0.002234415616840124f;
      _43 = 1.0045864582061768f;
      _44 = -0.002352118492126465f;
      _45 = -0.005013350863009691f;
      _46 = -0.025290070101618767f;
      _47 = 1.0303035974502563f;
    }
  } else {
    _39 = 1.3792141675949097f;
    _40 = -0.30886411666870117f;
    _41 = -0.0703500509262085f;
    _42 = -0.06933490186929703f;
    _43 = 1.08229660987854f;
    _44 = -0.012961871922016144f;
    _45 = -0.0021590073592960835f;
    _46 = -0.0454593189060688f;
    _47 = 1.0476183891296387f;
  }
  _60 = (exp2((((cb0_037x * (TEXCOORD.x - _10)) / _15) + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f;
  _61 = (exp2((((cb0_037x * (TEXCOORD.y - _10)) / _15) + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f;
  _62 = (exp2(((((float)((uint)(uint)(SV_RenderTargetArrayIndex))) / _15) + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f;
  [branch]
  if ((abs(cb0_037y + -6500.0f) > 9.99999993922529e-09f) | (abs(cb0_037z) > 9.99999993922529e-09f)) {
    _98 = (cb0_040w != 0);
    _100 = 0.9994439482688904f / cb0_037y;
    if (!((cb0_037y * 1.0005563497543335f) <= 7000.0f)) {
      _117 = (((((1901800.0f - (_100 * 2006400000.0f)) * _100) + 247.47999572753906f) * _100) + 0.23703999817371368f);
    } else {
      _117 = (((((2967800.0f - (_100 * 4607000064.0f)) * _100) + 99.11000061035156f) * _100) + 0.24406300485134125f);
    }
    _131 = ((((cb0_037y * 1.2864121856637212e-07f) + 0.00015411825734190643f) * cb0_037y) + 0.8601177334785461f) / ((((cb0_037y * 7.081451371959702e-07f) + 0.0008424202096648514f) * cb0_037y) + 1.0f);
    _138 = cb0_037y * cb0_037y;
    _141 = ((((cb0_037y * 4.204816761443908e-08f) + 4.228062607580796e-05f) * cb0_037y) + 0.31739872694015503f) / ((1.0f - (cb0_037y * 2.8974181986995973e-05f)) + (_138 * 1.6145605741257896e-07f));
    _146 = ((_131 * 2.0f) + 4.0f) - (_141 * 8.0f);
    _147 = (_131 * 3.0f) / _146;
    _149 = (_141 * 2.0f) / _146;
    _150 = (cb0_037y < 4000.0f);
    _159 = ((cb0_037y + 1189.6199951171875f) * cb0_037y) + 1412139.875f;
    _161 = ((-1137581184.0f - (cb0_037y * 1916156.25f)) - (_138 * 1.5317699909210205f)) / (_159 * _159);
    _168 = (6193636.0f - (cb0_037y * 179.45599365234375f)) + _138;
    _170 = ((1974715392.0f - (cb0_037y * 705674.0f)) - (_138 * 308.60699462890625f)) / (_168 * _168);
    _172 = rsqrt(dot(float2(_161, _170), float2(_161, _170)));
    _173 = cb0_037z * 0.05000000074505806f;
    _176 = ((_173 * _170) * _172) + _131;
    _179 = _141 - ((_173 * _161) * _172);
    _184 = (4.0f - (_179 * 8.0f)) + (_176 * 2.0f);
    _190 = (((_176 * 3.0f) / _184) - _147) + select(_150, _147, _117);
    _191 = (((_179 * 2.0f) / _184) - _149) + select(_150, _149, (((_117 * 2.869999885559082f) + -0.2750000059604645f) - ((_117 * _117) * 3.0f)));
    _192 = select(_98, _190, 0.3127000033855438f);
    _193 = select(_98, _191, 0.32899999618530273f);
    _194 = select(_98, 0.3127000033855438f, _190);
    _195 = select(_98, 0.32899999618530273f, _191);
    _196 = max(_193, 1.000000013351432e-10f);
    _197 = _192 / _196;
    _200 = ((1.0f - _192) - _193) / _196;
    _201 = max(_195, 1.000000013351432e-10f);
    _202 = _194 / _201;
    _205 = ((1.0f - _194) - _195) / _201;
    _224 = mad(-0.16140000522136688f, _205, ((_202 * 0.8950999975204468f) + 0.266400009393692f)) / mad(-0.16140000522136688f, _200, ((_197 * 0.8950999975204468f) + 0.266400009393692f));
    _225 = mad(0.03669999912381172f, _205, (1.7135000228881836f - (_202 * 0.7501999735832214f))) / mad(0.03669999912381172f, _200, (1.7135000228881836f - (_197 * 0.7501999735832214f)));
    _226 = mad(1.0296000242233276f, _205, ((_202 * 0.03889999911189079f) + -0.06849999725818634f)) / mad(1.0296000242233276f, _200, ((_197 * 0.03889999911189079f) + -0.06849999725818634f));
    _227 = mad(_225, -0.7501999735832214f, 0.0f);
    _228 = mad(_225, 1.7135000228881836f, 0.0f);
    _229 = mad(_225, 0.03669999912381172f, -0.0f);
    _230 = mad(_226, 0.03889999911189079f, 0.0f);
    _231 = mad(_226, -0.06849999725818634f, 0.0f);
    _232 = mad(_226, 1.0296000242233276f, 0.0f);
    _235 = mad(0.1599626988172531f, _230, mad(-0.1470542997121811f, _227, (_224 * 0.883457362651825f)));
    _238 = mad(0.1599626988172531f, _231, mad(-0.1470542997121811f, _228, (_224 * 0.26293492317199707f)));
    _241 = mad(0.1599626988172531f, _232, mad(-0.1470542997121811f, _229, (_224 * -0.15930065512657166f)));
    _244 = mad(0.04929120093584061f, _230, mad(0.5183603167533875f, _227, (_224 * 0.38695648312568665f)));
    _247 = mad(0.04929120093584061f, _231, mad(0.5183603167533875f, _228, (_224 * 0.11516613513231277f)));
    _250 = mad(0.04929120093584061f, _232, mad(0.5183603167533875f, _229, (_224 * -0.0697740763425827f)));
    _253 = mad(0.9684867262840271f, _230, mad(0.04004279896616936f, _227, (_224 * -0.007634039502590895f)));
    _256 = mad(0.9684867262840271f, _231, mad(0.04004279896616936f, _228, (_224 * -0.0022720457054674625f)));
    _259 = mad(0.9684867262840271f, _232, mad(0.04004279896616936f, _229, (_224 * 0.0013765322510153055f)));
    _262 = mad(_241, (WorkingColorSpace_000[2].x), mad(_238, (WorkingColorSpace_000[1].x), (_235 * (WorkingColorSpace_000[0].x))));
    _265 = mad(_241, (WorkingColorSpace_000[2].y), mad(_238, (WorkingColorSpace_000[1].y), (_235 * (WorkingColorSpace_000[0].y))));
    _268 = mad(_241, (WorkingColorSpace_000[2].z), mad(_238, (WorkingColorSpace_000[1].z), (_235 * (WorkingColorSpace_000[0].z))));
    _271 = mad(_250, (WorkingColorSpace_000[2].x), mad(_247, (WorkingColorSpace_000[1].x), (_244 * (WorkingColorSpace_000[0].x))));
    _274 = mad(_250, (WorkingColorSpace_000[2].y), mad(_247, (WorkingColorSpace_000[1].y), (_244 * (WorkingColorSpace_000[0].y))));
    _277 = mad(_250, (WorkingColorSpace_000[2].z), mad(_247, (WorkingColorSpace_000[1].z), (_244 * (WorkingColorSpace_000[0].z))));
    _280 = mad(_259, (WorkingColorSpace_000[2].x), mad(_256, (WorkingColorSpace_000[1].x), (_253 * (WorkingColorSpace_000[0].x))));
    _283 = mad(_259, (WorkingColorSpace_000[2].y), mad(_256, (WorkingColorSpace_000[1].y), (_253 * (WorkingColorSpace_000[0].y))));
    _286 = mad(_259, (WorkingColorSpace_000[2].z), mad(_256, (WorkingColorSpace_000[1].z), (_253 * (WorkingColorSpace_000[0].z))));
    _324 = mad(mad((WorkingColorSpace_064[0].z), _286, mad((WorkingColorSpace_064[0].y), _277, (_268 * (WorkingColorSpace_064[0].x)))), _62, mad(mad((WorkingColorSpace_064[0].z), _283, mad((WorkingColorSpace_064[0].y), _274, (_265 * (WorkingColorSpace_064[0].x)))), _61, (mad((WorkingColorSpace_064[0].z), _280, mad((WorkingColorSpace_064[0].y), _271, (_262 * (WorkingColorSpace_064[0].x)))) * _60)));
    _325 = mad(mad((WorkingColorSpace_064[1].z), _286, mad((WorkingColorSpace_064[1].y), _277, (_268 * (WorkingColorSpace_064[1].x)))), _62, mad(mad((WorkingColorSpace_064[1].z), _283, mad((WorkingColorSpace_064[1].y), _274, (_265 * (WorkingColorSpace_064[1].x)))), _61, (mad((WorkingColorSpace_064[1].z), _280, mad((WorkingColorSpace_064[1].y), _271, (_262 * (WorkingColorSpace_064[1].x)))) * _60)));
    _326 = mad(mad((WorkingColorSpace_064[2].z), _286, mad((WorkingColorSpace_064[2].y), _277, (_268 * (WorkingColorSpace_064[2].x)))), _62, mad(mad((WorkingColorSpace_064[2].z), _283, mad((WorkingColorSpace_064[2].y), _274, (_265 * (WorkingColorSpace_064[2].x)))), _61, (mad((WorkingColorSpace_064[2].z), _280, mad((WorkingColorSpace_064[2].y), _271, (_262 * (WorkingColorSpace_064[2].x)))) * _60)));
  } else {
    _324 = _60;
    _325 = _61;
    _326 = _62;
  }
  _341 = mad((WorkingColorSpace_128[0].z), _326, mad((WorkingColorSpace_128[0].y), _325, ((WorkingColorSpace_128[0].x) * _324)));
  _344 = mad((WorkingColorSpace_128[1].z), _326, mad((WorkingColorSpace_128[1].y), _325, ((WorkingColorSpace_128[1].x) * _324)));
  _347 = mad((WorkingColorSpace_128[2].z), _326, mad((WorkingColorSpace_128[2].y), _325, ((WorkingColorSpace_128[2].x) * _324)));
  _348 = dot(float3(_341, _344, _347), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _352 = (_341 / _348) + -1.0f;
  _353 = (_344 / _348) + -1.0f;
  _354 = (_347 / _348) + -1.0f;
  // ExpandGamut set to 0
  // _366 = (1.0f - exp2(((_348 * _348) * -4.0f) * cb0_038w)) * (1.0f - exp2(dot(float3(_352, _353, _354), float3(_352, _353, _354)) * -4.0f));
  _366 = (1.0f - exp2(((_348 * _348) * -4.0f) * 0.f)) * (1.0f - exp2(dot(float3(_352, _353, _354), float3(_352, _353, _354)) * -4.0f));
  _394 = ((mad(cb0_042z, _347, mad(cb0_042y, _344, (cb0_042x * _341))) - _341) * _366) + _341;
  _395 = ((mad(cb0_043z, _347, mad(cb0_043y, _344, (cb0_043x * _341))) - _344) * _366) + _344;
  _396 = ((mad(cb0_044z, _347, mad(cb0_044y, _344, (cb0_044x * _341))) - _347) * _366) + _347;
  _397 = dot(float3(_394, _395, _396), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _411 = cb0_021w + cb0_026w;
  _425 = cb0_020w * cb0_025w;
  _439 = cb0_019w * cb0_024w;
  _453 = cb0_018w * cb0_023w;
  _467 = cb0_017w * cb0_022w;
  _471 = _394 - _397;
  _472 = _395 - _397;
  _473 = _396 - _397;
  _530 = saturate(_397 / cb0_037w);
  _534 = (_530 * _530) * (3.0f - (_530 * 2.0f));
  _535 = 1.0f - _534;
  _544 = cb0_021w + cb0_036w;
  _553 = cb0_020w * cb0_035w;
  _562 = cb0_019w * cb0_034w;
  _571 = cb0_018w * cb0_033w;
  _580 = cb0_017w * cb0_032w;
  _643 = saturate((_397 - cb0_038x) / (cb0_038y - cb0_038x));
  _647 = (_643 * _643) * (3.0f - (_643 * 2.0f));
  _656 = cb0_021w + cb0_031w;
  _665 = cb0_020w * cb0_030w;
  _674 = cb0_019w * cb0_029w;
  _683 = cb0_018w * cb0_028w;
  _692 = cb0_017w * cb0_027w;
  _750 = _534 - _647;
  _761 = ((_647 * (((cb0_021x + cb0_036x) + _544) + (((cb0_020x * cb0_035x) * _553) * exp2(log2(exp2(((cb0_018x * cb0_033x) * _571) * log2(max(0.0f, ((((cb0_017x * cb0_032x) * _580) * _471) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_034x) * _562)))))) + (_535 * (((cb0_021x + cb0_026x) + _411) + (((cb0_020x * cb0_025x) * _425) * exp2(log2(exp2(((cb0_018x * cb0_023x) * _453) * log2(max(0.0f, ((((cb0_017x * cb0_022x) * _467) * _471) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_024x) * _439))))))) + ((((cb0_021x + cb0_031x) + _656) + (((cb0_020x * cb0_030x) * _665) * exp2(log2(exp2(((cb0_018x * cb0_028x) * _683) * log2(max(0.0f, ((((cb0_017x * cb0_027x) * _692) * _471) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_029x) * _674))))) * _750);
  _763 = ((_647 * (((cb0_021y + cb0_036y) + _544) + (((cb0_020y * cb0_035y) * _553) * exp2(log2(exp2(((cb0_018y * cb0_033y) * _571) * log2(max(0.0f, ((((cb0_017y * cb0_032y) * _580) * _472) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_034y) * _562)))))) + (_535 * (((cb0_021y + cb0_026y) + _411) + (((cb0_020y * cb0_025y) * _425) * exp2(log2(exp2(((cb0_018y * cb0_023y) * _453) * log2(max(0.0f, ((((cb0_017y * cb0_022y) * _467) * _472) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_024y) * _439))))))) + ((((cb0_021y + cb0_031y) + _656) + (((cb0_020y * cb0_030y) * _665) * exp2(log2(exp2(((cb0_018y * cb0_028y) * _683) * log2(max(0.0f, ((((cb0_017y * cb0_027y) * _692) * _472) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_029y) * _674))))) * _750);
  _765 = ((_647 * (((cb0_021z + cb0_036z) + _544) + (((cb0_020z * cb0_035z) * _553) * exp2(log2(exp2(((cb0_018z * cb0_033z) * _571) * log2(max(0.0f, ((((cb0_017z * cb0_032z) * _580) * _473) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_034z) * _562)))))) + (_535 * (((cb0_021z + cb0_026z) + _411) + (((cb0_020z * cb0_025z) * _425) * exp2(log2(exp2(((cb0_018z * cb0_023z) * _453) * log2(max(0.0f, ((((cb0_017z * cb0_022z) * _467) * _473) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_024z) * _439))))))) + ((((cb0_021z + cb0_031z) + _656) + (((cb0_020z * cb0_030z) * _665) * exp2(log2(exp2(((cb0_018z * cb0_028z) * _683) * log2(max(0.0f, ((((cb0_017z * cb0_027z) * _692) * _473) + _397)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_029z) * _674))))) * _750);
  // 85D6B14C: _761/_763/_765 are graded linear AP1, before the blue matrix.
  // Film c039/c040, tone c039.x, blue c038.z, polynomial c052,
  // scale c016, overlay c015 are independently traced in the original tail.
  // No external SDR LUTs or output-device field; the tail encodes sRGB.
  UECbufferConfig cb_config = CreateCbufferConfig();
  cb_config.ue_filmblackclip = cb0_040x;
  cb_config.ue_filmtoe = cb0_039z;
  cb_config.ue_filmshoulder = cb0_039w;
  cb_config.ue_filmslope = cb0_039y;
  cb_config.ue_filmwhiteclip = cb0_040y;
  cb_config.ue_tonecurveammount = cb0_039x;
  cb_config.ue_mappingpolynomial = float3(cb0_052x, cb0_052y, cb0_052z);
  cb_config.ue_colorscale = float3(cb0_016x, cb0_016y, cb0_016z);
  cb_config.ue_overlaycolor = float4(cb0_015x, cb0_015y, cb0_015z, cb0_015w);
  cb_config.ue_bluecorrection = cb0_038z;
  SV_Target = ProcessLutbuilder(
      float3(_761, _763, _765), cb_config, float4(0.f, 0.f, 0.f, 0.f), 0u);
  return SV_Target;

  _780 = ((mad(0.061360642313957214f, _765, mad(-4.540197551250458e-09f, _763, (_761 * 0.9386394023895264f))) - _761) * cb0_038z) + _761;
  _781 = ((mad(0.169205904006958f, _765, mad(0.8307942152023315f, _763, (_761 * 6.775371730327606e-08f))) - _763) * cb0_038z) + _763;
  _782 = (mad(-2.3283064365386963e-10f, _763, (_761 * -9.313225746154785e-10f)) * cb0_038z) + _765;
  _785 = mad(0.16386905312538147f, _782, mad(0.14067868888378143f, _781, (_780 * 0.6954522132873535f)));
  _788 = mad(0.0955343246459961f, _782, mad(0.8596711158752441f, _781, (_780 * 0.044794581830501556f)));
  _791 = mad(1.0015007257461548f, _782, mad(0.004025210160762072f, _781, (_780 * -0.005525882821530104f)));
  _795 = max(max(_785, _788), _791);
  _800 = (max(_795, 1.000000013351432e-10f) - max(min(min(_785, _788), _791), 1.000000013351432e-10f)) / max(_795, 0.009999999776482582f);
  _813 = ((_788 + _785) + _791) + (sqrt((((_791 - _788) * _791) + ((_788 - _785) * _788)) + ((_785 - _791) * _785)) * 1.75f);
  _814 = _813 * 0.3333333432674408f;
  _815 = _800 + -0.4000000059604645f;
  _816 = _815 * 5.0f;
  _820 = max((1.0f - abs(_815 * 2.5f)), 0.0f);
  _831 = ((float((int)(((int)(uint)((int)(_816 > 0.0f))) - ((int)(uint)((int)(_816 < 0.0f))))) * (1.0f - (_820 * _820))) + 1.0f) * 0.02500000037252903f;
  if (!(_814 <= 0.0533333346247673f)) {
    if (!(_814 >= 0.1599999964237213f)) {
      _840 = (((0.23999999463558197f / _813) + -0.5f) * _831);
    } else {
      _840 = 0.0f;
    }
  } else {
    _840 = _831;
  }
  _841 = _840 + 1.0f;
  _842 = _841 * _785;
  _843 = _841 * _788;
  _844 = _841 * _791;
  if (!((_842 == _843) && (_843 == _844))) {
    _851 = ((_842 * 2.0f) - _843) - _844;
    _854 = ((_788 - _791) * 1.7320507764816284f) * _841;
    _856 = atan(_854 / _851);
    _859 = (_851 < 0.0f);
    _860 = (_851 == 0.0f);
    _861 = (_854 >= 0.0f);
    _862 = (_854 < 0.0f);
    _873 = select((_861 && _860), 90.0f, select((_862 && _860), -90.0f, (select((_862 && _859), (_856 + -3.1415927410125732f), select((_861 && _859), (_856 + 3.1415927410125732f), _856)) * 57.2957763671875f)));
  } else {
    _873 = 0.0f;
  }
  _878 = min(max(select((_873 < 0.0f), (_873 + 360.0f), _873), 0.0f), 360.0f);
  if (_878 < -180.0f) {
    _887 = (_878 + 360.0f);
  } else {
    if (_878 > 180.0f) {
      _887 = (_878 + -360.0f);
    } else {
      _887 = _878;
    }
  }
  _891 = saturate(1.0f - abs(_887 * 0.014814814552664757f));
  _895 = (_891 * _891) * (3.0f - (_891 * 2.0f));
  _901 = ((_895 * _895) * ((_800 * 0.18000000715255737f) * (0.029999999329447746f - _842))) + _842;
  _911 = max(0.0f, mad(-0.21492856740951538f, _844, mad(-0.2365107536315918f, _843, (_901 * 1.4514392614364624f))));
  _912 = max(0.0f, mad(-0.09967592358589172f, _844, mad(1.17622971534729f, _843, (_901 * -0.07655377686023712f))));
  _913 = max(0.0f, mad(0.9977163076400757f, _844, mad(-0.006032449658960104f, _843, (_901 * 0.008316148072481155f))));
  _914 = dot(float3(_911, _912, _913), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _929 = (cb0_040x + 1.0f) - cb0_039z;
  _931 = cb0_040y + 1.0f;
  _933 = _931 - cb0_039w;
  if (cb0_039z > 0.800000011920929f) {
    _951 = (((0.8199999928474426f - cb0_039z) / cb0_039y) + -0.7447274923324585f);
  } else {
    _942 = (cb0_040x + 0.18000000715255737f) / _929;
    _951 = (-0.7447274923324585f - ((log2(_942 / (2.0f - _942)) * 0.3465735912322998f) * (_929 / cb0_039y)));
  }
  _954 = ((1.0f - cb0_039z) / cb0_039y) - _951;
  _956 = (cb0_039w / cb0_039y) - _954;
  _960 = log2(lerp(_914, _911, 0.9599999785423279f)) * 0.3010300099849701f;
  _961 = log2(lerp(_914, _912, 0.9599999785423279f)) * 0.3010300099849701f;
  _962 = log2(lerp(_914, _913, 0.9599999785423279f)) * 0.3010300099849701f;
  _966 = cb0_039y * (_960 + _954);
  _967 = cb0_039y * (_961 + _954);
  _968 = cb0_039y * (_962 + _954);
  _969 = _929 * 2.0f;
  _971 = (cb0_039y * -2.0f) / _929;
  _972 = _960 - _951;
  _973 = _961 - _951;
  _974 = _962 - _951;
  _993 = _933 * 2.0f;
  _995 = (cb0_039y * 2.0f) / _933;
  _1020 = select((_960 < _951), ((_969 / (exp2((_972 * 1.4426950216293335f) * _971) + 1.0f)) - cb0_040x), _966);
  _1021 = select((_961 < _951), ((_969 / (exp2((_973 * 1.4426950216293335f) * _971) + 1.0f)) - cb0_040x), _967);
  _1022 = select((_962 < _951), ((_969 / (exp2((_974 * 1.4426950216293335f) * _971) + 1.0f)) - cb0_040x), _968);
  _1029 = _956 - _951;
  _1033 = saturate(_972 / _1029);
  _1034 = saturate(_973 / _1029);
  _1035 = saturate(_974 / _1029);
  _1036 = (_956 < _951);
  _1040 = select(_1036, (1.0f - _1033), _1033);
  _1041 = select(_1036, (1.0f - _1034), _1034);
  _1042 = select(_1036, (1.0f - _1035), _1035);
  _1061 = (((_1040 * _1040) * (select((_960 > _956), (_931 - (_993 / (exp2(((_960 - _956) * 1.4426950216293335f) * _995) + 1.0f))), _966) - _1020)) * (3.0f - (_1040 * 2.0f))) + _1020;
  _1062 = (((_1041 * _1041) * (select((_961 > _956), (_931 - (_993 / (exp2(((_961 - _956) * 1.4426950216293335f) * _995) + 1.0f))), _967) - _1021)) * (3.0f - (_1041 * 2.0f))) + _1021;
  _1063 = (((_1042 * _1042) * (select((_962 > _956), (_931 - (_993 / (exp2(((_962 - _956) * 1.4426950216293335f) * _995) + 1.0f))), _968) - _1022)) * (3.0f - (_1042 * 2.0f))) + _1022;
  _1064 = dot(float3(_1061, _1062, _1063), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _1084 = (cb0_039x * (max(0.0f, (lerp(_1064, _1061, 0.9300000071525574f))) - _780)) + _780;
  _1085 = (cb0_039x * (max(0.0f, (lerp(_1064, _1062, 0.9300000071525574f))) - _781)) + _781;
  _1086 = (cb0_039x * (max(0.0f, (lerp(_1064, _1063, 0.9300000071525574f))) - _782)) + _782;
  _1102 = ((mad(-0.06537103652954102f, _1086, mad(1.451815478503704e-06f, _1085, (_1084 * 1.065374732017517f))) - _1084) * cb0_038z) + _1084;
  _1103 = ((mad(-0.20366770029067993f, _1086, mad(1.2036634683609009f, _1085, (_1084 * -2.57161445915699e-07f))) - _1085) * cb0_038z) + _1085;
  _1104 = ((mad(0.9999996423721313f, _1086, mad(2.0954757928848267e-08f, _1085, (_1084 * 1.862645149230957e-08f))) - _1086) * cb0_038z) + _1086;
  _1126 = max(0.0f, mad((WorkingColorSpace_192[0].z), _1104, mad((WorkingColorSpace_192[0].y), _1103, ((WorkingColorSpace_192[0].x) * _1102))));
  _1127 = max(0.0f, mad((WorkingColorSpace_192[1].z), _1104, mad((WorkingColorSpace_192[1].y), _1103, ((WorkingColorSpace_192[1].x) * _1102))));
  _1128 = max(0.0f, mad((WorkingColorSpace_192[2].z), _1104, mad((WorkingColorSpace_192[2].y), _1103, ((WorkingColorSpace_192[2].x) * _1102))));
  _1154 = cb0_016x * (((cb0_052y + (cb0_052x * _1126)) * _1126) + cb0_052z);
  _1155 = cb0_016y * (((cb0_052y + (cb0_052x * _1127)) * _1127) + cb0_052z);
  _1156 = cb0_016z * (((cb0_052y + (cb0_052x * _1128)) * _1128) + cb0_052z);
  _1177 = exp2(log2(max(0.0f, (lerp(_1154, cb0_015x, cb0_015w)))) * cb0_053y);
  _1178 = exp2(log2(max(0.0f, (lerp(_1155, cb0_015y, cb0_015w)))) * cb0_053y);
  _1179 = exp2(log2(max(0.0f, (lerp(_1156, cb0_015z, cb0_015w)))) * cb0_053y);
  if (WorkingColorSpace_384 == 0) {
    _1194 = mad((WorkingColorSpace_128[0].z), _1179, mad((WorkingColorSpace_128[0].y), _1178, ((WorkingColorSpace_128[0].x) * _1177)));
    _1197 = mad((WorkingColorSpace_128[1].z), _1179, mad((WorkingColorSpace_128[1].y), _1178, ((WorkingColorSpace_128[1].x) * _1177)));
    _1200 = mad((WorkingColorSpace_128[2].z), _1179, mad((WorkingColorSpace_128[2].y), _1178, ((WorkingColorSpace_128[2].x) * _1177)));
    _1211 = mad(_41, _1200, mad(_40, _1197, (_1194 * _39)));
    _1212 = mad(_44, _1200, mad(_43, _1197, (_1194 * _42)));
    _1213 = mad(_47, _1200, mad(_46, _1197, (_1194 * _45)));
  } else {
    _1211 = _1177;
    _1212 = _1178;
    _1213 = _1179;
  }
  _1218 = ((_1212 * 0.7152000069618225f) + (_1211 * 0.2125999927520752f)) + (_1213 * 0.0722000002861023f);
  _1227 = ((_1211 - _1218) * cb0_050z) + _1218;
  _1228 = ((_1212 - _1218) * cb0_050z) + _1218;
  _1229 = ((_1213 - _1218) * cb0_050z) + _1218;
  _1256 = cb0_050x * max((((((_1227 * _1227) * (3.0f - (_1227 * 2.0f))) - _1227) * cb0_050y) + _1227), 0.0f);
  _1257 = cb0_050x * max((((((_1228 * _1228) * (3.0f - (_1228 * 2.0f))) - _1228) * cb0_050y) + _1228), 0.0f);
  _1258 = cb0_050x * max((((((_1229 * _1229) * (3.0f - (_1229 * 2.0f))) - _1229) * cb0_050y) + _1229), 0.0f);
  if (_1256 < 0.0031306699384003878f) {
    _1269 = (_1256 * 12.920000076293945f);
  } else {
    _1269 = (((pow(_1256, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
  }
  if (_1257 < 0.0031306699384003878f) {
    _1280 = (_1257 * 12.920000076293945f);
  } else {
    _1280 = (((pow(_1257, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
  }
  if (_1258 < 0.0031306699384003878f) {
    _1291 = (_1258 * 12.920000076293945f);
  } else {
    _1291 = (((pow(_1258, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
  }
  SV_Target.x = (_1269 * 0.9523810148239136f);
  SV_Target.y = (_1280 * 0.9523810148239136f);
  SV_Target.z = (_1291 * 0.9523810148239136f);
  SV_Target.w = 0.0f;
  return SV_Target;
}