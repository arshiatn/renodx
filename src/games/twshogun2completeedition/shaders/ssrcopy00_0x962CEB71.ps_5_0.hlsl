// environment_specular_light.hlsl ps40_copy: adds the screen-space reflections to the scene (full-screen quad).
// Render scale (DLSS addon): the quad's texcoord runs 0-1 over the viewport, the top-left render scale part of the
// full-size reflection texture: it squeezed the whole texture into the picture (debug4: the river's reflections as a
// small copy in the sky, none on the river). Read at the pixel instead; at full size the same as the game's.
Texture2D<float4> t_in_radiance : register(t0);

float4 main(float4 position : SV_Position0, float2 tex : TEXCOORD0) : SV_Target0
{
  return t_in_radiance.Load(int3(position.xy, 0));
}
