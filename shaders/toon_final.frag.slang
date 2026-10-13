// Last pass of the toon graph: speed effects (radial blur and vignette growing with
// effects[0].x), the sight the player aims through (effects[4]), bloom, then tone mapping to
// the displayed range.

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

  // The sight: distances from its center are in screen heights, so its lens is round.
  uint width, height;
  sceneColor.GetDimensions(width, height);
  float4 sight = lighting.effects[4];
  float2 fromLens = (texCoord - sight.xy) * float2(float(width) / float(height), 1.0);
  float lensRadius = max(abs(sight.z), 1e-4);
  float lens = length(fromLens) / lensRadius; // 1 on the rim
  float scope = sight.z > 0.0 ? saturate(sight.w) : 0.0;
  float outside = scope * smoothstep(0.96, 1.04, lens);

  // The center, where the crosshair is, always stays sharp. Around a scope everything is
  // out of focus: the eye is on the lens.
  float3 color = float3(0.0);
  float stretch = speed * 0.09 * smoothstep(0.12, 0.6, radius) + outside * 0.05;
  for (uint i = 0; i < kBlurTaps; ++i)
    color += sceneColor.Sample(texCoord - fromCenter * stretch * (float(i) / float(kBlurTaps - 1))).rgb;
  color /= float(kBlurTaps);

  color += (bloomNear.Sample(texCoord).rgb * 0.7 + bloomWide.Sample(texCoord).rgb) * lighting.effects[0].w;

  float vignette = smoothstep(0.38, 0.85, radius);
  color *= 1.0 - vignette * (0.12 + 0.38 * speed);

  if (scope > 0.0) {
    // Through the lens: darker toward its rim, and a little cooler, as glass is.
    float inside = 1.0 - smoothstep(0.96, 1.0, lens);
    color *= 1.0 - scope * (outside * 0.97 + inside * 0.45 * smoothstep(0.6, 1.0, lens));
    color = lerp(color, color * float3(0.93, 1.0, 1.04), scope * inside * 0.6);
    // The rim itself.
    color *= 1.0 - scope * 0.9 * (1.0 - smoothstep(0.0, 0.035, abs(lens - 1.0)));

    // Reticle: four thin lines stopping short of the center, thicker toward the rim, and a dot.
    float2 offset = abs(fromLens) / lensRadius;
    float thickness = lerp(0.006, 0.02, smoothstep(0.45, 0.5, max(offset.x, offset.y)));
    float hairs = max(step(offset.x, thickness) * step(0.06, offset.y), step(offset.y, thickness) * step(0.06, offset.x));
    float middle = 1.0 - smoothstep(0.008, 0.014, lens);
    color = lerp(color, float3(0.02, 0.02, 0.02), scope * inside * max(hairs, middle) * 0.95);
  } else if (sight.z < 0.0 && lens < 1.0) {
    // A red dot sight throws its dot where the weapon points, the middle of the screen,
    // wherever the sight itself sways: it only shows through its window. Sized in pixels, so
    // it stays sharp: a small disc with one pixel of smooth edge, and a faint halo.
    float pixels = length(fromCenter * float2(float(width), float(height)));
    float size = max(float(height) * 0.0032, 2.0);
    float disc = 1.0 - smoothstep(size - 0.6, size + 0.6, pixels);
    float halo = 0.22 * (1.0 - smoothstep(size, size * 3.5, pixels));
    color = lerp(color, float3(1.0, 0.05, 0.03), saturate(sight.w) * max(disc, halo));
  }

  output.color = float4(toneMap(color), 1.0);
  return output;
}
