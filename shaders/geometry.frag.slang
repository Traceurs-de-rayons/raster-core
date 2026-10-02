struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD2;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  nointerpolation uint materialId : TEXCOORD1;
};

struct FragmentOutput {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 worldPos : SV_Target2;
};

[[vk::binding(0, 0)]]
Sampler2D textures[128];

[[vk::binding(19, 0)]]
StructuredBuffer<uint> textureIndices;

[shader("fragment")]
FragmentOutput main(VertexOutput input) {
  FragmentOutput output;

  uint textureIndex = textureIndices[input.materialId];
  output.albedo = textures[textureIndex].Sample(input.texCoord);
  output.normal = float4(normalize(input.normal) * 0.5 + 0.5, 1.0);
  output.worldPos = float4(input.worldPos, 1.0);

  return output;
}