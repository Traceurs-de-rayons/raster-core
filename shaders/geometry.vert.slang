#include "skinning.slang"

struct VertexInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(1)]] float3 normal : NORMAL;
  [[vk::location(2)]] float2 texCoord : TEXCOORD;
  [[vk::location(3)]] uint4 bones : BLENDINDICES;
  [[vk::location(4)]] float4 weights : BLENDWEIGHT;
};

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD2;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  nointerpolation uint materialId : TEXCOORD1;
};

struct CameraUBO {
  float4x4 viewModelProj; // projection of the view models (first person arms)
  float4x4 view;
  float4x4 proj;
};

[[vk::binding(16, 0)]]
ConstantBuffer<CameraUBO> camera;

[[vk::binding(17, 0)]]
StructuredBuffer<float4x4> modelMatrices;

[[vk::binding(18, 0)]]
StructuredBuffer<uint> materialIds;

[shader("vertex")]
VertexOutput main(VertexInput input, uint drawId : SV_DrawIndex) {
  VertexOutput output;

  float4x4 modelMatrix = mul(modelMatrices[drawId], skinMatrix(drawId, input.bones, input.weights));
  float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
  float3 normal = mul((float3x3)modelMatrix, input.normal);
  if (isViewModel(drawId)) {
    // A view model is placed relative to the camera, so it cannot lag behind it: its matrix
    // gives view space positions. It has its own field of view, and is squeezed into the front
    // of the depth range: whatever the world puts there, the view model is drawn over it.
    float4 viewPos = worldPos;
    output.position = mul(camera.viewModelProj, viewPos);
    output.position.z *= 0.05;

    // Back to world space for the lighting: the view matrix is a rotation and a translation.
    float3 viewOrigin = mul(camera.view, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    worldPos = float4(mul(float4(viewPos.xyz - viewOrigin, 0.0), camera.view).xyz, 1.0);
    normal = mul(float4(normal, 0.0), camera.view).xyz;
  } else {
    output.position = mul(camera.proj, mul(camera.view, worldPos));
  }

  output.worldPos = worldPos.xyz;
  output.normal = normal;
  output.texCoord = input.texCoord;
  output.materialId = materialIds[drawId];

  return output;
}