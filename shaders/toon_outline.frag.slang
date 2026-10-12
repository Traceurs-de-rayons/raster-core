// Ink outlines: an edge is drawn where the depth or the normal changes between neighbouring
// pixels. Flat surfaces seen at a grazing angle do not count: the test uses 1 / depth, which
// varies linearly across a plane.

#include "frame_uniforms.slang"

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]] Sampler2D sceneColor;
[[vk::binding(1, 0)]] Sampler2D gNormal;   // nearest
[[vk::binding(2, 0)]] Sampler2D gWorldPos; // nearest
[[vk::binding(21, 0)]] ConstantBuffer<LightUBO> lighting;

struct Sample {
  float3 normal;
  float inverseDepth; // 0 = background
};

Sample fetch(float2 uv) {
  Sample result;
  float3 encoded = gNormal.SampleLevel(uv, 0.0).xyz;
  if (dot(encoded, encoded) < 1e-4) {
    result.normal = float3(0.0);
    result.inverseDepth = 0.0;
    return result;
  }
  float3 worldPos = gWorldPos.SampleLevel(uv, 0.0).xyz;
  float depth = dot(worldPos - lighting.cameraPosition.xyz, lighting.cameraForward.xyz);
  result.normal = normalize(encoded * 2.0 - 1.0);
  result.inverseDepth = 1.0 / max(depth, 0.01);
  return result;
}

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;
  float3 color = sceneColor.Sample(texCoord).rgb;

  uint width, height;
  gNormal.GetDimensions(width, height);
  // About one pixel at 1080p, thicker on larger images so the line keeps its weight.
  float2 step = max(float(height) / 1080.0, 1.0) / float2(width, height);

  Sample center = fetch(texCoord);
  Sample left = fetch(texCoord - float2(step.x, 0.0));
  Sample right = fetch(texCoord + float2(step.x, 0.0));
  Sample up = fetch(texCoord - float2(0.0, step.y));
  Sample down = fetch(texCoord + float2(0.0, step.y));

  float nearest = max(max(center.inverseDepth, max(left.inverseDepth, right.inverseDepth)), max(up.inverseDepth, down.inverseDepth));
  float curvature = abs(left.inverseDepth + right.inverseDepth - 2.0 * center.inverseDepth) +
                    abs(up.inverseDepth + down.inverseDepth - 2.0 * center.inverseDepth);
  float depthEdge = nearest > 0.0 ? smoothstep(0.02, 0.06, curvature / nearest) : 0.0;

  float alike = min(min(dot(center.normal, left.normal), dot(center.normal, right.normal)),
                    min(dot(center.normal, up.normal), dot(center.normal, down.normal)));
  float normalEdge = center.inverseDepth > 0.0 ? smoothstep(0.1, 0.5, 1.0 - alike) : 0.0;

  // Far away the lines would cover everything: let them thin out.
  float distanceFade = saturate(nearest * 90.0) * 0.6 + 0.4;
  float edge = max(depthEdge, normalEdge) * lighting.effects[0].z * distanceFade;

  output.color = float4(lerp(color, lighting.effects[3].rgb, saturate(edge)), 1.0);
  return output;
}
