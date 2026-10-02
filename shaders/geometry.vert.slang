struct VertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
};

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD2;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  nointerpolation uint materialId : TEXCOORD1;
};

struct CameraUBO {
  float4x4 model;
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

  float4x4 modelMatrix = modelMatrices[drawId];
  float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
  float4 viewPos = mul(camera.view, worldPos);
  output.position = mul(camera.proj, viewPos);

  output.worldPos = worldPos.xyz;
  output.normal = mul((float3x3)modelMatrix, input.normal);
  output.texCoord = input.texCoord;
  output.materialId = materialIds[drawId];

  return output;
}