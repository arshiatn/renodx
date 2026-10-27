// Ray-traced sun shadows: the stream-output signature for the game's depth prepass vertex shaders, whose only output
// is SV_Position (rt_shadows.hpp builds its stream-output stage from this output signature).
float4 main(float4 position : ATTRIBUTE0) : SV_Position {
  return position;
}
