// Per-frame uniforms bound to every pass (RasterCore::binding::Light).
// Mirrors RasterCore::internal::LightUniform (pipeline/internal.hpp).

struct LightUBO {
  float4 cameraPosition;
  float4 cameraForward;
  float4 cascadeSplits; // view depth at which each cascade ends
  float4 shadowParams;  // x = tan(sun half angle), y = cascade blend fraction
  uint4 counts;         // x = lights, y = active cascades (0 = no sun shadow)
  // Free parameters for the graph's own shaders (RasterPipeline::setEffectParams). The toon
  // graph reads them as RasterCore::ToonParams:
  //   [0] x = speed effect 0..1, y = time (s), z = outline strength, w = bloom strength
  //   [1] rgb = fog color, w = fog density (per world unit)
  //   [2] x = fog height (full fog below), y = grid strength, z = shading bands, w = emissive brightness
  //   [3] rgb = outline color, w = ambient light
  //   [4] xy = center of the sight aimed through (0..1), z = its radius in screen heights
  //       (positive: a scope, negative: a red dot sight), w = how much it shows
  //   [5] x = vignette, y = chromatic aberration, z = film grain, w = sharpening
  //   [6] x = saturation, y = contrast, z = exposure, w = low health 0..1
  //   [7] rgb = color filter (multiplies), w = damage flash 0..1
  float4 effects[8];
};
