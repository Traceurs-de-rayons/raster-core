struct VertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
};

struct VertexOutput {
  float4 position : SV_Position;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
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

[shader("vertex")]
VertexOutput main(VertexInput input) {
  VertexOutput output;

  // float4x4 modelMatrix = modelMatrices[input.modelMatrixId];
  // float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
  float4 worldPos = float4(input.position, 1.0);
  float4 viewPos = mul(camera.view, worldPos);
  output.position = mul(camera.proj, viewPos);

  output.texCoord = input.texCoord;

  return output;
}
