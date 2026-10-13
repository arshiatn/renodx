// Found in Gears of War E-Day; native HDR/PQ and SDR LUT builder.
#include "../lutbuilderoutput.hlsli"

RWTexture3D<float4> u0 : register(u0);

cbuffer cb0 : register(b0) {
  float cb0_008x : packoffset(c008.x);
  float cb0_008y : packoffset(c008.y);
  float cb0_008z : packoffset(c008.z);
  float cb0_008w : packoffset(c008.w);
  float cb0_009x : packoffset(c009.x);
  float cb0_010x : packoffset(c010.x);
  float cb0_010y : packoffset(c010.y);
  float cb0_010z : packoffset(c010.z);
  float cb0_010w : packoffset(c010.w);
  float cb0_011x : packoffset(c011.x);
  float cb0_011y : packoffset(c011.y);
  float cb0_011z : packoffset(c011.z);
  float cb0_011w : packoffset(c011.w);
  float cb0_012x : packoffset(c012.x);
  float cb0_012y : packoffset(c012.y);
  float cb0_012z : packoffset(c012.z);
  float cb0_012w : packoffset(c012.w);
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
  float cb0_041x : packoffset(c041.x);
  float cb0_041y : packoffset(c041.y);
  float cb0_042x : packoffset(c042.x);
  float cb0_042y : packoffset(c042.y);
  float cb0_042z : packoffset(c042.z);
  float cb0_043x : packoffset(c043.x);
  float cb0_043y : packoffset(c043.y);
  float cb0_043z : packoffset(c043.z);
  float cb0_044x : packoffset(c044.x);
  float cb0_044y : packoffset(c044.y);
  float cb0_044z : packoffset(c044.z);
  float cb0_046x : packoffset(c046.x);
  float cb0_046y : packoffset(c046.y);
  float cb0_046z : packoffset(c046.z);
  float cb0_047x : packoffset(c047.x);
  float cb0_047y : packoffset(c047.y);
  float cb0_047z : packoffset(c047.z);
  float cb0_048x : packoffset(c048.x);
  float cb0_048y : packoffset(c048.y);
  float cb0_048z : packoffset(c048.z);
  float cb0_050x : packoffset(c050.x);
  float cb0_050y : packoffset(c050.y);
  float cb0_050z : packoffset(c050.z);
  float cb0_050w : packoffset(c050.w);
  float cb0_051x : packoffset(c051.x);
  float cb0_051y : packoffset(c051.y);
  float cb0_052x : packoffset(c052.x);
  float cb0_052y : packoffset(c052.y);
  float cb0_052z : packoffset(c052.z);
  float cb0_053y : packoffset(c053.y);
  float cb0_053z : packoffset(c053.z);
  int cb0_053w : packoffset(c053.w);
  int cb0_054x : packoffset(c054.x);
  float cb0_055x : packoffset(c055.x);
  float cb0_055y : packoffset(c055.y);
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

[numthreads(8, 8, 8)]
void main(
  uint3 SV_DispatchThreadID : SV_DispatchThreadID,
  uint3 SV_GroupID : SV_GroupID,
  uint3 SV_GroupThreadID : SV_GroupThreadID,
  uint SV_GroupIndex : SV_GroupIndex
) {
  float _31;
  float _36;
  float _37;
  float _38;
  float _40;
  float _60;
  float _61;
  float _62;
  float _63;
  float _64;
  float _65;
  float _66;
  float _67;
  float _68;
  float _126;
  float _127;
  float _128;
  float _183;
  float _390;
  float _391;
  float _392;
  float _965;
  float _998;
  float _1012;
  float _1076;
  float _1346;
  float _1347;
  float _1348;
  float _1404;
  float _1415;
  float _1584;
  float _1599;
  float _1614;
  float _1622;
  float _1623;
  float _1624;
  float _1691;
  float _1724;
  float _1738;
  float _1777;
  float _1899;
  float _1979;
  float _2053;
  float _2282;
  float _2297;
  float _2312;
  float _2320;
  float _2321;
  float _2322;
  float _2389;
  float _2422;
  float _2436;
  float _2475;
  float _2597;
  float _2683;
  float _2769;
  float _2984;
  float _2985;
  float _2986;
  bool _49;
  float _79;
  float _80;
  float _81;
  bool _164;
  float _166;
  float _197;
  float _204;
  float _207;
  float _212;
  float _213;
  float _215;
  bool _216;
  float _225;
  float _227;
  float _234;
  float _236;
  float _238;
  float _239;
  float _242;
  float _245;
  float _250;
  float _256;
  float _257;
  float _258;
  float _259;
  float _260;
  float _261;
  float _262;
  float _263;
  float _266;
  float _267;
  float _268;
  float _271;
  float _290;
  float _291;
  float _292;
  float _293;
  float _294;
  float _295;
  float _296;
  float _297;
  float _298;
  float _301;
  float _304;
  float _307;
  float _310;
  float _313;
  float _316;
  float _319;
  float _322;
  float _325;
  float _328;
  float _331;
  float _334;
  float _337;
  float _340;
  float _343;
  float _346;
  float _349;
  float _352;
  float _407;
  float _410;
  float _413;
  float _414;
  float _418;
  float _419;
  float _420;
  float _432;
  float _460;
  float _461;
  float _462;
  float _463;
  float _477;
  float _491;
  float _505;
  float _519;
  float _533;
  float _537;
  float _538;
  float _539;
  float _596;
  float _600;
  float _601;
  float _610;
  float _619;
  float _628;
  float _637;
  float _646;
  float _709;
  float _713;
  float _722;
  float _731;
  float _740;
  float _749;
  float _758;
  float _816;
  float _827;
  float _829;
  float _831;
  float _839;
  float _867;
  float _868;
  float _869;
  float _905;
  float _906;
  float _907;
  float _910;
  float _913;
  float _916;
  float _920;
  float _925;
  float _938;
  float _939;
  float _940;
  float _941;
  float _945;
  float _956;
  float _966;
  float _967;
  float _968;
  float _969;
  float _976;
  float _979;
  float _981;
  bool _984;
  bool _985;
  bool _986;
  bool _987;
  float _1003;
  float _1016;
  float _1020;
  float _1026;
  float _1036;
  float _1037;
  float _1038;
  float _1039;
  float _1054;
  float _1056;
  float _1058;
  float _1067;
  float _1079;
  float _1081;
  float _1085;
  float _1086;
  float _1087;
  float _1091;
  float _1092;
  float _1093;
  float _1094;
  float _1096;
  float _1097;
  float _1098;
  float _1099;
  float _1118;
  float _1120;
  float _1145;
  float _1146;
  float _1147;
  float _1154;
  float _1158;
  float _1159;
  float _1160;
  bool _1161;
  float _1165;
  float _1166;
  float _1167;
  float _1186;
  float _1187;
  float _1188;
  float _1189;
  float _1209;
  float _1210;
  float _1211;
  float _1229;
  float _1230;
  float _1231;
  float _1241;
  float _1242;
  float _1243;
  float _1269;
  float _1270;
  float _1271;
  float _1278;
  float _1279;
  float _1280;
  float _1281;
  float _1282;
  float _1283;
  float _1290;
  float _1291;
  float _1292;
  float _1304;
  float _1305;
  float _1306;
  float _1329;
  float _1332;
  float _1335;
  float _1353;
  float _1362;
  float _1363;
  float _1364;
  float _1391;
  float _1392;
  float _1393;
  float _1442;
  float _1445;
  float _1448;
  float _1451;
  float _1454;
  float _1457;
  float _1532;
  float _1533;
  float _1534;
  float _1537;
  float _1540;
  float _1543;
  float _1546;
  float _1549;
  float _1552;
  float _1554;
  float _1564;
  float _1565;
  float _1567;
  float _1569;
  float _1572;
  float _1587;
  float _1602;
  float _1640;
  float _1641;
  float _1642;
  float _1646;
  float _1651;
  float _1664;
  float _1665;
  float _1666;
  float _1667;
  float _1671;
  float _1682;
  float _1692;
  float _1693;
  float _1694;
  float _1695;
  float _1702;
  float _1705;
  float _1707;
  bool _1710;
  bool _1711;
  bool _1712;
  bool _1713;
  float _1729;
  float _1744;
  int _1745;
  float _1747;
  float _1748;
  float _1749;
  float _1786;
  float _1787;
  float _1788;
  float _1801;
  float _1802;
  float _1803;
  float _1804;
  float _1827;
  float _1828;
  float _1829;
  float _1830;
  float _1837;
  float _1838;
  float _1846;
  int _1847;
  float _1849;
  float _1851;
  float _1854;
  float _1859;
  float _1868;
  float _1876;
  int _1877;
  float _1879;
  float _1881;
  float _1884;
  float _1889;
  float _1909;
  float _1910;
  float _1917;
  float _1918;
  float _1926;
  int _1927;
  float _1929;
  float _1931;
  float _1934;
  float _1939;
  float _1948;
  float _1956;
  int _1957;
  float _1959;
  float _1961;
  float _1964;
  float _1969;
  float _1983;
  float _1984;
  float _1991;
  float _1992;
  float _2000;
  int _2001;
  float _2003;
  float _2005;
  float _2008;
  float _2013;
  float _2022;
  float _2030;
  int _2031;
  float _2033;
  float _2035;
  float _2038;
  float _2043;
  float _2057;
  float _2058;
  float _2060;
  float _2062;
  float _2065;
  float _2068;
  float _2071;
  float _2084;
  float _2085;
  float _2086;
  float _2089;
  float _2092;
  float _2095;
  float _2117;
  float _2118;
  float _2119;
  float _2122;
  float _2125;
  float _2128;
  float _2133;
  float _2143;
  float _2162;
  float _2163;
  float _2164;
  float _2230;
  float _2231;
  float _2232;
  float _2235;
  float _2238;
  float _2241;
  float _2244;
  float _2247;
  float _2250;
  float _2252;
  float _2262;
  float _2263;
  float _2265;
  float _2267;
  float _2270;
  float _2285;
  float _2300;
  float _2338;
  float _2339;
  float _2340;
  float _2344;
  float _2349;
  float _2362;
  float _2363;
  float _2364;
  float _2365;
  float _2369;
  float _2380;
  float _2390;
  float _2391;
  float _2392;
  float _2393;
  float _2400;
  float _2403;
  float _2405;
  bool _2408;
  bool _2409;
  bool _2410;
  bool _2411;
  float _2427;
  float _2442;
  int _2443;
  float _2445;
  float _2446;
  float _2447;
  float _2484;
  float _2485;
  float _2486;
  float _2499;
  float _2500;
  float _2501;
  float _2502;
  float _2525;
  float _2526;
  float _2527;
  float _2528;
  float _2535;
  float _2536;
  float _2544;
  int _2545;
  float _2547;
  float _2549;
  float _2552;
  float _2557;
  float _2566;
  float _2574;
  int _2575;
  float _2577;
  float _2579;
  float _2582;
  float _2587;
  float _2613;
  float _2614;
  float _2621;
  float _2622;
  float _2630;
  int _2631;
  float _2633;
  float _2635;
  float _2638;
  float _2643;
  float _2652;
  float _2660;
  int _2661;
  float _2663;
  float _2665;
  float _2668;
  float _2673;
  float _2699;
  float _2700;
  float _2707;
  float _2708;
  float _2716;
  int _2717;
  float _2719;
  float _2721;
  float _2724;
  float _2729;
  float _2738;
  float _2746;
  int _2747;
  float _2749;
  float _2751;
  float _2754;
  float _2759;
  float _2773;
  float _2774;
  float _2776;
  float _2778;
  float _2781;
  float _2784;
  float _2787;
  float _2800;
  float _2801;
  float _2802;
  float _2805;
  float _2808;
  float _2811;
  float _2833;
  float _2836;
  float _2837;
  float _2864;
  float _2867;
  float _2870;
  float _2889;
  float _2890;
  float _2891;
  float _2938;
  float _2941;
  float _2944;
  float _2957;
  float _2960;
  float _2963;
  float _9[6];
  float _10[6];
  float _11[6];
  float _12[6];
  float _13[6];
  float _14[6];
  float _15[6];
  float _16[6];
  float _17[6];
  float _18[6];
  float _19[6];
  _31 = 0.5f / cb0_037x;
  _36 = cb0_037x + -1.0f;
  _37 = (cb0_037x * ((cb0_055x * (((float)((uint)SV_DispatchThreadID.x)) + 0.5f)) - _31)) / _36;
  _38 = (cb0_037x * ((cb0_055y * (((float)((uint)SV_DispatchThreadID.y)) + 0.5f)) - _31)) / _36;
  _40 = ((float)((uint)SV_DispatchThreadID.z)) / _36;
  if (!(cb0_054x == 1)) {
    if (!(cb0_054x == 2)) {
      if (!(cb0_054x == 3)) {
        _49 = (cb0_054x == 4);
        _60 = select(_49, 1.0f, 1.705051064491272f);
        _61 = select(_49, 0.0f, -0.6217921376228333f);
        _62 = select(_49, 0.0f, -0.0832589864730835f);
        _63 = select(_49, 0.0f, -0.13025647401809692f);
        _64 = select(_49, 1.0f, 1.140804648399353f);
        _65 = select(_49, 0.0f, -0.010548308491706848f);
        _66 = select(_49, 0.0f, -0.024003351107239723f);
        _67 = select(_49, 0.0f, -0.1289689838886261f);
        _68 = select(_49, 1.0f, 1.1529725790023804f);
      } else {
        _60 = 0.6954522132873535f;
        _61 = 0.14067870378494263f;
        _62 = 0.16386906802654266f;
        _63 = 0.044794563204050064f;
        _64 = 0.8596711158752441f;
        _65 = 0.0955343171954155f;
        _66 = -0.005525882821530104f;
        _67 = 0.004025210160762072f;
        _68 = 1.0015007257461548f;
      }
    } else {
      _60 = 1.0258246660232544f;
      _61 = -0.020053181797266006f;
      _62 = -0.005771636962890625f;
      _63 = -0.002234415616840124f;
      _64 = 1.0045864582061768f;
      _65 = -0.002352118492126465f;
      _66 = -0.005013350863009691f;
      _67 = -0.025290070101618767f;
      _68 = 1.0303035974502563f;
    }
  } else {
    _60 = 1.3792141675949097f;
    _61 = -0.30886411666870117f;
    _62 = -0.0703500509262085f;
    _63 = -0.06933490186929703f;
    _64 = 1.08229660987854f;
    _65 = -0.012961871922016144f;
    _66 = -0.0021590073592960835f;
    _67 = -0.0454593189060688f;
    _68 = 1.0476183891296387f;
  }
  [branch]
  if ((uint)cb0_053w > (uint)2) {
    _79 = (pow(_37, 0.012683313339948654f));
    _80 = (pow(_38, 0.012683313339948654f));
    _81 = (pow(_40, 0.012683313339948654f));
    _126 = (exp2(log2(max(0.0f, (_79 + -0.8359375f)) / (18.8515625f - (_79 * 18.6875f))) * 6.277394771575928f) * 100.0f);
    _127 = (exp2(log2(max(0.0f, (_80 + -0.8359375f)) / (18.8515625f - (_80 * 18.6875f))) * 6.277394771575928f) * 100.0f);
    _128 = (exp2(log2(max(0.0f, (_81 + -0.8359375f)) / (18.8515625f - (_81 * 18.6875f))) * 6.277394771575928f) * 100.0f);
  } else {
    _126 = ((exp2((_37 + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f);
    _127 = ((exp2((_38 + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f);
    _128 = ((exp2((_40 + -0.4340175986289978f) * 14.0f) * 0.18000000715255737f) + -0.002667719265446067f);
  }
  [branch]
  if ((abs(cb0_037y + -6500.0f) > 9.99999993922529e-09f) | (abs(cb0_037z) > 9.99999993922529e-09f)) {
    _164 = (cb0_040w != 0);
    _166 = 0.9994439482688904f / cb0_037y;
    if (!((cb0_037y * 1.0005563497543335f) <= 7000.0f)) {
      _183 = (((((1901800.0f - (_166 * 2006400000.0f)) * _166) + 247.47999572753906f) * _166) + 0.23703999817371368f);
    } else {
      _183 = (((((2967800.0f - (_166 * 4607000064.0f)) * _166) + 99.11000061035156f) * _166) + 0.24406300485134125f);
    }
    _197 = ((((cb0_037y * 1.2864121856637212e-07f) + 0.00015411825734190643f) * cb0_037y) + 0.8601177334785461f) / ((((cb0_037y * 7.081451371959702e-07f) + 0.0008424202096648514f) * cb0_037y) + 1.0f);
    _204 = cb0_037y * cb0_037y;
    _207 = ((((cb0_037y * 4.204816761443908e-08f) + 4.228062607580796e-05f) * cb0_037y) + 0.31739872694015503f) / ((1.0f - (cb0_037y * 2.8974181986995973e-05f)) + (_204 * 1.6145605741257896e-07f));
    _212 = ((_197 * 2.0f) + 4.0f) - (_207 * 8.0f);
    _213 = (_197 * 3.0f) / _212;
    _215 = (_207 * 2.0f) / _212;
    _216 = (cb0_037y < 4000.0f);
    _225 = ((cb0_037y + 1189.6199951171875f) * cb0_037y) + 1412139.875f;
    _227 = ((-1137581184.0f - (cb0_037y * 1916156.25f)) - (_204 * 1.5317699909210205f)) / (_225 * _225);
    _234 = (6193636.0f - (cb0_037y * 179.45599365234375f)) + _204;
    _236 = ((1974715392.0f - (cb0_037y * 705674.0f)) - (_204 * 308.60699462890625f)) / (_234 * _234);
    _238 = rsqrt(dot(float2(_227, _236), float2(_227, _236)));
    _239 = cb0_037z * 0.05000000074505806f;
    _242 = ((_239 * _236) * _238) + _197;
    _245 = _207 - ((_239 * _227) * _238);
    _250 = (4.0f - (_245 * 8.0f)) + (_242 * 2.0f);
    _256 = (((_242 * 3.0f) / _250) - _213) + select(_216, _213, _183);
    _257 = (((_245 * 2.0f) / _250) - _215) + select(_216, _215, (((_183 * 2.869999885559082f) + -0.2750000059604645f) - ((_183 * _183) * 3.0f)));
    _258 = select(_164, _256, 0.3127000033855438f);
    _259 = select(_164, _257, 0.32899999618530273f);
    _260 = select(_164, 0.3127000033855438f, _256);
    _261 = select(_164, 0.32899999618530273f, _257);
    _262 = max(_259, 1.000000013351432e-10f);
    _263 = _258 / _262;
    _266 = ((1.0f - _258) - _259) / _262;
    _267 = max(_261, 1.000000013351432e-10f);
    _268 = _260 / _267;
    _271 = ((1.0f - _260) - _261) / _267;
    _290 = mad(-0.16140000522136688f, _271, ((_268 * 0.8950999975204468f) + 0.266400009393692f)) / mad(-0.16140000522136688f, _266, ((_263 * 0.8950999975204468f) + 0.266400009393692f));
    _291 = mad(0.03669999912381172f, _271, (1.7135000228881836f - (_268 * 0.7501999735832214f))) / mad(0.03669999912381172f, _266, (1.7135000228881836f - (_263 * 0.7501999735832214f)));
    _292 = mad(1.0296000242233276f, _271, ((_268 * 0.03889999911189079f) + -0.06849999725818634f)) / mad(1.0296000242233276f, _266, ((_263 * 0.03889999911189079f) + -0.06849999725818634f));
    _293 = mad(_291, -0.7501999735832214f, 0.0f);
    _294 = mad(_291, 1.7135000228881836f, 0.0f);
    _295 = mad(_291, 0.03669999912381172f, -0.0f);
    _296 = mad(_292, 0.03889999911189079f, 0.0f);
    _297 = mad(_292, -0.06849999725818634f, 0.0f);
    _298 = mad(_292, 1.0296000242233276f, 0.0f);
    _301 = mad(0.1599626988172531f, _296, mad(-0.1470542997121811f, _293, (_290 * 0.883457362651825f)));
    _304 = mad(0.1599626988172531f, _297, mad(-0.1470542997121811f, _294, (_290 * 0.26293492317199707f)));
    _307 = mad(0.1599626988172531f, _298, mad(-0.1470542997121811f, _295, (_290 * -0.15930065512657166f)));
    _310 = mad(0.04929120093584061f, _296, mad(0.5183603167533875f, _293, (_290 * 0.38695648312568665f)));
    _313 = mad(0.04929120093584061f, _297, mad(0.5183603167533875f, _294, (_290 * 0.11516613513231277f)));
    _316 = mad(0.04929120093584061f, _298, mad(0.5183603167533875f, _295, (_290 * -0.0697740763425827f)));
    _319 = mad(0.9684867262840271f, _296, mad(0.04004279896616936f, _293, (_290 * -0.007634039502590895f)));
    _322 = mad(0.9684867262840271f, _297, mad(0.04004279896616936f, _294, (_290 * -0.0022720457054674625f)));
    _325 = mad(0.9684867262840271f, _298, mad(0.04004279896616936f, _295, (_290 * 0.0013765322510153055f)));
    _328 = mad(_307, (WorkingColorSpace_000[2].x), mad(_304, (WorkingColorSpace_000[1].x), (_301 * (WorkingColorSpace_000[0].x))));
    _331 = mad(_307, (WorkingColorSpace_000[2].y), mad(_304, (WorkingColorSpace_000[1].y), (_301 * (WorkingColorSpace_000[0].y))));
    _334 = mad(_307, (WorkingColorSpace_000[2].z), mad(_304, (WorkingColorSpace_000[1].z), (_301 * (WorkingColorSpace_000[0].z))));
    _337 = mad(_316, (WorkingColorSpace_000[2].x), mad(_313, (WorkingColorSpace_000[1].x), (_310 * (WorkingColorSpace_000[0].x))));
    _340 = mad(_316, (WorkingColorSpace_000[2].y), mad(_313, (WorkingColorSpace_000[1].y), (_310 * (WorkingColorSpace_000[0].y))));
    _343 = mad(_316, (WorkingColorSpace_000[2].z), mad(_313, (WorkingColorSpace_000[1].z), (_310 * (WorkingColorSpace_000[0].z))));
    _346 = mad(_325, (WorkingColorSpace_000[2].x), mad(_322, (WorkingColorSpace_000[1].x), (_319 * (WorkingColorSpace_000[0].x))));
    _349 = mad(_325, (WorkingColorSpace_000[2].y), mad(_322, (WorkingColorSpace_000[1].y), (_319 * (WorkingColorSpace_000[0].y))));
    _352 = mad(_325, (WorkingColorSpace_000[2].z), mad(_322, (WorkingColorSpace_000[1].z), (_319 * (WorkingColorSpace_000[0].z))));
    _390 = mad(mad((WorkingColorSpace_064[0].z), _352, mad((WorkingColorSpace_064[0].y), _343, (_334 * (WorkingColorSpace_064[0].x)))), _128, mad(mad((WorkingColorSpace_064[0].z), _349, mad((WorkingColorSpace_064[0].y), _340, (_331 * (WorkingColorSpace_064[0].x)))), _127, (mad((WorkingColorSpace_064[0].z), _346, mad((WorkingColorSpace_064[0].y), _337, (_328 * (WorkingColorSpace_064[0].x)))) * _126)));
    _391 = mad(mad((WorkingColorSpace_064[1].z), _352, mad((WorkingColorSpace_064[1].y), _343, (_334 * (WorkingColorSpace_064[1].x)))), _128, mad(mad((WorkingColorSpace_064[1].z), _349, mad((WorkingColorSpace_064[1].y), _340, (_331 * (WorkingColorSpace_064[1].x)))), _127, (mad((WorkingColorSpace_064[1].z), _346, mad((WorkingColorSpace_064[1].y), _337, (_328 * (WorkingColorSpace_064[1].x)))) * _126)));
    _392 = mad(mad((WorkingColorSpace_064[2].z), _352, mad((WorkingColorSpace_064[2].y), _343, (_334 * (WorkingColorSpace_064[2].x)))), _128, mad(mad((WorkingColorSpace_064[2].z), _349, mad((WorkingColorSpace_064[2].y), _340, (_331 * (WorkingColorSpace_064[2].x)))), _127, (mad((WorkingColorSpace_064[2].z), _346, mad((WorkingColorSpace_064[2].y), _337, (_328 * (WorkingColorSpace_064[2].x)))) * _126)));
  } else {
    _390 = _126;
    _391 = _127;
    _392 = _128;
  }
  _407 = mad((WorkingColorSpace_128[0].z), _392, mad((WorkingColorSpace_128[0].y), _391, ((WorkingColorSpace_128[0].x) * _390)));
  _410 = mad((WorkingColorSpace_128[1].z), _392, mad((WorkingColorSpace_128[1].y), _391, ((WorkingColorSpace_128[1].x) * _390)));
  _413 = mad((WorkingColorSpace_128[2].z), _392, mad((WorkingColorSpace_128[2].y), _391, ((WorkingColorSpace_128[2].x) * _390)));
  _414 = dot(float3(_407, _410, _413), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _418 = (_407 / _414) + -1.0f;
  _419 = (_410 / _414) + -1.0f;
  _420 = (_413 / _414) + -1.0f;
  // ExpandGamut set to 0
  // _432 = (1.0f - exp2(((_414 * _414) * -4.0f) * cb0_038w)) * (1.0f - exp2(dot(float3(_418, _419, _420), float3(_418, _419, _420)) * -4.0f));
  _432 = (1.0f - exp2(((_414 * _414) * -4.0f) * 0.f)) * (1.0f - exp2(dot(float3(_418, _419, _420), float3(_418, _419, _420)) * -4.0f));
  _460 = ((mad(cb0_042z, _413, mad(cb0_042y, _410, (cb0_042x * _407))) - _407) * _432) + _407;
  _461 = ((mad(cb0_043z, _413, mad(cb0_043y, _410, (cb0_043x * _407))) - _410) * _432) + _410;
  _462 = ((mad(cb0_044z, _413, mad(cb0_044y, _410, (cb0_044x * _407))) - _413) * _432) + _413;
  _463 = dot(float3(_460, _461, _462), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _477 = cb0_021w + cb0_026w;
  _491 = cb0_020w * cb0_025w;
  _505 = cb0_019w * cb0_024w;
  _519 = cb0_018w * cb0_023w;
  _533 = cb0_017w * cb0_022w;
  _537 = _460 - _463;
  _538 = _461 - _463;
  _539 = _462 - _463;
  _596 = saturate(_463 / cb0_037w);
  _600 = (_596 * _596) * (3.0f - (_596 * 2.0f));
  _601 = 1.0f - _600;
  _610 = cb0_021w + cb0_036w;
  _619 = cb0_020w * cb0_035w;
  _628 = cb0_019w * cb0_034w;
  _637 = cb0_018w * cb0_033w;
  _646 = cb0_017w * cb0_032w;
  _709 = saturate((_463 - cb0_038x) / (cb0_038y - cb0_038x));
  _713 = (_709 * _709) * (3.0f - (_709 * 2.0f));
  _722 = cb0_021w + cb0_031w;
  _731 = cb0_020w * cb0_030w;
  _740 = cb0_019w * cb0_029w;
  _749 = cb0_018w * cb0_028w;
  _758 = cb0_017w * cb0_027w;
  _816 = _600 - _713;
  _827 = ((_713 * (((cb0_021x + cb0_036x) + _610) + (((cb0_020x * cb0_035x) * _619) * exp2(log2(exp2(((cb0_018x * cb0_033x) * _637) * log2(max(0.0f, ((((cb0_017x * cb0_032x) * _646) * _537) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_034x) * _628)))))) + (_601 * (((cb0_021x + cb0_026x) + _477) + (((cb0_020x * cb0_025x) * _491) * exp2(log2(exp2(((cb0_018x * cb0_023x) * _519) * log2(max(0.0f, ((((cb0_017x * cb0_022x) * _533) * _537) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_024x) * _505))))))) + ((((cb0_021x + cb0_031x) + _722) + (((cb0_020x * cb0_030x) * _731) * exp2(log2(exp2(((cb0_018x * cb0_028x) * _749) * log2(max(0.0f, ((((cb0_017x * cb0_027x) * _758) * _537) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019x * cb0_029x) * _740))))) * _816);
  _829 = ((_713 * (((cb0_021y + cb0_036y) + _610) + (((cb0_020y * cb0_035y) * _619) * exp2(log2(exp2(((cb0_018y * cb0_033y) * _637) * log2(max(0.0f, ((((cb0_017y * cb0_032y) * _646) * _538) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_034y) * _628)))))) + (_601 * (((cb0_021y + cb0_026y) + _477) + (((cb0_020y * cb0_025y) * _491) * exp2(log2(exp2(((cb0_018y * cb0_023y) * _519) * log2(max(0.0f, ((((cb0_017y * cb0_022y) * _533) * _538) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_024y) * _505))))))) + ((((cb0_021y + cb0_031y) + _722) + (((cb0_020y * cb0_030y) * _731) * exp2(log2(exp2(((cb0_018y * cb0_028y) * _749) * log2(max(0.0f, ((((cb0_017y * cb0_027y) * _758) * _538) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019y * cb0_029y) * _740))))) * _816);
  _831 = ((_713 * (((cb0_021z + cb0_036z) + _610) + (((cb0_020z * cb0_035z) * _619) * exp2(log2(exp2(((cb0_018z * cb0_033z) * _637) * log2(max(0.0f, ((((cb0_017z * cb0_032z) * _646) * _539) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_034z) * _628)))))) + (_601 * (((cb0_021z + cb0_026z) + _477) + (((cb0_020z * cb0_025z) * _491) * exp2(log2(exp2(((cb0_018z * cb0_023z) * _519) * log2(max(0.0f, ((((cb0_017z * cb0_022z) * _533) * _539) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_024z) * _505))))))) + ((((cb0_021z + cb0_031z) + _722) + (((cb0_020z * cb0_030z) * _731) * exp2(log2(exp2(((cb0_018z * cb0_028z) * _749) * log2(max(0.0f, ((((cb0_017z * cb0_027z) * _758) * _539) + _463)) * 5.55555534362793f)) * 0.18000000715255737f) * (1.0f / ((cb0_019z * cb0_029z) * _740))))) * _816);
  // Hash-specific layout traced below: c039/c040 film, c038.z blue,
  // c039.x tone strength, c052 polynomial, c016 scale, c015 overlay.
  // _827/_829/_831 are graded linear AP1, before blue correction and film/RRT.
  // No external SDR LUT textures are bound by this shader.
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
  u0[SV_DispatchThreadID] = ProcessLutbuilder(
      float3(_827, _829, _831), cb_config, float4(0.f, 0.f, 0.f, 0.f), cb0_053w);
  return;

  // Original dead tail retained for field verification.
  _839 = saturate((max(max(_827, _829), _831) - cb0_041x) * cb0_041y);
  _867 = ((mad(cb0_046z, _831, mad(cb0_046y, _829, (cb0_046x * _827))) - _827) * _839) + _827;
  _868 = ((mad(cb0_047z, _831, mad(cb0_047y, _829, (cb0_047x * _827))) - _829) * _839) + _829;
  _869 = ((mad(cb0_048z, _831, mad(cb0_048y, _829, (cb0_048x * _827))) - _831) * _839) + _831;
  _905 = ((mad(0.061360642313957214f, _831, mad(-4.540197551250458e-09f, _829, (_827 * 0.9386394023895264f))) - _827) * cb0_038z) + _827;
  _906 = ((mad(0.169205904006958f, _831, mad(0.8307942152023315f, _829, (_827 * 6.775371730327606e-08f))) - _829) * cb0_038z) + _829;
  _907 = (mad(-2.3283064365386963e-10f, _829, (_827 * -9.313225746154785e-10f)) * cb0_038z) + _831;
  _910 = mad(0.16386905312538147f, _907, mad(0.14067868888378143f, _906, (_905 * 0.6954522132873535f)));
  _913 = mad(0.0955343246459961f, _907, mad(0.8596711158752441f, _906, (_905 * 0.044794581830501556f)));
  _916 = mad(1.0015007257461548f, _907, mad(0.004025210160762072f, _906, (_905 * -0.005525882821530104f)));
  _920 = max(max(_910, _913), _916);
  _925 = (max(_920, 1.000000013351432e-10f) - max(min(min(_910, _913), _916), 1.000000013351432e-10f)) / max(_920, 0.009999999776482582f);
  _938 = ((_913 + _910) + _916) + (sqrt((((_916 - _913) * _916) + ((_913 - _910) * _913)) + ((_910 - _916) * _910)) * 1.75f);
  _939 = _938 * 0.3333333432674408f;
  _940 = _925 + -0.4000000059604645f;
  _941 = _940 * 5.0f;
  _945 = max((1.0f - abs(_940 * 2.5f)), 0.0f);
  _956 = ((float((int)(((int)(uint)((int)(_941 > 0.0f))) - ((int)(uint)((int)(_941 < 0.0f))))) * (1.0f - (_945 * _945))) + 1.0f) * 0.02500000037252903f;
  if (!(_939 <= 0.0533333346247673f)) {
    if (!(_939 >= 0.1599999964237213f)) {
      _965 = (((0.23999999463558197f / _938) + -0.5f) * _956);
    } else {
      _965 = 0.0f;
    }
  } else {
    _965 = _956;
  }
  _966 = _965 + 1.0f;
  _967 = _966 * _910;
  _968 = _966 * _913;
  _969 = _966 * _916;
  if (!((_967 == _968) && (_968 == _969))) {
    _976 = ((_967 * 2.0f) - _968) - _969;
    _979 = ((_913 - _916) * 1.7320507764816284f) * _966;
    _981 = atan(_979 / _976);
    _984 = (_976 < 0.0f);
    _985 = (_976 == 0.0f);
    _986 = (_979 >= 0.0f);
    _987 = (_979 < 0.0f);
    _998 = select((_986 && _985), 90.0f, select((_987 && _985), -90.0f, (select((_987 && _984), (_981 + -3.1415927410125732f), select((_986 && _984), (_981 + 3.1415927410125732f), _981)) * 57.2957763671875f)));
  } else {
    _998 = 0.0f;
  }
  _1003 = min(max(select((_998 < 0.0f), (_998 + 360.0f), _998), 0.0f), 360.0f);
  if (_1003 < -180.0f) {
    _1012 = (_1003 + 360.0f);
  } else {
    if (_1003 > 180.0f) {
      _1012 = (_1003 + -360.0f);
    } else {
      _1012 = _1003;
    }
  }
  _1016 = saturate(1.0f - abs(_1012 * 0.014814814552664757f));
  _1020 = (_1016 * _1016) * (3.0f - (_1016 * 2.0f));
  _1026 = ((_1020 * _1020) * ((_925 * 0.18000000715255737f) * (0.029999999329447746f - _967))) + _967;
  _1036 = max(0.0f, mad(-0.21492856740951538f, _969, mad(-0.2365107536315918f, _968, (_1026 * 1.4514392614364624f))));
  _1037 = max(0.0f, mad(-0.09967592358589172f, _969, mad(1.17622971534729f, _968, (_1026 * -0.07655377686023712f))));
  _1038 = max(0.0f, mad(0.9977163076400757f, _969, mad(-0.006032449658960104f, _968, (_1026 * 0.008316148072481155f))));
  _1039 = dot(float3(_1036, _1037, _1038), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _1054 = (cb0_040x + 1.0f) - cb0_039z;
  _1056 = cb0_040y + 1.0f;
  _1058 = _1056 - cb0_039w;
  if (cb0_039z > 0.800000011920929f) {
    _1076 = (((0.8199999928474426f - cb0_039z) / cb0_039y) + -0.7447274923324585f);
  } else {
    _1067 = (cb0_040x + 0.18000000715255737f) / _1054;
    _1076 = (-0.7447274923324585f - ((log2(_1067 / (2.0f - _1067)) * 0.3465735912322998f) * (_1054 / cb0_039y)));
  }
  _1079 = ((1.0f - cb0_039z) / cb0_039y) - _1076;
  _1081 = (cb0_039w / cb0_039y) - _1079;
  _1085 = log2(lerp(_1039, _1036, 0.9599999785423279f)) * 0.3010300099849701f;
  _1086 = log2(lerp(_1039, _1037, 0.9599999785423279f)) * 0.3010300099849701f;
  _1087 = log2(lerp(_1039, _1038, 0.9599999785423279f)) * 0.3010300099849701f;
  _1091 = cb0_039y * (_1085 + _1079);
  _1092 = cb0_039y * (_1086 + _1079);
  _1093 = cb0_039y * (_1087 + _1079);
  _1094 = _1054 * 2.0f;
  _1096 = (cb0_039y * -2.0f) / _1054;
  _1097 = _1085 - _1076;
  _1098 = _1086 - _1076;
  _1099 = _1087 - _1076;
  _1118 = _1058 * 2.0f;
  _1120 = (cb0_039y * 2.0f) / _1058;
  _1145 = select((_1085 < _1076), ((_1094 / (exp2((_1097 * 1.4426950216293335f) * _1096) + 1.0f)) - cb0_040x), _1091);
  _1146 = select((_1086 < _1076), ((_1094 / (exp2((_1098 * 1.4426950216293335f) * _1096) + 1.0f)) - cb0_040x), _1092);
  _1147 = select((_1087 < _1076), ((_1094 / (exp2((_1099 * 1.4426950216293335f) * _1096) + 1.0f)) - cb0_040x), _1093);
  _1154 = _1081 - _1076;
  _1158 = saturate(_1097 / _1154);
  _1159 = saturate(_1098 / _1154);
  _1160 = saturate(_1099 / _1154);
  _1161 = (_1081 < _1076);
  _1165 = select(_1161, (1.0f - _1158), _1158);
  _1166 = select(_1161, (1.0f - _1159), _1159);
  _1167 = select(_1161, (1.0f - _1160), _1160);
  _1186 = (((_1165 * _1165) * (select((_1085 > _1081), (_1056 - (_1118 / (exp2(((_1085 - _1081) * 1.4426950216293335f) * _1120) + 1.0f))), _1091) - _1145)) * (3.0f - (_1165 * 2.0f))) + _1145;
  _1187 = (((_1166 * _1166) * (select((_1086 > _1081), (_1056 - (_1118 / (exp2(((_1086 - _1081) * 1.4426950216293335f) * _1120) + 1.0f))), _1092) - _1146)) * (3.0f - (_1166 * 2.0f))) + _1146;
  _1188 = (((_1167 * _1167) * (select((_1087 > _1081), (_1056 - (_1118 / (exp2(((_1087 - _1081) * 1.4426950216293335f) * _1120) + 1.0f))), _1093) - _1147)) * (3.0f - (_1167 * 2.0f))) + _1147;
  _1189 = dot(float3(_1186, _1187, _1188), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
  _1209 = (cb0_039x * (max(0.0f, (lerp(_1189, _1186, 0.9300000071525574f))) - _905)) + _905;
  _1210 = (cb0_039x * (max(0.0f, (lerp(_1189, _1187, 0.9300000071525574f))) - _906)) + _906;
  _1211 = (cb0_039x * (max(0.0f, (lerp(_1189, _1188, 0.9300000071525574f))) - _907)) + _907;
  _1229 = ((mad(-0.06537103652954102f, _1211, mad(1.451815478503704e-06f, _1210, (_1209 * 1.065374732017517f))) - _1209) * cb0_038z) + _1209;
  _1230 = ((mad(-0.20366770029067993f, _1211, mad(1.2036634683609009f, _1210, (_1209 * -2.57161445915699e-07f))) - _1210) * cb0_038z) + _1210;
  _1231 = ((mad(0.9999996423721313f, _1211, mad(2.0954757928848267e-08f, _1210, (_1209 * 1.862645149230957e-08f))) - _1211) * cb0_038z) + _1211;
  _1241 = max(0.0f, mad((WorkingColorSpace_192[0].z), _1231, mad((WorkingColorSpace_192[0].y), _1230, ((WorkingColorSpace_192[0].x) * _1229))));
  _1242 = max(0.0f, mad((WorkingColorSpace_192[1].z), _1231, mad((WorkingColorSpace_192[1].y), _1230, ((WorkingColorSpace_192[1].x) * _1229))));
  _1243 = max(0.0f, mad((WorkingColorSpace_192[2].z), _1231, mad((WorkingColorSpace_192[2].y), _1230, ((WorkingColorSpace_192[2].x) * _1229))));
  _1269 = cb0_016x * (((cb0_052y + (cb0_052x * _1241)) * _1241) + cb0_052z);
  _1270 = cb0_016y * (((cb0_052y + (cb0_052x * _1242)) * _1242) + cb0_052z);
  _1271 = cb0_016z * (((cb0_052y + (cb0_052x * _1243)) * _1243) + cb0_052z);
  _1278 = ((cb0_015x - _1269) * cb0_015w) + _1269;
  _1279 = ((cb0_015y - _1270) * cb0_015w) + _1270;
  _1280 = ((cb0_015z - _1271) * cb0_015w) + _1271;
  _1281 = cb0_016x * mad((WorkingColorSpace_192[0].z), _869, mad((WorkingColorSpace_192[0].y), _868, ((WorkingColorSpace_192[0].x) * _867)));
  _1282 = cb0_016y * mad((WorkingColorSpace_192[1].z), _869, mad((WorkingColorSpace_192[1].y), _868, ((WorkingColorSpace_192[1].x) * _867)));
  _1283 = cb0_016z * mad((WorkingColorSpace_192[2].z), _869, mad((WorkingColorSpace_192[2].y), _868, ((WorkingColorSpace_192[2].x) * _867)));
  _1290 = ((cb0_015x - _1281) * cb0_015w) + _1281;
  _1291 = ((cb0_015y - _1282) * cb0_015w) + _1282;
  _1292 = ((cb0_015z - _1283) * cb0_015w) + _1283;
  _1304 = exp2(log2(max(0.0f, _1278)) * cb0_053y);
  _1305 = exp2(log2(max(0.0f, _1279)) * cb0_053y);
  _1306 = exp2(log2(max(0.0f, _1280)) * cb0_053y);
  [branch]
  if (cb0_053w == 0) {
    if (WorkingColorSpace_384 == 0) {
      _1329 = mad((WorkingColorSpace_128[0].z), _1306, mad((WorkingColorSpace_128[0].y), _1305, ((WorkingColorSpace_128[0].x) * _1304)));
      _1332 = mad((WorkingColorSpace_128[1].z), _1306, mad((WorkingColorSpace_128[1].y), _1305, ((WorkingColorSpace_128[1].x) * _1304)));
      _1335 = mad((WorkingColorSpace_128[2].z), _1306, mad((WorkingColorSpace_128[2].y), _1305, ((WorkingColorSpace_128[2].x) * _1304)));
      _1346 = mad(_62, _1335, mad(_61, _1332, (_1329 * _60)));
      _1347 = mad(_65, _1335, mad(_64, _1332, (_1329 * _63)));
      _1348 = mad(_68, _1335, mad(_67, _1332, (_1329 * _66)));
    } else {
      _1346 = _1304;
      _1347 = _1305;
      _1348 = _1306;
    }
    _1353 = ((_1347 * 0.7152000069618225f) + (_1346 * 0.2125999927520752f)) + (_1348 * 0.0722000002861023f);
    _1362 = ((_1346 - _1353) * cb0_050z) + _1353;
    _1363 = ((_1347 - _1353) * cb0_050z) + _1353;
    _1364 = ((_1348 - _1353) * cb0_050z) + _1353;
    _1391 = cb0_050x * max((((((_1362 * _1362) * (3.0f - (_1362 * 2.0f))) - _1362) * cb0_050y) + _1362), 0.0f);
    _1392 = cb0_050x * max((((((_1363 * _1363) * (3.0f - (_1363 * 2.0f))) - _1363) * cb0_050y) + _1363), 0.0f);
    _1393 = cb0_050x * max((((((_1364 * _1364) * (3.0f - (_1364 * 2.0f))) - _1364) * cb0_050y) + _1364), 0.0f);
    if (_1391 < 0.0031306699384003878f) {
      _1404 = (_1391 * 12.920000076293945f);
    } else {
      _1404 = (((pow(_1391, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
    }
    if (_1392 < 0.0031306699384003878f) {
      _1415 = (_1392 * 12.920000076293945f);
    } else {
      _1415 = (((pow(_1392, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
    }
    if (_1393 < 0.0031306699384003878f) {
      _2984 = _1404;
      _2985 = _1415;
      _2986 = (_1393 * 12.920000076293945f);
    } else {
      _2984 = _1404;
      _2985 = _1415;
      _2986 = (((pow(_1393, 0.4166666567325592f)) * 1.0549999475479126f) + -0.054999999701976776f);
    }
  } else {
    if (cb0_053w == 1) {
      _1442 = mad((WorkingColorSpace_128[0].z), _1306, mad((WorkingColorSpace_128[0].y), _1305, ((WorkingColorSpace_128[0].x) * _1304)));
      _1445 = mad((WorkingColorSpace_128[1].z), _1306, mad((WorkingColorSpace_128[1].y), _1305, ((WorkingColorSpace_128[1].x) * _1304)));
      _1448 = mad((WorkingColorSpace_128[2].z), _1306, mad((WorkingColorSpace_128[2].y), _1305, ((WorkingColorSpace_128[2].x) * _1304)));
      _1451 = mad(_62, _1448, mad(_61, _1445, (_1442 * _60)));
      _1454 = mad(_65, _1448, mad(_64, _1445, (_1442 * _63)));
      _1457 = mad(_68, _1448, mad(_67, _1445, (_1442 * _66)));
      _2984 = min((_1451 * 4.5f), ((exp2(log2(max(_1451, 0.017999999225139618f)) * 0.44999998807907104f) * 1.0989999771118164f) + -0.0989999994635582f));
      _2985 = min((_1454 * 4.5f), ((exp2(log2(max(_1454, 0.017999999225139618f)) * 0.44999998807907104f) * 1.0989999771118164f) + -0.0989999994635582f));
      _2986 = min((_1457 * 4.5f), ((exp2(log2(max(_1457, 0.017999999225139618f)) * 0.44999998807907104f) * 1.0989999771118164f) + -0.0989999994635582f));
    } else {
      if ((uint)((int)((uint)(cb0_053w) + (uint)(-3))) < (uint)2) {
        _9[0] = cb0_010x;
        _9[1] = cb0_010y;
        _9[2] = cb0_010z;
        _9[3] = cb0_010w;
        _9[4] = cb0_012x;
        _9[5] = cb0_012x;
        _10[0] = cb0_011x;
        _10[1] = cb0_011y;
        _10[2] = cb0_011z;
        _10[3] = cb0_011w;
        _10[4] = cb0_012y;
        _10[5] = cb0_012y;
        _1532 = cb0_012z * _1290;
        _1533 = cb0_012z * _1291;
        _1534 = cb0_012z * _1292;
        _1537 = mad((WorkingColorSpace_256[0].z), _1534, mad((WorkingColorSpace_256[0].y), _1533, ((WorkingColorSpace_256[0].x) * _1532)));
        _1540 = mad((WorkingColorSpace_256[1].z), _1534, mad((WorkingColorSpace_256[1].y), _1533, ((WorkingColorSpace_256[1].x) * _1532)));
        _1543 = mad((WorkingColorSpace_256[2].z), _1534, mad((WorkingColorSpace_256[2].y), _1533, ((WorkingColorSpace_256[2].x) * _1532)));
        _1546 = mad(-0.21492856740951538f, _1543, mad(-0.2365107536315918f, _1540, (_1537 * 1.4514392614364624f)));
        _1549 = mad(-0.09967592358589172f, _1543, mad(1.17622971534729f, _1540, (_1537 * -0.07655377686023712f)));
        _1552 = mad(0.9977163076400757f, _1543, mad(-0.006032449658960104f, _1540, (_1537 * 0.008316148072481155f)));
        _1554 = max(_1546, max(_1549, _1552));
        if (!(_1554 < 1.000000013351432e-10f)) {
          if (!(((_1537 < 0.0f) || (_1540 < 0.0f)) || (_1543 < 0.0f))) {
            _1564 = abs(_1554);
            _1565 = (_1554 - _1546) / _1564;
            _1567 = (_1554 - _1549) / _1564;
            _1569 = (_1554 - _1552) / _1564;
            if (!(_1565 < 0.8149999976158142f)) {
              _1572 = _1565 + -0.8149999976158142f;
              _1584 = ((_1572 / exp2(log2(exp2(log2(_1572 * 3.0552830696105957f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8149999976158142f);
            } else {
              _1584 = _1565;
            }
            if (!(_1567 < 0.8029999732971191f)) {
              _1587 = _1567 + -0.8029999732971191f;
              _1599 = ((_1587 / exp2(log2(exp2(log2(_1587 * 3.4972610473632812f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8029999732971191f);
            } else {
              _1599 = _1567;
            }
            if (!(_1569 < 0.8799999952316284f)) {
              _1602 = _1569 + -0.8799999952316284f;
              _1614 = ((_1602 / exp2(log2(exp2(log2(_1602 * 6.810994625091553f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8799999952316284f);
            } else {
              _1614 = _1569;
            }
            _1622 = (_1554 - (_1564 * _1584));
            _1623 = (_1554 - (_1564 * _1599));
            _1624 = (_1554 - (_1564 * _1614));
          } else {
            _1622 = _1546;
            _1623 = _1549;
            _1624 = _1552;
          }
        } else {
          _1622 = _1546;
          _1623 = _1549;
          _1624 = _1552;
        }
        _1640 = ((mad(0.16386906802654266f, _1624, mad(0.14067870378494263f, _1623, (_1622 * 0.6954522132873535f))) - _1537) * cb0_012w) + _1537;
        _1641 = ((mad(0.0955343171954155f, _1624, mad(0.8596711158752441f, _1623, (_1622 * 0.044794563204050064f))) - _1540) * cb0_012w) + _1540;
        _1642 = ((mad(1.0015007257461548f, _1624, mad(0.004025210160762072f, _1623, (_1622 * -0.005525882821530104f))) - _1543) * cb0_012w) + _1543;
        _1646 = max(max(_1640, _1641), _1642);
        _1651 = (max(_1646, 1.000000013351432e-10f) - max(min(min(_1640, _1641), _1642), 1.000000013351432e-10f)) / max(_1646, 0.009999999776482582f);
        _1664 = ((_1641 + _1640) + _1642) + (sqrt((((_1642 - _1641) * _1642) + ((_1641 - _1640) * _1641)) + ((_1640 - _1642) * _1640)) * 1.75f);
        _1665 = _1664 * 0.3333333432674408f;
        _1666 = _1651 + -0.4000000059604645f;
        _1667 = _1666 * 5.0f;
        _1671 = max((1.0f - abs(_1666 * 2.5f)), 0.0f);
        _1682 = ((float((int)(((int)(uint)((int)(_1667 > 0.0f))) - ((int)(uint)((int)(_1667 < 0.0f))))) * (1.0f - (_1671 * _1671))) + 1.0f) * 0.02500000037252903f;
        if (!(_1665 <= 0.0533333346247673f)) {
          if (!(_1665 >= 0.1599999964237213f)) {
            _1691 = (((0.23999999463558197f / _1664) + -0.5f) * _1682);
          } else {
            _1691 = 0.0f;
          }
        } else {
          _1691 = _1682;
        }
        _1692 = _1691 + 1.0f;
        _1693 = _1692 * _1640;
        _1694 = _1692 * _1641;
        _1695 = _1692 * _1642;
        if (!((_1693 == _1694) && (_1694 == _1695))) {
          _1702 = ((_1693 * 2.0f) - _1694) - _1695;
          _1705 = ((_1641 - _1642) * 1.7320507764816284f) * _1692;
          _1707 = atan(_1705 / _1702);
          _1710 = (_1702 < 0.0f);
          _1711 = (_1702 == 0.0f);
          _1712 = (_1705 >= 0.0f);
          _1713 = (_1705 < 0.0f);
          _1724 = select((_1712 && _1711), 90.0f, select((_1713 && _1711), -90.0f, (select((_1713 && _1710), (_1707 + -3.1415927410125732f), select((_1712 && _1710), (_1707 + 3.1415927410125732f), _1707)) * 57.2957763671875f)));
        } else {
          _1724 = 0.0f;
        }
        _1729 = min(max(select((_1724 < 0.0f), (_1724 + 360.0f), _1724), 0.0f), 360.0f);
        if (_1729 < -180.0f) {
          _1738 = (_1729 + 360.0f);
        } else {
          if (_1729 > 180.0f) {
            _1738 = (_1729 + -360.0f);
          } else {
            _1738 = _1729;
          }
        }
        if ((_1738 > -67.5f) && (_1738 < 67.5f)) {
          _1744 = (_1738 + 67.5f) * 0.029629629105329514f;
          _1745 = int(_1744);
          _1747 = _1744 - float((int)(_1745));
          _1748 = _1747 * _1747;
          _1749 = _1748 * _1747;
          if (_1745 == 3) {
            _1777 = (((0.1666666716337204f - (_1747 * 0.5f)) + (_1748 * 0.5f)) - (_1749 * 0.1666666716337204f));
          } else {
            if (_1745 == 2) {
              _1777 = ((0.6666666865348816f - _1748) + (_1749 * 0.5f));
            } else {
              if (_1745 == 1) {
                _1777 = (((_1749 * -0.5f) + 0.1666666716337204f) + ((_1748 + _1747) * 0.5f));
              } else {
                _1777 = select((_1745 == 0), (_1749 * 0.1666666716337204f), 0.0f);
              }
            }
          }
        } else {
          _1777 = 0.0f;
        }
        _1786 = min(max(((((_1651 * 0.27000001072883606f) * (0.029999999329447746f - _1693)) * _1777) + _1693), 0.0f), 65535.0f);
        _1787 = min(max(_1694, 0.0f), 65535.0f);
        _1788 = min(max(_1695, 0.0f), 65535.0f);
        _1801 = min(max(mad(-0.21492856740951538f, _1788, mad(-0.2365107536315918f, _1787, (_1786 * 1.4514392614364624f))), 0.0f), 65504.0f);
        _1802 = min(max(mad(-0.09967592358589172f, _1788, mad(1.17622971534729f, _1787, (_1786 * -0.07655377686023712f))), 0.0f), 65504.0f);
        _1803 = min(max(mad(0.9977163076400757f, _1788, mad(-0.006032449658960104f, _1787, (_1786 * 0.008316148072481155f))), 0.0f), 65504.0f);
        _1804 = dot(float3(_1801, _1802, _1803), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
        _17[0] = cb0_010x;
        _17[1] = cb0_010y;
        _17[2] = cb0_010z;
        _17[3] = cb0_010w;
        _17[4] = cb0_012x;
        _17[5] = cb0_012x;
        _18[0] = cb0_011x;
        _18[1] = cb0_011y;
        _18[2] = cb0_011z;
        _18[3] = cb0_011w;
        _18[4] = cb0_012y;
        _18[5] = cb0_012y;
        _1827 = log2(max((lerp(_1804, _1801, 0.9599999785423279f)), 1.000000013351432e-10f));
        _1828 = _1827 * 0.3010300099849701f;
        _1829 = log2(cb0_008x);
        _1830 = _1829 * 0.3010300099849701f;
        if (!(_1828 <= _1830)) {
          _1837 = log2(cb0_009x);
          _1838 = _1837 * 0.3010300099849701f;
          if ((_1828 > _1830) && (_1828 < _1838)) {
            _1846 = ((_1827 - _1829) * 0.9030900001525879f) / ((_1837 - _1829) * 0.3010300099849701f);
            _1847 = int(_1846);
            _1849 = _1846 - float((int)(_1847));
            _1851 = _17[min((uint)(_1847), 5u)];
            _1854 = _17[min((uint)((_1847 + 1)), 5u)];
            _1859 = _1851 * 0.5f;
            _1899 = dot(float3((_1849 * _1849), _1849, 1.0f), float3(mad((_17[min((uint)((_1847 + 2)), 5u)]), 0.5f, mad(_1854, -1.0f, _1859)), (_1854 - _1851), mad(_1854, 0.5f, _1859)));
          } else {
            if (!(_1828 >= _1838)) {
              _1899 = (log2(cb0_008w) * 0.3010300099849701f);
            } else {
              _1868 = log2(cb0_008z);
              if (!(_1828 < (_1868 * 0.3010300099849701f))) {
                _1899 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _1876 = ((_1827 - _1837) * 0.9030900001525879f) / ((_1868 - _1837) * 0.3010300099849701f);
                _1877 = int(_1876);
                _1879 = _1876 - float((int)(_1877));
                _1881 = _18[min((uint)(_1877), 5u)];
                _1884 = _18[min((uint)((_1877 + 1)), 5u)];
                _1889 = _1881 * 0.5f;
                _1899 = dot(float3((_1879 * _1879), _1879, 1.0f), float3(mad((_18[min((uint)((_1877 + 2)), 5u)]), 0.5f, mad(_1884, -1.0f, _1889)), (_1884 - _1881), mad(_1884, 0.5f, _1889)));
              }
            }
          }
        } else {
          _1899 = (log2(cb0_008y) * 0.3010300099849701f);
        }
        _19[0] = cb0_011x;
        _19[1] = cb0_011y;
        _19[2] = cb0_011z;
        _19[3] = cb0_011w;
        _19[4] = cb0_012y;
        _19[5] = cb0_012y;
        _1909 = log2(max((lerp(_1804, _1802, 0.9599999785423279f)), 1.000000013351432e-10f));
        _1910 = _1909 * 0.3010300099849701f;
        if (!(_1910 <= _1830)) {
          _1917 = log2(cb0_009x);
          _1918 = _1917 * 0.3010300099849701f;
          if ((_1910 > _1830) && (_1910 < _1918)) {
            _1926 = ((_1909 - _1829) * 0.9030900001525879f) / ((_1917 - _1829) * 0.3010300099849701f);
            _1927 = int(_1926);
            _1929 = _1926 - float((int)(_1927));
            _1931 = _9[min((uint)(_1927), 5u)];
            _1934 = _9[min((uint)((_1927 + 1)), 5u)];
            _1939 = _1931 * 0.5f;
            _1979 = dot(float3((_1929 * _1929), _1929, 1.0f), float3(mad((_9[min((uint)((_1927 + 2)), 5u)]), 0.5f, mad(_1934, -1.0f, _1939)), (_1934 - _1931), mad(_1934, 0.5f, _1939)));
          } else {
            if (!(_1910 >= _1918)) {
              _1979 = (log2(cb0_008w) * 0.3010300099849701f);
            } else {
              _1948 = log2(cb0_008z);
              if (!(_1910 < (_1948 * 0.3010300099849701f))) {
                _1979 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _1956 = ((_1909 - _1917) * 0.9030900001525879f) / ((_1948 - _1917) * 0.3010300099849701f);
                _1957 = int(_1956);
                _1959 = _1956 - float((int)(_1957));
                _1961 = _19[min((uint)(_1957), 5u)];
                _1964 = _19[min((uint)((_1957 + 1)), 5u)];
                _1969 = _1961 * 0.5f;
                _1979 = dot(float3((_1959 * _1959), _1959, 1.0f), float3(mad((_19[min((uint)((_1957 + 2)), 5u)]), 0.5f, mad(_1964, -1.0f, _1969)), (_1964 - _1961), mad(_1964, 0.5f, _1969)));
              }
            }
          }
        } else {
          _1979 = (log2(cb0_008y) * 0.3010300099849701f);
        }
        _1983 = log2(max((lerp(_1804, _1803, 0.9599999785423279f)), 1.000000013351432e-10f));
        _1984 = _1983 * 0.3010300099849701f;
        if (!(_1984 <= _1830)) {
          _1991 = log2(cb0_009x);
          _1992 = _1991 * 0.3010300099849701f;
          if ((_1984 > _1830) && (_1984 < _1992)) {
            _2000 = ((_1983 - _1829) * 0.9030900001525879f) / ((_1991 - _1829) * 0.3010300099849701f);
            _2001 = int(_2000);
            _2003 = _2000 - float((int)(_2001));
            _2005 = _9[min((uint)(_2001), 5u)];
            _2008 = _9[min((uint)((_2001 + 1)), 5u)];
            _2013 = _2005 * 0.5f;
            _2053 = dot(float3((_2003 * _2003), _2003, 1.0f), float3(mad((_9[min((uint)((_2001 + 2)), 5u)]), 0.5f, mad(_2008, -1.0f, _2013)), (_2008 - _2005), mad(_2008, 0.5f, _2013)));
          } else {
            if (!(_1984 >= _1992)) {
              _2053 = (log2(cb0_008w) * 0.3010300099849701f);
            } else {
              _2022 = log2(cb0_008z);
              if (!(_1984 < (_2022 * 0.3010300099849701f))) {
                _2053 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _2030 = ((_1983 - _1991) * 0.9030900001525879f) / ((_2022 - _1991) * 0.3010300099849701f);
                _2031 = int(_2030);
                _2033 = _2030 - float((int)(_2031));
                _2035 = _10[min((uint)(_2031), 5u)];
                _2038 = _10[min((uint)((_2031 + 1)), 5u)];
                _2043 = _2035 * 0.5f;
                _2053 = dot(float3((_2033 * _2033), _2033, 1.0f), float3(mad((_10[min((uint)((_2031 + 2)), 5u)]), 0.5f, mad(_2038, -1.0f, _2043)), (_2038 - _2035), mad(_2038, 0.5f, _2043)));
              }
            }
          }
        } else {
          _2053 = (log2(cb0_008y) * 0.3010300099849701f);
        }
        _2057 = cb0_008w - cb0_008y;
        _2058 = (exp2(_1899 * 3.321928024291992f) - cb0_008y) / _2057;
        _2060 = (exp2(_1979 * 3.321928024291992f) - cb0_008y) / _2057;
        _2062 = (exp2(_2053 * 3.321928024291992f) - cb0_008y) / _2057;
        _2065 = mad(0.15618768334388733f, _2062, mad(0.13400420546531677f, _2060, (_2058 * 0.6624541878700256f)));
        _2068 = mad(0.053689517080783844f, _2062, mad(0.6740817427635193f, _2060, (_2058 * 0.2722287178039551f)));
        _2071 = mad(1.0103391408920288f, _2062, mad(0.00406073359772563f, _2060, (_2058 * -0.005574649665504694f)));
        _2084 = min(max(mad(-0.23642469942569733f, _2071, mad(-0.32480329275131226f, _2068, (_2065 * 1.6410233974456787f))), 0.0f), 1.0f);
        _2085 = min(max(mad(0.016756348311901093f, _2071, mad(1.6153316497802734f, _2068, (_2065 * -0.663662850856781f))), 0.0f), 1.0f);
        _2086 = min(max(mad(0.9883948564529419f, _2071, mad(-0.008284442126750946f, _2068, (_2065 * 0.011721894145011902f))), 0.0f), 1.0f);
        _2089 = mad(0.15618768334388733f, _2086, mad(0.13400420546531677f, _2085, (_2084 * 0.6624541878700256f)));
        _2092 = mad(0.053689517080783844f, _2086, mad(0.6740817427635193f, _2085, (_2084 * 0.2722287178039551f)));
        _2095 = mad(1.0103391408920288f, _2086, mad(0.00406073359772563f, _2085, (_2084 * -0.005574649665504694f)));
        _2117 = min(max((min(max(mad(-0.23642469942569733f, _2095, mad(-0.32480329275131226f, _2092, (_2089 * 1.6410233974456787f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f);
        _2118 = min(max((min(max(mad(0.016756348311901093f, _2095, mad(1.6153316497802734f, _2092, (_2089 * -0.663662850856781f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f);
        _2119 = min(max((min(max(mad(0.9883948564529419f, _2095, mad(-0.008284442126750946f, _2092, (_2089 * 0.011721894145011902f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f);
        _2122 = mad(_62, _2119, mad(_61, _2118, (_2117 * _60)));
        _2125 = mad(_65, _2119, mad(_64, _2118, (_2117 * _63)));
        _2128 = mad(_68, _2119, mad(_67, _2118, (_2117 * _66)));
        _2133 = ((_2125 * 0.7152000069618225f) + (_2122 * 0.2125999927520752f)) + (_2128 * 0.0722000002861023f);
        _2143 = (saturate((_2133 - cb0_051x) / cb0_051x) * cb0_051y) + cb0_050w;
        _2162 = exp2(log2(((_2143 * (_2122 - _2133)) + _2133) * 9.999999747378752e-05f) * 0.1593017578125f);
        _2163 = exp2(log2(((_2143 * (_2125 - _2133)) + _2133) * 9.999999747378752e-05f) * 0.1593017578125f);
        _2164 = exp2(log2(((_2143 * (_2128 - _2133)) + _2133) * 9.999999747378752e-05f) * 0.1593017578125f);
        _2984 = exp2(log2((1.0f / ((_2162 * 18.6875f) + 1.0f)) * ((_2162 * 18.8515625f) + 0.8359375f)) * 78.84375f);
        _2985 = exp2(log2((1.0f / ((_2163 * 18.6875f) + 1.0f)) * ((_2163 * 18.8515625f) + 0.8359375f)) * 78.84375f);
        _2986 = exp2(log2((1.0f / ((_2164 * 18.6875f) + 1.0f)) * ((_2164 * 18.8515625f) + 0.8359375f)) * 78.84375f);
      } else {
        if ((uint)((int)((uint)(cb0_053w) + (uint)(-5))) < (uint)2) {
          _2230 = cb0_012z * _1290;
          _2231 = cb0_012z * _1291;
          _2232 = cb0_012z * _1292;
          _2235 = mad((WorkingColorSpace_256[0].z), _2232, mad((WorkingColorSpace_256[0].y), _2231, ((WorkingColorSpace_256[0].x) * _2230)));
          _2238 = mad((WorkingColorSpace_256[1].z), _2232, mad((WorkingColorSpace_256[1].y), _2231, ((WorkingColorSpace_256[1].x) * _2230)));
          _2241 = mad((WorkingColorSpace_256[2].z), _2232, mad((WorkingColorSpace_256[2].y), _2231, ((WorkingColorSpace_256[2].x) * _2230)));
          _2244 = mad(-0.21492856740951538f, _2241, mad(-0.2365107536315918f, _2238, (_2235 * 1.4514392614364624f)));
          _2247 = mad(-0.09967592358589172f, _2241, mad(1.17622971534729f, _2238, (_2235 * -0.07655377686023712f)));
          _2250 = mad(0.9977163076400757f, _2241, mad(-0.006032449658960104f, _2238, (_2235 * 0.008316148072481155f)));
          _2252 = max(_2244, max(_2247, _2250));
          if (!(_2252 < 1.000000013351432e-10f)) {
            if (!(((_2235 < 0.0f) || (_2238 < 0.0f)) || (_2241 < 0.0f))) {
              _2262 = abs(_2252);
              _2263 = (_2252 - _2244) / _2262;
              _2265 = (_2252 - _2247) / _2262;
              _2267 = (_2252 - _2250) / _2262;
              if (!(_2263 < 0.8149999976158142f)) {
                _2270 = _2263 + -0.8149999976158142f;
                _2282 = ((_2270 / exp2(log2(exp2(log2(_2270 * 3.0552830696105957f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8149999976158142f);
              } else {
                _2282 = _2263;
              }
              if (!(_2265 < 0.8029999732971191f)) {
                _2285 = _2265 + -0.8029999732971191f;
                _2297 = ((_2285 / exp2(log2(exp2(log2(_2285 * 3.4972610473632812f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8029999732971191f);
              } else {
                _2297 = _2265;
              }
              if (!(_2267 < 0.8799999952316284f)) {
                _2300 = _2267 + -0.8799999952316284f;
                _2312 = ((_2300 / exp2(log2(exp2(log2(_2300 * 6.810994625091553f) * 1.2000000476837158f) + 1.0f) * 0.8333333134651184f)) + 0.8799999952316284f);
              } else {
                _2312 = _2267;
              }
              _2320 = (_2252 - (_2262 * _2282));
              _2321 = (_2252 - (_2262 * _2297));
              _2322 = (_2252 - (_2262 * _2312));
            } else {
              _2320 = _2244;
              _2321 = _2247;
              _2322 = _2250;
            }
          } else {
            _2320 = _2244;
            _2321 = _2247;
            _2322 = _2250;
          }
          _2338 = ((mad(0.16386906802654266f, _2322, mad(0.14067870378494263f, _2321, (_2320 * 0.6954522132873535f))) - _2235) * cb0_012w) + _2235;
          _2339 = ((mad(0.0955343171954155f, _2322, mad(0.8596711158752441f, _2321, (_2320 * 0.044794563204050064f))) - _2238) * cb0_012w) + _2238;
          _2340 = ((mad(1.0015007257461548f, _2322, mad(0.004025210160762072f, _2321, (_2320 * -0.005525882821530104f))) - _2241) * cb0_012w) + _2241;
          _2344 = max(max(_2338, _2339), _2340);
          _2349 = (max(_2344, 1.000000013351432e-10f) - max(min(min(_2338, _2339), _2340), 1.000000013351432e-10f)) / max(_2344, 0.009999999776482582f);
          _2362 = ((_2339 + _2338) + _2340) + (sqrt((((_2340 - _2339) * _2340) + ((_2339 - _2338) * _2339)) + ((_2338 - _2340) * _2338)) * 1.75f);
          _2363 = _2362 * 0.3333333432674408f;
          _2364 = _2349 + -0.4000000059604645f;
          _2365 = _2364 * 5.0f;
          _2369 = max((1.0f - abs(_2364 * 2.5f)), 0.0f);
          _2380 = ((float((int)(((int)(uint)((int)(_2365 > 0.0f))) - ((int)(uint)((int)(_2365 < 0.0f))))) * (1.0f - (_2369 * _2369))) + 1.0f) * 0.02500000037252903f;
          if (!(_2363 <= 0.0533333346247673f)) {
            if (!(_2363 >= 0.1599999964237213f)) {
              _2389 = (((0.23999999463558197f / _2362) + -0.5f) * _2380);
            } else {
              _2389 = 0.0f;
            }
          } else {
            _2389 = _2380;
          }
          _2390 = _2389 + 1.0f;
          _2391 = _2390 * _2338;
          _2392 = _2390 * _2339;
          _2393 = _2390 * _2340;
          if (!((_2391 == _2392) && (_2392 == _2393))) {
            _2400 = ((_2391 * 2.0f) - _2392) - _2393;
            _2403 = ((_2339 - _2340) * 1.7320507764816284f) * _2390;
            _2405 = atan(_2403 / _2400);
            _2408 = (_2400 < 0.0f);
            _2409 = (_2400 == 0.0f);
            _2410 = (_2403 >= 0.0f);
            _2411 = (_2403 < 0.0f);
            _2422 = select((_2410 && _2409), 90.0f, select((_2411 && _2409), -90.0f, (select((_2411 && _2408), (_2405 + -3.1415927410125732f), select((_2410 && _2408), (_2405 + 3.1415927410125732f), _2405)) * 57.2957763671875f)));
          } else {
            _2422 = 0.0f;
          }
          _2427 = min(max(select((_2422 < 0.0f), (_2422 + 360.0f), _2422), 0.0f), 360.0f);
          if (_2427 < -180.0f) {
            _2436 = (_2427 + 360.0f);
          } else {
            if (_2427 > 180.0f) {
              _2436 = (_2427 + -360.0f);
            } else {
              _2436 = _2427;
            }
          }
          if ((_2436 > -67.5f) && (_2436 < 67.5f)) {
            _2442 = (_2436 + 67.5f) * 0.029629629105329514f;
            _2443 = int(_2442);
            _2445 = _2442 - float((int)(_2443));
            _2446 = _2445 * _2445;
            _2447 = _2446 * _2445;
            if (_2443 == 3) {
              _2475 = (((0.1666666716337204f - (_2445 * 0.5f)) + (_2446 * 0.5f)) - (_2447 * 0.1666666716337204f));
            } else {
              if (_2443 == 2) {
                _2475 = ((0.6666666865348816f - _2446) + (_2447 * 0.5f));
              } else {
                if (_2443 == 1) {
                  _2475 = (((_2447 * -0.5f) + 0.1666666716337204f) + ((_2446 + _2445) * 0.5f));
                } else {
                  _2475 = select((_2443 == 0), (_2447 * 0.1666666716337204f), 0.0f);
                }
              }
            }
          } else {
            _2475 = 0.0f;
          }
          _2484 = min(max(((((_2349 * 0.27000001072883606f) * (0.029999999329447746f - _2391)) * _2475) + _2391), 0.0f), 65535.0f);
          _2485 = min(max(_2392, 0.0f), 65535.0f);
          _2486 = min(max(_2393, 0.0f), 65535.0f);
          _2499 = min(max(mad(-0.21492856740951538f, _2486, mad(-0.2365107536315918f, _2485, (_2484 * 1.4514392614364624f))), 0.0f), 65504.0f);
          _2500 = min(max(mad(-0.09967592358589172f, _2486, mad(1.17622971534729f, _2485, (_2484 * -0.07655377686023712f))), 0.0f), 65504.0f);
          _2501 = min(max(mad(0.9977163076400757f, _2486, mad(-0.006032449658960104f, _2485, (_2484 * 0.008316148072481155f))), 0.0f), 65504.0f);
          _2502 = dot(float3(_2499, _2500, _2501), float3(0.2722287178039551f, 0.6740817427635193f, 0.053689517080783844f));
          _15[0] = cb0_010x;
          _15[1] = cb0_010y;
          _15[2] = cb0_010z;
          _15[3] = cb0_010w;
          _15[4] = cb0_012x;
          _15[5] = cb0_012x;
          _16[0] = cb0_011x;
          _16[1] = cb0_011y;
          _16[2] = cb0_011z;
          _16[3] = cb0_011w;
          _16[4] = cb0_012y;
          _16[5] = cb0_012y;
          _2525 = log2(max((lerp(_2502, _2499, 0.9599999785423279f)), 1.000000013351432e-10f));
          _2526 = _2525 * 0.3010300099849701f;
          _2527 = log2(cb0_008x);
          _2528 = _2527 * 0.3010300099849701f;
          if (!(_2526 <= _2528)) {
            _2535 = log2(cb0_009x);
            _2536 = _2535 * 0.3010300099849701f;
            if ((_2526 > _2528) && (_2526 < _2536)) {
              _2544 = ((_2525 - _2527) * 0.9030900001525879f) / ((_2535 - _2527) * 0.3010300099849701f);
              _2545 = int(_2544);
              _2547 = _2544 - float((int)(_2545));
              _2549 = _15[min((uint)(_2545), 5u)];
              _2552 = _15[min((uint)((_2545 + 1)), 5u)];
              _2557 = _2549 * 0.5f;
              _2597 = dot(float3((_2547 * _2547), _2547, 1.0f), float3(mad((_15[min((uint)((_2545 + 2)), 5u)]), 0.5f, mad(_2552, -1.0f, _2557)), (_2552 - _2549), mad(_2552, 0.5f, _2557)));
            } else {
              if (!(_2526 >= _2536)) {
                _2597 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _2566 = log2(cb0_008z);
                if (!(_2526 < (_2566 * 0.3010300099849701f))) {
                  _2597 = (log2(cb0_008w) * 0.3010300099849701f);
                } else {
                  _2574 = ((_2525 - _2535) * 0.9030900001525879f) / ((_2566 - _2535) * 0.3010300099849701f);
                  _2575 = int(_2574);
                  _2577 = _2574 - float((int)(_2575));
                  _2579 = _16[min((uint)(_2575), 5u)];
                  _2582 = _16[min((uint)((_2575 + 1)), 5u)];
                  _2587 = _2579 * 0.5f;
                  _2597 = dot(float3((_2577 * _2577), _2577, 1.0f), float3(mad((_16[min((uint)((_2575 + 2)), 5u)]), 0.5f, mad(_2582, -1.0f, _2587)), (_2582 - _2579), mad(_2582, 0.5f, _2587)));
                }
              }
            }
          } else {
            _2597 = (log2(cb0_008y) * 0.3010300099849701f);
          }
          _11[0] = cb0_010x;
          _11[1] = cb0_010y;
          _11[2] = cb0_010z;
          _11[3] = cb0_010w;
          _11[4] = cb0_012x;
          _11[5] = cb0_012x;
          _12[0] = cb0_011x;
          _12[1] = cb0_011y;
          _12[2] = cb0_011z;
          _12[3] = cb0_011w;
          _12[4] = cb0_012y;
          _12[5] = cb0_012y;
          _2613 = log2(max((lerp(_2502, _2500, 0.9599999785423279f)), 1.000000013351432e-10f));
          _2614 = _2613 * 0.3010300099849701f;
          if (!(_2614 <= _2528)) {
            _2621 = log2(cb0_009x);
            _2622 = _2621 * 0.3010300099849701f;
            if ((_2614 > _2528) && (_2614 < _2622)) {
              _2630 = ((_2613 - _2527) * 0.9030900001525879f) / ((_2621 - _2527) * 0.3010300099849701f);
              _2631 = int(_2630);
              _2633 = _2630 - float((int)(_2631));
              _2635 = _11[min((uint)(_2631), 5u)];
              _2638 = _11[min((uint)((_2631 + 1)), 5u)];
              _2643 = _2635 * 0.5f;
              _2683 = dot(float3((_2633 * _2633), _2633, 1.0f), float3(mad((_11[min((uint)((_2631 + 2)), 5u)]), 0.5f, mad(_2638, -1.0f, _2643)), (_2638 - _2635), mad(_2638, 0.5f, _2643)));
            } else {
              if (!(_2614 >= _2622)) {
                _2683 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _2652 = log2(cb0_008z);
                if (!(_2614 < (_2652 * 0.3010300099849701f))) {
                  _2683 = (log2(cb0_008w) * 0.3010300099849701f);
                } else {
                  _2660 = ((_2613 - _2621) * 0.9030900001525879f) / ((_2652 - _2621) * 0.3010300099849701f);
                  _2661 = int(_2660);
                  _2663 = _2660 - float((int)(_2661));
                  _2665 = _12[min((uint)(_2661), 5u)];
                  _2668 = _12[min((uint)((_2661 + 1)), 5u)];
                  _2673 = _2665 * 0.5f;
                  _2683 = dot(float3((_2663 * _2663), _2663, 1.0f), float3(mad((_12[min((uint)((_2661 + 2)), 5u)]), 0.5f, mad(_2668, -1.0f, _2673)), (_2668 - _2665), mad(_2668, 0.5f, _2673)));
                }
              }
            }
          } else {
            _2683 = (log2(cb0_008y) * 0.3010300099849701f);
          }
          _13[0] = cb0_010x;
          _13[1] = cb0_010y;
          _13[2] = cb0_010z;
          _13[3] = cb0_010w;
          _13[4] = cb0_012x;
          _13[5] = cb0_012x;
          _14[0] = cb0_011x;
          _14[1] = cb0_011y;
          _14[2] = cb0_011z;
          _14[3] = cb0_011w;
          _14[4] = cb0_012y;
          _14[5] = cb0_012y;
          _2699 = log2(max((lerp(_2502, _2501, 0.9599999785423279f)), 1.000000013351432e-10f));
          _2700 = _2699 * 0.3010300099849701f;
          if (!(_2700 <= _2528)) {
            _2707 = log2(cb0_009x);
            _2708 = _2707 * 0.3010300099849701f;
            if ((_2700 > _2528) && (_2700 < _2708)) {
              _2716 = ((_2699 - _2527) * 0.9030900001525879f) / ((_2707 - _2527) * 0.3010300099849701f);
              _2717 = int(_2716);
              _2719 = _2716 - float((int)(_2717));
              _2721 = _13[min((uint)(_2717), 5u)];
              _2724 = _13[min((uint)((_2717 + 1)), 5u)];
              _2729 = _2721 * 0.5f;
              _2769 = dot(float3((_2719 * _2719), _2719, 1.0f), float3(mad((_13[min((uint)((_2717 + 2)), 5u)]), 0.5f, mad(_2724, -1.0f, _2729)), (_2724 - _2721), mad(_2724, 0.5f, _2729)));
            } else {
              if (!(_2700 >= _2708)) {
                _2769 = (log2(cb0_008w) * 0.3010300099849701f);
              } else {
                _2738 = log2(cb0_008z);
                if (!(_2700 < (_2738 * 0.3010300099849701f))) {
                  _2769 = (log2(cb0_008w) * 0.3010300099849701f);
                } else {
                  _2746 = ((_2699 - _2707) * 0.9030900001525879f) / ((_2738 - _2707) * 0.3010300099849701f);
                  _2747 = int(_2746);
                  _2749 = _2746 - float((int)(_2747));
                  _2751 = _14[min((uint)(_2747), 5u)];
                  _2754 = _14[min((uint)((_2747 + 1)), 5u)];
                  _2759 = _2751 * 0.5f;
                  _2769 = dot(float3((_2749 * _2749), _2749, 1.0f), float3(mad((_14[min((uint)((_2747 + 2)), 5u)]), 0.5f, mad(_2754, -1.0f, _2759)), (_2754 - _2751), mad(_2754, 0.5f, _2759)));
                }
              }
            }
          } else {
            _2769 = (log2(cb0_008y) * 0.3010300099849701f);
          }
          _2773 = cb0_008w - cb0_008y;
          _2774 = (exp2(_2597 * 3.321928024291992f) - cb0_008y) / _2773;
          _2776 = (exp2(_2683 * 3.321928024291992f) - cb0_008y) / _2773;
          _2778 = (exp2(_2769 * 3.321928024291992f) - cb0_008y) / _2773;
          _2781 = mad(0.15618768334388733f, _2778, mad(0.13400420546531677f, _2776, (_2774 * 0.6624541878700256f)));
          _2784 = mad(0.053689517080783844f, _2778, mad(0.6740817427635193f, _2776, (_2774 * 0.2722287178039551f)));
          _2787 = mad(1.0103391408920288f, _2778, mad(0.00406073359772563f, _2776, (_2774 * -0.005574649665504694f)));
          _2800 = min(max(mad(-0.23642469942569733f, _2787, mad(-0.32480329275131226f, _2784, (_2781 * 1.6410233974456787f))), 0.0f), 1.0f);
          _2801 = min(max(mad(0.016756348311901093f, _2787, mad(1.6153316497802734f, _2784, (_2781 * -0.663662850856781f))), 0.0f), 1.0f);
          _2802 = min(max(mad(0.9883948564529419f, _2787, mad(-0.008284442126750946f, _2784, (_2781 * 0.011721894145011902f))), 0.0f), 1.0f);
          _2805 = mad(0.15618768334388733f, _2802, mad(0.13400420546531677f, _2801, (_2800 * 0.6624541878700256f)));
          _2808 = mad(0.053689517080783844f, _2802, mad(0.6740817427635193f, _2801, (_2800 * 0.2722287178039551f)));
          _2811 = mad(1.0103391408920288f, _2802, mad(0.00406073359772563f, _2801, (_2800 * -0.005574649665504694f)));
          _2833 = min(max((min(max(mad(-0.23642469942569733f, _2811, mad(-0.32480329275131226f, _2808, (_2805 * 1.6410233974456787f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f);
          _2836 = min(max((min(max(mad(0.016756348311901093f, _2811, mad(1.6153316497802734f, _2808, (_2805 * -0.663662850856781f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f) * 0.012500000186264515f;
          _2837 = min(max((min(max(mad(0.9883948564529419f, _2811, mad(-0.008284442126750946f, _2808, (_2805 * 0.011721894145011902f))), 0.0f), 65535.0f) * cb0_008w), 0.0f), 65535.0f) * 0.012500000186264515f;
          _2984 = mad(-0.0832589864730835f, _2837, mad(-0.6217921376228333f, _2836, (_2833 * 0.0213131383061409f)));
          _2985 = mad(-0.010548308491706848f, _2837, mad(1.140804648399353f, _2836, (_2833 * -0.0016282059950754046f)));
          _2986 = mad(1.1529725790023804f, _2837, mad(-0.1289689838886261f, _2836, (_2833 * -0.00030004189466126263f)));
        } else {
          if (cb0_053w == 7) {
            _2864 = mad((WorkingColorSpace_128[0].z), _1292, mad((WorkingColorSpace_128[0].y), _1291, ((WorkingColorSpace_128[0].x) * _1290)));
            _2867 = mad((WorkingColorSpace_128[1].z), _1292, mad((WorkingColorSpace_128[1].y), _1291, ((WorkingColorSpace_128[1].x) * _1290)));
            _2870 = mad((WorkingColorSpace_128[2].z), _1292, mad((WorkingColorSpace_128[2].y), _1291, ((WorkingColorSpace_128[2].x) * _1290)));
            _2889 = exp2(log2(mad(_62, _2870, mad(_61, _2867, (_2864 * _60))) * 9.999999747378752e-05f) * 0.1593017578125f);
            _2890 = exp2(log2(mad(_65, _2870, mad(_64, _2867, (_2864 * _63))) * 9.999999747378752e-05f) * 0.1593017578125f);
            _2891 = exp2(log2(mad(_68, _2870, mad(_67, _2867, (_2864 * _66))) * 9.999999747378752e-05f) * 0.1593017578125f);
            _2984 = exp2(log2((1.0f / ((_2889 * 18.6875f) + 1.0f)) * ((_2889 * 18.8515625f) + 0.8359375f)) * 78.84375f);
            _2985 = exp2(log2((1.0f / ((_2890 * 18.6875f) + 1.0f)) * ((_2890 * 18.8515625f) + 0.8359375f)) * 78.84375f);
            _2986 = exp2(log2((1.0f / ((_2891 * 18.6875f) + 1.0f)) * ((_2891 * 18.8515625f) + 0.8359375f)) * 78.84375f);
          } else {
            if (!(cb0_053w == 8)) {
              if (cb0_053w == 9) {
                _2938 = mad((WorkingColorSpace_128[0].z), _1280, mad((WorkingColorSpace_128[0].y), _1279, ((WorkingColorSpace_128[0].x) * _1278)));
                _2941 = mad((WorkingColorSpace_128[1].z), _1280, mad((WorkingColorSpace_128[1].y), _1279, ((WorkingColorSpace_128[1].x) * _1278)));
                _2944 = mad((WorkingColorSpace_128[2].z), _1280, mad((WorkingColorSpace_128[2].y), _1279, ((WorkingColorSpace_128[2].x) * _1278)));
                _2984 = mad(_62, _2944, mad(_61, _2941, (_2938 * _60)));
                _2985 = mad(_65, _2944, mad(_64, _2941, (_2938 * _63)));
                _2986 = mad(_68, _2944, mad(_67, _2941, (_2938 * _66)));
              } else {
                _2957 = mad((WorkingColorSpace_128[0].z), _1306, mad((WorkingColorSpace_128[0].y), _1305, ((WorkingColorSpace_128[0].x) * _1304)));
                _2960 = mad((WorkingColorSpace_128[1].z), _1306, mad((WorkingColorSpace_128[1].y), _1305, ((WorkingColorSpace_128[1].x) * _1304)));
                _2963 = mad((WorkingColorSpace_128[2].z), _1306, mad((WorkingColorSpace_128[2].y), _1305, ((WorkingColorSpace_128[2].x) * _1304)));
                _2984 = exp2(log2(mad(_62, _2963, mad(_61, _2960, (_2957 * _60)))) * cb0_053z);
                _2985 = exp2(log2(mad(_65, _2963, mad(_64, _2960, (_2957 * _63)))) * cb0_053z);
                _2986 = exp2(log2(mad(_68, _2963, mad(_67, _2960, (_2957 * _66)))) * cb0_053z);
              }
            } else {
              _2984 = _1290;
              _2985 = _1291;
              _2986 = _1292;
            }
          }
        }
      }
    }
  }
  u0[int3((int)(SV_DispatchThreadID.x), (int)(SV_DispatchThreadID.y), (int)(SV_DispatchThreadID.z))] = float4((_2984 * 0.9523810148239136f), (_2985 * 0.9523810148239136f), (_2986 * 0.9523810148239136f), 0.0f);
}