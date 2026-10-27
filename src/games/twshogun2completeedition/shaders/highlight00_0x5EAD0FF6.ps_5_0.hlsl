// gbuffer_highlight.hlsl ps_main: outlines of highlighted objects behind transparent ones (stencil), written from the
// game's source. Render scale (DLSS addon): the full-screen quad's texcoord runs 0-1 over the viewport, the top-left
// render scale part of the full-size stencil: read at the pixel instead. At full size the same as the game's.
#define HIGHLIGHT_TRANSPARENT_STENCIL_REF (1 << 2)
#define HIGHLIGHT_COLOUR_SHIFT            5
#define HIGHLIGHT_COLOUR_MASK             96

cbuffer highlight_parameters : register(b0)
{
  float4 g_highlight_colours[3] : packoffset(c0);
}

Texture2D<uint4> t_depth_stencil_texture : register(t0);

float4 main(float4 position : SV_Position0, float2 tex : TEXCOORD0) : SV_Target0
{
  const int2 device_coords = int2(position.xy);

  const uint center_stencil = t_depth_stencil_texture.Load(int3(device_coords, 0)).g;
  const uint center_colour_idx = (center_stencil & HIGHLIGHT_COLOUR_MASK) >> HIGHLIGHT_COLOUR_SHIFT;
  if (!(center_stencil & HIGHLIGHT_TRANSPARENT_STENCIL_REF) || center_colour_idx != 0) discard;

  float4 colour = 0;
  float colour_count = 0;
  float total_count = 0;
  for (int y = -2; y < 3; y++)
  {
    for (int x = -2; x < 3; x++)
    {
      const uint stencil = t_depth_stencil_texture.Load(int3(device_coords + int2(x, y), 0)).g;
      if (stencil & HIGHLIGHT_TRANSPARENT_STENCIL_REF)
      {
        const uint colour_idx = (stencil & HIGHLIGHT_COLOUR_MASK) >> HIGHLIGHT_COLOUR_SHIFT;
        if (colour_idx != 0)
        {
          colour += g_highlight_colours[colour_idx - 1];
          ++colour_count;
        }
      }
      ++total_count;
    }
  }

  if (colour_count > 0) colour.rgb /= colour_count;
  if (total_count > 0) colour.a /= total_count;
  return colour;
}
