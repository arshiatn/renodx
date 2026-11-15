// Batched Unit Capture (rt_core.hpp FlushBatch): a run of batched unit draws on one animation texture, their corners as
// their per-draw stream output would have written them (instance after instance, each its mesh's vertices in its remap
// order), into the output at each draw's place. A group is 64 corners of one draw.
// The includer includes its capture version (main renamed: its GameMain, bones at t0 and the width at b2 as its draws had
// them) and defines RT_BATCH_MESH_TEXELS (float4s a streamed vertex): 2 for vs30_main's (POSITION0-1: the place, two bones'
// weight and indices; rt_batch_unit_mesh), 3 for main_4_bones' (POSITION0-2: the place, four bones' indices, three weights;
// rt_batch_unit_4_bones_mesh). The rows are TEXCOORD0-2 and TEXCOORD3.x in both (rt_batch_unit_rows, _4_bones_rows).

// The capture version's world position from a streamed vertex and an instance's 4 rows: GameMain with identity rows (the
// world position comes out as the clip position, as in its main), the inputs its position doesn't read 0.
float4 RtBatchWorld(float4 mesh[RT_BATCH_MESH_TEXELS], float4 rows[4])
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float4 o8 = 0;
  float4 o9 = 0;
  float4 o10 = 0;
  uint4 p10 = 0;
  float4 o11 = 0;
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
#if RT_BATCH_MESH_TEXELS == 3
  GameMain(mesh[0], mesh[1], mesh[2], 0, 0, 0, 0, rows[0], rows[1], rows[2], float4(rows[3].x, 0, 0, 0), 0, 0, 0, 0, 0, o0, o1, o2, o3,
           o4, o5, o6, o7, o8, o9, o10, p10, o11);
#else
  GameMain(mesh[0], mesh[1], 0, 0, 0, 0, rows[0], rows[1], rows[2], float4(rows[3].x, 0, 0, 0), 0, 0, 0, 0, 0, o0, o1, o2, o3, o4, o5,
           o6, o7, o8, o9, o10, p10, o11);
#endif
  return o0;
}

Buffer<float4> rt_batch_meshes : register(t2);  // RT_BATCH_MESH_TEXELS a vertex
Buffer<float4> rt_batch_rows : register(t3);    // 4 an instance: TEXCOORD0-2, then (bone base, its draw's animation width)
// 2 a draw: (its mesh's first texel, vertices, its rows' first instance, instances), (its output's first texel, its first
// group in the run, stale, 0).
Buffer<uint4> rt_batch_draws : register(t4);
RWByteAddressBuffer rt_batch_output : register(u0);

cbuffer rt_batch_run : register(b3)
{
  uint rt_batch_first_draw;
  uint rt_batch_draw_count;
  uint rt_batch_groups;
  float rt_batch_width;  // the animation texture's width (b2's)
}

[numthreads(64, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint thread : SV_GroupIndex)
{
  const uint group = group_id.y * 65535 + group_id.x;
  if (group >= rt_batch_groups) return;
  // The run's last draw whose first group is this one or before.
  uint low = 0;
  uint high = rt_batch_draw_count;
  while (high - low > 1) {
    const uint middle = (low + high) / 2;
    if (rt_batch_draws[((rt_batch_first_draw + middle) * 2) + 1].y <= group) {
      low = middle;
    } else {
      high = middle;
    }
  }
  const uint4 draw = rt_batch_draws[(rt_batch_first_draw + low) * 2];
  const uint4 place = rt_batch_draws[((rt_batch_first_draw + low) * 2) + 1];
  const uint corner = ((group - place.y) * 64) + thread;
  if (corner >= draw.y * draw.w) return;
  const uint instance = corner / draw.y;
  const uint vertex = corner - (instance * draw.y);
  float4 mesh[RT_BATCH_MESH_TEXELS];
  for (uint texel = 0; texel < RT_BATCH_MESH_TEXELS; ++texel) {
    mesh[texel] = rt_batch_meshes[draw.x + (vertex * RT_BATCH_MESH_TEXELS) + texel];
  }
  float4 rows[4];
  for (uint row = 0; row < 4; ++row) {
    rows[row] = rt_batch_rows[((draw.z + instance) * 4) + row];
  }
  float4 world = RtBatchWorld(mesh, rows);
  // Its bones written by the game since its draw (another soldier's now), or its draw's width not the texture's: left out (a
  // NaN corner: the helper leaves out its triangles).
  if (place.z != 0 || floor(0.5 + rows[3].y) != floor(0.5 + rt_batch_width)) {
    world = asfloat(0x7FC00000u).xxxx;
  }
  rt_batch_output.Store4((place.x + corner) * 16, asuint(world));
}
