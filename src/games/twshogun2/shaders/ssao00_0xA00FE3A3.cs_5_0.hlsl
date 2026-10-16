// ssao_3k.hlsl cs50_new_sweep: the ambient occlusion's line sweeps (convex hull of the depth along 36 directions),
// written from the game's source.
// Render scale (DLSS addon): the 3D passes draw into the top-left render scale part of the full-size depth. The view
// positions came from the screen UV (the rendered part squeezed into a corner of the view: wrong occlusion) and the
// lines ran through the stale rest. Now the view positions come from the viewport (the camera's g_viewport_dimensions,
// patched to the render size) and the depth outside the rendered part is the sky (far). At full size the same as the
// game's.
cbuffer camera_VS_PS : register(b0)
{
  float4x4 inv_projection : packoffset(c26);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
}

cbuffer cb_ssao : register(b1)
{
  float g_SSAO_fallof_mult : packoffset(c0);
}

Texture2D<float> gbuffer_depth_texture : register(t0);
RWTexture2D<unorm float> t_rw_line_buffer : register(u0);

static const uint new_line_step = 10;
static const uint new_line_spacing = 2;
static const uint new_num_directions_x = 9;
static const uint hull_size = 8;
static const uint num_threads = 256;

#define RAND_MAX int(0x7FFF)

struct HullFront
{
  min16float3 hull[num_threads];
};
groupshared HullFront hull_cache[hull_size];

void set_hull(int point_index, int group_index, min16float3 value)
{
  hull_cache[point_index & (hull_size - 1)].hull[group_index] = value;
}

min16float3 get_hull(int point_index, int group_index)
{
  return hull_cache[point_index & (hull_size - 1)].hull[group_index];
}

float3 get_view_pos_from_depth(uint2 vpos, float depth)
{
  const float2 tc = (vpos - g_viewport_origin) * g_viewport_dimensions.zw;
  const float4 view_pos = mul(float4(tc * float2(2.f, -2.f) - float2(1.f, -1.f), depth, 1), inv_projection);
  return view_pos.xyz / view_pos.w;
}

static const int2 dir_table[36] = {
  { 10, 8 },   { 10, 6 },   { 10, 4 },   { 10, 2 },   { 10, 0 },   { 10, -2 },  { 10, -4 },  { 10, -6 },  { 10, -8 },
  { 8, -10 },  { 6, -10 },  { 4, -10 },  { 2, -10 },  { 0, -10 },  { -2, -10 }, { -4, -10 }, { -6, -10 }, { -8, -10 },
  { -10, -8 }, { -10, -6 }, { -10, -4 }, { -10, -2 }, { -10, 0 },  { -10, 2 },  { -10, 4 },  { -10, 6 },  { -10, 8 },
  { -8, 10 },  { -6, 10 },  { -4, 10 },  { -2, 10 },  { 0, 10 },   { 2, 10 },   { 4, 10 },   { 6, 10 },   { 8, 10 }
};

uint wang_hash(uint seed)
{
  seed = (seed ^ 61) ^ (seed >> 16);
  seed *= 9;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2d;
  seed = seed ^ (seed >> 15);
  return seed;
}

int jitter(uint d, uint l)
{
  const uint seed_table[36] = { 7944, 23397, 304, 3806, 1983, 169, 169, 7703, 3347, 7944, 23397, 304,
                                679,  1983,  169, 169,  7703, 954, 3347, 7703, 169,  169,  1983, 3806,
                                304,  23397, 7944, 954, 7703, 169, 169,  1983, 679,  304,  23397, 7944 };
  const uint seed = seed_table[d % 36] + 48271 * (l & 15);
  return int(wang_hash(seed) & RAND_MAX);
}

float falloff(float2 h, float depth)
{
  const float d = length(h) * lerp(0.25f, 0.0001f, depth * depth);
  const float r = 1.0;
  return max(0, (r * g_SSAO_fallof_mult) / (r + d * d) - 0.3f);
}

float compute_occlusion_new(float2 h, float2 u, float depth)
{
  const float sin_t = -0.85f;
  return sin_t + max(0, dot(normalize(h), u) - sin_t) * falloff(h, depth);
}

[numthreads(num_threads, 1, 1)]
void main(uint2 coords : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
  uint width, height;
  gbuffer_depth_texture.GetDimensions(width, height);
  const int2 rendered = int2(min(g_viewport_dimensions.xy, float2(width, height)));

  const uint l = coords.x % (width / new_line_spacing);  // Line index
  const uint d = coords.x / (width / new_line_spacing);  // Direction index

  const int2 dir = dir_table[d % 36];
  const float2 dir_n = 0.1f * dir_table[d % 36];

  int2 start;
  uint iteration_length;
  if (abs(dir.x) > abs(dir.y))
  {
    start.x = (dir.x > 0) ? 0 : (width - 1);
    start.y = l * new_line_spacing;
    iteration_length = width / new_line_step;
  }
  else
  {
    start.x = l * new_line_spacing;
    start.y = (dir.y > 0) ? 0 : (height - 1);
    iteration_length = height / new_line_step;
  }

  const uint2 line_buffer_offset = uint2((d % new_num_directions_x) * (width / new_line_step),
                                         (d / new_num_directions_x) * (width / new_line_spacing));

  bool at_edge = true;
  uint num_points = 0;
  uint hull_base = 0;

  if (start.x >= width || start.y >= height) return;

  start += dir * jitter(d, l) / RAND_MAX;

  for (uint i = 0; i < iteration_length; i++)
  {
    int2 pos = start + dir * i;

    if (any(pos < 0) || any(pos >= int2(width, height)))
    {
      pos = (pos + int2(width, height)) % int2(width, height);
      start = pos - dir * i;

      at_edge = true;
      num_points = 0;
      hull_base = 0;
    }

    const float depth = any(pos >= rendered) ? 1.f : gbuffer_depth_texture[pos].r;
    const float3 view_pos = get_view_pos_from_depth(pos, depth);

    // Project the view position to hull space.
    const float2 hull_pos = float2(dot(view_pos.xy, dir_n), view_pos.z);
    const float2 u = -normalize(hull_pos);

    if (num_points >= 2)
    {
      min16float3 hull2 = get_hull(hull_base - 2, group_index);
      float2 h1 = float3(get_hull(hull_base - 1, group_index)).xy - hull_pos;
      float2 h2 = float3(hull2).xy - hull_pos;

      [loop]
      for (uint j = 0; j < hull_size && num_points >= 2 && compute_occlusion_new(h1, u, depth) < compute_occlusion_new(h2, u, depth); j++)
      {
        num_points--;
        hull_base--;

        h1 = h2;
        hull2 = get_hull(hull_base - 2, group_index);
        h2 = float3(hull2).xy - hull_pos;
      }

      const float occlusion = 1.0 - compute_occlusion_new(h1, u, depth);
      t_rw_line_buffer[line_buffer_offset + uint2(i, l)] = occlusion;

      if (at_edge)
      {
        // The first proper occlusion value, also for the previous 2 samples (at the edge of the buffer).
        t_rw_line_buffer[line_buffer_offset + uint2(i - 1, l)] = occlusion;
        t_rw_line_buffer[line_buffer_offset + uint2(i - 2, l)] = occlusion;
        at_edge = false;
      }
    }

    set_hull(hull_base, group_index, min16float3(float3(hull_pos, 0)));
    num_points = min(num_points + 1, hull_size - 1);
    hull_base++;
  }
}
