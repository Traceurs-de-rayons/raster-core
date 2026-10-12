// Depth-only pass rendering the scene into one tile of a shadow atlas.
// The tile (viewport) and the view index are set per draw by the render graph.

#include "skinning.slang"

struct VertexInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(3)]] uint4 bones : BLENDINDICES;
  [[vk::location(4)]] float4 weights : BLENDWEIGHT;
};

struct VertexOutput {
  float4 position : SV_Position;
};

struct ViewPush {
  uint viewIndex; // RasterCore::ViewPushConstants
};

[[vk::push_constant]]
ConstantBuffer<ViewPush> push;

[[vk::binding(17, 0)]] // RasterCore::binding::ModelMatrices
StructuredBuffer<float4x4> modelMatrices;

[[vk::binding(23, 0)]] // RasterCore::binding::ShadowViews (10 float4 per view)
StructuredBuffer<float4> shadowViews;

// Matrices are stored column by column (cu::math layout).
float4 transformByColumns(uint base, float4 v) {
  return shadowViews[base + 0] * v.x + shadowViews[base + 1] * v.y + shadowViews[base + 2] * v.z +
         shadowViews[base + 3] * v.w;
}

[shader("vertex")]
VertexOutput main(VertexInput input, uint drawId : SV_DrawIndex) {
  VertexOutput output;

  uint base = push.viewIndex * 10;
  float4x4 modelMatrix = mul(modelMatrices[drawId], skinMatrix(drawId, input.bones, input.weights));
  float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
  float4 viewPos = transformByColumns(base + 0, worldPos);
  output.position = transformByColumns(base + 4, viewPos);
  // A view model casts no shadow: its triangles collapse and are not rasterized.
  if (isViewModel(drawId))
    output.position = float4(0.0, 0.0, 0.0, 1.0);

  return output;
}
