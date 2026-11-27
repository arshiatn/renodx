// The helper's micromap bake cutoff against its ray queries' alpha test (X14), on the CPU: NVIDIA's OMM SDK 1.12.0 CPU
// baker (the helper's, build\downloads\omm\extracted) bakes one triangle over a mask whose every texel sits exactly at the
// cutoff (k / 255 of a UNORM8 texel, every k = 1 .. 255: the table prints k = 1, 64, 128, 200, 254, 255).
// The ray queries' AlphaPasses (dlaa_helper/rt_scene.hlsli) pass alpha >= cutoff: such a triangle is solid. The baker's
// texel test is "texture > alphaCutoff ? Opaque : Transparent" (src: omm.h:406): at the raw cutoff the triangle bakes
// fully transparent (special index -1), a state no alpha test sees again (D3D12 Raytracing spec, "Opacity micromaps and
// TraceRayInline"). Fixed (rt_d3d12.hpp BakeCutoff: the next float toward 0): fully opaque (-2), as AlphaPasses says.
// The masks one texel below the cutoff bake fully transparent either way (the fix moves only the tie).
// Also read: rt_d3d12.hpp's BakeCutoff holds std::nextafter(cutoff, 0.f) and both bakers (GPU and CPU) take it.
// tests.exe <repository>: exit 0 when the fixed rule matches AlphaPasses at every k and the raw cutoff (CONTROL) does not.
#include <omm.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t MASK_SIZE = 8;

// The bake's special index of one triangle over a MASK_SIZE x MASK_SIZE mask of `texel` everywhere (0: not special).
int32_t BakeTriangle(ommBaker baker, uint8_t texel, float cutoff) {
  const std::vector<uint8_t> mask(MASK_SIZE * MASK_SIZE, texel);
  ommCpuTextureMipDesc mip = ommCpuTextureMipDescDefault();
  mip.width = MASK_SIZE;
  mip.height = MASK_SIZE;
  mip.rowPitch = MASK_SIZE;
  mip.textureData = mask.data();
  ommCpuTextureDesc texture_desc = ommCpuTextureDescDefault();
  texture_desc.format = ommCpuTextureFormat_UNORM8;
  texture_desc.mips = &mip;
  texture_desc.mipCount = 1;
  texture_desc.alphaCutoff = cutoff;
  ommCpuTexture texture = nullptr;
  if (ommCpuCreateTexture(baker, &texture_desc, &texture) != ommResult_SUCCESS) return 1;
  // UV16_FLOAT corners (0, 0), (1, 0), (0, 1): 0x3C00 is 1.0 as a half.
  const uint16_t uvs[6] = {0, 0, 0x3C00, 0, 0, 0x3C00};
  const uint32_t corners[3] = {0, 1, 2};
  ommCpuBakeInputDesc input = ommCpuBakeInputDescDefault();
  input.bakeFlags = ommCpuBakeFlags_Force32BitIndices;
  input.texture = texture;
  input.runtimeSamplerDesc = {.addressingMode = ommTextureAddressMode_Wrap, .filter = ommTextureFilterMode_Linear, .borderAlpha = 0.f};
  input.alphaMode = ommAlphaMode_Test;
  input.texCoordFormat = ommTexCoordFormat_UV16_FLOAT;
  input.texCoords = uvs;
  input.indexFormat = ommIndexFormat_UINT_32;
  input.indexBuffer = corners;
  input.indexCount = 3;
  input.alphaCutoff = cutoff;
  input.maxSubdivisionLevel = 3;
  ommCpuBakeResult result = nullptr;
  const ommCpuBakeResultDesc* desc = nullptr;
  int32_t special = 1;
  if (ommCpuBake(baker, &input, &result) == ommResult_SUCCESS && ommCpuGetBakeResultDesc(result, &desc) == ommResult_SUCCESS
      && desc->indexCount == 1 && desc->indexFormat == ommIndexFormat_UINT_32) {
    const int32_t index = *static_cast<const int32_t*>(desc->indexBuffer);
    special = (index < 0) ? index : 0;
  }
  if (result != nullptr) ommCpuDestroyBakeResult(result);
  ommCpuDestroyTexture(baker, texture);
  return special;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: tests.exe <repository>\n");
    return 2;
  }
  std::ifstream source(std::string(argv[1]) + "\\src\\games\\twattila\\dlaa_helper\\rt_d3d12.hpp");
  std::stringstream text;
  text << source.rdbuf();
  const std::string host = text.str();
  size_t takers = 0;
  for (size_t at = host.find("BakeCutoff(bake->cutoff)"); at != std::string::npos; at = host.find("BakeCutoff(bake->cutoff)", at + 1)) ++takers;
  const bool rule_in_source = host.find("return std::nextafter(cutoff, 0.f);") != std::string::npos && takers == 2;
  std::printf("rt_d3d12.hpp: BakeCutoff's nextafter %s, %zu bakers take it (2 expected)\n", rule_in_source ? "found" : "MISSING", takers);

  ommBakerCreationDesc creation = ommBakerCreationDescDefault();
  creation.type = ommBakerType_CPU;
  ommBaker baker = nullptr;
  if (ommCreateBaker(&creation, &baker) != ommResult_SUCCESS) {
    std::printf("SETUP FAILED: ommCreateBaker\n");
    return 2;
  }
  bool fixed_matches = true;
  uint32_t raw_ties = 0;  // ties the raw cutoff bakes other than opaque
  bool below_transparent = true;
  std::printf("| k | cutoff k/255 | AlphaPasses | raw cutoff (CONTROL) | BakeCutoff | one texel below, BakeCutoff |\n|---|---|---|---|---|---|\n");
  for (uint32_t value = 1; value <= 255; ++value) {
    const uint8_t k = static_cast<uint8_t>(value);
    // D3D's UNORM8 -> float (D3D11.3 3.2.3.6: c / 255, the float nearest), what SampleLevel returns at a texel's centre.
    const float cutoff = static_cast<float>(k) / 255.f;
    const int32_t raw = BakeTriangle(baker, k, cutoff);
    const int32_t fixed = BakeTriangle(baker, k, std::nextafter(cutoff, 0.f));
    const int32_t below = BakeTriangle(baker, static_cast<uint8_t>(k - 1), std::nextafter(cutoff, 0.f));
    // AlphaPasses: texel >= cutoff, every sample of a uniform mask: solid.
    fixed_matches = fixed_matches && fixed == ommSpecialIndex_FullyOpaque;
    raw_ties += (raw != ommSpecialIndex_FullyOpaque) ? 1u : 0u;
    below_transparent = below_transparent && below == ommSpecialIndex_FullyTransparent;
    if (k == 1 || k == 64 || k == 128 || k == 200 || k >= 254 || fixed != ommSpecialIndex_FullyOpaque || below != ommSpecialIndex_FullyTransparent) {
      std::printf("| %u | %.9g | solid | %d | %d | %d |\n", k, cutoff, raw, fixed, below);
    }
  }
  ommDestroyBaker(baker);
  std::printf("(-1 fully transparent, -2 fully opaque)\n");
  std::printf("%s fixed: all 255 ties bake fully opaque as AlphaPasses passes them\n", fixed_matches ? "PASS" : "FAIL");
  std::printf("%s CONTROL raw cutoff: %u of 255 ties bake other than opaque (the bug seen)\n", (raw_ties != 0) ? "PASS" : "FAIL", raw_ties);
  std::printf("%s one texel below the cutoff: fully transparent with the fix, all 255\n", below_transparent ? "PASS" : "FAIL");
  return (rule_in_source && fixed_matches && raw_ties != 0 && below_transparent) ? 0 : 1;
}
