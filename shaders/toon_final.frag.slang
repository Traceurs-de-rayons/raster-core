// Last pass of the toon graph: speed effects (radial blur and vignette growing with
// effects[0].x), the sight the player aims through (effects[4]), bloom, then tone mapping to
// the displayed range, and the grade of the picture (effects[5] to [7]: vignette, chromatic
// aberration, grain, sharpening, saturation, contrast, exposure, a color filter, and what the
// health of the player does to it).

#include "frame_uniforms.slang"

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]] Sampler2D sceneColor;
[[vk::binding(1, 0)]] Sampler2D bloomNear;
[[vk::binding(2, 0)]] Sampler2D bloomWide;
[[vk::binding(21, 0)]] ConstantBuffer<LightUBO> lighting;

static const uint kBlurTaps = 8;

// A number from a pixel and a frame, 0..1, for the grain.
float hash(float2 p) {
  float3 q = frac(float3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return frac((q.x + q.y) * q.z);
}

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
  // Chromatic aberration: red and blue pulled apart, more toward the edges.
  float4 look = lighting.effects[5];
  float2 split = fromCenter * saturate(look.y) * 0.012 * smoothstep(0.0, 0.7, radius);
  float3 color = float3(0.0);
  float stretch = speed * 0.09 * smoothstep(0.12, 0.6, radius) + outside * 0.05;
  for (uint i = 0; i < kBlurTaps; ++i) {
    float2 at = texCoord - fromCenter * stretch * (float(i) / float(kBlurTaps - 1));
    if (look.y > 0.0)
      color += float3(sceneColor.Sample(at + split).r, sceneColor.Sample(at).g, sceneColor.Sample(at - split).b);
    else
      color += sceneColor.Sample(at).rgb;
  }
  color /= float(kBlurTaps);

  // Sharpening: the picture minus what its neighbours blur it to.
  if (look.w > 0.0) {
    float2 pixel = 1.0 / float2(float(width), float(height));
    float3 center = sceneColor.Sample(texCoord).rgb;
    float3 around = sceneColor.Sample(texCoord + float2(pixel.x, 0.0)).rgb + sceneColor.Sample(texCoord - float2(pixel.x, 0.0)).rgb +
                    sceneColor.Sample(texCoord + float2(0.0, pixel.y)).rgb + sceneColor.Sample(texCoord - float2(0.0, pixel.y)).rgb;
    color += (center * 4.0 - around) * saturate(look.w) * 0.35;
  }

  color += (bloomNear.Sample(texCoord).rgb * 0.7 + bloomWide.Sample(texCoord).rgb) * lighting.effects[0].w;

  float vignette = smoothstep(0.38, 0.85, radius);
  color *= 1.0 - vignette * saturate(0.12 * look.x + 0.38 * speed);

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

  // The grade: exposure and the color filter, then tone mapping, saturation and contrast.
  // All zero (nothing set yet) is the picture as it is.
  float4 grade = lighting.effects[6];
  float4 filter = lighting.effects[7];
  bool graded = grade.x + grade.y + grade.z > 0.0;
  if (graded)
    color *= grade.z * filter.rgb;
  color = toneMap(max(color, 0.0));
  float health = graded ? saturate(grade.w) : 0.0;
  float luma = dot(color, float3(0.299, 0.587, 0.114));
  if (graded) {
    color = lerp(float3(luma), color, grade.x * (1.0 - 0.65 * health));
    color = (color - 0.45) * grade.y + 0.45;
  }

  // Hurt: a red flash at the edges, and low on health a slow red pulse there.
  float edge = smoothstep(0.22, 0.75, radius);
  float pulse = health * (0.55 + 0.45 * sin(lighting.effects[0].y * 5.5));
  float hurt = saturate(saturate(filter.w) * 0.8 + pulse * 0.5) * edge;
  color = lerp(color, float3(0.62, 0.02, 0.04), hurt);

  // Grain, a new pattern every frame.
  if (look.z > 0.0)
    color += (hash(texCoord * float2(float(width), float(height)) + frac(lighting.effects[0].y * 7.13) * 431.0) - 0.5) * saturate(look.z) * 0.09;

  output.color = float4(saturate(color), 1.0);
  return output;
}
