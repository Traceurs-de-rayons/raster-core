// Bloom, step 1: keeps what is brighter than white (the emissive surfaces) while shrinking
// the image. Rendered at a fraction of the viewport size.

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]] Sampler2D sceneColor;

static const float kThreshold = 1.1;

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  uint width, height;
  sceneColor.GetDimensions(width, height);
  float2 texel = 1.0 / float2(width, height);

  // 4 bilinear taps: every source pixel under the target pixel takes part.
  float3 sum = float3(0.0);
  sum += max(sceneColor.Sample(texCoord + texel * float2(-1.0, -1.0)).rgb - kThreshold, 0.0);
  sum += max(sceneColor.Sample(texCoord + texel * float2(1.0, -1.0)).rgb - kThreshold, 0.0);
  sum += max(sceneColor.Sample(texCoord + texel * float2(-1.0, 1.0)).rgb - kThreshold, 0.0);
  sum += max(sceneColor.Sample(texCoord + texel * float2(1.0, 1.0)).rgb - kThreshold, 0.0);

  output.color = float4(sum * 0.25, 1.0);
  return output;
}
