// Vertex shader pseudo-Slang / HLSL
struct VertexInput {
  float3 position : POSITION;
};

struct CameraUBO {
  float4x4 view;
  float4x4 proj;
};

struct VertexOutput {
  float4 position : SV_Position;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> camera;

[shader("vertex")]
VertexOutput main(VertexInput input) {
  VertexOutput output;
  output.position =
      mul(camera.proj, mul(camera.view, float4(input.position, 1.0)));
  return output;
}
