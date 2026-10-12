// Last pass of the toon graph: speed effects (radial blur and vignette growing with
// effects[0].x), bloom, then tone mapping to the displayed range.

#include "frame_uniforms.slang"

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]] Sampler2D sceneColor;
[[vk::binding(1, 0)]] Sampler2D bloomNear;
[[vk::binding(2, 0)]] Sampler2D bloomWide;
[[vk::binding(21, 0)]] ConstantBuffer<LightUBO> lighting;

static const uint kBlurTaps = 8;

// Colors up to 0.8 are untouched (flat colors stay exactly as authored), brighter ones roll off to 1.
float3 toneMap(float3 color) {
  float3 over = max(color - 0.8, 0.0);
  return min(color, 0.8) + 0.2 * (1.0 - exp(-over * 4.0));
}

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  float speed = saturate(lighting.effects[0].x);
  float2 fromCenter = texCoord - 0.5;
  float radius = length(fromCenter);

  // The center, where the crosshair is, always stays sharp.
  float3 color = float3(0.0);
  float stretch = speed * 0.09 * smoothstep(0.12, 0.6, radius);
  for (uint i = 0; i < kBlurTaps; ++i)
    color += sceneColor.Sample(texCoord - fromCenter * stretch * (float(i) / float(kBlurTaps - 1))).rgb;
  color /= float(kBlurTaps);

  color += (bloomNear.Sample(texCoord).rgb * 0.7 + bloomWide.Sample(texCoord).rgb) * lighting.effects[0].w;

  float vignette = smoothstep(0.38, 0.85, radius);
  color *= 1.0 - vignette * (0.12 + 0.38 * speed);

  output.color = float4(toneMap(color), 1.0);
  return output;
}
